# Basic stuff
BACKEND_NAME ?= gutenprint52+usb
EXEC_NAME ?= dyesub_backend

# Destination directories
DESTDIR ?=
CUPS_BACKEND_DIR ?= $(DESTDIR)/usr/lib/cups/backend
CUPS_DATA_DIR ?= $(DESTDIR)/usr/share/cups

# Tools
CC ?= $(CROSS_COMPILE)gcc
CPPCHECK ?= cppcheck
MKDIR ?= mkdir
INSTALL ?= install
LN ?= ln
RM ?= rm

# Flags
CFLAGS += -Wall -Wextra -g -Os -D_GNU_SOURCE -std=c99
LDFLAGS += `pkg-config --libs libusb-1.0`
CPPFLAGS += `pkg-config --cflags libusb-1.0`
# CPPFLAGS += -DLIBUSB_PRE_1_0_10
CPPFLAGS += -DURI_PREFIX=\"$(BACKEND_NAME)\"

# If you have the binary s6145 library, use it..
# The result is *NOT* GPL compatible.
ifneq ($(LIBS6145),)
CFLAGS += -DWITH_6145_LIB -I$(LIBS6145)

#LIBS6145_NAME = S6145ImageProcess-x64  # x86_64
#LIBS6145_NAME = S6145ImageProcess-x32  # x86_32
LIBS6145_NAME = S6145ImageProcessRE    # Reverse-engineered
DEPS += $(LIBS6145)/libS6145ImageProcessRE.so

LDFLAGS += -L$(LIBS6145) -l$(LIBS6145_NAME)
endif

# List of backends
BACKENDS = sonyupdr150 kodak6800 kodak1400 shinkos2145 shinkos1245 canonselphy mitsu70x kodak605 dnpds40 citizencw01 mitsu9550 shinkos6245 shinkos6145

# Build stuff
DEPS += backend_common.h
SOURCES = backend_common.c $(addsuffix .c,$(addprefix backend_,$(BACKENDS)))

# And now the rules!
all: $(EXEC_NAME) $(BACKENDS)

$(EXEC_NAME): $(SOURCES) $(DEPS)
	$(CC) -o $@ $(SOURCES)  $(CFLAGS) $(CPPFLAGS) $(LDFLAGS)

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
	$(RM) -f $(EXEC_NAME) $(BACKENDS)

ifneq ($(LIBS6145),)
$(LIBS6145)/libS6145ImageProcessRE.so:  $(LIBS6145)/libS6145ImageProcess.o
	$(CC) -Os -g -shared -o $@ $<

$(LIBS6145)/libS6145ImageProcess.o:  $(LIBS6145)/libS6145ImageProcess.c
	$(CC) -c -o $@ -fPIC $<

endif
