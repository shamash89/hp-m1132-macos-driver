# HP LaserJet Pro M1132 MFP driver for macOS - build rules
#
#   make            universal filter in build/
#   make tools      stand-alone foo2xqx + xqxdecode (offline testing only)
#   make check      encode the test page and decode it again
#   make pkg        build/M1132-foo2xqx-Driver-$(VERSION).pkg (see scripts/make-pkg.sh)
#   make clean

VERSION  = 1.0.0
ARCHS    = -arch arm64 -arch x86_64
MINOS    = -mmacosx-version-min=11.0
CFLAGS   = -O2 $(ARCHS) $(MINOS)
V        = vendor/foo2zjs
B        = build

all: $(B)/rastertoxqx

$(B):
	mkdir -p $(B)

$(B)/jbig.o: $(V)/jbig.c | $(B)
	$(CC) $(CFLAGS) -w -c -o $@ $<

$(B)/jbig_ar.o: $(V)/jbig_ar.c | $(B)
	$(CC) $(CFLAGS) -w -c -o $@ $<

# The encoder, with main() renamed so it can live inside the filter.
$(B)/foo2xqx_lib.o: $(V)/foo2xqx.c | $(B)
	$(CC) $(CFLAGS) -w -Dmain=foo2xqx_main -c -o $@ $<

$(B)/rastertoxqx: src/rastertoxqx.c $(B)/foo2xqx_lib.o $(B)/jbig.o $(B)/jbig_ar.o
	$(CC) $(CFLAGS) -Wall -o $@ $^ -lcups
	lipo -archs $@

tools: $(B)/foo2xqx $(B)/xqxdecode

$(B)/foo2xqx: $(V)/foo2xqx.c $(B)/jbig.o $(B)/jbig_ar.o
	$(CC) $(CFLAGS) -w -o $@ $^

$(B)/xqxdecode: $(V)/xqxdecode.c $(B)/jbig.o $(B)/jbig_ar.o
	$(CC) $(CFLAGS) -w -o $@ $^

# Runs Apple's own PDF rasteriser, then the filter, then decodes the result.
check: all tools
	PPD=$(CURDIR)/ppd/HP-LaserJet_Pro_M1132_MFP-foo2xqx.ppd \
	    /usr/libexec/cups/filter/cgpdftoraster 1 check check 1 "" test/ruler-letter.pdf \
	    2>/dev/null > $(B)/check.ras
	$(B)/rastertoxqx 1 check check 1 "" $(B)/check.ras > $(B)/check.xqx
	$(B)/xqxdecode < $(B)/check.xqx 2>&1 | grep -E "RESOLUTION|DMPAPER|END_DOC"
	rm -f $(B)/check.ras

pkg: all
	VERSION=$(VERSION) scripts/make-pkg.sh

clean:
	rm -rf $(B)

.PHONY: all tools check pkg clean
