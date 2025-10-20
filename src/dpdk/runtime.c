/* SPDX-License-Identifier: MIT */
/**
 * @file runtime.c
 * @brief DPDK initialization and packet processing
 */

#include "dpdk_runtime.h"
#include "nat_engine.h"
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_mempool.h>
#include <rte_mbuf.h>
#include <rte_cycles.h>
#include <stdio.h>
#include <signal.h>

/* Global flag for graceful shutdown */
static volatile bool force_quit = false;

/* Signal handler */
static void
signal_handler(int signum)
{
    if (signum == SIGINT || signum == SIGTERM) {
        printf("\n\nSignal %d received, preparing to exit...\n", signum);
        force_quit = true;
    }
}

int
dpdk_eal_init(int argc, char **argv)
{
    /* Initialize DPDK Environment Abstraction Layer */
    int ret = rte_eal_init(argc, argv);
    if (ret < 0) {
        fprintf(stderr, "Error: DPDK EAL initialization failed\n");
        return -1;
    }
    
    /* Register signal handlers */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    printf("[DPDK] EAL initialized successfully\n");
    printf("[DPDK] Available lcores: %u\n", rte_lcore_count());
    printf("[DPDK] Socket count: %u\n", rte_socket_count());
    
    return ret;
}

struct rte_mempool *
dpdk_create_mbuf_pool(const char *name, unsigned int num_mbufs,
                      unsigned int socket_id)
{
    struct rte_mempool *mbuf_pool;
    
    mbuf_pool = rte_pktmbuf_pool_create(name,
                                       num_mbufs,
                                       MBUF_CACHE_SIZE,
                                       0,
                                       RTE_MBUF_DEFAULT_BUF_SIZE,
                                       socket_id);
    
    if (!mbuf_pool) {
        fprintf(stderr, "Error: Cannot create mbuf pool '%s'\n", name);
        return NULL;
    }
    
    printf("[DPDK] Created mbuf pool '%s': %u buffers on socket %u\n",
           name, num_mbufs, socket_id);
    
    return mbuf_pool;
}

int
dpdk_port_init(uint16_t port_id, uint16_t num_queues,
               struct rte_mempool *mbuf_pool)
{
    struct rte_eth_conf port_conf = {
        .rxmode = {
            .mtu = RTE_ETHER_MTU,
            .max_lro_pkt_size = RTE_ETHER_MAX_LEN,
        },
        .txmode = {
            .mq_mode = RTE_ETH_MQ_TX_NONE,
        },
        .rx_adv_conf = {
            .rss_conf = {
                .rss_key = NULL,
                .rss_hf = RTE_ETH_RSS_IP | RTE_ETH_RSS_TCP | RTE_ETH_RSS_UDP,
            },
        },
    };
    
    struct rte_eth_dev_info dev_info;
    int ret;
    
    /* Get device info */
    ret = rte_eth_dev_info_get(port_id, &dev_info);
    if (ret != 0) {
        fprintf(stderr, "Error getting device info for port %u: %s\n",
                port_id, rte_strerror(-ret));
        return ret;
    }
    
    /* Configure port */
    ret = rte_eth_dev_configure(port_id, num_queues, num_queues, &port_conf);
    if (ret != 0) {
        fprintf(stderr, "Error configuring port %u: %s\n",
                port_id, rte_strerror(-ret));
        return ret;
    }
    
    /* Setup RX queues */
    for (uint16_t q = 0; q < num_queues; q++) {
        ret = rte_eth_rx_queue_setup(port_id, q, 1024,
                                     rte_eth_dev_socket_id(port_id),
                                     NULL, mbuf_pool);
        if (ret < 0) {
            fprintf(stderr, "Error setting up RX queue %u on port %u: %s\n",
                    q, port_id, rte_strerror(-ret));
            return ret;
        }
    }
    
    /* Setup TX queues */
    for (uint16_t q = 0; q < num_queues; q++) {
        ret = rte_eth_tx_queue_setup(port_id, q, 1024,
                                     rte_eth_dev_socket_id(port_id),
                                     NULL);
        if (ret < 0) {
            fprintf(stderr, "Error setting up TX queue %u on port %u: %s\n",
                    q, port_id, rte_strerror(-ret));
            return ret;
        }
    }
    
    printf("[DPDK] Port %u configured with %u RX/TX queues\n", port_id, num_queues);
    return 0;
}

