CC=gcc
CFLAGS=-Werror -g -std=gnu11 -I. -Itokenizer -Ivector

# Collect all object files
SHELL_OBJS=shell.o tokenizer/tokens.o vector/vect.o

ifeq ($(shell uname), Darwin)
    LEAKTEST ?= leaks --atExit --
else
    LEAKTEST ?= valgrind --leak-check=full
endif

.PHONY: all valgrind clean test

all: shell

valgrind: shell
	$(LEAKTEST) ./shell

shell-tests : %-tests: %
	env python3 tests/$*_tests.py

test: shell-tests

clean:
	rm -rf *.o tokenizer/*.o vector/*.o
	rm -f shell

shell: $(SHELL_OBJS)
	$(CC) $(CFLAGS) -o shell $(SHELL_OBJS)

shell.o: shell.c tokenizer/tokens.h
	$(CC) $(CFLAGS) -c -o shell.o shell.c

tokenizer/tokens.o: tokenizer/tokens.c tokenizer/tokens.h vector/vect.h
	$(CC) $(CFLAGS) -c -o tokenizer/tokens.o tokenizer/tokens.c

vector/vect.o: vector/vect.c vector/vect.h
	$(CC) $(CFLAGS) -c -o vector/vect.o vector/vect.c