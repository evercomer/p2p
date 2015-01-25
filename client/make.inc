CSRCS=$(wildcard *.c)
CPPSRCS=$(wildcard *.cpp)

COBJS=$(patsubst %.c, %.o, $(CSRCS))
CPPOBJS=$(patsubst %.cpp, %.o, $(CPPSRCS))

$(COBJS): %.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(CPPOBJS): %.o: %.cpp
	$(CC) $(CPPFLAGS) -c -o $@ $<

