CC = gcc
all: prepare buildlib server client

prepare: 
	if [ ! -d bin ]; then mkdir bin; fi;

server: common.o server.o
	$(CC) -g -rdynamic -lmcrypt -llz4 -lpthread bin/server.o bin/common.o bin/ikcp.o bin/queue.o bin/hashtable.o bin/linklist.o bin/refcnt.o bin/rqueue.o bin/rbtree.o -o bin/server

client: common.o client.o
	$(CC) -g -rdynamic -lmcrypt -llz4 -lpthread bin/client.o bin/common.o bin/ikcp.o bin/queue.o bin/hashtable.o bin/linklist.o bin/refcnt.o bin/rqueue.o bin/rbtree.o -o bin/client

server.o: 
	$(CC) -g -rdynamic -c src/server.c -o bin/server.o

client.o: 
	$(CC) -g -rdynamic -c src/client.c -o bin/client.o

common.o: 
	$(CC) -g -rdynamic -c src/common.c -o bin/common.o

buildlib: ikcp.o queue.o hashtable.o linklist.o refcnt.o rqueue.o rbtree.o
	echo "lib compiled."

ikcp.o:
	$(CC) -g -rdynamic -c src/lib/ikcp.c -o bin/ikcp.o

queue.o:
	$(CC) -g -rdynamic -c src/lib/queue.c -o bin/queue.o

hashtable.o:
	$(CC) -g -rdynamic -c src/lib/hashtable.c -o bin/hashtable.o

linklist.o:
	$(CC) -g -rdynamic -c src/lib/linklist.c -o bin/linklist.o

refcnt.o: rqueue.o
	$(CC) -g -rdynamic -c src/lib/refcnt.c -o bin/refcnt.o

rqueue.o:
	$(CC) -g -rdynamic -c src/lib/rqueue.c -o bin/rqueue.o

rbtree.o:
	$(CC) -g -rdynamic -c src/lib/rbtree.c -o bin/rbtree.o

deb:
	mkdir -p chroot/opt/sedge/vpn
	mkdir -p chroot/var/log/sedge
	mkdir -p chroot/DEBIAN
	cp bin/server chroot/opt/sedge/vpn
	cp bin/client chroot/opt/sedge/vpn
	chmod +x chroot/opt/sedge/vpn/client
	chmod +x chroot/opt/sedge/vpn/server
	cp control chroot/DEBIAN/
	dpkg -b chroot sedge-vpn-0.1.0_amd64.deb
	rm -rf chroot

rmo:
	rm bin/*.o

clean:
	rm bin/*
