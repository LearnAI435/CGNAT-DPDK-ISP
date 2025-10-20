/* SPDX-License-Identifier: MIT */
/**
 * @file config.c
 * @brief Configuration file parsing (YAML)
 */

#include "cgnat_types.h"
#include <stdio.h>
#include <stdlib.h>

/* Placeholder for YAML parsing */
/* In production, use libyaml or similar */

int
config_load(const char *filename, struct cgnat_config *config)
{
    printf("[CONFIG] Loading configuration from %s\n", filename);
    
    /* TODO: Implement YAML parsing */
    /* For now, use default configuration */
    
    return 0;
}

int
config_validate(const struct cgnat_config *config)
{
    if (config->num_public_ips == 0) {
        fprintf(stderr, "Error: No public IPs configured\n");
        return -1;
    }
    
    if (config->num_workers == 0 || config->num_workers > MAX_CORES) {
        fprintf(stderr, "Error: Invalid number of workers\n");
        return -1;
    }
    
    return 0;
}
