# This is a Makefile for Travis CI, not tested for other purposes

SOURCES = $(wildcard *.c) \
	drivers/serial/pcserialport.c \
	drivers/tcpip/tcpclient.c

OBJECTS = $(SOURCES:%.c=%.o)

#user optionsare the ones starting with -D below
#be sure to check also user_options.h for more
CPPFLAGS = -I. -Iutils/ -DENABLE_BUILT_IN_DRIVERS -fsanitize=undefined -fstrict-overflow
CFLAGS = -Wall -Wextra -DENABLE_BUILT_IN_DRIVERS -Iutils/ -fsanitize=undefined -fstrict-overflow

all: libsimplemotionv2.a test

libsimplemotionv2.a: $(OBJECTS)
	ar rcs $@ $^

test:
	make -C tests

.PHONY: clean
clean:
	rm -f $(OBJECTS)
	make -C tests clean