int
dpdk_port_start(uint16_t port_id)
{
    int ret;
    
    /* Start device */
    ret = rte_eth_dev_start(port_id);
    if (ret < 0) {
        fprintf(stderr, "Error starting port %u: %s\n",
                port_id, rte_strerror(-ret));
        return ret;
    }
    
    /* Enable promiscuous mode */
    ret = rte_eth_promiscuous_enable(port_id);
    if (ret != 0) {
        fprintf(stderr, "Error enabling promiscuous mode on port %u: %s\n",
                port_id, rte_strerror(-ret));
        return ret;
    }
    
    printf("[DPDK] Port %u started (promiscuous mode enabled)\n", port_id);
    return 0;
}

void
dpdk_port_stop(uint16_t port_id)
{
    int ret = rte_eth_dev_stop(port_id);
    if (ret != 0)
        fprintf(stderr, "Error stopping port %u: %s\n",
                port_id, rte_strerror(-ret));
    
    rte_eth_dev_close(port_id);
    printf("[DPDK] Port %u stopped\n", port_id);
}

int
dpdk_get_link_status(uint16_t port_id, struct rte_eth_link *link)
{
    return rte_eth_link_get_nowait(port_id, link);
}

/* Worker context passed to each core */
struct worker_ctx {
    unsigned int core_id;
    unsigned int queue_id;
    uint16_t port_id;
    struct nat_core_ctx *nat_ctx;
};

/**
 * Main packet processing loop (per worker core)
 */
int
dpdk_worker_main(void *arg)
{
    struct worker_ctx *ctx = (struct worker_ctx *)arg;
    struct rte_mbuf *rx_pkts[RX_BURST_SIZE];
    struct rte_mbuf *tx_pkts[TX_BURST_SIZE];
    uint16_t nb_rx, nb_tx;
    unsigned int tx_count;
    
    printf("[WORKER %u] Started on lcore %u (queue %u)\n",
           ctx->core_id, rte_lcore_id(), ctx->queue_id);
    
    /* Main processing loop */
    while (!force_quit) {
        /* Receive packet burst */
        nb_rx = rte_eth_rx_burst(ctx->port_id, ctx->queue_id,
                                rx_pkts, RX_BURST_SIZE);
        
        if (unlikely(nb_rx == 0))
            continue;
        
        ctx->nat_ctx->stats.packets_rx += nb_rx;
        
        tx_count = 0;
        for (uint16_t i = 0; i < nb_rx; i++) {
            struct rte_mbuf *m = rx_pkts[i];
            ctx->nat_ctx->stats.bytes_rx += m->pkt_len;
            
            /* Determine packet direction and process */
            struct rte_ether_hdr *eth = rte_pktmbuf_mtod(m, struct rte_ether_hdr *);
            if (eth->ether_type == rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4)) {
                struct rte_ipv4_hdr *ip = (struct rte_ipv4_hdr *)(eth + 1);
                uint32_t src_ip = rte_be_to_cpu_32(ip->src_addr);
                
                int ret;
                /* Check if outbound (from customer) or inbound (to customer) */
                if ((src_ip & ctx->nat_ctx->customer_netmask) == ctx->nat_ctx->customer_subnet) {
                    /* Outbound packet */
                    ret = nat_process_outbound(ctx->nat_ctx, m);
                } else {
                    /* Inbound packet */
                    ret = nat_process_inbound(ctx->nat_ctx, m);
                }
                
                if (ret == 0) {
                    /* Successfully translated - queue for TX */
                    tx_pkts[tx_count++] = m;
                } else {
                    /* Translation failed - drop packet */
                    rte_pktmbuf_free(m);
                    ctx->nat_ctx->stats.packets_dropped++;
                }
            } else {
                /* Non-IPv4 packet - drop */
                rte_pktmbuf_free(m);
                ctx->nat_ctx->stats.packets_dropped++;
            }
        }
        
        /* Transmit translated packets */
        if (tx_count > 0) {
            nb_tx = rte_eth_tx_burst(ctx->port_id, ctx->queue_id,
                                    tx_pkts, tx_count);
            ctx->nat_ctx->stats.packets_tx += nb_tx;
            
            /* Update byte counter */
            for (uint16_t i = 0; i < nb_tx; i++) {
                ctx->nat_ctx->stats.bytes_tx += tx_pkts[i]->pkt_len;
            }
            
            /* Free any unsent packets */
            if (unlikely(nb_tx < tx_count)) {
                for (uint16_t i = nb_tx; i < tx_count; i++) {
                    rte_pktmbuf_free(tx_pkts[i]);
                    ctx->nat_ctx->stats.packets_dropped++;
                }
            }
        }
        
        /* Periodic cleanup (every ~1M packets) */
        if (unlikely((ctx->nat_ctx->stats.packets_rx & 0xFFFFF) == 0)) {
            nat_expire_sessions(ctx->nat_ctx);
        }
    }
    
    printf("[WORKER %u] Shutting down gracefully\n", ctx->core_id);
    return 0;
}
