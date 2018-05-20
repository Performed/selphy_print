### Makefile for selphy_print and family
#
#  SPDX-License-Identifier: GPL-3.0+
#

# Basic stuff
EXEC_NAME ?= dyesub_backend
#NO_GUTENPRINT = 1

# Destination directories (rely on CUPS to tell us where)
PREFIX ?=
CUPS_BACKEND_DIR ?= $(PREFIX)`cups-config --serverbin`/backend
CUPS_DATA_DIR ?= $(PREFIX)`cups-config --datadir`
BACKEND_DATA_DIR ?= $(PREFIX)/usr/local/share/gutenprint/backend_data

# Figure out what the backend name needs to be
ifeq ($(NO_GUTENPRINT),)
GUTENPRINT_INCLUDE := $(shell pkg-config --variable=includedir gutenprint)/gutenprint
GUTENPRINT_MAJOR := $(shell grep 'define STP_MAJOR' $(GUTENPRINT_INCLUDE)/gutenprint-version.h | tr -d '()\t' | cut -c33- )
GUTENPRINT_MINOR := $(shell grep 'define STP_MINOR' $(GUTENPRINT_INCLUDE)/gutenprint-version.h | tr -d '()\t' | cut -c33- )
BACKEND_NAME ?= gutenprint$(GUTENPRINT_MAJOR)$(GUTENPRINT_MINOR)+usb
endif

# Fallthrough
BACKEND_NAME ?= gutenprint5X+usb

# Tools
CC ?= $(CROSS_COMPILE)gcc
LD ?= $(CROSS_COMPILE)gcc
CPPCHECK ?= cppcheck
MKDIR ?= mkdir
INSTALL ?= install
LN ?= ln
RM ?= rm

# Flags
CFLAGS += -Wall -Wextra -g -Og -D_GNU_SOURCE -std=c99 # -Wconversion
LDFLAGS += `pkg-config --libs libusb-1.0`
CPPFLAGS += `pkg-config --cflags libusb-1.0`
# CPPFLAGS += -DLIBUSB_PRE_1_0_10
CPPFLAGS += -DURI_PREFIX=\"$(BACKEND_NAME)\"

# If you want to use LTO..
#CFLAGS += -flto
# If not...
CFLAGS += -funit-at-a-time

# List of backends
BACKENDS = sonyupdr150 kodak6800 kodak1400 shinkos2145 shinkos1245 canonselphy mitsu70x kodak605 dnpds40 mitsu9550 shinkos6245 shinkos6145 canonselphyneo mitsup95d magicard mitsud90

# For the s6145 and mitsu70x backends
CPPFLAGS += -DUSE_DLOPEN
LDFLAGS += -ldl
#CPPFLAGS += -DUSE_LTDL
#LDFLAGS += -lltdl

# Build stuff
DEPS += backend_common.h
SOURCES = backend_common.c $(addsuffix .c,$(addprefix backend_,$(BACKENDS)))

# Backend-specific joy:
backend_mitsu70x.o: CPPFLAGS += -DCORRTABLE_PATH=\"$(BACKEND_DATA_DIR)\" -include lib70x/libMitsuD70ImageReProcess.h
backend_mitsu9550.o: CPPFLAGS += -DCORRTABLE_PATH=\"$(BACKEND_DATA_DIR)\"

# And now the rules!
.PHONY: clean all install cppcheck

all: $(EXEC_NAME) $(BACKENDS)

%.o: %.c $(DEPS)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

$(EXEC_NAME): $(SOURCES:.c=.o) $(DEPS)
	$(CC) -o $@ $(SOURCES:.c=.o) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS)

$(BACKENDS): $(EXEC_NAME)
	$(LN) -sf $(EXEC_NAME) $@

sloccount:
	sloccount *.[ch] lib*/*.[ch] *.pl

test: dyesub_backend
	./regression.pl < regression.csv 2>&1 |grep FAIL ; \
	if [ $$? -eq 0 ] ; then exit 1 ; fi

cppcheck:
	$(CPPCHECK) -q -v --std=c99 --enable=all --suppress=variableScope --suppress=selfAssignment --suppress=unusedStructMember -I. -I/usr/include  -DCORRTABLE_PATH=\"$(BACKEND_DATA_DIR)\" --include=lib70x/libMitsuD70ImageReProcess.h $(CPPFLAGS) $(SOURCES)

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
