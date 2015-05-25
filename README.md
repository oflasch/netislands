# netislands
copyright (c) 2015 Oliver Flasch.
All rights reserved.


## Introduction

Netislands is a tiny network library to support highly scalable parallel
distributed computing tasks with modest bandwidth and latency demands, such as
evolutionary and genetic algorithms, through a model of connected islands. An
island is an abstract object supporting the following operations:

1. **Init:** `int island_init(Netislands_Island *island, const int port, const unsigned n_neighbors, const char *neighbor_hostnames[n_neighbors], const int neighbor_ports[n_neighbors], const long max_message_queue_length, const unsigned max_failures)` 
   initializes an island listening on  `port` that has outgoing connections to
   `n_neighbors` with hostnames `neighbor_hostnames` (an array of strings)
   and ports `neighbor_ports` (an array of ints). Received neighbor messages
   are stored in a queue of maximum length `max_message_queue_length`. If this
   length is exceeded, the oldest message in the queue will be silently dropped
   when a new message arrives. Set to `max_message_queue_length` to disable this
   behavior. Neighbors are considered as failed and will be removed if
   `max_failures` send attempt failed. Set this to `0` to disable neighbor
   removal.
2. **Send:** `int island_send(const Netislands_Island *island, const char *message)`
   sends the string `message` to all neighbors of an `island`.
3. **Dequeue Message:** `char *island_dequeue_message(const Netislands_Island *island)` dequeues the
   oldest message from `island`s message queue and returns it. If no message is
   present, 0 (NULL) is returned. The caller is responsible to call `free()`
   on the message returned after use.
4. **Destroy:** `int island_destroy(Netislands_Island *island)` cleanups an `island`.

The network topology is defined implicitly by the neighborhood relation,
enabling very good scalability. New islands announce their presence to their
defined neighbors when started, while unreachable neighbors are removed
automatically, increasing robustness. After a network of islands has been
set up, no central control instance is needed. Islands can freely join and
leave the network.


## Compatability

Netislands has been tested on Mac OS X 10.10.3. It should also work under
Linux and other POSIX-compatible operating systems. Support for Windows is
included, but untested.


## Installation and Use 

Just add the following source files to your project:

* `netislands.h`
* `netislands.c`
* `queue.h`
* `queue.c`
* `tinycthread.h`
* `tinycthread.c`

Then include the Netislands API via `#include "netislands.h"`.


## Demo / Test

Netislands contains `netislands_test.c`, a simple test driver and demo
application. It is built via the included `Makefile`'s `all` target, i.e.
by just typing `make` on the command line.


## License

Netislands is copyright (c) 2015 Oliver Flasch and released under the MIT
licence. See the file `LICENSE` for details.

TinyCThread uses the following license:

>Copyright (c) 2012 Marcus Geelnard
>              2013-2014 Evan Nemerson
>
>This software is provided 'as-is', without any express or implied
>warranty. In no event will the authors be held liable for any damages
>arising from the use of this software.
>
>Permission is granted to anyone to use this software for any purpose,
>including commercial applications, and to alter it and redistribute it
>freely, subject to the following restrictions:
>
>    1. The origin of this software must not be misrepresented; you must not
>    claim that you wrote the original software. If you use this software
>    in a product, an acknowledgment in the product documentation would be
>    appreciated but is not required.
>
>    2. Altered source versions must be plainly marked as such, and must not be
>    misrepresented as being the original software.
>
>    3. This notice may not be removed or altered from any source
>    distribution.

