# This is a Makefile for Travis CI, not tested for other purposes

SOURCES = $(wildcard *.c) \
	drivers/serial/pcserialport.c \
	drivers/tcpip/tcpclient.c

OBJECTS = $(SOURCES:%.c=%.o)

#user optionsare the ones starting with -D below
#be sure to check also user_options.h for more
CPPFLAGS = -I. -Iutils/ -DENABLE_BUILT_IN_DRIVERS
CFLAGS = -Wall -Wextra -DENABLE_BUILT_IN_DRIVERS -Iutils/

all: libsimplemotionv2.a

libsimplemotionv2.a: $(OBJECTS)
	ar rcs $@ $^

test:
	# this target executes the test cases; the test build depends on
	# gcc/clag sanitizers and those are not available except on unix alike
	# platforms/targets
	make -C tests

.PHONY: clean
clean:
	rm -f $(OBJECTS)
	make -C tests clean

