/* SPDX-License-Identifier: MIT */
/**
 * @file prometheus.c
 * @brief Prometheus HTTP exporter
 */

#include "telemetry.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* External reference to global stats (set by main) */
extern struct cgnat_global_stats *g_global_stats;
extern pthread_mutex_t g_stats_lock;

static int prometheus_running = 0;
static pthread_t prometheus_thread;

static void *
prometheus_server_thread(void *arg)
{
    uint16_t port = *(uint16_t *)arg;
    int server_fd, client_fd;
    struct sockaddr_in address;
    int opt = 1;
    char buffer[16384];
    char response[32768];
    
    /* Create socket */
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == 0) {
        perror("socket failed");
        return NULL;
    }
    
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);
    
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        close(server_fd);
        return NULL;
    }
    
    if (listen(server_fd, 3) < 0) {
        perror("listen failed");
        close(server_fd);
        return NULL;
    }
    
    printf("[PROMETHEUS] HTTP server listening on port %u\n", port);
    
    while (prometheus_running) {
        socklen_t addrlen = sizeof(address);
        client_fd = accept(server_fd, (struct sockaddr *)&address, &addrlen);
        if (client_fd < 0) {
            if (prometheus_running)
                perror("accept failed");
            continue;
        }
        
        /* Read HTTP request (we ignore it and just return metrics) */
        read(client_fd, buffer, sizeof(buffer) - 1);
        
        /* Generate Prometheus metrics */
        char metrics[16384];
        int metrics_len;
        
        pthread_mutex_lock(&g_stats_lock);
        metrics_len = telemetry_export_prometheus(g_global_stats, metrics, sizeof(metrics));
        pthread_mutex_unlock(&g_stats_lock);
        
        /* Build HTTP response */
        int response_len = snprintf(response, sizeof(response),
                                   "HTTP/1.1 200 OK\r\n"
                                   "Content-Type: text/plain; version=0.0.4\r\n"
                                   "Content-Length: %d\r\n"
                                   "Connection: close\r\n"
                                   "\r\n"
                                   "%s",
                                   metrics_len, metrics);
        
        write(client_fd, response, response_len);
        close(client_fd);
    }
    
    close(server_fd);
    printf("[PROMETHEUS] HTTP server stopped\n");
    return NULL;
}

int
telemetry_start_prometheus(uint16_t port)
{
    static uint16_t server_port;
    server_port = port;
    
    prometheus_running = 1;
    
    if (pthread_create(&prometheus_thread, NULL, prometheus_server_thread, &server_port) != 0) {
        fprintf(stderr, "Failed to create Prometheus thread\n");
        return -1;
    }
    
    return 0;
}
