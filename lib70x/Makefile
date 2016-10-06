# Basic stuff
LIBMITSUD70_NAME ?= MitsuD70ImageReProcess

# Destination directories
DESTDIR ?=
LIBDIR ?= $(DESTDIR)/usr/local/lib
BACKEND_DATA_DIR ?= $(PREFIX)/usr/local/share/gutenprint/backend_data

# Tools
CC ?= $(CROSS_COMPILE)gcc
CPPCHECK ?= cppcheck
MKDIR ?= mkdir
INSTALL ?= install
RM ?= rm

# Flags
CFLAGS += -Wall -Wextra -g -Os -std=c99 -fPIC --no-strict-overflow # -Wconversion
#LDFLAGS +=
#CPPFLAGS +=
CFLAGS += -funit-at-a-time

# Build stuff
SOURCES = libMitsuD70ImageReProcess.c

# And now the rules!
.PHONY: clean all install cppcheck

all: lib$(LIBMITSUD70_NAME).so

cppcheck:
	$(CPPCHECK) -q -v --std=c99 --enable=all -I/usr/include $(CPPFLAGS) $(SOURCES)

install:
	$(MKDIR) -p $(LIBDIR)
	$(INSTALL) -o root -m 755 lib$(LIBMITSUD70_NAME).so $(LIBDIR)
	$(MKDIR) -p $(BACKEND_DATA_DIR)
	$(INSTALL) -o root -m 644 data/*raw $(BACKEND_DATA_DIR)
	$(INSTALL) -o root -m 644 data/*lut $(BACKEND_DATA_DIR)
	$(INSTALL) -o root -m 644 data/*cpc $(BACKEND_DATA_DIR)

clean:
	$(RM) -f lib$(LIBMITSUD70_NAME).so *.o

lib$(LIBMITSUD70_NAME).so:  $(SOURCES:.c=.o)
	$(CC) $(LDFLAGS) -g -shared -o $@ $^

%.o: %.c
	$(CC) -c $(CFLAGS) -o $@ $<
