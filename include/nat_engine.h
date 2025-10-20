/* SPDX-License-Identifier: MIT */
/**
 * @file nat_engine.h
 * @brief NAT translation engine (per-core lockless)
 */

#ifndef NAT_ENGINE_H
#define NAT_ENGINE_H

#include "cgnat_types.h"
#include <rte_mbuf.h>

/**
 * Initialize per-core NAT context
 * 
 * @param ctx Per-core NAT context to initialize
 * @param core_id Logical core ID
 * @param config Global configuration
 * @return 0 on success, negative on error
 */
int nat_core_init(struct nat_core_ctx *ctx, unsigned int core_id,
                  const struct cgnat_config *config);

/**
 * Cleanup per-core NAT context
 * 
 * @param ctx Per-core NAT context
 */
void nat_core_cleanup(struct nat_core_ctx *ctx);

/**
 * Process outbound packet (private -> public translation)
 * 
 * @param ctx Per-core NAT context
 * @param m Packet mbuf
 * @return 0 on success (packet translated), negative on drop
 */
int nat_process_outbound(struct nat_core_ctx *ctx, struct rte_mbuf *m);

/**
 * Process inbound packet (public -> private translation)
 * 
 * @param ctx Per-core NAT context
 * @param m Packet mbuf
 * @return 0 on success (packet translated), negative on drop
 */
int nat_process_inbound(struct nat_core_ctx *ctx, struct rte_mbuf *m);

/**
 * Age out expired NAT sessions
 * Called periodically by worker cores
 * 
 * @param ctx Per-core NAT context
 * @return Number of sessions expired
 */
int nat_expire_sessions(struct nat_core_ctx *ctx);

/**
 * Get per-core statistics
 * 
 * @param ctx Per-core NAT context
 * @param stats Output statistics buffer
 */
void nat_get_stats(const struct nat_core_ctx *ctx, struct core_stats *stats);

/**
 * Allocate port from pool
 * 
 * @param pool Port pool
 * @return Allocated port number, or 0 on failure
 */
uint16_t port_pool_alloc(struct port_pool *pool);

/**
 * Free port back to pool
 * 
 * @param pool Port pool
 * @param port Port number to free
 */
void port_pool_free(struct port_pool *pool, uint16_t port);

/**
 * Check if port is allocated
 * 
 * @param pool Port pool
 * @param port Port number to check
 * @return true if allocated, false otherwise
 */
bool port_pool_is_allocated(const struct port_pool *pool, uint16_t port);

#endif /* NAT_ENGINE_H */
