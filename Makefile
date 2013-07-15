CFLAGS = -Wall
LDFLAGS = -lusb-1.0

CUPS_BACKEND_DIR = /usr/lib/cups/backend
DEPS = backend_common.c

all: canon-selphy_print kodak-1400_print kodak-6800_print shinko-s2145_print sony-updr150_print

canon-selphy_print: selphy_print.c $(DEPS)
	gcc -o $@ $< $(LDFLAGS) $(CFLAGS)

kodak-6800_print: kodak6800_print.c $(DEPS)
	gcc -o $@ $< $(LDFLAGS) $(CFLAGS)

kodak-1400_print: kodak1400_print.c $(DEPS)
	gcc -o $@ $< $(LDFLAGS) $(CFLAGS)

shinko-s2145_print: shinko_s2145_print.c $(DEPS)
	gcc -o $@ $< $(LDFLAGS) $(CFLAGS)

sony-updr150_print: sony_updr150_print.c $(DEPS)
	gcc -o $@ $< $(LDFLAGS) $(CFLAGS)

install:	
	install -o root -m 700 canon-selphy_print $(CUPS_BACKEND_DIR)/canon-selphy
	install -o root -m 700 kodak-1400_print $(CUPS_BACKEND_DIR)/kodak-1400
	install -o root -m 700 kodak-6800_print $(CUPS_BACKEND_DIR)/kodak-6800
	install -o root -m 700 shinko-s2145_print $(CUPS_BACKEND_DIR)/shinko-s2145
	install -o root -m 700 sony-updr150_print $(CUPS_BACKEND_DIR)/sony-updr150

clean:
	rm -f selphy_print kodak6800_print kodak1400_print shinko_s2145 sony_updr150
