# Basic stuff
LIBS6145_NAME ?= S6145ImageReProcess

# Destination directories
DESTDIR ?=
LIBDIR ?= $(DESTDIR)/usr/local/lib

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

# Build stuff
SOURCES = libS6145ImageReProcess.c

# And now the rules!
.PHONY: clean all install cppcheck

all: lib$(LIBS6145_NAME).so

cppcheck:
	$(CPPCHECK) -q -v --std=c99 --enable=all -I/usr/include $(CPPFLAGS) $(SOURCES)

install:
	$(INSTALL) -o root -m 755 lib$(LIBS6145_NAME).so $(LIBDIR)
clean:
	$(RM) -f lib$(LIBS6145_NAME).so *.o

lib$(LIBS6145_NAME).so:  $(SOURCES:.c=.o)
	$(CC) $(LDFLAGS) -g -shared -o $@ $^

%.o: %.c
	$(CC) -c $(CFLAGS) -o $@ $<
