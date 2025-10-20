/* SPDX-License-Identifier: MIT */
/**
 * @file main.c
 * @brief DPDK CGNAT main entry point
 */

#include "cgnat_types.h"
#include "dpdk_runtime.h"
#include "nat_engine.h"
#include "telemetry.h"
#include <rte_eal.h>
#include <rte_launch.h>
#include <rte_lcore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

/* Global configuration */
static struct cgnat_config g_config;

/* Per-core NAT contexts */
static struct nat_core_ctx g_nat_cores[MAX_CORES];

/* Global statistics (protected by mutex) */
struct cgnat_global_stats *g_global_stats = NULL;
pthread_mutex_t g_stats_lock = PTHREAD_MUTEX_INITIALIZER;

/* Worker contexts */
struct worker_ctx {
    unsigned int core_id;
    unsigned int queue_id;
    uint16_t port_id;
    struct nat_core_ctx *nat_ctx;
};

static struct worker_ctx g_workers[MAX_CORES];

/* Statistics update thread */
static volatile bool stats_running = true;

static void *
stats_thread_main(void *arg)
{
    struct cgnat_global_stats stats;
    
    printf("[STATS] Statistics thread started\n");
    
    while (stats_running) {
        sleep(2);  /* Update every 2 seconds */
        
        /* Aggregate stats from all cores */
        telemetry_aggregate_stats(g_nat_cores, g_config.num_workers, &stats);
        
        /* Update global stats under lock */
        pthread_mutex_lock(&g_stats_lock);
        memcpy(g_global_stats, &stats, sizeof(stats));
        pthread_mutex_unlock(&g_stats_lock);
        
        /* Log metrics */
        telemetry_log_metrics(&stats);
    }
    
    printf("[STATS] Statistics thread stopped\n");
    return NULL;
}

static void
print_usage(const char *prgname)
{
    printf("Usage: %s [EAL options] -- [APP options]\n"
           "\n"
           "EAL options:\n"
           "  -c COREMASK    : Hexadecimal bitmask of cores to run on\n"
           "  -n NUM         : Number of memory channels (4 recommended)\n"
           "  --huge-dir DIR : Directory for huge pages\n"
           "\n"
           "APP options:\n"
           "  -p PORTMASK    : Hexadecimal bitmask of ports (e.g., 0x1)\n"
           "  -P             : Enable promiscuous mode\n"
           "  -q NQ          : Number of queues per port\n"
           "\n"
           "Example:\n"
           "  sudo %s -c 0xff -n 4 -- -p 0x1 -q 8\n"
           "\n",
           prgname, prgname);
}

static int
parse_args(int argc, char **argv)
{
    int opt;
    
    /* Default configuration */
    g_config.port_id = 0;
    g_config.num_queues = 4;
    g_config.num_workers = 4;
    
    /* Public IPs (default TEST-NET-2 range) */
    g_config.num_public_ips = 10;
    for (int i = 0; i < 10; i++) {
        g_config.public_ips[i] = (203 << 24) | (0 << 16) | (113 << 8) | (i + 1);
    }
    
    /* Customer network (10.0.0.0/16) */
    g_config.customer_subnet = (10 << 24);
    g_config.customer_netmask = 0xFFFF0000;
    
    /* Timeouts */
    g_config.timeout_tcp_established = TIMEOUT_TCP_ESTABLISHED;
    g_config.timeout_tcp_syn = TIMEOUT_TCP_SYN;
    g_config.timeout_tcp_fin = TIMEOUT_TCP_FIN;
    g_config.timeout_udp = TIMEOUT_UDP;
    g_config.timeout_icmp = TIMEOUT_ICMP;
    
    /* Limits */
    g_config.max_sessions_per_customer = 100;
    
    /* Monitoring */
    g_config.telemetry_enabled = true;
    g_config.prometheus_port = 9091;
    g_config.api_port = 8080;
    
    while ((opt = getopt(argc, argv, "p:Pq:")) != -1) {
        switch (opt) {
        case 'p':
            /* Port mask (we use first set bit) */
            g_config.port_id = 0;
            break;
        case 'P':
            printf("Promiscuous mode enabled (default)\n");
            break;
        case 'q':
            g_config.num_queues = atoi(optarg);
            g_config.num_workers = g_config.num_queues;
            if (g_config.num_workers > MAX_CORES) {
                fprintf(stderr, "Error: Too many queues (max %d)\n", MAX_CORES);
                return -1;
            }
            break;
        default:
            print_usage(argv[0]);
            return -1;
        }
    }
    
    printf("[CONFIG] Port: %u, Queues: %u, Workers: %u\n",
           g_config.port_id, g_config.num_queues, g_config.num_workers);
    printf("[CONFIG] Public IPs: %d (%u.%u.%u.%u - %u.%u.%u.%u)\n",
           g_config.num_public_ips,
           (g_config.public_ips[0] >> 24) & 0xFF,
           (g_config.public_ips[0] >> 16) & 0xFF,
           (g_config.public_ips[0] >> 8) & 0xFF,
           g_config.public_ips[0] & 0xFF,
           (g_config.public_ips[9] >> 24) & 0xFF,
           (g_config.public_ips[9] >> 16) & 0xFF,
           (g_config.public_ips[9] >> 8) & 0xFF,
           g_config.public_ips[9] & 0xFF);
    
    return 0;
}

