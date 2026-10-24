# Makefile -- build the page-I/O and index-I/O module tests.

CC=gcc
UTILDIR=../utils
LIBDIR=../lib
CPPFLAGS=-I$(UTILDIR)
CFLAGS=-Wall -pedantic -std=c11 -g
LDFLAGS=-L$(LIBDIR)
LIBS=-lutils -lcurl
PROGRAMS=pageio_test indexio_test
INDEXER_LOGIC=indexer_logic.o

.PHONY: all clean

# Build both module tests after the utility archive is available.
all: $(PROGRAMS)

$(LIBDIR)/libutils.a:
	$(MAKE) -C $(UTILDIR)

# The page-I/O test needs only functions from the shared utility library.
pageio_test: pageio_test.o $(LIBDIR)/libutils.a
	$(CC) $(CFLAGS) $(LDFLAGS) $< $(LIBS) -o $@

# Link the index-I/O program with the functions that create and inspect an index.
indexio_test: indexio_test.o $(INDEXER_LOGIC) $(LIBDIR)/libutils.a
	$(CC) $(CFLAGS) $(LDFLAGS) $< $(INDEXER_LOGIC) $(LIBS) -o $@

# Compile the index operations without the command-line main function.
$(INDEXER_LOGIC): ../indexer/indexer.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -DINDEXER_TEST_BUILD -c $< -o $@

# Compile each test whenever its source changes.
%.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

# Remove only generated test executables and objects.
clean:
	rm -f $(PROGRAMS) *.o
