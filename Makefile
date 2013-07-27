CFLAGS = -Wall -Wextra -g
LDFLAGS = -lusb-1.0

CUPS_BACKEND_DIR = /usr/lib/cups/backend
DEPS = backend_common.h
SOURCES = backend_sonyupdr150.c backend_kodak6800.c backend_common.c backend_kodak1400.c backend_shinkos2145.c backend_canonselphy.c

all: gutenprint sonyupdr150 kodak6800 kodak1400 shinkos2145 canonselphy

gutenprint: $(SOURCES) $(DEPS)
	gcc -o $@ $(SOURCES) $(LDFLAGS) $(CFLAGS)

sonyupdr150: gutenprint
	ln -sf gutenprint $@

kodak6800: gutenprint
	ln -sf gutenprint $@

kodak1400: gutenprint
	ln -sf gutenprint $@

shinkos2145: gutenprint
	ln -sf gutenprint $@

canonselphy: gutenprint
	ln -sf gutenprint $@

install:	
	install -o root -m 700 gutenprint $(CUPS_BACKEND_DIR)/gutenprint+usb

clean:
	rm -f gutenprint canonselphy kodak6800 kodak1400 shinkos2145 sonyupdr150
