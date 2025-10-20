/* SPDX-License-Identifier: MIT */
/**
 * @file engine.c
 * @brief NAT translation engine implementation
 */

#include "nat_engine.h"
#include "cgnat_types.h"
#include <rte_hash.h>
#include <rte_jhash.h>
#include <rte_mempool.h>
#include <rte_cycles.h>
#include <rte_ip.h>
#include <rte_tcp.h>
#include <rte_udp.h>
#include <string.h>
#include <stdio.h>

/* Helper: Update IP checksum after modification */
static inline void
update_ip_checksum(struct rte_ipv4_hdr *ip_hdr)
{
    ip_hdr->hdr_checksum = 0;
    ip_hdr->hdr_checksum = rte_ipv4_cksum(ip_hdr);
}

/* Helper: Update TCP/UDP checksum */
static inline void
update_l4_checksum(struct rte_mbuf *m, struct rte_ipv4_hdr *ip_hdr, uint8_t proto)
{
    if (proto == PROTO_TCP) {
        struct rte_tcp_hdr *tcp = (struct rte_tcp_hdr *)
            ((uint8_t *)ip_hdr + (ip_hdr->version_ihl & 0x0F) * 4);
        tcp->cksum = 0;
        tcp->cksum = rte_ipv4_udptcp_cksum(ip_hdr, tcp);
    } else if (proto == PROTO_UDP) {
        struct rte_udp_hdr *udp = (struct rte_udp_hdr *)
            ((uint8_t *)ip_hdr + (ip_hdr->version_ihl & 0x0F) * 4);
        udp->dgram_cksum = 0;
        udp->dgram_cksum = rte_ipv4_udptcp_cksum(ip_hdr, udp);
    }
}

/* Helper: Extract flow key from packet */
static inline int
extract_flow_key(struct rte_mbuf *m, struct flow_key *key)
{
    struct rte_ether_hdr *eth = rte_pktmbuf_mtod(m, struct rte_ether_hdr *);
    
    /* Only handle IPv4 */
    if (eth->ether_type != rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4))
        return -1;
    
    struct rte_ipv4_hdr *ip = (struct rte_ipv4_hdr *)(eth + 1);
    
    key->src_ip = rte_be_to_cpu_32(ip->src_addr);
    key->dst_ip = rte_be_to_cpu_32(ip->dst_addr);
    key->protocol = ip->next_proto_id;
    
    if (ip->next_proto_id == PROTO_TCP) {
        struct rte_tcp_hdr *tcp = (struct rte_tcp_hdr *)
            ((uint8_t *)ip + (ip->version_ihl & 0x0F) * 4);
        key->src_port = rte_be_to_cpu_16(tcp->src_port);
        key->dst_port = rte_be_to_cpu_16(tcp->dst_port);
    } else if (ip->next_proto_id == PROTO_UDP) {
        struct rte_udp_hdr *udp = (struct rte_udp_hdr *)
            ((uint8_t *)ip + (ip->version_ihl & 0x0F) * 4);
        key->src_port = rte_be_to_cpu_16(udp->src_port);
        key->dst_port = rte_be_to_cpu_16(udp->dst_port);
    } else if (ip->next_proto_id == PROTO_ICMP) {
        key->src_port = 0;
        key->dst_port = 0;
    } else {
        return -1;  /* Unsupported protocol */
    }
    
    return 0;
}

