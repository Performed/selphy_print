CFLAGS = -Wall

all: selphy_print_linux selphy_print_libusb

selphy_print_linux:  es_print_linux.c es_print_common.h
	gcc -o selphy_print_linux es_print_linux.c $(CFLAGS)

selphy_print_libusb:  es_print_libusb.c es_print_common.h
	gcc -o selphy_print_libusb es_print_libusb.c -lusb-1.0 $(CFLAGS)
