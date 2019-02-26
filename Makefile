svpn: prepare buildlib server client

prepare:
	if [ ! -d bin ]; then mkdir bin; fi;
ifndef $(ARCH)
ARCH=X86
endif
ifeq ($(ARCH),X86)
CC:=/usr/bin/gcc
SOC_FLAG:=-DWITH_MCRYPT
else
export STAGING_DIR=/home/alexw/workspace/openwrt-sdk-18.06.2-ramips-mt7620_gcc-7.3.0_musl.Linux-x86_64/staging_dir/
CC := /home/alexw/workspace/openwrt-sdk-18.06.2-ramips-mt7620_gcc-7.3.0_musl.Linux-x86_64/staging_dir/toolchain-mipsel_24kc_gcc-7.3.0_musl/bin/mipsel-openwrt-linux-gcc
SOC_FLAG := -DNO_MCRYPT
endif
	CFLAGS += $(SOC_FLAG)

server: common.o server.o
ifeq ($(ARCH),X86)
	$(CC) -g -rdynamic -lmcrypt -llz4 -lpthread bin/server.o bin/common.o bin/ikcp.o -o bin/svpn_server_x86
else
	$(CC) -g -rdynamic -lpthread bin/server.o bin/common.o bin/ikcp.o -o bin/svpn_server_mips
endif

client: common.o client.o
ifeq ($(ARCH),X86)
	$(CC) -g -rdynamic -lmcrypt -llz4 -lpthread bin/client.o bin/common.o bin/ikcp.o -o bin/svpn_client_x86
else
	$(CC) -g -rdynamic -lpthread bin/client.o bin/common.o bin/ikcp.o -o bin/svpn_client_mips
endif
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
