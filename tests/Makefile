CC = gcc
CFLAGS = -std=c11 -I../ -fsanitize=undefined -fstrict-overflow
LDFLAGS = -L../ -fsanitize=undefined -fstrict-overflow
# LDLIBS = -lsimplemotionv2

.PHONY: clean

test: descriptions
	./descriptions

descriptions: descriptions.c ../libsimplemotionv2.a

clean:
	rm -f $(OBJ) descriptions
