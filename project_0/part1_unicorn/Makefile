# Part 1: build the Unicorn hello. Needs libunicorn-dev (Project 1's emulator).
CC      ?= cc
CFLAGS  ?= -O2 -Wall
UNICORN := $(shell pkg-config --cflags --libs unicorn)

hello: hello.c
	$(CC) $(CFLAGS) -o hello hello.c $(UNICORN)

clean:
	rm -f hello

.PHONY: clean
