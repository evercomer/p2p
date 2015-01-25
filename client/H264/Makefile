#CROSS=/usr/local/arm-linux-androideabi-4.6/bin/arm-linux-androideabi-
#CROSS=arm-linux-androideabi-
#CROSS=i686-linux-android-

CC=$(CROSS)gcc
CFLAGS=-DHAVE_BYTESWAP_H -DHAVE_LLRINT -DHAVE_LRINT -DHAVE_LRINTF -DHAVE_ROUND -DHAVE_ROUNDF -DARCH_ARMV4L
SO=libh264dec.so
A=libh264dec.a

SRCS=allcodecs.c bitstream.c cabac.c error_resilience.c golomb.c h264.c h264_parser.c h264idct.c h264pred.c imgconvert.c jrevdct.c log.c mem.c opt.c parser.c rational.c simple_idct.c svq3.c utils.c vp3dsp.c mpegvideo.c h264dec.c dsputil.c

OBJS=$(patsubst %.c, %.o, $(SRCS))

.PHONY: clean
	
$(A): $(OBJS)
	$(AR) -r $@ $^

demo: $(OBJS) main.o
	$(CC) -o $@ $^ -lm

$(SO): $(OBJS)
	$(CC) -shared -Wl,-soname,$(SO).0 -o $@ $^ -lm -lc

%.o:%.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f *.o *.a demo *.so
