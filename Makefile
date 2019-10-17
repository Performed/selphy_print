### Makefile for selphy_print and family
#
#  SPDX-License-Identifier: GPL-3.0+
#

# Basic stuff
EXEC_NAME ?= dyesub_backend
CPUS ?= $(shell nproc)
#NO_GUTENPRINT = 1
REVISION ?= -g$(shell if [ -d .git ] ; then git rev-parse --short HEAD; else echo "NONE" ; fi)

# Destination directories (rely on CUPS to tell us where)
GP_PREFIX ?= $(shell pkg-config --variable=prefix gutenprint)
PREFIX ?=
CUPS_BACKEND_DIR ?= $(PREFIX)$(shell cups-config --serverbin)/backend
CUPS_DATA_DIR ?= $(PREFIX)$(shell cups-config --datadir)

ifeq ($(GP_PREFIX),)
GP_PREFIX=/usr/local
endif
BACKEND_DATA_DIR ?= $(GP_PREFIX)/share/gutenprint/backend_data

# Figure out what the backend name needs to be
ifeq ($(NO_GUTENPRINT),)
GUTENPRINT_INCLUDE := $(shell pkg-config --variable=includedir gutenprint)/gutenprint
GUTENPRINT_MAJOR := $(shell grep 'define STP_MAJOR' $(GUTENPRINT_INCLUDE)/gutenprint-version.h | tr -d '()\t' | cut -c33- )
GUTENPRINT_MINOR := $(shell grep 'define STP_MINOR' $(GUTENPRINT_INCLUDE)/gutenprint-version.h | tr -d '()\t' | cut -c33- )
BACKEND_NAME ?= gutenprint$(GUTENPRINT_MAJOR)$(GUTENPRINT_MINOR)+usb
endif

# For Gutenprint 5.2, use old URI scheme
ifneq ($(GUTENPRINT_MINOR),3)
OLD_URI := -DOLD_URI=1
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
CFLAGS += -Wall -Wextra -Wformat-security -g -Og -D_FORTIFY_SOURCE=2 -D_GNU_SOURCE -std=c99 # -Wconversion
LDFLAGS += $(shell pkg-config --libs libusb-1.0)
CPPFLAGS += $(shell pkg-config --cflags libusb-1.0)
# CPPFLAGS += -DLIBUSB_PRE_1_0_10
CPPFLAGS += -DURI_PREFIX=\"$(BACKEND_NAME)\" $(OLD_URI)

# If you want to use LTO..
#CFLAGS += -flto
# If not...
CFLAGS += -funit-at-a-time

# List of backends
BACKENDS = canonselphy canonselphyneo dnpds40 hiti kodak605 kodak1400 kodak6800 magicard mitsu70x mitsu9550  mitsud90 mitsup95d shinkos1245 shinkos2145 shinkos6145 shinkos6245 sonyupd sonyupdneo

# For the s6145, mitsu70x, and mitsu9550 backends
CPPFLAGS += -DUSE_DLOPEN
LDFLAGS += -ldl
#CPPFLAGS += -DUSE_LTDL
#LDFLAGS += -lltdl

# Build stuff
DEPS += backend_common.h
SOURCES = backend_common.c backend_sinfonia.c $(addsuffix .c,$(addprefix backend_,$(BACKENDS)))

# Dependencies for sinfonia backends..
SINFONIA_BACKENDS = sinfonia kodak605 kodak6800 shinkos1245 shinkos2145 shinkos6145 shinkos6245
SINFONIA_BACKENDS_O = $(addsuffix .o,$(addprefix backend_,$(SINFONIA_BACKENDS)))

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
	STP_PARALLEL=$(CPUS) ./regression.pl regression.csv

test_%: dyesub_backend
	STP_PARALLEL=$(CPUS) ./regression.pl regression.csv $(subst test_,,$@)

testgp: dyesub_backend
	STP_PARALLEL=$(CPUS) ./regression-gp.pl regression-gp.csv

testgp_%: dyesub_backend
	STP_PARALLEL=$(CPUS) ./regression-gp.pl regression-gp.csv $(subst testgp_,,$@)

cppcheck:
	$(CPPCHECK) -q -v --std=c99 --enable=all --suppress=variableScope --suppress=selfAssignment --suppress=unusedStructMember -I. -I/usr/include  -DCORRTABLE_PATH=\"$(BACKEND_DATA_DIR)\" --include=lib70x/libMitsuD70ImageReProcess.h $(CPPFLAGS) $(SOURCES)

install:
	$(MKDIR) -p $(CUPS_BACKEND_DIR)
	$(INSTALL) -o root -m 700 $(EXEC_NAME) $(CUPS_BACKEND_DIR)/$(BACKEND_NAME)
	$(MKDIR) -p $(CUPS_DATA_DIR)/usb
	$(INSTALL) -o root -m 644 blacklist $(CUPS_DATA_DIR)/usb/net.sf.gimp-print.usb-quirks
	$(MKDIR) -p $(BACKEND_DATA_DIR)
	$(INSTALL) -o root -m 644 hiti_data/*bin $(BACKEND_DATA_DIR)

clean:
	$(RM) $(EXEC_NAME) $(BACKENDS) $(SOURCES:.c=.o) lib70x/*o lib6145/*o

release: clean
	$(RM) -Rf selphy_print$(REVISION)
	$(MKDIR) -p selphy_print$(REVISION)
	cp -a *.c *.h Makefile blacklist COPYING README lib6145 lib70x selphy_print$(REVISION)
	tar -czvf selphy_print$(REVISION).tar.gz selphy_print$(REVISION)
	$(RM) -Rf selphy_print$(REVISION)

# Backend-specific joy:
$(SINFONIA_BACKENDS_O): backend_sinfonia.h
backend_mitsu70x.o: CPPFLAGS += -DCORRTABLE_PATH=\"$(BACKEND_DATA_DIR)\" -include lib70x/libMitsuD70ImageReProcess.h
backend_mitsu9550.o: CPPFLAGS += -DCORRTABLE_PATH=\"$(BACKEND_DATA_DIR)\" -include lib70x/libMitsuD70ImageReProcess.h
backend_hiti.o: CPPFLAGS += -DCORRTABLE_PATH=\"$(BACKEND_DATA_DIR)\"
