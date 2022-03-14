.PHONY: all

all:
	gcc -std=c99 -g -Wall -pedantic -D_DEFAULT_SOURCE replay_trace.c

check: all
	cppcheck --enable=all --std=c99 replay_trace.c

test: all
	./a.out volume_up.trace