int
nat_core_init(struct nat_core_ctx *ctx, unsigned int core_id,
              const struct cgnat_config *config)
{
    char name[64];
    unsigned int socket_id = rte_socket_id();
    
    memset(ctx, 0, sizeof(*ctx));
    ctx->core_id = core_id;
    ctx->socket_id = socket_id;
    
    /* Create outbound hash table (private -> public) */
    snprintf(name, sizeof(name), "outbound_hash_%u", core_id);
    struct rte_hash_parameters hash_params = {
        .name = name,
        .entries = ENTRIES_PER_CORE,
        .key_len = sizeof(struct flow_key),
        .hash_func = rte_jhash,
        .hash_func_init_val = 0,
        .socket_id = socket_id,
    };
    ctx->outbound_hash = rte_hash_create(&hash_params);
    if (!ctx->outbound_hash) {
        printf("Failed to create outbound hash table on core %u\n", core_id);
        return -1;
    }
    
    /* Create inbound hash table (public -> private) */
    snprintf(name, sizeof(name), "inbound_hash_%u", core_id);
    hash_params.name = name;
    ctx->inbound_hash = rte_hash_create(&hash_params);
    if (!ctx->inbound_hash) {
        printf("Failed to create inbound hash table on core %u\n", core_id);
        rte_hash_free(ctx->outbound_hash);
        return -1;
    }
    
    /* Create NAT entry memory pool */
    snprintf(name, sizeof(name), "nat_entry_pool_%u", core_id);
    ctx->entry_pool = rte_mempool_create(name,
                                         ENTRIES_PER_CORE,
                                         sizeof(struct nat_entry),
                                         MBUF_CACHE_SIZE,
                                         0, NULL, NULL, NULL, NULL,
                                         socket_id, 0);
    if (!ctx->entry_pool) {
        printf("Failed to create NAT entry pool on core %u\n", core_id);
        rte_hash_free(ctx->outbound_hash);
        rte_hash_free(ctx->inbound_hash);
        return -1;
    }
    
    /* Initialize port pools for each public IP */
    ctx->num_public_ips = config->num_public_ips;
    for (int i = 0; i < config->num_public_ips; i++) {
        ctx->port_pools[i].public_ip = config->public_ips[i];
        ctx->port_pools[i].cursor = PORT_RANGE_START;
        ctx->port_pools[i].ports_allocated = 0;
        memset(ctx->port_pools[i].bitmap, 0, sizeof(ctx->port_pools[i].bitmap));
        rte_atomic32_init(&ctx->port_pools[i].exhaustion_events);
    }
    
    /* Store customer subnet config */
    ctx->customer_subnet = config->customer_subnet;
    ctx->customer_netmask = config->customer_netmask;
    
    printf("[CORE %u] NAT engine initialized (socket %u)\n", core_id, socket_id);
    return 0;
}

void
nat_core_cleanup(struct nat_core_ctx *ctx)
{
    if (ctx->outbound_hash)
        rte_hash_free(ctx->outbound_hash);
    if (ctx->inbound_hash)
        rte_hash_free(ctx->inbound_hash);
    if (ctx->entry_pool)
        rte_mempool_free(ctx->entry_pool);
    
    printf("[CORE %u] NAT engine cleaned up\n", ctx->core_id);
}

