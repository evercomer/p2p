ARCH=

ifeq ($(ARCH), i386)
EXTRALIBDIR=libs/i386
CFLAGS = -g -D__SIMULATE__ -DCURSE_CONSOLE #-D_DEBUG -DVERBOSE
LDFLAGS += -lpthread -lrt -lncurses
else ifeq ($(ARCH), hi3518)
CROSS=arm-hisiv100nptl-linux-
EXTRALIBDIR=libs/hi3518
CFLAGS = -g -D__SIMULATE__#-D_DEBUG -DVERBOSE
LDFLAGS += -lpthread -lrt
else
CROSS=arm-linux-androideabi-
#CROSS=i686-linux-android-
#CROSS=/usr/local/arm-linux-androideabi-4.6/bin/arm-linux-androideabi-
EXTRALIBDIR=libs/arm-android
CFLAGS += -D__ANDROID__ -O2
LDFLAGS += -llog
endif

CC=$(CROSS)gcc $(SYSROOT)
CXX=$(CROSS)g++ $(SYSROOT)
AR=$(CROSS)ar

TOPDIR=$(shell pwd)

CFLAGS += -D__LINUX__ -DEXTERN=
CFLAGS += -DNO_UPNP_IGD
CFLAGS += -I$(TOPDIR)/p2p -I$(TOPDIR)/jni -I$(TOPDIR)/app -I$(TOPDIR) -I$(TOPDIR)/../H264
LDFLAGS += -lm -lc -Wl,-Bsymbolic 
LDFLAGS += -L$(EXTRALIBDIR)

export CFLAGS LDFLAGS CC CXX TOPDIR



P2POBJ := $(patsubst %.c, %.o, $(wildcard p2p/*.c)) $(patsubst %.cpp, %.o, $(wildcard p2p/*.cpp))


list_obj=$(patsubst %.$(2), %.o, $(wildcard $(1)/*.$(2)))
objs_from_ext=$(patsubst %.$(1), %.o, $(wildcard $(dir)/*.$(1)))


src-dirs = p2p app app/rtsp stunc jni
CLTOBJ = $(foreach dir, $(src-dirs), $(call list_obj,$(dir),c) $(call list_obj,$(dir),cpp))

all: libp2pclt.so

demo_base: demo/demo_base

demo: demo/demo

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.cpp
	$(CXX) $(CFLAGS) -c $< -o $@

.PHONY: clean

libp2pclt.so: $(CLTOBJ)
	$(CXX) -shared $(LDFLAGS) -o $@ $^ -lh264dec -lfaac -lfaad
	$(CROSS)strip $@
#	cp $@ /cygdrive/D/develop/workspace/Danale_Now/libs/armeabi
#	cp $@ /cygdrive/D/develop/workspace/IpcPad/libs/armeabi
	
demo/demo_base: $(P2POBJ) demo/p2pclt_test.o app/ReadWriter.o
	$(CXX) $(LDFLAGS) -o $@ $^

demo/demo: $(CLTOBJ) demo/p2psess_test.o
	$(CXX) $(LDFLAGS) -o $@ $^ -lh264dec -lfaac -lfaad

clean:
	rm -f p2p/*.o jni/*.o app/*.o app/rtsp/*.o demo/*.o *.so demo/demo demo/demo_base