int
main(int argc, char **argv)
{
    int ret;
    unsigned int lcore_id;
    struct rte_mempool *mbuf_pool;
    pthread_t stats_thread;
    
    printf("╔════════════════════════════════════════════════════════╗\n");
    printf("║   DPDK-Based CGNAT for Production ISPs                ║\n");
    printf("║   High-Performance Carrier-Grade NAT                  ║\n");
    printf("╚════════════════════════════════════════════════════════╝\n\n");
    
    /* Initialize DPDK EAL */
    ret = dpdk_eal_init(argc, argv);
    if (ret < 0) {
        return -1;
    }
    
    argc -= ret;
    argv += ret;
    
    /* Parse application arguments */
    ret = parse_args(argc, argv);
    if (ret < 0) {
        return -1;
    }
    
    /* Check if we have enough cores */
    if (rte_lcore_count() < g_config.num_workers + 1) {
        fprintf(stderr, "Error: Need at least %u cores (%u workers + 1 main)\n",
                g_config.num_workers + 1, g_config.num_workers);
        return -1;
    }
    
    /* Create packet buffer pool */
    mbuf_pool = dpdk_create_mbuf_pool("mbuf_pool", MBUF_POOL_SIZE, rte_socket_id());
    if (!mbuf_pool) {
        return -1;
    }
    
    /* Initialize port */
    ret = dpdk_port_init(g_config.port_id, g_config.num_queues, mbuf_pool);
    if (ret < 0) {
        return -1;
    }
    
    /* Initialize per-core NAT contexts */
    unsigned int worker_idx = 0;
    RTE_LCORE_FOREACH_WORKER(lcore_id) {
        if (worker_idx >= g_config.num_workers)
            break;
        
        ret = nat_core_init(&g_nat_cores[worker_idx], lcore_id, &g_config);
        if (ret < 0) {
            fprintf(stderr, "Failed to initialize NAT on core %u\n", lcore_id);
            return -1;
        }
        
        /* Setup worker context */
        g_workers[worker_idx].core_id = lcore_id;
        g_workers[worker_idx].queue_id = worker_idx;
        g_workers[worker_idx].port_id = g_config.port_id;
        g_workers[worker_idx].nat_ctx = &g_nat_cores[worker_idx];
        
        g_config.worker_cores[worker_idx] = lcore_id;
        worker_idx++;
    }
    
    /* Start port */
    ret = dpdk_port_start(g_config.port_id);
    if (ret < 0) {
        return -1;
    }
    
    /* Initialize telemetry */
    telemetry_init(&g_config);
    
    /* Allocate global stats structure */
    g_global_stats = malloc(sizeof(struct cgnat_global_stats));
    memset(g_global_stats, 0, sizeof(struct cgnat_global_stats));
    
    /* Start Prometheus exporter */
    if (g_config.telemetry_enabled) {
        telemetry_start_prometheus(g_config.prometheus_port);
    }
    
    /* Start statistics thread */
    pthread_create(&stats_thread, NULL, stats_thread_main, NULL);
    
    printf("\n╔════════════════════════════════════════════════════════╗\n");
    printf("║              CGNAT System Started                     ║\n");
    printf("║                                                        ║\n");
    printf("║  Workers:           %2u cores                          ║\n", g_config.num_workers);
    printf("║  Port capacity:     %d ports total                 ║\n", 
           g_config.num_public_ips * PORTS_PER_IP);
    printf("║  Session capacity:  %d concurrent                  ║\n", MAX_NAT_ENTRIES);
    printf("║                                                        ║\n");
    printf("║  Prometheus:        http://0.0.0.0:%u/metrics       ║\n", g_config.prometheus_port);
    printf("║                                                        ║\n");
    printf("║  Press Ctrl+C to stop                                 ║\n");
    printf("╚════════════════════════════════════════════════════════╝\n\n");
    
    /* Launch workers on each core */
    worker_idx = 0;
    RTE_LCORE_FOREACH_WORKER(lcore_id) {
        if (worker_idx >= g_config.num_workers)
            break;
        
        rte_eal_remote_launch(dpdk_worker_main, &g_workers[worker_idx], lcore_id);
        worker_idx++;
    }
    
    /* Wait for workers to complete */
    rte_eal_mp_wait_lcore();
    
    /* Cleanup */
    stats_running = false;
    pthread_join(stats_thread, NULL);
    
    dpdk_port_stop(g_config.port_id);
    
    for (unsigned int i = 0; i < g_config.num_workers; i++) {
        nat_core_cleanup(&g_nat_cores[i]);
    }
    
    free(g_global_stats);
    
    printf("\nCGNAT system shutdown complete\n");
    return 0;
}
