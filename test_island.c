#include "tinycthread.h"

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


typedef struct {
  int local_port; 
  int remote_port; 
  // TODO
} Island;


void netisland_init() {
#ifdef _WIN32
  WSADATA ws_data;
  int err = WSAStartup(MAKEWORD(2, 2), &ws_data);
  if (err != 0) {
    printf("WSAStartup failed (%d)", err);
    return;
  }
#endif
}

void netisland_shutdown() {
#ifdef _WIN32
  WSACleanup();
#endif
}


/*
int island_initialize(Island *island, const char *address,
                      const unsigned n_neighbors, const char *neighbor_addresses[n_neighbors],
                      const unsigned msg_queue_length) {
  return EXIT_FAILURE; // TODO
}

int island_send(const Island *island, const char *message) {
  return EXIT_FAILURE; // TODO
}

char *island_dequeue_message(const Island *island) {
  return "NOT IMPLEMENTED\n"; // TODO
}

int island_destroy(Island *island) {
  return EXIT_FAILURE; // TODO
}
*/

static int server_thread_exit_flag = 0;

int server_thread(void *args) {
  Island *island = (Island*) args;

  int listenfd, connfd;
  struct sockaddr_in servaddr, cliaddr;
  socklen_t clilen;
  char mesg[BUFLEN];

  if ((listenfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1) {
    perror("socket");
    return EXIT_FAILURE;
  }

  memset((char *) &servaddr, 0, sizeof(servaddr));
  servaddr.sin_family = AF_INET;
  servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
  servaddr.sin_port = htons(island->local_port);
  if (bind(listenfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) == -1) {
    perror("bind");
    return EXIT_FAILURE;
  }

  if (listen(listenfd, BACKLOG) == -1) {
    perror("listen");
    return EXIT_FAILURE;
  }
  printf("Server socket connected at port %d. Listening for a TCP connection...\n",
      island->local_port);

  while (!server_thread_exit_flag) {
    clilen = sizeof(cliaddr);
    // TODO DEBUG use select with a short timeout instead of accept!
    if ((connfd = accept(listenfd, (struct sockaddr *)&cliaddr, &clilen)) == -1) {
      perror("accept");
      return EXIT_FAILURE;
    }
    printf("Server accepted a connection.\n");

    int n = 0;
    do {
      if ((n = recvfrom(connfd, mesg, BUFLEN, 0, (struct sockaddr *)&cliaddr, &clilen)) == -1) {
        perror("recvfrom");
        return EXIT_FAILURE;
      }
      char ok_mesg[] = "OK\n";
      if (sendto(connfd, ok_mesg, sizeof(ok_mesg), 0, (struct sockaddr *)&cliaddr, sizeof(cliaddr)) == -1) {
        perror("sendto");
        return EXIT_FAILURE;
      }
      mesg[n] = 0;
      printf("Server received %d chars:\n", n);
      printf("%s",mesg);
    } while (n != 0);
    printf("Client closed the connection.\n");
    close(connfd);
  }
  printf("Received server thread exit flag, exiting.\n");
  close(listenfd);

  return EXIT_SUCCESS;
}

int main(int argc, char* argv[]) {
  if (3 != argc) {
    printf("usage: test_island local_port remote_port\n");
    return 1;
  }
  Island island = {
    .local_port = atoi(argv[1]), 
    .remote_port = atoi(argv[2])
  };

  printf("Island local_port: %d remote_port: %d\n", island.local_port, island.remote_port);
  thrd_t t;
  if (thrd_create(&t, &server_thread, &island) != thrd_success) {
    perror("thrd_create");
    return EXIT_FAILURE;
  }
  //thrd_join(t, NULL); // wait for the server thread to exit 
  sleep(30); // run for some time...
  server_thread_exit_flag = 1; // ...then signal the server thread to exit
  thrd_join(t, NULL); // wait for the server thread to exit 

  printf("Server thread exited, exiting.\n\n");
  return EXIT_SUCCESS;
}

