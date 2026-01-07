CC = gcc
CFLAGS = -Wall -Wextra -O2
LDFLAGS = -lm

TARGETS = process_generator.out clk.out scheduler.out process.out test_generator.out

build: $(TARGETS)

process_generator.out: process_generator.c headers.h
	$(CC) $(CFLAGS) process_generator.c -o process_generator.out $(LDFLAGS)

clk.out: clk.c headers.h
	$(CC) $(CFLAGS) clk.c -o clk.out $(LDFLAGS)

scheduler.out: scheduler.c headers.h
	$(CC) $(CFLAGS) scheduler.c -o scheduler.out $(LDFLAGS)

process.out: process.c headers.h
	$(CC) $(CFLAGS) process.c -o process.out $(LDFLAGS)

test_generator.out: test_generator.c
	$(CC) $(CFLAGS) test_generator.c -o test_generator.out $(LDFLAGS)

clean:
	rm -f $(TARGETS) processes.txt scheduler.log scheduler.perf

all: clean build

run:
	./process_generator.out

test:
	./test_generator.out

.PHONY: build clean all run test