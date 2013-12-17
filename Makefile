CFLAGS = -Wall -Wextra -g
LDFLAGS = -lusb-1.0

CUPS_BACKEND_DIR = /usr/lib/cups/backend
CUPS_DATA_DIR = /usr/share/cups

BACKENDS = sonyupdr150 kodak6800 kodak1400 shinkos2145 canonselphy mitsu70x kodak605 dnpds40

DEPS = backend_common.h

SOURCES = backend_common.c $(addsuffix .c,$(addprefix backend_,$(BACKENDS)))

all: gutenprint $(BACKENDS)

gutenprint: $(SOURCES) $(DEPS)
	gcc -o $@ $(SOURCES) $(LDFLAGS) $(CFLAGS)

$(BACKENDS): gutenprint
	ln -sf gutenprint $@

install:	
	install -o root -m 700 gutenprint $(CUPS_BACKEND_DIR)/gutenprint52+usb
	mkdir -p $(CUPS_DATA_DIR)/usb
	install -o root -m 644 blacklist $(CUPS_DATA_DIR)/usb/net.sf.gimp-print.usb-quirks

clean:
	rm -f gutenprint $(BACKENDS)
