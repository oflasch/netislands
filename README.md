# netislands
2015 Oliver Flasch
All rights reserved.

## Introduction

Netislands is a tiny network library to support massively parallel distributed
computing tasks, such as evolutionary algorithms, through a model of connected
islands. An island is an abstract object supporting the following operations:

1. `int island_init(Island *island, const int port, const unsigned n_neighbors, const char *neighbor_addresses[n_neighbors], const unsigned msg_queue_length)`
   initializes an island listening on  `port` that has outgoing connections to
   `n_neighbors` with addresses `neighbor_addresses` (an array of strings in
   format `host_name:port`) and a queue for `msg_queue_length` incoming
   messages.
2. `int island_send(const Island *island, const char *message)` sends the
   string `message` to all neighbors of an `island`.
3. `char *island_dequeue_message(const Island *island)` dequeues the oldest
   message from `island`s message queue and returns it. If no message is
   present, 0 (NULL) is returned. The caller is responsible to call `free()`
   on the message returned after use.
4. `int island_destroy(Island *island)` cleanups an `island`.

TODO
