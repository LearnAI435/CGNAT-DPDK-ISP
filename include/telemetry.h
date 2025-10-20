/* SPDX-License-Identifier: MIT */
/**
 * @file telemetry.h
 * @brief Prometheus metrics and monitoring
 */

#ifndef TELEMETRY_H
#define TELEMETRY_H

#include "cgnat_types.h"

/**
 * Initialize telemetry system
 * 
 * @param config Global configuration
 * @return 0 on success, negative on error
 */
int telemetry_init(const struct cgnat_config *config);

/**
 * Start Prometheus metrics exporter
 * Runs in separate thread
 * 
 * @param port TCP port for HTTP server
 * @return 0 on success, negative on error
 */
int telemetry_start_prometheus(uint16_t port);

/**
 * Aggregate statistics from all cores
 * 
 * @param cores Array of per-core contexts
 * @param num_cores Number of cores
 * @param global_stats Output aggregated statistics
 */
void telemetry_aggregate_stats(const struct nat_core_ctx *cores,
                               unsigned int num_cores,
                               struct cgnat_global_stats *global_stats);

/**
 * Export metrics in Prometheus format
 * 
 * @param global_stats Aggregated statistics
 * @param buffer Output buffer
 * @param buf_size Buffer size
 * @return Number of bytes written
 */
int telemetry_export_prometheus(const struct cgnat_global_stats *global_stats,
                                char *buffer, size_t buf_size);

/**
 * Log performance metrics
 * 
 * @param global_stats Aggregated statistics
 */
void telemetry_log_metrics(const struct cgnat_global_stats *global_stats);

#endif /* TELEMETRY_H */
