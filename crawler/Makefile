# Makefile -- build the crawler with the shared page-I/O utility module.

CC=gcc
UTILDIR=../utils
LIBDIR=../lib
CPPFLAGS=-I$(UTILDIR) -I.
CFLAGS=-Wall -pedantic -std=c11 -g
LDFLAGS=-L$(LIBDIR)
LIBS=-lutils -lcurl
PROGRAMS=crawler

.PHONY: all test clean

# Build the crawler after the utility archive is available.
all: $(PROGRAMS)

# Build the utility library before linking the crawler against it.
$(LIBDIR)/libutils.a:
	$(MAKE) -C $(UTILDIR)

# Keep libraries after the object files that refer to their functions.
crawler: crawler.o crawlerlib.o $(LIBDIR)/libutils.a
	$(CC) $(CFLAGS) $(LDFLAGS) crawler.o crawlerlib.o $(LIBS) -o $@

# Recompile the crawler when either local interface or page-I/O changes.
crawler.o: crawler.c crawlerlib.h $(UTILDIR)/pageio.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

# Directory validation depends only on its own public interface.
crawlerlib.o: crawlerlib.c crawlerlib.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

# Run argument checks that do not require network access.
test: $(PROGRAMS)
	@./crawler >/dev/null 2>&1; test $$? -ne 0
	@./crawler "https://thayer.github.io/engs50/" /missing -1 >/dev/null 2>&1; test $$? -ne 0
	@echo "PASS crawler: invalid arguments rejected"

# Remove only files generated in the crawler directory.
clean:
	rm -f $(PROGRAMS) *.o
