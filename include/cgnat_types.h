/* SPDX-License-Identifier: MIT */
/**
 * @file cgnat_types.h
 * @brief Core data structures for DPDK-based CGNAT
 */

#ifndef CGNAT_TYPES_H
#define CGNAT_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_tcp.h>
#include <rte_udp.h>
#include <rte_hash.h>
#include <rte_atomic.h>

/* Configuration constants */
#define MAX_PUBLIC_IPS        10
#define MAX_CORES             16
#define MAX_NAT_ENTRIES       50000
#define ENTRIES_PER_CORE      (MAX_NAT_ENTRIES / MAX_CORES)
#define HASH_TABLE_BUCKETS    65536
#define PORT_RANGE_START      1024
#define PORT_RANGE_END        65535
#define PORTS_PER_IP          (PORT_RANGE_END - PORT_RANGE_START + 1)

/* Packet burst sizes */
#define RX_BURST_SIZE         32
#define TX_BURST_SIZE         32
#define MBUF_CACHE_SIZE       512
#define MBUF_POOL_SIZE        (512 * 1024)

/* Protocol types */
#define PROTO_TCP             6
#define PROTO_UDP             17
#define PROTO_ICMP            1

/* NAT session states (TCP) */
enum nat_state {
    NAT_STATE_CLOSED = 0,
    NAT_STATE_SYN_SENT,
    NAT_STATE_ESTABLISHED,
    NAT_STATE_FIN_WAIT,
    NAT_STATE_CLOSING,
    NAT_STATE_TIME_WAIT,
    NAT_STATE_UDP_ACTIVE,
    NAT_STATE_ICMP_ACTIVE
};

/* Connection timeouts (seconds) */
#define TIMEOUT_TCP_ESTABLISHED  7200
#define TIMEOUT_TCP_SYN          60
#define TIMEOUT_TCP_FIN          120
#define TIMEOUT_UDP              300
#define TIMEOUT_ICMP             30

/**
 * 5-tuple flow key for NAT lookup
 */
struct flow_key {
    uint32_t src_ip;
    uint32_t dst_ip;
    uint16_t src_port;
    uint16_t dst_port;
    uint8_t  protocol;
    uint8_t  reserved[3];  /* Padding for alignment */
} __attribute__((__packed__));

/**
 * NAT session entry (lockless per-core)
 */
struct nat_entry {
    /* Original (private) flow */
    struct flow_key private_flow;
    
    /* Translated (public) flow */
    uint32_t public_ip;
    uint16_t public_port;
    uint16_t reserved;
    
    /* State tracking */
    enum nat_state state;
    uint64_t last_activity;  /* TSC timestamp */
    uint32_t packet_count;
    uint64_t byte_count;
    
    /* Customer information */
    uint32_t customer_id;    /* Hash of private IP for tracking */
    
    /* Timer wheel linkage */
    uint32_t timer_index;
    
    /* Hash table linkage */
    struct nat_entry *next;
    
    /* Flags */
    uint8_t flags;
    uint8_t padding[7];
} __attribute__((aligned(64)));  /* Cache line aligned */

/**
 * Port pool for a single public IP (per-core)
 */
struct port_pool {
    uint32_t public_ip;
    uint16_t cursor;                    /* Rotating allocation cursor */
    uint16_t ports_allocated;
    uint64_t bitmap[1024];              /* 64K bits for port tracking */
    rte_atomic32_t exhaustion_events;
} __attribute__((aligned(64)));

/**
 * Per-core NAT statistics (no locks needed)
 */
struct core_stats {
    uint64_t packets_rx;
    uint64_t packets_tx;
    uint64_t packets_dropped;
    uint64_t bytes_rx;
    uint64_t bytes_tx;
    
    uint64_t nat_created;
    uint64_t nat_expired;
    uint64_t nat_lookup_hit;
    uint64_t nat_lookup_miss;
    
    uint64_t port_alloc_success;
    uint64_t port_alloc_fail;
    uint64_t port_freed;
    
    uint64_t errors_no_memory;
    uint64_t errors_invalid_packet;
    uint64_t errors_no_ports;
    
    /* Latency tracking (in CPU cycles) */
    uint64_t latency_sum;
    uint64_t latency_count;
    uint64_t latency_max;
} __attribute__((aligned(64)));

/**
 * Per-core NAT context (lockless design)
 */
struct nat_core_ctx {
    unsigned int core_id;
    unsigned int socket_id;
    
    /* DPDK structures */
    struct rte_hash *outbound_hash;     /* Private -> Public */
    struct rte_hash *inbound_hash;      /* Public -> Private */
    struct rte_mempool *entry_pool;     /* NAT entry allocator */
    
    /* Port pools (one per public IP) */
    struct port_pool port_pools[MAX_PUBLIC_IPS];
    int num_public_ips;
    
    /* Statistics (lockless, per-core) */
    struct core_stats stats;
    
    /* Configuration */
    uint32_t customer_subnet;
    uint32_t customer_netmask;
} __attribute__((aligned(64)));

/**
 * Global CGNAT configuration (read-only after init)
 */
struct cgnat_config {
    /* DPDK configuration */
    uint16_t port_id;
    uint16_t num_queues;
    unsigned int num_workers;
    unsigned int worker_cores[MAX_CORES];
    
    /* NAT configuration */
    uint32_t public_ips[MAX_PUBLIC_IPS];
    int num_public_ips;
    
    uint32_t customer_subnet;
    uint32_t customer_netmask;
    
    /* Timeouts */
    uint32_t timeout_tcp_established;
    uint32_t timeout_tcp_syn;
    uint32_t timeout_tcp_fin;
    uint32_t timeout_udp;
    uint32_t timeout_icmp;
    
    /* Limits */
    uint32_t max_sessions_per_customer;
    
    /* Monitoring */
    bool telemetry_enabled;
    uint16_t prometheus_port;
    uint16_t api_port;
};

/**
 * Aggregated statistics for monitoring
 */
struct cgnat_global_stats {
    uint64_t total_packets_rx;
    uint64_t total_packets_tx;
    uint64_t total_packets_dropped;
    uint64_t total_bytes_rx;
    uint64_t total_bytes_tx;
    
    uint64_t total_nat_sessions;
    uint64_t total_nat_created;
    uint64_t total_nat_expired;
    
    uint64_t total_port_alloc_fail;
    
    double avg_latency_us;
    uint64_t max_latency_us;
    
    uint64_t timestamp;
};

#endif /* CGNAT_TYPES_H */