int
nat_process_outbound(struct nat_core_ctx *ctx, struct rte_mbuf *m)
{
    struct flow_key key;
    struct nat_entry *entry;
    uint64_t start_tsc = rte_rdtsc();
    
    /* Extract 5-tuple */
    if (extract_flow_key(m, &key) < 0) {
        ctx->stats.errors_invalid_packet++;
        return -1;
    }
    
    /* Check if this is a customer packet */
    if ((key.src_ip & ctx->customer_netmask) != ctx->customer_subnet) {
        ctx->stats.errors_invalid_packet++;
        return -1;
    }
    
    /* Lookup existing NAT session */
    int32_t ret = rte_hash_lookup(ctx->outbound_hash, &key);
    if (ret >= 0) {
        /* Session exists - retrieve entry */
        if (rte_mempool_get(ctx->entry_pool, (void **)&entry) < 0) {
            ctx->stats.errors_no_memory++;
            return -1;
        }
        
        ctx->stats.nat_lookup_hit++;
        entry->last_activity = start_tsc;
        entry->packet_count++;
        entry->byte_count += m->pkt_len;
    } else {
        /* New session - create NAT entry */
        if (rte_mempool_get(ctx->entry_pool, (void **)&entry) < 0) {
            ctx->stats.errors_no_memory++;
            return -1;
        }
        
        /* Allocate public port (round-robin across IPs) */
        int ip_idx = (ctx->stats.nat_created % ctx->num_public_ips);
        uint16_t public_port = port_pool_alloc(&ctx->port_pools[ip_idx]);
        if (public_port == 0) {
            /* Try other IPs if first fails */
            for (int i = 0; i < ctx->num_public_ips; i++) {
                public_port = port_pool_alloc(&ctx->port_pools[i]);
                if (public_port != 0) {
                    ip_idx = i;
                    break;
                }
            }
        }
        
        if (public_port == 0) {
            rte_mempool_put(ctx->entry_pool, entry);
            ctx->stats.errors_no_ports++;
            ctx->stats.port_alloc_fail++;
            return -1;
        }
        
        /* Fill NAT entry */
        memcpy(&entry->private_flow, &key, sizeof(key));
        entry->public_ip = ctx->port_pools[ip_idx].public_ip;
        entry->public_port = public_port;
        entry->state = (key.protocol == PROTO_TCP) ? NAT_STATE_SYN_SENT : NAT_STATE_UDP_ACTIVE;
        entry->last_activity = start_tsc;
        entry->packet_count = 1;
        entry->byte_count = m->pkt_len;
        entry->customer_id = rte_jhash(&key.src_ip, 4, 0);
        
        /* Add to hash tables */
        rte_hash_add_key_data(ctx->outbound_hash, &key, entry);
        
        struct flow_key reverse_key = {
            .src_ip = key.dst_ip,
            .dst_ip = entry->public_ip,
            .src_port = key.dst_port,
            .dst_port = public_port,
            .protocol = key.protocol,
        };
        rte_hash_add_key_data(ctx->inbound_hash, &reverse_key, entry);
        
        ctx->stats.nat_created++;
        ctx->stats.nat_lookup_miss++;
        ctx->stats.port_alloc_success++;
    }
    
    /* Rewrite packet */
    struct rte_ether_hdr *eth = rte_pktmbuf_mtod(m, struct rte_ether_hdr *);
    struct rte_ipv4_hdr *ip = (struct rte_ipv4_hdr *)(eth + 1);
    
    ip->src_addr = rte_cpu_to_be_32(entry->public_ip);
    
    if (key.protocol == PROTO_TCP) {
        struct rte_tcp_hdr *tcp = (struct rte_tcp_hdr *)
            ((uint8_t *)ip + (ip->version_ihl & 0x0F) * 4);
        tcp->src_port = rte_cpu_to_be_16(entry->public_port);
    } else if (key.protocol == PROTO_UDP) {
        struct rte_udp_hdr *udp = (struct rte_udp_hdr *)
            ((uint8_t *)ip + (ip->version_ihl & 0x0F) * 4);
        udp->src_port = rte_cpu_to_be_16(entry->public_port);
    }
    
    /* Update checksums */
    update_ip_checksum(ip);
    update_l4_checksum(m, ip, key.protocol);
    
    /* Track latency */
    uint64_t latency = rte_rdtsc() - start_tsc;
    ctx->stats.latency_sum += latency;
    ctx->stats.latency_count++;
    if (latency > ctx->stats.latency_max)
        ctx->stats.latency_max = latency;
    
    return 0;
}

