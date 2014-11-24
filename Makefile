TARGET=batman_adv

CC?=gcc
CFLAGS?=-O3 -Wall -Werror
LIBTOOL?=libtool

COLLECTD_PREFIX?=/usr
COLLECTD_HEADERS?=$(COLLECTD_PREFIX)/include/collectd/core

all: $(TARGET).la

install: all
	$(LIBTOOL) --mode=install /usr/bin/install -c $(TARGET).la \
		$(COLLECTD_PREFIX)/lib/collectd
	$(LIBTOOL) --finish \
		$(COLLECTD_PREFIX)/lib/collectd

clean:
	rm -rf .libs
	rm -rf build
	rm -f $(TARGET).la

$(TARGET).la: build/$(TARGET).lo
	$(LIBTOOL) --tag=CC --mode=link $(CC) $(CFLAGS) -module \
		-avoid-version -o $@ \
		-rpath $(COLLECTD_PREFIX)/lib/collectd \
		-lpthread build/$(TARGET).lo

build/$(TARGET).lo: src/$(TARGET).c
	$(LIBTOOL) --mode=compile $(CC) -DHAVE_CONFIG_H \
		-I $(COLLECTD_HEADERS) $(CFLAGS) -MD -MP -c \
		-o $@ src/$(TARGET).c
