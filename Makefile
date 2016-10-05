# Basic stuff
BACKEND_NAME ?= gutenprint52+usb
EXEC_NAME ?= dyesub_backend

# Destination directories (rely on CUPS to tell us where)
PREFIX ?=
CUPS_BACKEND_DIR ?= $(PREFIX)`cups-config --serverbin`/backend
CUPS_DATA_DIR ?= $(PREFIX)`cups-config --datadir`
BACKEND_DATA_DIR ?= $(PREFIX)/usr/share/gutenprint/backend_data

# Tools
CC ?= $(CROSS_COMPILE)gcc
LD ?= $(CROSS_COMPILE)gcc
CPPCHECK ?= cppcheck
MKDIR ?= mkdir
INSTALL ?= install
LN ?= ln
RM ?= rm

# Flags
CFLAGS += -Wall -Wextra -g -Os -D_GNU_SOURCE -std=c99 # -Wconversion
LDFLAGS += `pkg-config --libs libusb-1.0` 
CPPFLAGS += `pkg-config --cflags libusb-1.0`
# CPPFLAGS += -DLIBUSB_PRE_1_0_10
CPPFLAGS += -DURI_PREFIX=\"$(BACKEND_NAME)\"

# If you want to use LTO..
#CFLAGS += -flto
# If not...
CFLAGS += -funit-at-a-time

# List of backends
BACKENDS = sonyupdr150 kodak6800 kodak1400 shinkos2145 shinkos1245 canonselphy mitsu70x kodak605 dnpds40 citizencw01 mitsu9550 shinkos6245 shinkos6145

# For the s6145 and mitsu70x backends
CPPFLAGS += -DUSE_DLOPEN
LDFLAGS += -ldl
#CPPFLAGS += -DUSE_LTDL
#LDFLAGS += -lltdl

# For the mitsu70x backend
CPPFLAGS += -DCORRTABLE_PATH=\"$(BACKEND_DATA_DIR)\"

# Build stuff
DEPS += backend_common.h
SOURCES = backend_common.c $(addsuffix .c,$(addprefix backend_,$(BACKENDS)))

# And now the rules!
.PHONY: clean all install cppcheck

all: $(EXEC_NAME) $(BACKENDS)

%.o: %.c $(DEPS)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

$(EXEC_NAME): $(SOURCES:.c=.o) $(DEPS)
	$(CC) -o $@ $(SOURCES:.c=.o) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS)

$(BACKENDS): $(EXEC_NAME)
	$(LN) -sf $(EXEC_NAME) $@

cppcheck:
	$(CPPCHECK) -q -v --std=c99 --enable=all -I/usr/include $(CPPFLAGS) $(SOURCES)

install:
	$(MKDIR) -p $(CUPS_BACKEND_DIR)
	$(INSTALL) -o root -m 700 $(EXEC_NAME) $(CUPS_BACKEND_DIR)/$(BACKEND_NAME)
	$(MKDIR) -p $(CUPS_DATA_DIR)/usb
	$(INSTALL) -o root -m 644 blacklist $(CUPS_DATA_DIR)/usb/net.sf.gimp-print.usb-quirks

clean:
	$(RM) -f $(EXEC_NAME) $(BACKENDS) $(SOURCES:.c=.o)

release:
	$(RM) -Rf selphy_print-rel
	$(MKDIR) -p selphy_print-rel
	cp -a *.c *.h Makefile blacklist COPYING README lib6145 D70 selphy_print-rel
	tar -czvf selphy_print-rel.tar.gz selphy_print-rel
	$(RM) -Rf selphy_print-rel
