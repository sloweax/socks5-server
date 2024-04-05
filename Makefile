CC=cc
CFLAGS=-Wall -Wextra -O2
BINDSTPATH=/usr/local/bin

all: ss5

%.o: %.c
	$(CC) $(CFLAGS) $^ -c -o $@

ss5: ss5.o util.o socks5.o ll.o
	$(CC) $(CFLAGS) $^ -o $@

debug: CFLAGS+=-g -DS5DEBUG
debug: all

clean:
	rm -f *.o ss5

install: all
	mkdir -p $(BINDSTPATH)
	install -m755 ss5 $(BINDSTPATH)

uninstall:
	rm -f $(BINDSTPATH)/ss5

.PHONY: clean debug all install uninstall
