# Makefile for netisland
# 2015 Oliver Flasch
# All rights reserved.
#

# check if we're on Windows...
ifeq ($(findstring mingw32, $(MAKE)), mingw32)
WINDOWS = 1
endif

# compiler settings...
CC = gcc
ifeq ($(findstring gcc, $(CC)), gcc)
CFLAGS = -W -std=c99 -pedantic -O3 -c -I.
else
CFLAGS = -W -O3 -c -I.
endif
LFLAGS =
LIBS =

# Unix-like systems need pthread...
ifndef WINDOWS
LIBS += -lpthread 
endif

# Linux also need rt
ifeq ($(UNAME), Linux)
LIBS += -lrt
endif

# MinGW32 GCC 4.5 link problem fix...
ifdef WINDOWS
ifeq ($(findstring 4.5.,$(shell g++ -dumpversion)), 4.5.)
LFLAGS += -static-libgcc
endif
endif

# misc system commands...
ifdef WINDOWS
RM = del /Q
else
RM = rm -f
endif

# executable file ending...
ifdef WINDOWS
EXE = .exe
else
EXE =
endif

# object files...
OBJS = test_island.o tinycthread.o queue.o

# targets...
all: test_island$(EXE)

clean:
	$(RM) $(EXE) test_island$(EXE) $(OBJS)

test_island$(EXE): $(OBJS)
	$(CC) $(LFLAGS) -o $@ $(OBJS) $(LIBS)

%.o: %.cpp
	$(CC) $(CFLAGS) $<

%.o: %.c
	$(CC) $(CFLAGS) $<

# dependencies...
test_island.o: test_island.c tinycthread.h queue.h
tinycthread.o: tinycthread.c tinycthread.h
queue.o: queue.c queue.h 

