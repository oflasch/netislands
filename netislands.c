/* netislands.c
 * Copyright (c) 2015 Oliver Flasch. All rights reserved.
 */

#include "netislands.h"

#ifdef _WIN32
  #define _WIN32_WINNT 0x501
  #define _CRT_SECURE_NO_WARNINGS
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <windows.h>
#else
  //#define _POSIX_C_SOURCE 200809L
  #ifdef __APPLE__
    #define _DARWIN_UNLIMITED_SELECT
  #endif
  #include <unistd.h>
  #include <netdb.h>
  #include <fcntl.h>
  #include <sys/types.h>
  #include <sys/socket.h>
  #include <sys/time.h>
  #include <netinet/in.h>
  #include <netinet/tcp.h>
  #include <arpa/inet.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <signal.h>
#include <errno.h>
#include <limits.h>

#ifdef _WIN32
  #define close(a) closesocket(a)
  #define getsockopt(a,b,c,d,e) getsockopt((a),(b),(c),(char*)(d),(e))
  #define setsockopt(a,b,c,d,e) setsockopt((a),(b),(c),(char*)(d),(e))

  #undef  errno
  #define errno WSAGetLastError()

  #undef  EWOULDBLOCK
  #define EWOULDBLOCK WSAEWOULDBLOCK

  const char *inet_ntop(int af, const void *src, char *dst, socklen_t size) {
    union { struct sockaddr sa; struct sockaddr_in sai;
            struct sockaddr_in6 sai6; } addr;
    int res;
    memset(&addr, 0, sizeof(addr));
    addr.sa.sa_family = af;
    if (af == AF_INET6) {
      memcpy(&addr.sai6.sin6_addr, src, sizeof(addr.sai6.sin6_addr));
    } else {
      memcpy(&addr.sai.sin_addr, src, sizeof(addr.sai.sin_addr));
    }
    res = WSAAddressToString(&addr.sa, sizeof(addr), 0, dst, (LPDWORD) &size);
    if (res != 0) return NULL;
    return dst;
  }
#endif

#define NETISLANDS_TAG_LENGTH 8
#define NETISLANDS_JOIN_TAG "join---"
#define NETISLANDS_DATA_TAG "data---"
#define NETISLANDS_PROTOCOL_HEADER_LENGTH NETISLANDS_PROTOCOL_ID_LENGTH + NETISLANDS_PROTOCOL_VERSION_LENGTH + NETISLANDS_TAG_LENGTH


typedef struct {
  char hostname[NETISLANDS_MAX_HOSTNAME_LENGTH];
  int port;
  unsigned failure_count;
} Neighbor;


static int n_islands = 0;

static void netislands_init() {
#ifdef _WIN32
  WSADATA ws_data;
  int err = WSAStartup(MAKEWORD(2, 2), &ws_data);
  if (err != 0) {
    printf("WSAStartup failed (%d)", err);
    return;
  }
#endif
}

static void netislands_shutdown() {
#ifdef _WIN32
  WSACleanup();
#endif
}

static int receive_until_close(int connfd, char *message_buf, const long message_buf_size, long *message_length) {
  struct sockaddr_in client_address;
  socklen_t client_address_length = sizeof(client_address);
  long message_buf_remaining = message_buf_size; 
  char *message_buf_pos = message_buf;
  for (;;) {
    ssize_t bytes_received = recvfrom(connfd, message_buf_pos, message_buf_remaining, 0,
                                      (struct sockaddr *)&client_address, &client_address_length);
    if (bytes_received < 0) {
#ifdef NETISLANDS_DEBUG
      perror("recvfrom");
#endif
      // TODO maybe close connfd after error?
      return EXIT_FAILURE;
    } else if (bytes_received == 0) {
      // TODO client closes connection?
      if (close(connfd) == -1) {
#ifdef NETISLANDS_DEBUG
        perror("receive_until_close: close connfd");
#endif
        //return EXIT_FAILURE;
        return EXIT_SUCCESS; // ignore error
      }
      return EXIT_SUCCESS;
    } else {
      message_buf_pos += bytes_received;
      message_buf_remaining -= bytes_received;
      *message_length += bytes_received;
    }
  }
  fprintf(stderr, "netislands internal error. (%s line# %d)\n", __FILE__, __LINE__);
  return EXIT_FAILURE; // we should never end up here
}

