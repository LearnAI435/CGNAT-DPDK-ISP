/* SPDX-License-Identifier: MIT */
/**
 * @file dpdk_runtime.h
 * @brief DPDK initialization and runtime management
 */

#ifndef DPDK_RUNTIME_H
#define DPDK_RUNTIME_H

#include "cgnat_types.h"
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_mempool.h>

/**
 * Initialize DPDK EAL (Environment Abstraction Layer)
 * 
 * @param argc Argument count
 * @param argv Argument vector
 * @return Number of parsed arguments, negative on error
 */
int dpdk_eal_init(int argc, char **argv);

/**
 * Configure NIC port for DPDK
 * 
 * @param port_id Port identifier
 * @param num_queues Number of RX/TX queues (one per worker core)
 * @param mbuf_pool Memory pool for packet buffers
 * @return 0 on success, negative on error
 */
int dpdk_port_init(uint16_t port_id, uint16_t num_queues,
                   struct rte_mempool *mbuf_pool);

/**
 * Create packet buffer memory pool
 * 
 * @param name Pool name
 * @param num_mbufs Number of buffers
 * @param socket_id NUMA socket ID
 * @return Pointer to mempool, NULL on error
 */
struct rte_mempool *dpdk_create_mbuf_pool(const char *name,
                                          unsigned int num_mbufs,
                                          unsigned int socket_id);

/**
 * Start NIC port
 * 
 * @param port_id Port identifier
 * @return 0 on success, negative on error
 */
int dpdk_port_start(uint16_t port_id);

/**
 * Stop NIC port
 * 
 * @param port_id Port identifier
 */
void dpdk_port_stop(uint16_t port_id);

/**
 * Get link status
 * 
 * @param port_id Port identifier
 * @param link Output link status
 * @return 0 on success, negative on error
 */
int dpdk_get_link_status(uint16_t port_id, struct rte_eth_link *link);

/**
 * Worker core main loop (packet processing)
 * 
 * @param arg Pointer to worker-specific context
 * @return 0 on normal exit
 */
int dpdk_worker_main(void *arg);

#endif /* DPDK_RUNTIME_H */
