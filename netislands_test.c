/* netislands_test.c
 * Copyright (c) 2015 Oliver Flasch. All rights reserved.
 */

#define NETISLANDS_DEBUG
#include "netislands.h"

#ifdef _WIN32
  #include <windows.h>
#else
  #include <unistd.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NETISLAND_TEST_TIMESTEP_USECS 500000
#define NETISLAND_TEST_MAX_FAILURES 16


int parse_hostname_port_string(char *s, char *hostname, int *port) {
  char *hostname_string = strtok(s, ":");
  if (hostname_string == 0) {
    printf("parse_hostname_port_string: invalid hostname:port syntax.\n");
    return EXIT_FAILURE;
  }
  strcpy(hostname, hostname_string);
  char *port_string = strtok(0, ":");
  if (port_string == 0) {
    printf("parse_hostname_port_string: invalid hostname:port syntax.\n");
    return EXIT_FAILURE;
  }
  int port_int = atoi(port_string); 
  if (port_int == 0) {
    printf("parse_hostname_port_string: invalid hostname:port syntax.\n");
    return EXIT_FAILURE;
  }
  *port = port_int; 
  return EXIT_SUCCESS;
}

int main(int argc, char* argv[]) {
  if (argc < 3) {
    printf("usage: %s time_to_live port [neighbor_hostname:port]*\n", argv[0]);
    return 1;
  }
  unsigned long time_remaining = 1e6 * atoi(argv[1]);
  // init Netislands_Island...
  Netislands_Island island;
  const unsigned n_neighbors = argc - 3;
  char *neighbor_hostnames[n_neighbors];
  int neighbor_ports[n_neighbors];
  for (unsigned i = 0; i < n_neighbors; i++) {
    char *neighbor_hostname = malloc(NETISLANDS_MAX_HOSTNAME_LENGTH);
    neighbor_hostnames[i] = neighbor_hostname;
    if (parse_hostname_port_string(argv[i + 3], neighbor_hostname, &neighbor_ports[i]) == EXIT_FAILURE) {
      return(EXIT_FAILURE);
    }
  }
  island_init(&island, atoi(argv[2]),
              n_neighbors, (const char **) neighbor_hostnames, neighbor_ports,
              NETISLAND_TEST_MAX_FAILURES);

  // test code...
  printf("Island initialized at port: %d\n", island.port);
  // test loop, send some messages to the remote island until time_remaining seconds are up...
  while (time_remaining > 0) {
    usleep(NETISLAND_TEST_TIMESTEP_USECS);
    char data_message[1024];
    sprintf(data_message, "Message from port %d: %lu microseconds remaining until our island sinks!\n",
            island.port, time_remaining);
    island_send(&island, data_message);
    time_remaining -= NETISLAND_TEST_TIMESTEP_USECS;
      
    // dequeue and print all messages from our queue...
    mtx_lock(island.message_queue_mutex);
    if (queue_length(island.message_queue)) {
      printf("=MESSAGE=QUEUE=================================================================\n");
    }
    mtx_unlock(island.message_queue_mutex);
    char *recv_message;
    while ((recv_message = island_dequeue_message(&island)) != NULL) {
      printf("%s", recv_message);
      printf("-------------------------------------------------------------------------------\n");
      free(recv_message);
    }
  }
  // cleanup island...
  island_destroy(&island);
  return EXIT_SUCCESS;
}

