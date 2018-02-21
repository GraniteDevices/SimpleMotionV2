SOURCES = $(wildcard *.c) \
	drivers/serial/pcserialport.c \
	drivers/tcpip/tcpclient.c

OBJECTS = $(SOURCES:%.c=%.o)

CPPFLAGS = -I. -DENABLE_BUILT_IN_DRIVERS
CFLAGS = -Wall -Wextra -DENABLE_BUILT_IN_DRIVERS

libsimplemotionv2.a: $(OBJECTS)
	ar rcs $@ $^

.PHONY: clean
clean:
	rm -f $(OBJECTS)

