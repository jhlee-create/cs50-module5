# Makefile -- build the crawler-page indexer.

CC=gcc
UTILDIR=../utils
LIBDIR=../lib
CPPFLAGS=-I$(UTILDIR)
CFLAGS=-Wall -pedantic -std=c11 -g
LDFLAGS=-L$(LIBDIR)
LIBS=-lutils -lcurl
PROGRAM=indexer

.PHONY: all clean

# Build the indexer after the shared utility archive is available.
all: $(PROGRAM)

$(LIBDIR)/libutils.a:
	$(MAKE) -C $(UTILDIR)

# Place the utility and curl libraries after the object that refers to them.
$(PROGRAM): indexer.o $(LIBDIR)/libutils.a
	$(CC) $(CFLAGS) $(LDFLAGS) indexer.o $(LIBS) -o $@

# Recompile when the program or either shared file interface changes.
indexer.o: indexer.c $(UTILDIR)/indexio.h $(UTILDIR)/pageio.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

# Remove only files generated in the indexer directory.
clean:
	rm -f $(PROGRAM) *.o
