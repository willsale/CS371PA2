/*
# Copyright 2025 University of Kentucky
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# SPDX-License-Identifier: Apache-2.0
*/

/* 
Please specify the group members here
# Student #1: William Alexander
# Student #2: none :(
# Student #3: none :(
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <pthread.h>

#define MAX_EVENTS 64
#define MESSAGE_SIZE 16
#define PAYLOAD_SIZE 8
#define DEFAULT_CLIENT_THREADS 4
#define TIMEOUT_MS 500

char *server_ip = "127.0.0.1";
int server_port = 12345;
int num_client_threads = DEFAULT_CLIENT_THREADS;
int num_requests = 1000;

typedef struct {
    int client_id;
    int seq_num;
    char payload[PAYLOAD_SIZE];
} __attribute__((packed)) packet_t;

/* Structure for storing client thread data */
typedef struct {
    int epoll_fd;
    int socket_fd;
    struct sockaddr_in server_addr;
    long long total_rtt;
    long total_messages;
    int tx_cnt;
    int rx_cnt;
    int client_id;
} client_thread_data_t;

/* Client thread function */
void *client_thread_func(void *arg) {
    client_thread_data_t *data = (client_thread_data_t *)arg;
    struct epoll_event event, events[MAX_EVENTS];
    char recv_buf[MESSAGE_SIZE];
    struct timeval start, end;
 
    // Hint 1: register the "connected" client_thread's socket in the its epoll instance
    // Hint 2: use gettimeofday() and "struct timeval start, end" to record timestamp, which can be used to calculated RTT.
    // Changed to keep track of the packets
    for (int i = 0; i < num_requests; i++) {
        packet_t pkt;
        pkt.client_id = data->client_id;
        pkt.seq_num = i;
        snprintf(pkt.payload, PAYLOAD_SIZE, "Msg%04d", i);

        int acked = 0;
        while (!acked) {
            gettimeofday(&start, NULL);
            sendto(data->socket_fd, &pkt, sizeof(pkt), 0,
                   (struct sockaddr *)&data->server_addr, sizeof(data->server_addr));
            data->tx_cnt++;

            int nfds = epoll_wait(data->epoll_fd, events, MAX_EVENTS, TIMEOUT_MS);
            if (nfds > 0) {
                socklen_t addr_len = sizeof(data->server_addr);
                ssize_t bytes = recvfrom(data->socket_fd, recv_buf, MESSAGE_SIZE, 0,
                                         (struct sockaddr *)&data->server_addr, &addr_len);
                if (bytes > 0) {
                    packet_t *ack_pkt = (packet_t *)recv_buf;
                    if (ack_pkt->client_id == data->client_id && ack_pkt->seq_num == i) {
                        gettimeofday(&end, NULL);
                        long long rtt = (end.tv_sec - start.tv_sec) * 1000000LL + (end.tv_usec - start.tv_usec);
                        data->total_rtt += rtt;
                        data->total_messages++;
                        data->rx_cnt++;
                        acked = 1;
                    }
                }
            }
        }
    }
    return NULL;
}

/*
 * This function orchestrates multiple client threads to send requests to a server,
 * collect performance data of each threads, and compute aggregated metrics of all threads.
 */
void run_client() {
    pthread_t threads[num_client_threads];
    client_thread_data_t thread_data[num_client_threads];

    for (int i = 0; i < num_client_threads; i++) {
        thread_data[i].socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
        thread_data[i].epoll_fd = epoll_create1(0);
        thread_data[i].total_rtt = 0;
        thread_data[i].total_messages = 0;
        thread_data[i].tx_cnt = 0;
        thread_data[i].rx_cnt = 0;
        thread_data[i].client_id = i;

        thread_data[i].server_addr.sin_family = AF_INET;
        thread_data[i].server_addr.sin_port = htons(server_port);
        inet_pton(AF_INET, server_ip, &thread_data[i].server_addr.sin_addr);

        struct epoll_event event;
        event.events = EPOLLIN;
        event.data.fd = thread_data[i].socket_fd;
        epoll_ctl(thread_data[i].epoll_fd, EPOLL_CTL_ADD, thread_data[i].socket_fd, &event);

        pthread_create(&threads[i], NULL, client_thread_func, &thread_data[i]);
    }

    long long total_rtt = 0;
    long total_messages = 0;
    int total_tx = 0, total_rx = 0;



    // Hint: use thread_data to save the created socket and epoll instance for each thread
    // You will pass the thread_data to pthread_create() as below
    for (int i = 0; i < num_client_threads; i++) {
        pthread_join(threads[i], NULL);
        total_rtt += thread_data[i].total_rtt;
        total_messages += thread_data[i].total_messages;
        total_tx += thread_data[i].tx_cnt;
        total_rx += thread_data[i].rx_cnt;
        close(thread_data[i].socket_fd);
        close(thread_data[i].epoll_fd);
    }

    /* Keeps track of total RTT, total number of requests, and total number of packets */
    printf("Average RTT: %lld us\n", total_messages ? total_rtt / total_messages : 0);
    printf("Total Request Rate: %f messages/s\n", (double)total_messages / (total_rtt / 1000000.0));
    printf("Total Packets Sent: %d, Received: %d, Lost (retransmitted): %d\n", total_tx, total_rx, total_tx - total_rx);
}

/* Server implementation */
void run_server() {
    int server_fd, epoll_fd;
    struct sockaddr_in server_addr, client_addr;
    struct epoll_event event, events[MAX_EVENTS];

    server_fd = socket(AF_INET, SOCK_DGRAM, 0);
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(server_port);

    bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr));

    epoll_fd = epoll_create1(0);
    event.events = EPOLLIN;
    event.data.fd = server_fd;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &event);
    
    /* Server's run-to-completion event loop | changed to use UDP*/
    while (1) {
        int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        for (int i = 0; i < nfds; i++) {
            if (events[i].data.fd == server_fd) {
                char buffer[MESSAGE_SIZE];
                socklen_t addr_len = sizeof(client_addr);
                ssize_t recv_len = recvfrom(server_fd, buffer, sizeof(packet_t), 0,
                                            (struct sockaddr *)&client_addr, &addr_len);
                if (recv_len > 0) {
                    // Echo back the same packet as ACK
                    sendto(server_fd, buffer, recv_len, 0,
                           (struct sockaddr *)&client_addr, addr_len);
                }
            }
        }
    }

    close(server_fd);
    close(epoll_fd);
}

/* Main function, calls either run_server() or run_client() | if the arguments are invalid it will print how to use properly. */
int main(int argc, char *argv[]) {
    if (argc > 1 && strcmp(argv[1], "server") == 0) {
        if (argc > 2) server_ip = argv[2];
        if (argc > 3) server_port = atoi(argv[3]);
        run_server();
    } else if (argc > 1 && strcmp(argv[1], "client") == 0) {
        if (argc > 2) server_ip = argv[2];
        if (argc > 3) server_port = atoi(argv[3]);
        if (argc > 4) num_client_threads = atoi(argv[4]);
        if (argc > 5) num_requests = atoi(argv[5]);
        run_client();
    } else {
        printf("Usage: %s <server|client> [server_ip server_port num_client_threads num_requests]\n", argv[0]);
    }
    return 0;
}
