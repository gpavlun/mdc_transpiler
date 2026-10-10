CC ?= cc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Wpedantic
CPPFLAGS ?=
LDFLAGS ?=
PREFIX ?= /usr/local

.PHONY: all clean test install

all: mdc

mdc: src/mdc.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -o $@ $(LDFLAGS)

test: mdc
	MDCTOOL=./mdc sh tests/test.sh

clean:
	rm -f mdc

install: mdc
	install -Dm755 mdc "$(DESTDIR)$(PREFIX)/bin/mdc"