int
nat_process_inbound(struct nat_core_ctx *ctx, struct rte_mbuf *m)
{
    struct flow_key key;
    struct nat_entry *entry;
    
    /* Extract 5-tuple */
    if (extract_flow_key(m, &key) < 0) {
        ctx->stats.errors_invalid_packet++;
        return -1;
    }
    
    /* Lookup NAT session */
    int32_t ret = rte_hash_lookup_data(ctx->inbound_hash, &key, (void **)&entry);
    if (ret < 0) {
        ctx->stats.nat_lookup_miss++;
        return -1;  /* No NAT session - drop */
    }
    
    ctx->stats.nat_lookup_hit++;
    entry->last_activity = rte_rdtsc();
    entry->packet_count++;
    entry->byte_count += m->pkt_len;
    
    /* Rewrite packet back to private address */
    struct rte_ether_hdr *eth = rte_pktmbuf_mtod(m, struct rte_ether_hdr *);
    struct rte_ipv4_hdr *ip = (struct rte_ipv4_hdr *)(eth + 1);
    
    ip->dst_addr = rte_cpu_to_be_32(entry->private_flow.src_ip);
    
    if (key.protocol == PROTO_TCP) {
        struct rte_tcp_hdr *tcp = (struct rte_tcp_hdr *)
            ((uint8_t *)ip + (ip->version_ihl & 0x0F) * 4);
        tcp->dst_port = rte_cpu_to_be_16(entry->private_flow.src_port);
    } else if (key.protocol == PROTO_UDP) {
        struct rte_udp_hdr *udp = (struct rte_udp_hdr *)
            ((uint8_t *)ip + (ip->version_ihl & 0x0F) * 4);
        udp->dst_port = rte_cpu_to_be_16(entry->private_flow.src_port);
    }
    
    /* Update checksums */
    update_ip_checksum(ip);
    update_l4_checksum(m, ip, key.protocol);
    
    return 0;
}

int
nat_expire_sessions(struct nat_core_ctx *ctx)
{
    /* TODO: Implement timer wheel-based aging */
    /* For now, this is a placeholder */
    return 0;
}

void
nat_get_stats(const struct nat_core_ctx *ctx, struct core_stats *stats)
{
    memcpy(stats, &ctx->stats, sizeof(*stats));
}

/* Port pool management */
uint16_t
port_pool_alloc(struct port_pool *pool)
{
    uint16_t start_cursor = pool->cursor;
    
    /* Scan from cursor with wraparound */
    do {
        uint16_t port = pool->cursor;
        if (port < PORT_RANGE_START || port > PORT_RANGE_END) {
            pool->cursor = PORT_RANGE_START;
            continue;
        }
        
        /* Check bitmap */
        uint16_t idx = port - PORT_RANGE_START;
        uint16_t word_idx = idx / 64;
        uint16_t bit_idx = idx % 64;
        
        if (!(pool->bitmap[word_idx] & (1ULL << bit_idx))) {
            /* Port is free - allocate it */
            pool->bitmap[word_idx] |= (1ULL << bit_idx);
            pool->ports_allocated++;
            pool->cursor = (port + 1 > PORT_RANGE_END) ? PORT_RANGE_START : port + 1;
            return port;
        }
        
        /* Try next port */
        pool->cursor++;
        if (pool->cursor > PORT_RANGE_END)
            pool->cursor = PORT_RANGE_START;
            
    } while (pool->cursor != start_cursor);
    
    /* Pool exhausted */
    rte_atomic32_inc(&pool->exhaustion_events);
    return 0;
}

void
port_pool_free(struct port_pool *pool, uint16_t port)
{
    if (port < PORT_RANGE_START || port > PORT_RANGE_END)
        return;
    
    uint16_t idx = port - PORT_RANGE_START;
    uint16_t word_idx = idx / 64;
    uint16_t bit_idx = idx % 64;
    
    pool->bitmap[word_idx] &= ~(1ULL << bit_idx);
    pool->ports_allocated--;
}

bool
port_pool_is_allocated(const struct port_pool *pool, uint16_t port)
{
    if (port < PORT_RANGE_START || port > PORT_RANGE_END)
        return false;
    
    uint16_t idx = port - PORT_RANGE_START;
    uint16_t word_idx = idx / 64;
    uint16_t bit_idx = idx % 64;
    
    return (pool->bitmap[word_idx] & (1ULL << bit_idx)) != 0;
}
