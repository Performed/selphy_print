BACKEND_NAME = gutenprint52+usb
EXEC_NAME = dyesub_backend

CFLAGS = -Wall -Wextra -g
LDFLAGS = `pkg-config --libs libusb-1.0`
CPPFLAGS = `pkg-config --cflags libusb-1.0`

CUPS_BACKEND_DIR = /usr/lib/cups/backend
CUPS_DATA_DIR = /usr/share/cups

BACKENDS = sonyupdr150 kodak6800 kodak1400 shinkos2145 canonselphy mitsu70x kodak605 dnpds40

DEPS = backend_common.h

SOURCES = backend_common.c $(addsuffix .c,$(addprefix backend_,$(BACKENDS)))

all: $(EXEC_NAME) $(BACKENDS)

$(EXEC_NAME): $(SOURCES) $(DEPS)
	gcc -o $@ $(SOURCES) $(LDFLAGS) $(CFLAGS) $(CPPFLAGS) -DURI_PREFIX=\"$(BACKEND_NAME)\"

$(BACKENDS): $(EXEC_NAME)
	ln -sf $(EXEC_NAME) $@

cppcheck:
	cppcheck -q -v --std=c99 --enable=all -I/usr/include -DURI_PREFIX=\"$(BACKEND_NAME)\" $(SOURCES) 

install:	
	install -o root -m 700 $(EXEC_NAME) $(CUPS_BACKEND_DIR)/$(BACKEND_NAME)
	mkdir -p $(CUPS_DATA_DIR)/usb
	install -o root -m 644 blacklist $(CUPS_DATA_DIR)/usb/net.sf.gimp-print.usb-quirks

clean:
	rm -f $(EXEC_NAME) $(BACKENDS)
