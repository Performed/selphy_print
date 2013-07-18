CFLAGS = -Wall
LDFLAGS = -lusb-1.0

CUPS_BACKEND_DIR = /usr/lib/cups/backend
DEPS = backend_common.c backend_common.h

all: gutenprint sonyupdr150

gutenprint: sony_updr150_print.c $(DEPS)
	gcc -o $@ $<  backend_common.c $(LDFLAGS) $(CFLAGS)

canon-selphy_print: selphy_print.c $(DEPS)
	gcc -o $@ $< $(LDFLAGS) $(CFLAGS)

kodak-6800_print: kodak6800_print.c $(DEPS)
	gcc -o $@ $< $(LDFLAGS) $(CFLAGS)

kodak-1400_print: kodak1400_print.c $(DEPS)
	gcc -o $@ $< $(LDFLAGS) $(CFLAGS)

shinko-s2145_print: shinko_s2145_print.c $(DEPS)
	gcc -o $@ $< $(LDFLAGS) $(CFLAGS)

sonyupdr150: 
	ln -s gutenprint $@

install:	
	install -o root -m 700 gutenprint $(CUPS_BACKEND_DIR)/gutenprint+usb

clean:
	rm -f gutenprint canonselphy kodak6800 kodak1400 shinkos2145 sonyupdr150
