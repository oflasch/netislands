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

#define NETISLAND_VERSION "0.1.0"

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

#define BUFLEN 4096 
#define BACKLOG 1024 
#define MAX_HOSTNAME_LENGTH 1024

typedef struct {
  int port; 
  unsigned n_neighbors;
  char remote_hostname[MAX_HOSTNAME_LENGTH]; // TODO we need an array of remote hostnames
  int remote_port; // TODO we need an array of remote ports
  Queue *message_queue;
  mtx_t *message_queue_mutex; 
  thrd_t thread;
  int exit_flag;
  // TODO
} Island;


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


/*
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

int island_send(const Island *island, const char *message) {
  return EXIT_FAILURE; // TODO
}

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

static int receive_until_close(int connfd, char *message_buf, const long message_buf_size, long *message_length) {
  struct sockaddr_in cliaddr;
  socklen_t clilen = sizeof(cliaddr);
  long message_buf_remaining = message_buf_size; 
  char *message_buf_pos = message_buf;
  for (;;) {
    ssize_t bytes_received = recvfrom(connfd, message_buf_pos, message_buf_remaining, 0, (struct sockaddr *)&cliaddr, &clilen);
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
  return EXIT_FAILURE; // we should never end up here
}

int island_thread(void *args) {
  Island *island = (Island*) args;
  int listenfd, connfd;
  fd_set fd_read_set;
  socklen_t clilen;
  char mesg[BUFLEN];

  if ((listenfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1) {
    perror("socket");
    return EXIT_FAILURE;
  }
  struct sockaddr_in servaddr;
  memset((char *) &servaddr, 0, sizeof(servaddr));
  servaddr.sin_family = AF_INET;
  servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
  servaddr.sin_port = htons(island->port);
  struct timeval select_timeout = {0, 500000}; // 0.5sec
  if (bind(listenfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) == -1) {
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
      struct sockaddr_in cliaddr;
      clilen = sizeof(cliaddr);
      if ((connfd = accept(listenfd, (struct sockaddr *)&cliaddr, &clilen)) == -1) {
        perror("accept");
        return EXIT_FAILURE;
      }
      //printf("+ Server accepted a connection.\n");
      long mesg_len = 0;
      receive_until_close(connfd, mesg, BUFLEN, &mesg_len);

      // allocate memory and store the received message in the islands message_queue...
      char *new_message = (char *) malloc(mesg_len);
      strcpy(new_message, mesg);
      mtx_lock(island->message_queue_mutex);
      queue_enqueue(island->message_queue, new_message);
      mtx_unlock(island->message_queue_mutex);

      //printf("Server received %ld chars:\n", mesg_len);
      //printf("-------------------------------------------------------------------------------\n");
      //printf("%s", new_message);
      //printf("-------------------------------------------------------------------------------\n");
      //printf("- Client closed the connection.\n");
    }
  }
  printf("Received server thread exit flag, exiting.\n");
  close(listenfd);

  return EXIT_SUCCESS;
}

static int connect_send_close(const int port, const char *message, const long message_length) {
  struct sockaddr_in servaddr;
  memset((char *) &servaddr, 0, sizeof(servaddr));
  servaddr.sin_family = AF_INET;
  servaddr.sin_addr.s_addr = inet_addr("127.0.0.1");
  servaddr.sin_port = htons(port);

  int sockfd, connfd;

  if ((sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1) {
    perror("socket");
    return EXIT_FAILURE;
  }

  if ((connfd = connect(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr))) == -1) { // TODO use select for timeouts
    return EXIT_FAILURE;
  }

  long remaining = message_length;
  char *message_pos = (char *) message;
  while (remaining > 0) {
    ssize_t bytes_send = send(sockfd, message_pos, remaining, 0);
    if (bytes_send < 0) {
      perror("send");
      return EXIT_FAILURE;
    } else if (bytes_send == 0) {
      break; // socket closed by server
    } else {
      message_pos += bytes_send;
      remaining -= bytes_send;
    }
  }

  close(connfd);
  close(sockfd);
  return EXIT_SUCCESS;
}

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
  netisland_init();

  if (4 != argc) {
    printf("usage: test_island port remote_hostname:remote_port time_to_live\n");
    return 1;
  }
  Island island;
  island.port = atoi(argv[1]); 
  island.n_neighbors = 1; // TODO
  if (parse_hostname_port_string(argv[2], island.remote_hostname, &island.remote_port) == EXIT_FAILURE) {
    return(EXIT_FAILURE);
  }
  Queue message_queue;
  queue_init(&message_queue);
  island.message_queue = &message_queue;
  mtx_t message_queue_mutex;
  mtx_init(&message_queue_mutex, mtx_plain);
  island.message_queue_mutex = &message_queue_mutex;
  island.exit_flag = 0;

  printf("Island port: %d remote_hostname: %s remote_port: %d\n",
      island.port, island.remote_hostname, island.remote_port);

  if (thrd_create(&island.thread, &island_thread, &island) != thrd_success) {
    perror("thrd_create");
    return EXIT_FAILURE;
  }
  // test loop, send some messages to the remote island until time_remaining seconds are up...
  unsigned time_remaining = atoi(argv[3]);
  while (time_remaining > 0) {
    sleep(1);
    char send_message[1024];
    sprintf(send_message, "Message from port %d: %d seconds remaining until our island sinks!\n",
        island.port, time_remaining);
    connect_send_close(island.remote_port, send_message, 1024); // TODO
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
  // TODO cleanup island and free all elements of its message_queue
  mtx_destroy(island.message_queue_mutex);
  netisland_shutdown();
  return EXIT_SUCCESS;
}

