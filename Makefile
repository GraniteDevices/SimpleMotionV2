BUILD_DIR := build
PREFIX ?= /usr/local

SOURCES = $(wildcard *.c) \
	drivers/serial/pcserialport.c \
	drivers/tcpip/tcpclient.c \
	utils/crc.c

HEADERS = $(wildcard *.h)

# Set library type to static or dynamic, used to
# set build directory etc.
ifeq ($(MAKECMDGOALS),libsimplemotionv2.so)
	LIBRARY_TYPE = dynamic
else
	LIBRARY_TYPE = static
endif

OBJECTS = $(SOURCES:%.c=$(BUILD_DIR)/$(LIBRARY_TYPE)/%.o)

# User options are the ones starting with -D below,
# be sure to check also user_options.h for more
CPPFLAGS := -DENABLE_BUILT_IN_DRIVERS
CFLAGS   := -g -Wall -Wextra
CFLAGS   += -DENABLE_BUILT_IN_DRIVERS

# Include directories
INCLUDES := -I.
INCLUDES += -Idrivers/serial/
INCLUDES += -Idrivers/tcpip/
INCLUDES += -Iutils/

# Linker libraries
LDLIBS := -lm

# Linker flags
LDFLAGS :=

all:
	$(MAKE) libsimplemotionv2.a
	$(MAKE) libsimplemotionv2.so

libsimplemotionv2.a: $(BUILD_DIR)/libsimplemotionv2.a

libsimplemotionv2.so: CFLAGS  += -fPIC
libsimplemotionv2.so: LDFLAGS += -shared
libsimplemotionv2.so: $(BUILD_DIR)/libsimplemotionv2.so

$(BUILD_DIR)/$(LIBRARY_TYPE)/%.o: ./%.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

$(BUILD_DIR)/libsimplemotionv2.a: $(OBJECTS)
	@mkdir -p $(@D)
	$(AR) rcs $@ $^

$(BUILD_DIR)/libsimplemotionv2.so: $(OBJECTS)
	@mkdir -p $(@D)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

# This target executes the test cases; the test build depends on
# gcc/clang sanitizers and those are not available except on unix alike
# platforms/targets
.PHONY: test
test:
	make -C tests

.PHONY: clean
clean:
	$(RM) -r $(BUILD_DIR)
	make -C tests clean

# Install library. To override desti
.PHONY: install
install:
	@mkdir -p $(DESTDIR)$(PREFIX)/lib
	@mkdir -p $(DESTDIR)$(PREFIX)/include
	cp $(BUILD_DIR)/libsimplemotionv2.so $(DESTDIR)$(PREFIX)/lib/libsimplemotionv2.so
	cp $(BUILD_DIR)/libsimplemotionv2.a $(DESTDIR)$(PREFIX)/lib/libsimplemotionv2.a
	cp *.h $(DESTDIR)$(PREFIX)/include/
	@echo "\nHeader files installed to $(PREFIX)/include"
	@echo "Library installed to $(PREFIX)/lib"
	@echo "You may need to run ldconfig"

.PHONY: uninstall
uninstall:
	cd $(DESTDIR)$(PREFIX)/include && rm -f $(HEADERS) && cd -
	rm -f $(DESTDIR)$(PREFIX)/lib/libsimplemotionv2.so
	rm -f $(DESTDIR)$(PREFIX)/lib/libsimplemotionv2.a
