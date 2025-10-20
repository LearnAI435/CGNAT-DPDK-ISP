/* SPDX-License-Identifier: MIT */
/**
 * @file metrics.c
 * @brief Telemetry and monitoring implementation
 */

#include "telemetry.h"
#include <rte_cycles.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static uint64_t tsc_hz = 0;

int
telemetry_init(const struct cgnat_config *config)
{
    tsc_hz = rte_get_tsc_hz();
    printf("[TELEMETRY] Initialized (TSC frequency: %lu Hz)\n", tsc_hz);
    return 0;
}

void
telemetry_aggregate_stats(const struct nat_core_ctx *cores,
                          unsigned int num_cores,
                          struct cgnat_global_stats *global_stats)
{
    memset(global_stats, 0, sizeof(*global_stats));
    
    uint64_t total_latency_sum = 0;
    uint64_t total_latency_count = 0;
    uint64_t max_latency_cycles = 0;
    
    /* Aggregate from all cores */
    for (unsigned int i = 0; i < num_cores; i++) {
        const struct core_stats *stats = &cores[i].stats;
        
        global_stats->total_packets_rx += stats->packets_rx;
        global_stats->total_packets_tx += stats->packets_tx;
        global_stats->total_packets_dropped += stats->packets_dropped;
        global_stats->total_bytes_rx += stats->bytes_rx;
        global_stats->total_bytes_tx += stats->bytes_tx;
        
        global_stats->total_nat_created += stats->nat_created;
        global_stats->total_nat_expired += stats->nat_expired;
        global_stats->total_port_alloc_fail += stats->port_alloc_fail;
        
        total_latency_sum += stats->latency_sum;
        total_latency_count += stats->latency_count;
        
        if (stats->latency_max > max_latency_cycles)
            max_latency_cycles = stats->latency_max;
    }
    
    /* Calculate active sessions (created - expired) */
    global_stats->total_nat_sessions = global_stats->total_nat_created -
                                       global_stats->total_nat_expired;
    
    /* Convert latency from TSC cycles to microseconds */
    if (total_latency_count > 0 && tsc_hz > 0) {
        double avg_cycles = (double)total_latency_sum / total_latency_count;
        global_stats->avg_latency_us = (avg_cycles * 1000000.0) / tsc_hz;
        global_stats->max_latency_us = (max_latency_cycles * 1000000) / tsc_hz;
    }
    
    global_stats->timestamp = time(NULL);
}

int
telemetry_export_prometheus(const struct cgnat_global_stats *global_stats,
                            char *buffer, size_t buf_size)
{
    int offset = 0;
    
    #define APPEND(fmt, ...) do { \
        int n = snprintf(buffer + offset, buf_size - offset, fmt, ##__VA_ARGS__); \
        if (n > 0) offset += n; \
    } while (0)
    
    APPEND("# HELP cgnat_packets_received_total Total packets received\n");
    APPEND("# TYPE cgnat_packets_received_total counter\n");
    APPEND("cgnat_packets_received_total %lu\n", global_stats->total_packets_rx);
    
    APPEND("# HELP cgnat_packets_transmitted_total Total packets transmitted\n");
    APPEND("# TYPE cgnat_packets_transmitted_total counter\n");
    APPEND("cgnat_packets_transmitted_total %lu\n", global_stats->total_packets_tx);
    
    APPEND("# HELP cgnat_packets_dropped_total Total packets dropped\n");
    APPEND("# TYPE cgnat_packets_dropped_total counter\n");
    APPEND("cgnat_packets_dropped_total %lu\n", global_stats->total_packets_dropped);
    
    APPEND("# HELP cgnat_bytes_received_total Total bytes received\n");
    APPEND("# TYPE cgnat_bytes_received_total counter\n");
    APPEND("cgnat_bytes_received_total %lu\n", global_stats->total_bytes_rx);
    
    APPEND("# HELP cgnat_bytes_transmitted_total Total bytes transmitted\n");
    APPEND("# TYPE cgnat_bytes_transmitted_total counter\n");
    APPEND("cgnat_bytes_transmitted_total %lu\n", global_stats->total_bytes_tx);
    
    APPEND("# HELP cgnat_nat_sessions_active Active NAT sessions\n");
    APPEND("# TYPE cgnat_nat_sessions_active gauge\n");
    APPEND("cgnat_nat_sessions_active %lu\n", global_stats->total_nat_sessions);
    
    APPEND("# HELP cgnat_nat_sessions_created_total NAT sessions created\n");
    APPEND("# TYPE cgnat_nat_sessions_created_total counter\n");
    APPEND("cgnat_nat_sessions_created_total %lu\n", global_stats->total_nat_created);
    
    APPEND("# HELP cgnat_nat_sessions_expired_total NAT sessions expired\n");
    APPEND("# TYPE cgnat_nat_sessions_expired_total counter\n");
    APPEND("cgnat_nat_sessions_expired_total %lu\n", global_stats->total_nat_expired);
    
    APPEND("# HELP cgnat_port_allocation_failures_total Port allocation failures\n");
    APPEND("# TYPE cgnat_port_allocation_failures_total counter\n");
    APPEND("cgnat_port_allocation_failures_total %lu\n", global_stats->total_port_alloc_fail);
    
    APPEND("# HELP cgnat_packet_latency_microseconds_avg Average packet processing latency\n");
    APPEND("# TYPE cgnat_packet_latency_microseconds_avg gauge\n");
    APPEND("cgnat_packet_latency_microseconds_avg %.2f\n", global_stats->avg_latency_us);
    
    APPEND("# HELP cgnat_packet_latency_microseconds_max Maximum packet processing latency\n");
    APPEND("# TYPE cgnat_packet_latency_microseconds_max gauge\n");
    APPEND("cgnat_packet_latency_microseconds_max %lu\n", global_stats->max_latency_us);
    
    #undef APPEND
    
    return offset;
}

void
telemetry_log_metrics(const struct cgnat_global_stats *global_stats)
{
    printf("\n====== CGNAT Performance Metrics ======\n");
    printf("Packets RX:       %lu\n", global_stats->total_packets_rx);
    printf("Packets TX:       %lu\n", global_stats->total_packets_tx);
    printf("Packets Dropped:  %lu\n", global_stats->total_packets_dropped);
    printf("Bytes RX:         %lu (%.2f MB)\n",
           global_stats->total_bytes_rx,
           global_stats->total_bytes_rx / (1024.0 * 1024.0));
    printf("Bytes TX:         %lu (%.2f MB)\n",
           global_stats->total_bytes_tx,
           global_stats->total_bytes_tx / (1024.0 * 1024.0));
    printf("Active Sessions:  %lu\n", global_stats->total_nat_sessions);
    printf("Sessions Created: %lu\n", global_stats->total_nat_created);
    printf("Sessions Expired: %lu\n", global_stats->total_nat_expired);
    printf("Port Alloc Fails: %lu\n", global_stats->total_port_alloc_fail);
    printf("Avg Latency:      %.2f μs\n", global_stats->avg_latency_us);
    printf("Max Latency:      %lu μs\n", global_stats->max_latency_us);
    printf("=======================================\n\n");
}
