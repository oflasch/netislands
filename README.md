# netislands
2015 Oliver Flasch
All rights reserved.

## Introduction

Netislands is a tiny network library to support massively parallel distributed
computing tasks, such as evolutionary algorithms, through a model of connected
islands. An island is an abstract object supporting the following operations:

1. `int island_init(Netislands_Island *island, const int port, const unsigned n_neighbors, const char *neighbor_hostnames[n_neighbors], const int neighbor_ports[n_neighbors])` 
   initializes an island listening on  `port` that has outgoing connections to
   `n_neighbors` with hostnames `neighbor_hostnames` (an array of strings)
   and ports `neighbor_ports` (an array of ints).
2. `int island_send(const Netislands_Island *island, const char *message)`
   sends the string `message` to all neighbors of an `island`.
3. `char *island_dequeue_message(const Netislands_Island *island)` dequeues the
   oldest message from `island`s message queue and returns it. If no message is
   present, 0 (NULL) is returned. The caller is responsible to call `free()`
   on the message returned after use.
4. `int island_destroy(Netislands_Island *island)` cleanups an `island`.

TODO
