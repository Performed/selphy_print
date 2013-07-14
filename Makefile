CFLAGS = -Wall
LDFLAGS = -lusb-1.0

CUPS_BACKEND_DIR = /usr/lib/cups/backend
DEPS = backend_common.c

all: selphy_print kodak1400_print kodak6800_print shinko_s2145_print sony_updr150_print

selphy_print: selphy_print.c $(DEPS)
	gcc -o $@ $< $(LDFLAGS) $(CFLAGS)

kodak6800_print: kodak6800_print.c $(DEPS)
	gcc -o $@ $< $(LDFLAGS) $(CFLAGS)

kodak1400_print: kodak1400_print.c $(DEPS)
	gcc -o $@ $< $(LDFLAGS) $(CFLAGS)

shinko_s2145_print: shinko_s2145_print.c $(DEPS)
	gcc -o $@ $< $(LDFLAGS) $(CFLAGS)

sony_updr150_print: sony_updr150_print.c $(DEPS)
	gcc -o $@ $< $(LDFLAGS) $(CFLAGS)

install:	
	install -o root -m 700 selphy_print $(CUPS_BACKEND_DIR)/selphy
	install -o root -m 700 kodak1400_print $(CUPS_BACKEND_DIR)/kodak1400
	install -o root -m 700 kodak6800_print $(CUPS_BACKEND_DIR)/kodak6800
	install -o root -m 700 shinko_s2145_print $(CUPS_BACKEND_DIR)/shinko_s2145
	install -o root -m 700 sony_updr150_print $(CUPS_BACKEND_DIR)/sony_updr150

clean:
	rm -f selphy_print kodak6800_print kodak1400_print shinko_s2145 sony_updr150
