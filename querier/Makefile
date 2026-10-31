# Makefile -- build the search-query program.

CC=gcc
UTILDIR=../utils
LIBDIR=../lib
CPPFLAGS=-I$(UTILDIR)
CFLAGS=-Wall -pedantic -std=c11 -g
LDFLAGS=-L$(LIBDIR)
LIBS=-lutils -lcurl
PROGRAM=query

.PHONY: all clean

# Build the query program after the shared utility archive is available.
all: $(PROGRAM)

$(LIBDIR)/libutils.a:
	$(MAKE) -C $(UTILDIR)

# Place shared libraries after the object that refers to their functions.
$(PROGRAM): query.o $(LIBDIR)/libutils.a
	$(CC) $(CFLAGS) $(LDFLAGS) query.o $(LIBS) -o $@

# Recompile when the program or any shared interface changes.
query.o: query.c $(UTILDIR)/indexio.h $(UTILDIR)/pageio.h \
	$(UTILDIR)/hash.h $(UTILDIR)/queue.h $(UTILDIR)/webpage.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

# Remove only files generated in the query directory.
clean:
	rm -f $(PROGRAM) *.o
