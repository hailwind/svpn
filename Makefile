svpn: prepare buildlib server client

prepare:
	if [ ! -d bin ]; then mkdir bin; fi;
ifndef $(ARCH)
ARCH=x86
endif
ifeq ($(ARCH),x86)
CC:=/usr/bin/gcc
else
export STAGING_DIR=/home/alexw/workspace/openwrt/staging_dir/target-mipsel_24kc_musl
CC := /home/alexw/workspace/openwrt/staging_dir/toolchain-mipsel_24kc_gcc-7.4.0_musl/bin/mipsel-openwrt-linux-gcc
endif

server: common.o server.o
	$(CC) -g -rdynamic -lmcrypt -llz4 -lpthread bin/server.o bin/common.o bin/ikcp.o -o bin/svpn_server_$(ARCH)


client: common.o client.o
	$(CC) -g -rdynamic -lmcrypt -llz4 -lpthread bin/client.o bin/common.o bin/ikcp.o -o bin/svpn_client_$(ARCH)

server.o: 
	$(CC) $(CFLAGS) -g -rdynamic -c src/server.c -o bin/server.o

client.o: 
	$(CC) $(CFLAGS) -g -rdynamic -c src/client.c -o bin/client.o

common.o: 
	$(CC) $(CFLAGS) -g -rdynamic -c src/common.c -o bin/common.o

buildlib: ikcp.o
	echo "lib compiled."

ikcp.o:
	$(CC) -g -rdynamic -c src/lib/ikcp.c -o bin/ikcp.o

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
