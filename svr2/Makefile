COMMONSRC := p2pbase.c platform_adpt.c md5.c netbase.c p2plog.c

ifeq ($(arch), android)
CROSS=arm-linux-androideabi-
CFLAGS = -Iextlib/android/
endif

CC=$(CROSS)gcc
CXX=$(CROSS)g++
AR=$(CROSS)ar


SVRSRC := $(COMMONSRC) ExtractSetting.c svrdb.cpp p2psvc.cpp timerq.c
SVROBJ := $(patsubst %.c, %.o, $(SVRSRC))
SVROBJ := $(patsubst %.cpp, %.o, $(SVROBJ))

RELAYDSRC := relaysvr.cpp $(COMMONSRC)
RELAYDOBJ := $(patsubst %.c, %.o, $(RELAYDSRC))
RELAYDOBJ := $(patsubst %.cpp, %.o, $(RELAYDOBJ))

CFLAGS += -I mysql/include/ -D__LINUX__ 

ifndef RELEASE
	CFLAGS += -D_DEBUG -g
else
	CFLAGS += -O2
endif


all: p2psvr relayd #mystunsvr

$(SVROBJ): $(SVRSRC)

$(RELAYDOBJ) : $(RELAYDSRC)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.cpp
	$(CXX) $(CFLAGS) -c $< -o $@

.PHONY: clean


p2psvr: $(SVROBJ) svr.o
	$(CXX) -o p2psvr $^ -luuid -lpthread -lrt -Lmysql/lib -lmysqlclient /home/song/open-source/libevent-2.0.21-stable/.libs/libevent_core.a


libp2psvr.a: $(SVROBJ)
	ar -r $@ $^

relayd: $(RELAYDOBJ)
	$(CXX) -o relayd $(RELAYDOBJ) -lpthread -lrt /home/song/open-source/libevent-2.0.21-stable/.libs/libevent_core.a #-levent_core


clean:
	rm -f *.o p2psvr dcsclt p2pmonitor relayd mystunsvr stuntest

