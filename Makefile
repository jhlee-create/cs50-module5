# Makefile -- build the utilities, crawler, indexer, querier, and module checks.

.PHONY: all test clean

# Build the utility archive before linking programs and tests that use it.
all:
	$(MAKE) -C utils
	$(MAKE) -C crawler
	$(MAKE) -C indexer
	$(MAKE) -C querier
	$(MAKE) -C test

# Run checks that do not require access to the course website.
test: all
	$(MAKE) -C crawler test

# Remove generated files from each build directory.
clean:
	$(MAKE) -C test clean
	$(MAKE) -C querier clean
	$(MAKE) -C indexer clean
	$(MAKE) -C crawler clean
	$(MAKE) -C utils clean
