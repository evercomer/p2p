TOPDIR=$(shell pwd)
ARCH ?= i386

ifeq ($(ARCH),i386) 
TDIR=$(TOPDIR)/dep/i386
CROSS=
endif


ifeq ($(ARCH), hi3518)
TDIR=$(TOPDIR)/dep/hi3518
CROSS=arm-hisiv100nptl-linux-
LDFLAGS=-Ldep/hi3518/ -L/home/song/hi3518/ipcam/hi3518/extlib/
endif

CC=$(CROSS)gcc
AR=$(CROSS)ar

#list_obj=$(patsubst %.$(2), %.o, $(wildcard $(1)/*.$(2)))
#src-dirs = .
#P2PCOREOBJ := $(foreach dir, $(src-dirs), $(call list_obj,$(dir),c) $(call list_obj,$(dir),cpp))


CFLAGS += -D__LINUX__ -Wall -I$(TOPDIR) #-D__BIG_ENDIAN__

#depended external libraries
LDFLAGS += -lrt -lpthread -lresolv

ifndef RELEASE
	CFLAGS += -g #-D_DEBUG -DVERBOSE
else
	CFLAGS += -O2
endif
	LIBP2PCORE=libp2pcore.a
	LIBP2PSESS=libp2psess.a

export CC CFLAGS LDFLAGS TDIR TOPDIR 


all: cam_demo


%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@


.PHONY: clean cleanall demo

p2ptest: demo/p2ptest

cam_demo: ipcam.o
	$(CC) -o $@ $< -L../p2p/core/dep/$(ARCH) -lp2pcore -lp2psess -lpthread -lrt


clean:
	rm -f *.o stunc/*.o stunc/*.a
	make -C demo clean