static int check_netislands_message(const char *message, const long message_length) {
  if (message_length < NETISLANDS_PROTOCOL_HEADER_LENGTH) {
    return EXIT_FAILURE;
  }
  if (strncmp(message, NETISLANDS_PROTOCOL_ID NETISLANDS_PROTOCOL_VERSION,
              NETISLANDS_PROTOCOL_ID_LENGTH + NETISLANDS_PROTOCOL_VERSION_LENGTH)) {
    return EXIT_FAILURE;
  }
  // TODO
  return EXIT_SUCCESS;
}

static int neighbor_equal_predicate(const void *a, const void *b) {
  if (a == b) {
    return 1;
  } else {
    const Neighbor *neighbor_a = (Neighbor *) a;
    const Neighbor *neighbor_b = (Neighbor *) b;
    if (strcmp(neighbor_a->hostname, neighbor_b->hostname) == 0
        && neighbor_a->port == neighbor_b->port) { // ignore failure_count during comparison
      return 1;
    } else {
      return 0;
    }
  }
}

static int island_thread_main(void *args) {
  Netislands_Island *island = (Netislands_Island*) args;
  int listenfd, connfd;
  fd_set fd_read_set;
  socklen_t client_address_length;

  if ((listenfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1) {
#ifdef NETISLANDS_DEBUG
    perror("socket");
#endif
    return EXIT_FAILURE;
  }
  // allow socket address reuse to avoid "address alreay in use" errors...
  int option_value = 1;
  setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &option_value, sizeof option_value);

  struct sockaddr_in server_address;
  memset((char *) &server_address, 0, sizeof(server_address));
  server_address.sin_family = AF_INET;
  server_address.sin_addr.s_addr = htonl(INADDR_ANY);
  server_address.sin_port = htons(island->port);
  struct timeval select_timeout = {0, 500000}; // 0.5sec
  if (bind(listenfd, (struct sockaddr *)&server_address, sizeof(server_address)) == -1) {
#ifdef NETISLANDS_DEBUG
    perror("bind");
#endif
    return EXIT_FAILURE;
  }
  if (listen(listenfd, NETISLANDS_BACKLOG) == -1) {
#ifdef NETISLANDS_DEBUG
    perror("listen");
#endif
    return EXIT_FAILURE;
  }
#ifdef NETISLANDS_DEBUG
  fprintf(stderr, "Server socket bound to port %d. Listening for a TCP connection...\n",
          island->port);
#endif

  while (!island->exit_flag) {
    FD_ZERO(&fd_read_set);
    FD_SET(listenfd, &fd_read_set);
    int select_ret = select(listenfd + 1, &fd_read_set, 0, 0, &select_timeout);
    if (select_ret == -1) {
#ifdef NETISLANDS_DEBUG
      perror("select");
#endif
      return EXIT_FAILURE;
    }
    if (select_ret > 0) {
      struct sockaddr_in client_address;
      client_address_length = sizeof(client_address);
      if ((connfd = accept(listenfd, (struct sockaddr *)&client_address, &client_address_length)) == -1) {
#ifdef NETISLANDS_DEBUG
        perror("accept");
#endif
        return EXIT_FAILURE;
      }
#ifdef NETISLANDS_DEBUG
      fprintf(stderr, "+ Server accepted a connection.\n");
#endif
      long message_length = 0;
      if (receive_until_close(connfd, island->message_buffer, NETISLANDS_SERVER_BUFFER_LENGTH, &message_length) == EXIT_FAILURE) {
#ifdef NETISLANDS_DEBUG
        fprintf(stderr, "Network error while receiving netislands message, ignoring message. (%s line# %d)\n", __FILE__, __LINE__);
#endif
        continue;
      }
      
      if (check_netislands_message(island->message_buffer, message_length) == EXIT_FAILURE) {
#ifdef NETISLANDS_DEBUG
        fprintf(stderr, "Received malformed netislands message, ignoring. (%s line# %d)\n", __FILE__, __LINE__);
#endif
        continue;
      }
      char tag[NETISLANDS_TAG_LENGTH];
      strncpy(tag, island->message_buffer + NETISLANDS_PROTOCOL_ID_LENGTH + NETISLANDS_PROTOCOL_VERSION_LENGTH, NETISLANDS_TAG_LENGTH); 

      // handle message based on message tag...
      if (strcmp(NETISLANDS_DATA_TAG, tag) == 0) { // data message
        // if the maximum message queue length is not exceeded, allocate memory
        // and store the received data message content in the islands message_queue,
        // otherwise drop an old message first...
        mtx_lock(island->message_queue_mutex);
        if (island->max_message_queue_length != 0
            && queue_length(island->message_queue) >= island->max_message_queue_length) {
          char *message_to_drop;
          queue_dequeue(island->message_queue, (void **) &message_to_drop);
          free(message_to_drop);
        }
        char *new_message = (char *) malloc(message_length - NETISLANDS_PROTOCOL_HEADER_LENGTH);
        strncpy(new_message, island->message_buffer + NETISLANDS_PROTOCOL_HEADER_LENGTH, message_length - NETISLANDS_PROTOCOL_HEADER_LENGTH);
        queue_enqueue(island->message_queue, new_message);
        mtx_unlock(island->message_queue_mutex);
      } else if (strcmp(NETISLANDS_JOIN_TAG, tag) == 0) { // join message
        // create and initialize new neighbor...
        Neighbor *new_neighbor = (Neighbor *) malloc(sizeof(Neighbor));
        inet_ntop(AF_INET, &(client_address.sin_addr), new_neighbor->hostname, NETISLANDS_MAX_HOSTNAME_LENGTH);
        char port_string[NETISLANDS_MAX_PORT_STRING_LENGTH];
        strncpy(port_string, island->message_buffer + NETISLANDS_PROTOCOL_HEADER_LENGTH, 8);
        new_neighbor->port = atoi(port_string);  
        new_neighbor->failure_count = 0;
        // check if the new neighbor is already in the neighbor queue...
        mtx_lock(island->neighbor_queue_mutex);
        long new_neighbor_index = queue_first_index_of(island->neighbor_queue, new_neighbor, &neighbor_equal_predicate); 
        if (new_neighbor_index == -1) { // unknown new neighbor, add it to the queue...
          queue_enqueue(island->neighbor_queue, new_neighbor);
        } else { // known new neighbor, reset its failure count...
          free(new_neighbor);
          Neighbor *known_neighbor;
          queue_get_index(island->neighbor_queue, new_neighbor_index, (void **) &known_neighbor);
          known_neighbor->failure_count = 0;
        }
        mtx_unlock(island->neighbor_queue_mutex);
      } else { // unknown message tag
#ifdef NETISLANDS_DEBUG
        fprintf(stderr, "Received netislands message with unknown tag '%s', ignoring. (%s line# %d)\n", tag, __FILE__, __LINE__);
#endif
        continue;
      }
    }
  }
#ifdef NETISLANDS_DEBUG
  fprintf(stderr, "Island server thread clean exit.\n");
#endif
  if (close(listenfd) == -1) {
#ifdef NETISLANDS_DEBUG
    perror("island_thread_main: close listenfd");
#endif
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}

static int send_all(const int sockfd, const char *message, const long message_length) {
  long remaining = message_length;
  char *message_pos = (char *) message;
  while (remaining > 0) {
    ssize_t bytes_send = send(sockfd, message_pos, remaining, 0);
    if (bytes_send < 0) {
#ifdef NETISLANDS_DEBUG
      perror("send");
#endif
      return EXIT_FAILURE;
    } else if (bytes_send == 0) {
#ifdef NETISLANDS_DEBUG
      fprintf(stderr, "Socket closed by receiving neighbor, ignoring. (%s line# %d)\n", __FILE__, __LINE__);
#endif
      break; // socket closed by server
    } else {
      message_pos += bytes_send;
      remaining -= bytes_send;
    }
  }
  return EXIT_SUCCESS;
}

static int connect_send_close(const char *hostname, const int port, const char *tag, const char *message, const long message_length) {
  struct sockaddr_in server_address;
  memset((char *) &server_address, 0, sizeof(server_address));
  server_address.sin_family = AF_INET;
  server_address.sin_addr.s_addr = inet_addr(hostname); // hostname has to be in x.x.x.x format
  server_address.sin_port = htons(port);

  int sockfd, connfd;

  // create client socket and connect to neighbor...
  if ((sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1) {
#ifdef NETISLANDS_DEBUG
    perror("socket");
#endif
    return EXIT_FAILURE;
  }
  if ((connfd = connect(sockfd, (struct sockaddr *)&server_address, sizeof(server_address))) == -1) { // TODO use select for timeouts
#ifdef NETISLANDS_DEBUG
    perror("connect");
#endif
    return EXIT_FAILURE;
  }

  // send message protocol header and protocol version...
  if (send_all(sockfd, NETISLANDS_PROTOCOL_ID NETISLANDS_PROTOCOL_VERSION,
               NETISLANDS_PROTOCOL_ID_LENGTH + NETISLANDS_PROTOCOL_VERSION_LENGTH) == EXIT_FAILURE) {
    return EXIT_FAILURE;
  }
  // send message tag...
  if (send_all(sockfd, tag, NETISLANDS_TAG_LENGTH) == EXIT_FAILURE) {
    return EXIT_FAILURE;
  }
  // send message data...
  if (send_all(sockfd, message, message_length) == EXIT_FAILURE) {
    return EXIT_FAILURE;
  }

  // close connection...
  /*
  // TODO does connfd need to be closed?
  if (close(connfd) == -1) {
//#ifdef NETISLANDS_DEBUG
    perror("connect_send_close: close connfd");
//#endif
    //return EXIT_FAILURE;
    return EXIT_SUCCESS; // ignore error
  }
  */
  // TODO does sockfd need to be closed?...
  if (close(sockfd) == -1) {
#ifdef NETISLANDS_DEBUG
    perror("connect_send_close: close sockfd");
#endif
    //return EXIT_FAILURE;
    return EXIT_SUCCESS; // ignore error
  }
  return EXIT_SUCCESS;
}

static void send_join_to_neighbor(void *element, void *args) {
  Neighbor *neighbor = (Neighbor *) element;
  const char *message = (char *) args;
  const long message_length = strlen(message) + 1; // include the terminating \0
  const int ret = connect_send_close(neighbor->hostname, neighbor->port, NETISLANDS_JOIN_TAG, message, message_length);
  if (ret == EXIT_FAILURE) {
    neighbor->failure_count++;
#ifdef NETISLANDS_DEBUG
    fprintf(stderr, "send_join_to_neighbor: Failed to send to neighbor %s:%d. (failure count = %u)\n",
            neighbor->hostname, neighbor->port, neighbor->failure_count);
#endif
  }
}

static void remove_failed_neighbors(Queue *neighbor_queue, const unsigned max_failures) {
  if (max_failures == 0) { // do nothing when neighbor removal is disabled
    return;
  }
  // this assumes that we have a mutex lock on neighbor_queue!
  long failed_neighbor_index;
  for (;;) { 
    // search for a failed neighbor...
    failed_neighbor_index = -1;
    long current_index = 0;
    for (QueueNode *iterator = neighbor_queue->front; iterator != NULL; iterator = iterator->next) {
      const Neighbor *current_neighbor = (const Neighbor *) iterator->data; 
      if (current_neighbor->failure_count >= max_failures) {
        failed_neighbor_index = current_index;
        break;
      } else {
        current_index++;
      }
    }
    if (failed_neighbor_index != -1) { // failed neighbor found, remove from queue...
      Neighbor *failed_neighbor;
      queue_remove_index(neighbor_queue, failed_neighbor_index, (void **) &failed_neighbor); 
#ifdef NETISLANDS_DEBUG
      fprintf(stderr, "Removed failed neighbor %s:%d. (failure count = %u)\n",
              failed_neighbor->hostname, failed_neighbor->port, failed_neighbor->failure_count);
#endif
      free(failed_neighbor);
    } else { // no failed_neighbor found, break from loop
      break;
    }
  }
}

static void island_send_join(const Netislands_Island *island, const char *message) {
  mtx_lock(island->neighbor_queue_mutex);
  queue_for_each(island->neighbor_queue, &send_join_to_neighbor, (void *) message);
  remove_failed_neighbors(island->neighbor_queue, island->max_failures);
  mtx_unlock(island->neighbor_queue_mutex);
}

static void send_data_to_neighbor(void *element, void *args) {
  Neighbor *neighbor = (Neighbor *) element;
  const char *message = (char *) args;
  const long message_length = strlen(message) + 1; // include the terminating \0
  const int ret = connect_send_close(neighbor->hostname, neighbor->port, NETISLANDS_DATA_TAG, message, message_length);
  if (ret == EXIT_FAILURE) {
    neighbor->failure_count++;
#ifdef NETISLANDS_DEBUG
    fprintf(stderr, "send_data_to_neighbor: Failed to send to neighbor %s:%d. (failure count = %u)\n",
            neighbor->hostname, neighbor->port, neighbor->failure_count);
#endif
  }
}

int island_init(Netislands_Island *island,
                const int port,
                const unsigned n_neighbors,
                const char *neighbor_hostnames[n_neighbors],
                const int neighbor_ports[n_neighbors],
                const long max_message_queue_length,
                const unsigned max_failures) {
  // maybe initialize network...
  if (0 == n_islands) {
    netislands_init();
  }
  n_islands++;
  // init port...
  island->port = port; 
  // init neighbor queue...
  Queue *neighbor_queue = malloc(sizeof(Queue));
  queue_init(neighbor_queue);
  island->neighbor_queue = neighbor_queue;
  mtx_t *neighbor_queue_mutex = malloc(sizeof(mtx_t));
  mtx_init(neighbor_queue_mutex, mtx_plain);
  island->neighbor_queue_mutex = neighbor_queue_mutex;
  // init message queue...
  Queue *message_queue = malloc(sizeof(Queue));
  queue_init(message_queue);
  island->message_queue = message_queue;
  mtx_t *message_queue_mutex = malloc(sizeof(mtx_t));
  mtx_init(message_queue_mutex, mtx_plain);
  island->message_queue_mutex = message_queue_mutex;
  // init neighbors...
  for (unsigned i = 0; i < n_neighbors; i++) {
    Neighbor *new_neighbor = (Neighbor *) malloc(sizeof(Neighbor));
    // resolve new neighbor hostname...
    struct hostent *hostname_entries;
    if ((hostname_entries = gethostbyname(neighbor_hostnames[i])) == NULL) {
      fprintf(stderr, "island_init: error resolving neighbor hostname '%s'.\n",
              neighbor_hostnames[i]);
      return EXIT_FAILURE;
    }
    // init neighbor fields...
    inet_ntop(AF_INET, hostname_entries->h_addr_list[0], new_neighbor->hostname, NETISLANDS_MAX_HOSTNAME_LENGTH);
    new_neighbor->port = neighbor_ports[i];
    new_neighbor->failure_count = 0;
    mtx_lock(island->neighbor_queue_mutex);
    queue_enqueue(island->neighbor_queue, new_neighbor);
    mtx_unlock(island->neighbor_queue_mutex);
  }
  // init message buffer by allocating memory on heap...
  island->message_buffer = malloc(NETISLANDS_SERVER_BUFFER_LENGTH);
  // init other members...
  island->exit_flag = 0;
  island->max_message_queue_length = max_message_queue_length;
  island->max_failures = max_failures;
  // init island thread...
  thrd_t island_thread = (thrd_t) malloc(sizeof(thrd_t));
  island->thread = island_thread;
  if (thrd_create(&island->thread, &island_thread_main, island) != thrd_success) {
#ifdef NETISLANDS_DEBUG
    perror("thrd_create");
#endif
    return EXIT_FAILURE;
  }
  // introduce this island to its neighbors...
  char port_string[NETISLANDS_MAX_PORT_STRING_LENGTH];
  sprintf(port_string, "%d", island->port);
  island_send_join(island, port_string); // send port number

  return EXIT_SUCCESS; 
}

int island_send(const Netislands_Island *island, const char *message) {
  mtx_lock(island->neighbor_queue_mutex);
  queue_for_each(island->neighbor_queue, &send_data_to_neighbor, (void *) message);
  remove_failed_neighbors(island->neighbor_queue, island->max_failures);
  mtx_unlock(island->neighbor_queue_mutex);
  return EXIT_SUCCESS;
}

char *island_dequeue_message(const Netislands_Island *island) {
  mtx_lock(island->message_queue_mutex);
  if (0 == queue_length(island->message_queue)) {
    mtx_unlock(island->message_queue_mutex);
    return NULL;
  } else {
    char *recv_message;
    queue_dequeue(island->message_queue, (void **) &recv_message);
    mtx_unlock(island->message_queue_mutex);
    return recv_message;
  }
}

int island_destroy(Netislands_Island *island) {
  // cleanup island server thread...
  island->exit_flag = 1; // signal the server thread to exit
  thrd_join(island->thread, NULL); // wait for the server thread to exit 
  thrd_detach(island->thread);
  // cleanup island message queue... 
  char *message;
  while ((message = island_dequeue_message(island)) != NULL) {
    free(message);
  }
  mtx_destroy(island->message_queue_mutex);
  free(island->message_queue_mutex);
  free(island->message_queue);
  // cleanup island neighbor queue... 
  Neighbor *neighbor;
  mtx_lock(island->neighbor_queue_mutex);
  while (queue_dequeue(island->neighbor_queue, (void **) &neighbor) != EXIT_FAILURE) {
    free(neighbor);
  }
  mtx_unlock(island->neighbor_queue_mutex);
  free(island->neighbor_queue_mutex);
  free(island->neighbor_queue);
  // cleanup message buffer...
  free(island->message_buffer);
  // maybe deinitialize network...
  n_islands--;
  if (0 == n_islands) {
    netislands_shutdown();
  }
#ifdef NETISLANDS_DEBUG
  fprintf(stderr, "Clean exit of island at port: %d\n", island->port);
#endif
  return EXIT_SUCCESS;
}

