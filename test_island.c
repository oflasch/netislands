#include "tinycthread.h"
#include "queue.h"

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

#define NETISLANDS_VERSION "1.0-0"
#define NETISLANDS_PROTOCOL_VERSION "1.0-0"
#define NETISLANDS_PROTOCOL_VERSION_LENGTH 5
#define NETISLANDS_PROTOCOL_ID "netislands"
#define NETISLANDS_PROTOCOL_ID_LENGTH 10

#define BUFLEN 4096 
#define BACKLOG 1024 
#define MAX_HOSTNAME_LENGTH 1024

typedef struct { // TODO should go into Island.h
  int port; 
  unsigned n_neighbors;
  Queue *neighbor_queue;
  mtx_t *neighbor_queue_mutex; 
  Queue *message_queue;
  mtx_t *message_queue_mutex; 
  thrd_t thread;
  int exit_flag;
  // TODO
} Island;

typedef struct {
  char hostname[MAX_HOSTNAME_LENGTH];
  int port;
} Neighbor;


static int n_islands = 0;

static void netisland_init() {
#ifdef _WIN32
  WSADATA ws_data;
  int err = WSAStartup(MAKEWORD(2, 2), &ws_data);
  if (err != 0) {
    printf("WSAStartup failed (%d)", err);
    return;
  }
#endif
}

static void netisland_shutdown() {
#ifdef _WIN32
  WSACleanup();
#endif
}

#define NETISLANDS_TAG_LENGTH 8
#define NETISLANDS_JOIN_TAG "join---"
#define NETISLANDS_DATA_TAG "data---"
#define NETISLANDS_PROTOCOL_HEADER_LENGTH NETISLANDS_PROTOCOL_ID_LENGTH + NETISLANDS_PROTOCOL_VERSION_LENGTH + NETISLANDS_TAG_LENGTH

