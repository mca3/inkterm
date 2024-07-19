CC = cc
CFLAGS = -O0 -std=c99 -pedantic -g

%.o: %.c
	$(CC) -c -o $@ $(CFLAGS) $<

all: inkterm

inkterm: main.o vt100.o
	$(CC) -o $@ $(CFLAGS) $^

.PHONY: clean

clean:
	rm -f main.o vt100.o inkterm
