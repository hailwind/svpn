svpn: prepare buildlib server client

prepare:
	if [ ! -d bin ]; then mkdir bin; fi;

ifndef $(ARCH)
ARCH=x86
endif

ifeq ($(ARCH),x86)
CC:=/usr/bin/gcc
endif

ifeq ($(ARCH),mipsel)
export STAGING_DIR=/home/alexw/openwrt-sdk-18.06.2-ramips-mt7620_gcc-7.3.0_musl.Linux-x86_64/staging_dir/target-mipsel_24kc_musl/
CC := /home/alexw/openwrt-sdk-18.06.2-ramips-mt7620_gcc-7.3.0_musl.Linux-x86_64/staging_dir/toolchain-mipsel_24kc_gcc-7.3.0_musl/bin/mipsel-openwrt-linux-musl-gcc
endif

ifeq ($(ARCH),mips)
export STAGING_DIR=/home/alexw/openwrt-sdk-18.06.2-ar71xx-generic_gcc-7.3.0_musl.Linux-x86_64/staging_dir/target-mips_24kc_musl
CC := /home/alexw/openwrt-sdk-18.06.2-ar71xx-generic_gcc-7.3.0_musl.Linux-x86_64/staging_dir/toolchain-mips_24kc_gcc-7.3.0_musl/bin/mips-openwrt-linux-musl-gcc
CFLAGS := -DIWORDS_BIG_ENDIAN=1
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
	$(CC) $(CFLAGS)  -g -rdynamic -c src/lib/ikcp.c -o bin/ikcp.o

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
