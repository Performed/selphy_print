CFLAGS = -Wall
LDFLAGS = -lusb-1.0

CUPS_BACKEND_DIR = /usr/lib/cups/backend

all: selphy_print kodak1400_print kodak6800_print

selphy_print: selphy_print.c
	gcc -o $@ $< $(LDFLAGS) $(CFLAGS)

kodak6800_print: $< kodak6800_print.c
	gcc -o $@ $< $(LDFLAGS) $(CFLAGS)

kodak1400_print:  kodak1400_print.c
	gcc -o $@ $< $(LDFLAGS) $(CFLAGS)

install:
	install -o root -m 700 selphy_print $(CUPS_BACKEND_DIR)/selphy
	install -o root -m 700 kodak1400_print $(CUPS_BACKEND_DIR)/kodak1400
	install -o root -m 700 kodak6800_print $(CUPS_BACKEND_DIR)/kodak6800

clean:
	rm -f selphy_print
