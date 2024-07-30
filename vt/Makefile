include ../config.mk

CFLAGS += -I..

OBJ = vt100.o
PROG = test

%.o: %.c
	$(CC) -c -o $@ $(CFLAGS) $<

all: libvt.a

libvt.a: $(OBJ)
	ar -rcs $@ $^

test: test.o $(OBJ)
	$(CC) -o $@ $(CFLAGS) $< $(OBJ) $(LDFLAGS) 

.PHONY: clean check

check: test
	@for i in testdata/*.in; do \
		printf '%s\n' "$$i"; \
		./test < "$$i" | cmp - "$${i%.in}.out"; \
	done

clean:
	rm -f $(OBJ)
	rm -f $(OBJ:.o=.gcno) $(OBJ:.o=.gcda)
	rm -f $(PROG)
	rm -f $(PROG:.o=.gcno) $(PROG:.o=.gcda)
	rm -f libvt.a
