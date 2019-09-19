# tests/Makefile
#
# Under this directory there will hopefully be more tests in the long run.
# Tests are in the form of standalone executables which can contain multiple
# test cases. Protocol is simple: the test passes if it returns 0 and fails
# otherwise. Usually the failure is caused by the standalone application ending
# up calling abort(3) through a failed assert(3).
#
# To understand the test failure, use of gdb or other debugging tools is adviced.
#
# As such the tests are meant to be run on developer desktops and never
# integrated into any unix-alike operation system environment (mcu, plc, etc).
#
# If you want to run the tests on a platform which does not yet have
# the sanitizer support, just modify this and let us know through the issues!
SANITIZERS = -fsanitize=address -fsanitize=undefined

CFLAGS = -std=c11 -g -Og -I../ -I../utils $(SANITIZERS) -fstrict-overflow
LIB_CFLAGS = $(CFLAGS)
LDFLAGS = $(SANITIZERS)

LIB_OUTDIR = ./lib

.PHONY: clean

LIB_SOURCES = $(wildcard ../*.c)
LIB_OBJECTS = $(patsubst %.c,$(LIB_OUTDIR)/%.o,$(notdir $(LIB_SOURCES)))

TEST_CASES_SRC = $(wildcard *.c)
TEST_CASES = $(patsubst %.c,%,$(notdir $(TEST_CASES_SRC)))

test: $(LIB_OUTDIR) test_all

test_all: $(TEST_CASES)
	@for test in $(TEST_CASES); do retval=0; ./$$test || retval=$?; if [ "$$retval" -ne 0 ]; then echo $$test: failed; exit 1; fi; echo $$test: ok; done

$(TEST_CASES): %: %.c libsimplemotionv2.a
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< libsimplemotionv2.a

$(LIB_OUTDIR):
	mkdir -p $(LIB_OUTDIR)

libsimplemotionv2.a: $(LIB_OBJECTS)
	ar rcs $@ $^

$(LIB_OUTDIR)/%.o: ../%.c
	$(CC) $(LIB_CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJ) $(LIB_OBJECTS) $(TEST_CASES) libsimplemotionv2.a
	rmdir $(LIB_OUTDIR)
