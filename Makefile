CFLAGS = -Wall
LDFLAGS = -lusb-1.0

CUPS_BACKEND_DIR = /usr/lib/cups/backend
DEPS = backend_common.h
SOURCES = sony_updr150_print.c kodak6800_print.c backend_common.c kodak1400_print.c

all: gutenprint sonyupdr150 kodak6800 kodak1400

gutenprint: $(SOURCES) $(DEPS)
	gcc -o $@ $(SOURCES) $(LDFLAGS) $(CFLAGS)

sonyupdr150: 
	ln -s gutenprint $@

kodak6800: 
	ln -s gutenprint $@

kodak1400: 
	ln -s gutenprint $@


install:	
	install -o root -m 700 gutenprint $(CUPS_BACKEND_DIR)/gutenprint+usb

clean:
	rm -f gutenprint canonselphy kodak6800 kodak1400 shinkos2145 sonyupdr150
