/* netislands.h
 * Copyright (c) 2015 Oliver Flasch. All rights reserved.
 */

#ifndef NETISLANDS_H
#define NETISLANDS_H

#include "tinycthread.h"
#include "queue.h"


#define NETISLANDS_VERSION "1.0-0"
#define NETISLANDS_PROTOCOL_VERSION "1.0-0"
#define NETISLANDS_PROTOCOL_VERSION_LENGTH 5
#define NETISLANDS_PROTOCOL_ID "netislands"
#define NETISLANDS_PROTOCOL_ID_LENGTH 10

#define NETISLANDS_SERVER_BUFFER_LENGTH 16384 // 16 kiB 
#define NETISLANDS_BACKLOG 1024 
#define NETISLANDS_MAX_HOSTNAME_LENGTH 1024
#define NETISLANDS_MAX_PORT_STRING_LENGTH 8


typedef struct {
  int port; 
  Queue *neighbor_queue;
  mtx_t *neighbor_queue_mutex; 
  long max_message_queue_length;
  unsigned max_failures;
  Queue *message_queue;
  mtx_t *message_queue_mutex; 
  thrd_t thread;
  int exit_flag;
} Netislands_Island;


int island_init(Netislands_Island *island,
                const int port,
                const unsigned n_neighbors,
                const char *neighbor_hostnames[n_neighbors],
                const int neighbor_ports[n_neighbors],
                const long max_message_queue_length,
                const unsigned max_failures); 

int island_send(const Netislands_Island *island, const char *message);

char *island_dequeue_message(const Netislands_Island *island);

int island_destroy(Netislands_Island *island);

#endif