static int receive_until_close(int connfd, char *message_buf, const long message_buf_size, long *message_length) {
  struct sockaddr_in client_address;
  socklen_t client_address_length = sizeof(client_address);
  long message_buf_remaining = message_buf_size; 
  char *message_buf_pos = message_buf;
  for (;;) {
    ssize_t bytes_received = recvfrom(connfd, message_buf_pos, message_buf_remaining, 0,
                                      (struct sockaddr *)&client_address, &client_address_length);
    if (bytes_received < 0) {
      perror("recvfrom");
      return EXIT_FAILURE;
    } else if (bytes_received == 0) {
      close(connfd);
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

int check_netislands_message(const char *message, const long message_length) {
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

int island_thread(void *args) {
  Island *island = (Island*) args;
  int listenfd, connfd;
  fd_set fd_read_set;
  socklen_t client_address_length;
  char mesg[BUFLEN];

  if ((listenfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1) {
    perror("socket");
    return EXIT_FAILURE;
  }
  struct sockaddr_in server_address;
  memset((char *) &server_address, 0, sizeof(server_address));
  server_address.sin_family = AF_INET;
  server_address.sin_addr.s_addr = htonl(INADDR_ANY);
  server_address.sin_port = htons(island->port);
  struct timeval select_timeout = {0, 500000}; // 0.5sec
  if (bind(listenfd, (struct sockaddr *)&server_address, sizeof(server_address)) == -1) {
    perror("bind");
    return EXIT_FAILURE;
  }
  if (listen(listenfd, BACKLOG) == -1) {
    perror("listen");
    return EXIT_FAILURE;
  }
  printf("Server socket bound to port %d. Listening for a TCP connection...\n",
      island->port);

  while (!island->exit_flag) {
    FD_ZERO(&fd_read_set);
    FD_SET(listenfd, &fd_read_set);
    int select_ret = select(listenfd + 1, &fd_read_set, 0, 0, &select_timeout);
    if (select_ret == -1) {
      perror("select");
      return EXIT_FAILURE;
    }
    if (select_ret > 0) {
      struct sockaddr_in client_address;
      client_address_length = sizeof(client_address);
      if ((connfd = accept(listenfd, (struct sockaddr *)&client_address, &client_address_length)) == -1) {
        perror("accept");
        return EXIT_FAILURE;
      }
      //printf("+ Server accepted a connection.\n");
      long mesg_len = 0;
      receive_until_close(connfd, mesg, BUFLEN, &mesg_len);
      
      if (check_netislands_message(mesg, mesg_len) == EXIT_FAILURE) {
        fprintf(stderr, "Received malformed netislands message, ignoring. (%s line# %d)\n", __FILE__, __LINE__);
        continue;
      }
      char tag[NETISLANDS_TAG_LENGTH];
      strncpy(tag, mesg + NETISLANDS_PROTOCOL_ID_LENGTH + NETISLANDS_PROTOCOL_VERSION_LENGTH, NETISLANDS_TAG_LENGTH); 

      // handle message based on message tag...
      if (strcmp(NETISLANDS_DATA_TAG, tag) == 0) { // data message
        // allocate memory and store the received data message content in the islands message_queue...
        char *new_message = (char *) malloc(mesg_len - NETISLANDS_PROTOCOL_HEADER_LENGTH);
        strncpy(new_message, mesg + NETISLANDS_PROTOCOL_HEADER_LENGTH, mesg_len - NETISLANDS_PROTOCOL_HEADER_LENGTH);
        mtx_lock(island->message_queue_mutex);
        queue_enqueue(island->message_queue, new_message);
        mtx_unlock(island->message_queue_mutex);
      } else if (strcmp(NETISLANDS_JOIN_TAG, tag) == 0) { // join message
        // get hostname and port of the new neighbor...
        Neighbor *new_neighbor = (Neighbor *) malloc(sizeof(Neighbor));
        inet_ntop(AF_INET, &(client_address.sin_addr), new_neighbor->hostname, MAX_HOSTNAME_LENGTH);
        //new_neighbor->port = ntohs(client_address.sin_port);
        char port_string[8];
        strncpy(port_string, mesg + NETISLANDS_PROTOCOL_HEADER_LENGTH, 8);
        new_neighbor->port = atoi(port_string);  
        // check if the new neighbor is already in the neighbor queue...
        int known_neighbor_flag = 0; 
        mtx_lock(island->message_queue_mutex);
        // TODO iterate and check...
        mtx_unlock(island->message_queue_mutex);
        if (!known_neighbor_flag) { // unknown new neighbar, add it to the queue...
          printf("New neighbor %s:%d joined.\n", new_neighbor->hostname, new_neighbor->port); // TODO DEBUG
          mtx_lock(island->message_queue_mutex);
          queue_enqueue(island->neighbor_queue, new_neighbor);
          mtx_unlock(island->message_queue_mutex);
        } else { // known new neighbor, ignore...
          printf("Neighbor %s:%d rejoined.\n", new_neighbor->hostname, new_neighbor->port); // TODO DEBUG
          free(new_neighbor);
        }
      } else { // unknown message tag
        fprintf(stderr, "Received netislands message with unknown tag '%s', ignoring. (%s line# %d)\n", tag, __FILE__, __LINE__);
        continue;
      }
    }
  }
  printf("Received server thread exit flag, exiting.\n");
  close(listenfd);

  return EXIT_SUCCESS;
}

static int send_all(const int sockfd, const char *message, const long message_length) {
  long remaining = message_length;
  char *message_pos = (char *) message;
  while (remaining > 0) {
    ssize_t bytes_send = send(sockfd, message_pos, remaining, 0);
    if (bytes_send < 0) {
      perror("send");
      return EXIT_FAILURE;
    } else if (bytes_send == 0) {
      fprintf(stderr, "Socket closed by receiving neighbor, ignoring. (%s line# %d)\n", __FILE__, __LINE__);
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
  server_address.sin_addr.s_addr = inet_addr(hostname);
  server_address.sin_port = htons(port);

  int sockfd, connfd;

  // create client socket and connect to neighbor...
  if ((sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1) {
    perror("socket");
    return EXIT_FAILURE;
  }
  if ((connfd = connect(sockfd, (struct sockaddr *)&server_address, sizeof(server_address))) == -1) { // TODO use select for timeouts
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
  close(connfd);
  close(sockfd);
  return EXIT_SUCCESS;
}

static void send_join_to_neighbor(void *element, void *args) {
  const Neighbor *neighbor = (Neighbor *) element;
  const char *message = (char *) args;
  const long message_length = strlen(message);
  connect_send_close(neighbor->hostname, neighbor->port, NETISLANDS_JOIN_TAG, message, message_length);
}

static void island_send_join(const Island *island, const char *message) {
  mtx_lock(island->neighbor_queue_mutex);
  queue_for_each(island->neighbor_queue, &send_join_to_neighbor, (void *) message);
  mtx_unlock(island->neighbor_queue_mutex);
}

static void send_data_to_neighbor(void *element, void *args) {
  const Neighbor *neighbor = (Neighbor *) element;
  const char *message = (char *) args;
  const long message_length = strlen(message);
  connect_send_close(neighbor->hostname, neighbor->port, NETISLANDS_DATA_TAG, message, message_length);
}

/*
// TODO public interface to Island...
int island_init(Island *island, const int port,
                const unsigned n_neighbors, const char *neighbor_addresses[n_neighbors],
                const unsigned msg_queue_length) {
  if (0 == n_islands) {
    netisland_init();
  }
  // TODO
  n_islands++;
  return EXIT_FAILURE; // TODO
}
*/

int island_send(const Island *island, const char *message) {
  mtx_lock(island->neighbor_queue_mutex);
  queue_for_each(island->neighbor_queue, &send_data_to_neighbor, (void *) message);
  mtx_unlock(island->neighbor_queue_mutex);
  return EXIT_SUCCESS;
}

/*
char *island_dequeue_message(const Island *island) {
  return "NOT IMPLEMENTED\n"; // TODO
}

int island_destroy(Island *island) {
  // TODO
  n_islands--;
  if (0 == n_islands) {
    netisland_shutdown();
  }
  return EXIT_FAILURE; // TODO
}
*/

int parse_hostname_port_string(char *s, char hostname[MAX_HOSTNAME_LENGTH], int *port) {
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
  netisland_init(); // TODO should be automatic

  if (argc < 3) {
    printf("usage: test_island time_to_live port [neighbor_hostname:port]*\n");
    return 1;
  }
  unsigned time_remaining = atoi(argv[1]);
  // init Island...
  Island island;
  island.port = atoi(argv[2]); 
  // init neighbor queue...
  Queue neighbor_queue;
  queue_init(&neighbor_queue);
  island.neighbor_queue = &neighbor_queue;
  mtx_t neighbor_queue_mutex;
  mtx_init(&neighbor_queue_mutex, mtx_plain);
  island.neighbor_queue_mutex = &neighbor_queue_mutex;
  // init message queue...
  Queue message_queue;
  queue_init(&message_queue);
  island.message_queue = &message_queue;
  mtx_t message_queue_mutex;
  mtx_init(&message_queue_mutex, mtx_plain);
  island.message_queue_mutex = &message_queue_mutex;
  // init neighbors...
  island.n_neighbors = argc - 3;
  for (int i = 3; i < argc; i++) {
    Neighbor *new_neighbor = (Neighbor *) malloc(sizeof(Neighbor));
    if (parse_hostname_port_string(argv[i], new_neighbor->hostname, &new_neighbor->port) == EXIT_FAILURE) {
      return(EXIT_FAILURE);
    }
    mtx_lock(island.neighbor_queue_mutex);
    queue_enqueue(island.neighbor_queue, new_neighbor);
    mtx_unlock(island.neighbor_queue_mutex);
  }
  // init flags...
  island.exit_flag = 0;
  // init island thread...
  if (thrd_create(&island.thread, &island_thread, &island) != thrd_success) {
    perror("thrd_create");
    return EXIT_FAILURE;
  }
  // introduce this island to its neighbors...
  char port_string[8];
  sprintf(port_string, "%d", island.port);
  island_send_join(&island, port_string); // send port number

  // test code...
  printf("Island initialized at port: %d\n", island.port);
  // test loop, send some messages to the remote island until time_remaining seconds are up...
  while (time_remaining > 0) {
    sleep(1);
    char data_message[1024];
    sprintf(data_message, "Message from port %d: %d seconds remaining until our island sinks!\n",
            island.port, time_remaining);
    island_send(&island, data_message);
    time_remaining--;
      
    // dequeue and print all messages from our queue...
    mtx_lock(island.message_queue_mutex);
    if (queue_length(island.message_queue)) {
      printf("=MESSAGE=QUEUE=================================================================\n");
    }
    while (queue_length(island.message_queue)) {
      char *recv_message;
      queue_dequeue(island.message_queue, (void **) &recv_message);
      printf("%s", recv_message);
      printf("-------------------------------------------------------------------------------\n");
      free(recv_message);
    }
    mtx_unlock(island.message_queue_mutex);
  }
  island.exit_flag = 1; // signal the server thread to exit
  thrd_join(island.thread, NULL); // wait for the server thread to exit 

  printf("Server thread exited, exiting.\n\n");
  // TODO cleanup island and free all elements of its neighbor_queue and message_queue
  mtx_destroy(island.neighbor_queue_mutex);
  mtx_destroy(island.message_queue_mutex);
  netisland_shutdown(); // TODO should be automatic
  return EXIT_SUCCESS;
}

