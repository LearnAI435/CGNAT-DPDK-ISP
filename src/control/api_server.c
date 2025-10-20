/* SPDX-License-Identifier: MIT */
/**
 * @file api_server.c
 * @brief REST API server for control and monitoring
 */

#include "cgnat_types.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>

/* External references */
extern struct cgnat_global_stats *g_global_stats;
extern pthread_mutex_t g_stats_lock;

static int api_running = 0;
static pthread_t api_thread;

static void
send_json_response(int client_fd, const char *json)
{
    char response[8192];
    int response_len = snprintf(response, sizeof(response),
                               "HTTP/1.1 200 OK\r\n"
                               "Content-Type: application/json\r\n"
                               "Content-Length: %lu\r\n"
                               "Access-Control-Allow-Origin: *\r\n"
                               "Connection: close\r\n"
                               "\r\n"
                               "%s",
                               strlen(json), json);
    
    write(client_fd, response, response_len);
}

static void
handle_stats_request(int client_fd)
{
    char json[4096];
    
    pthread_mutex_lock(&g_stats_lock);
    
    snprintf(json, sizeof(json),
            "{\n"
            "  \"packets_rx\": %lu,\n"
            "  \"packets_tx\": %lu,\n"
            "  \"packets_dropped\": %lu,\n"
            "  \"bytes_rx\": %lu,\n"
            "  \"bytes_tx\": %lu,\n"
            "  \"active_sessions\": %lu,\n"
            "  \"sessions_created\": %lu,\n"
            "  \"sessions_expired\": %lu,\n"
            "  \"port_allocation_failures\": %lu,\n"
            "  \"avg_latency_us\": %.2f,\n"
            "  \"max_latency_us\": %lu,\n"
            "  \"timestamp\": %lu\n"
            "}",
            g_global_stats->total_packets_rx,
            g_global_stats->total_packets_tx,
            g_global_stats->total_packets_dropped,
            g_global_stats->total_bytes_rx,
            g_global_stats->total_bytes_tx,
            g_global_stats->total_nat_sessions,
            g_global_stats->total_nat_created,
            g_global_stats->total_nat_expired,
            g_global_stats->total_port_alloc_fail,
            g_global_stats->avg_latency_us,
            g_global_stats->max_latency_us,
            g_global_stats->timestamp);
    
    pthread_mutex_unlock(&g_stats_lock);
    
    send_json_response(client_fd, json);
}

static void *
api_server_thread(void *arg)
{
    uint16_t port = *(uint16_t *)arg;
    int server_fd, client_fd;
    struct sockaddr_in address;
    int opt = 1;
    char buffer[4096];
    
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
    
    if (listen(server_fd, 10) < 0) {
        perror("listen failed");
        close(server_fd);
        return NULL;
    }
    
    printf("[API] REST API server listening on port %u\n", port);
    
    while (api_running) {
        socklen_t addrlen = sizeof(address);
        client_fd = accept(server_fd, (struct sockaddr *)&address, &addrlen);
        if (client_fd < 0) {
            if (api_running)
                perror("accept failed");
            continue;
        }
        
        /* Read HTTP request */
        ssize_t bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);
        if (bytes_read > 0) {
            buffer[bytes_read] = '\0';
            
            /* Parse request */
            if (strstr(buffer, "GET /api/stats")) {
                handle_stats_request(client_fd);
            } else {
                /* 404 Not Found */
                char *response = "HTTP/1.1 404 Not Found\r\n"
                                "Content-Length: 0\r\n"
                                "Connection: close\r\n\r\n";
                write(client_fd, response, strlen(response));
            }
        }
        
        close(client_fd);
    }
    
    close(server_fd);
    printf("[API] REST API server stopped\n");
    return NULL;
}

int
api_server_start(uint16_t port)
{
    static uint16_t server_port;
    server_port = port;
    
    api_running = 1;
    
    if (pthread_create(&api_thread, NULL, api_server_thread, &server_port) != 0) {
        fprintf(stderr, "Failed to create API server thread\n");
        return -1;
    }
    
    return 0;
}
