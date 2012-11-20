CFLAGS = -Wall
CUPS_BACKENDS = /usr/lib/cups/backend

all: selphy_print_linux selphy_print

selphy_print_linux:  selphy_print_linux.c selphy_print_common.h
	gcc -o selphy_print_linux selphy_print_linux.c $(CFLAGS)

selphy_print:  selphy_print.c selphy_print_common.h
	gcc -o selphy_print selphy_print.c -lusb-1.0 $(CFLAGS)

install:
	install -o root -m 700 selphy_print $(CUPS_BACKENDS)/selphy

clean:
	rm -f selphy_print selphy_print_linux
