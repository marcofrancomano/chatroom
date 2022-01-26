CC = gcc -Wall -O0 -g
LDFLAGS = -lpthread
ARCH = $(shell uname -m)

all: client server

server: common.h main.c msg_queue.c send_recv.c util.c
	rm -f build/*.o
	$(CC) -c msg_queue.c -o build/msg_queue.o
	$(CC) -c main.c -o build/main.o
	$(CC) -c send_recv.c -o build/send_recv.o
	$(CC) -c util.c -o build/util.o
	$(CC) -o server build/*.o $(LDFLAGS)

client:
	ln -s -f client-$(ARCH) client

:phony
clean:
	rm -f client server build/*.o
