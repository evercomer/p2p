TOPDIR=$(shell pwd)
ARCH ?= PC

ifeq ($(ARCH), PC)
TDIR=$(TOPDIR)/dep/i386
CROSS=
endif


ifeq ($(ARCH), Hi3518)
TDIR=$(TOPDIR)/dep/hi3518
CROSS=arm-hisiv100nptl-linux-
LDFLAGS=-Ldep/hi3518/ -L/home/song/hi3518/ipcam/hi3518/extlib/
#CFLAGS += -D__FORCE_ALIGNMENT__
endif

CC=$(CROSS)gcc
AR=$(CROSS)ar

#list_obj=$(patsubst %.$(2), %.o, $(wildcard $(1)/*.$(2)))
#src-dirs = .
#P2PCOREOBJ := $(foreach dir, $(src-dirs), $(call list_obj,$(dir),c) $(call list_obj,$(dir),cpp))

P2PCOREOBJ=p2pcore.o p2plog.o p2pbase.o platform_adpt.o netbase.o rudp.o crc32.o upnp_igd_cp.o timerq.o stunc/udp.o stunc/stun.o stunc/det_nat.o
P2PSESSOBJ=p2psess.o chnbuf.o

CFLAGS += -D__LINUX__ -Wall -I$(TOPDIR) #-D__BIG_ENDIAN__

#depended external libraries
LDFLAGS += -lrt -lpthread -lresolv

ifndef RELEASE
	CFLAGS += -g -D_DEBUG -DVERBOSE
	#CFLAGS += -D_DEBUG_RUDP
	LIBP2PCORE=libp2pcored.a
	LIBP2PSESS=libp2psessd.a
else
	CFLAGS += -O2
	LIBP2PCORE=libp2pcore.a
	LIBP2PSESS=libp2psess.a
endif

export CC CFLAGS LDFLAGS TDIR TOPDIR LIBP2PCORE LIBP2PSESS ARCH


all: $(TDIR)/$(LIBP2PCORE) $(TDIR)/$(LIBP2PSESS) demo


%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@


.PHONY: clean cleanall demo

p2ptest: demo/p2ptest

demo: 
	make -C demo

$(TDIR)/$(LIBP2PCORE): $(P2PCOREOBJ)
	$(AR) -r $@ $^

$(TDIR)/$(LIBP2PSESS): $(P2PSESSOBJ)
	$(AR) -r $@ $^

clean:
	rm -f *.o stunc/*.o stunc/*.a
	make -C demo clean

cleanall: clean
	rm -fr win32/libcore/Debug win32/libsess/Debug win32/testcore/Debug win32/testcore/*.suo win32/testcore/*.ncb
