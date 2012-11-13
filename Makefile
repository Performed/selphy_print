CFLAGS = -Wall
CUPS_BACKENDS = /usr/lib/cups/backend

all: selphy_print_linux selphy_print_libusb

selphy_print_linux:  es_print_linux.c es_print_common.h
	gcc -o selphy_print_linux es_print_linux.c $(CFLAGS)

selphy_print_libusb:  es_print_libusb.c es_print_common.h
	gcc -o selphy_print_libusb es_print_libusb.c -lusb-1.0 $(CFLAGS)

install:
	install -o root -m 700 selphy_print_libusb $(CUPS_BACKENDS)/selphy

clean:
	rm -f selphy_print*
