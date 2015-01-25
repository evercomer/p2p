#CFLAGS +=
LDFLAGS +=  -L$(TDIR)

ifeq ($(ARCH),Hi3518)
	OUTDIR=hi3518
else
	OUTDIR=i386
endif

.PHONY: clean

all: $(OUTDIR)/core_test $(OUTDIR)/camera #$(OUTDIR)/client

$(OUTDIR)/core_test: core_test.o pktextrct.o $(TDIR)/$(LIBP2PCORE)
	$(CC) -o $@ $^ $(LDFLAGS) -lminiupnpc 

$(OUTDIR)/camera: camera.o ReadWriter.o $(TDIR)/$(LIBP2PSESS) $(TDIR)/$(LIBP2PCORE)
	$(CC) -o $@ $^ $(LDFLAGS) -lminiupnpc -lpthread

$(OUTDIR)/client: client.o $(TDIR)/$(LIBP2PCORE) $(TDIR)/$(LIBP2PSESS)
	$(CC) -o $@ $^ $(LDFLAGS) -lminiupnpc -lpthread

clean:
	rm -f *.o core_test camera client
