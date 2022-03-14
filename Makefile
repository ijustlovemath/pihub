.PHONY: all

all:
	gcc -std=c99 -g -Wall -pedantic replay_trace.c

test: all
	./a.out volume_up.trace
