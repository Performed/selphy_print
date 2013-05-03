CFLAGS = -Wall
CUPS_BACKENDS = /usr/lib/cups/backend

all: selphy_print kodak1400_print

selphy_print:  selphy_print.c
	gcc -o selphy_print selphy_print.c -lusb-1.0 $(CFLAGS)

kodak1400_print:  kodak1400_print.c
	gcc -o kodak1400_print kodak1400_print.c -lusb-1.0 $(CFLAGS)

install:
	install -o root -m 700 selphy_print $(CUPS_BACKENDS)/selphy
	install -o root -m 700 kodak1400_print $(CUPS_BACKENDS)/kodak1400

clean:
	rm -f selphy_print
