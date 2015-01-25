find_files=$(wildcard $(dir)/*.c) $(wildcard $(dir)/*.cpp)

find_objs=$(patsubst %.c, %.o, $(wildcard $(dir)/*.c))

objs_from_ext=$(patsubst %.$(1), %.o, $(wildcard $(dir)/*.$(1)))

dirs = p2p app app/rtsp stun
FF=$(foreach dir, $(dirs), $(find_files))
#FF=$(foreach dir, $(dirs), $(wildcard $(dir)/*.c) $(wildcard $(dir)/*.cpp))

OBJS=$(foreach dir, $(dirs), $(find_objs))
OBJS=$(foreach dir, $(dirs), $(call objs_from_ext,c) $(call objs_from_ext,cpp))

ifeq ($(v),1)
	V=1
else  ifeq ($(v),2)
	V=2
else
	V=0
endif

.PHONY: all

all:
	@echo $(OBJS)
