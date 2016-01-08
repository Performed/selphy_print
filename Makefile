# Basic stuff
BACKEND_NAME ?= gutenprint52+usb
EXEC_NAME ?= dyesub_backend

# Destination directories
DESTDIR ?=
CUPS_BACKEND_DIR ?= $(DESTDIR)/usr/lib/cups/backend
CUPS_DATA_DIR ?= $(DESTDIR)/usr/share/cups
LIBDIR ?= $(DESTDIR)/usr/local/lib

# Don't compile with libS6145ImageProcess by default.
LIBS6145 ?=
# If set, we have the reverse-engineered libS6145ImageProcess.
RE_LIBS6145 ?=

# Tools
CC ?= $(CROSS_COMPILE)gcc
CPPCHECK ?= cppcheck
MKDIR ?= mkdir
INSTALL ?= install
LN ?= ln
RM ?= rm

### libS6145 nonsense
# Figure out which library to use
ifneq ($(LIBS6145),)
# Figure out OS
UNAME_S := $(shell uname -s)
# Figure out Arch
UNAME_P := $(shell uname -p)

# Lib6145 only works under Linux, and we only have the binary versions
ifeq ($(UNAME_S),Linux)
ifeq ($(UNAME_P),x86_64)
 LIBS6145_NAME = S6145ImageProcess-x64
endif
ifneq ($(filter %86,$(UNAME_P)),)
 LIBS6145_NAME = S6145ImageProcess-x32
else
 LIBS6145 = 
endif
endif # Linux
endif # libS6145

ifneq ($(LIBS6145_RE),)
LIBS6145 = $(LIBS6145_RE)
LIBS6145_NAME = S6145ImageProcessRE
DEPS += $(LIBS6145)/libS6145ImageProcessRE.so
CPPFLAGS += -DWITH_6145_LIB -DS6145_RE -I$(LIBS6145)
endif

# Finally, if we have any version of the library, use it.
ifneq ($(LIBS6145),)
CPPFLAGS += -DWITH_6145_LIB -I$(LIBS6145)
LDFLAGS += -L$(LIBS6145) -l$(LIBS6145_NAME)
endif
### libS6145 nonsense

# Flags
CFLAGS += -Wall -Wextra -g -Os -D_GNU_SOURCE -std=c99
LDFLAGS += `pkg-config --libs libusb-1.0`
CPPFLAGS += `pkg-config --cflags libusb-1.0`
# CPPFLAGS += -DLIBUSB_PRE_1_0_10
CPPFLAGS += -DURI_PREFIX=\"$(BACKEND_NAME)\"

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
ifneq ($(LIBS6145),)
	$(INSTALL) -o root -m 755 $(LIBS6145)/lib$(LIBS6145_NAME).so $(LIBDIR)
endif

clean:
	$(RM) -f $(EXEC_NAME) $(BACKENDS)

# Reverse-engineered LibS6145ImageProcess
ifneq ($(LIBS6145_RE),)
$(LIBS6145)/libS6145ImageProcessRE.so:  $(LIBS6145)/libS6145ImageProcess.o
	$(CC) -lm -Os -g -shared -o $@ $<

$(LIBS6145)/libS6145ImageProcess.o:  $(LIBS6145)/libS6145ImageProcess.c
	$(CC) -c -Wall -Wextra -fno-strict-overflow -o $@ -fPIC $<

endif
