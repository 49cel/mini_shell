CC     = gcc
CFLAGS = -Wall -Wextra -std=c11 -O2

msh: shell.c
	$(CC) $(CFLAGS) -o msh shell.c

clean:
	rm -f msh

.PHONY: clean
