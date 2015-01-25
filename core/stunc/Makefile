CROSS=
CC=$(CROSS)gcc
AR=$(CROSS)ar

CFLAGS=-D__LINUX__ -O2 -I..

all: libstunc.a

.PHONY: clean

#%.o: %.c

libstunc.a: det_nat.o stun.o udp.o
	$(AR) -r $@ $^

det_nat: det_nat.c stun.c udp.c
	$(CC) -o $@ $^ -D__LINUX__ -I.. -DDET_NAT_MAIN

clean:
	rm -f *.o libstunc.a det_nat
