CC = gcc

CFLAGS = -std=c99 -O3 -march=native -fopenmp -Wall -Wextra
LDFLAGS = -fopenmp

TARGETS = compress decompress benchmark

.PHONY: all clean

all: $(TARGETS)

compress: src/compress.c src/shared.h
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

decompress: src/decompress.c src/shared.h
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

benchmark: src/benchmark.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

clean:
	rm -f $(TARGETS) tmp.* ver.*
