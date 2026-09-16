CC      ?= cc
CFLAGS  ?= -std=c11 -O2 -g
CFLAGS  += -Iinclude -Wall -Wextra -Wshadow -Wconversion -Wsign-conversion \
           -Wpointer-arith -Wstrict-prototypes -D_GNU_SOURCE
CFLAGS  += -MMD -MP
LDFLAGS ?=

ifdef SAN
CFLAGS  += -fsanitize=address,undefined -fno-omit-frame-pointer
LDFLAGS += -fsanitize=address,undefined
endif

SRC := src/util/log.c \
       src/util/bstream.c \
       src/net/addr.c \
       src/raknet/server.c \
       src/raknet/offline.c

MAIN_SRC         := $(SRC) src/main.c
TEST_COMMON_SRC  := $(SRC) tests/test_common.c
TEST_BSTREAM_SRC := $(SRC) tests/test_bstream.c

MAIN_OBJ         := $(MAIN_SRC:.c=.o)
TEST_COMMON_OBJ  := $(TEST_COMMON_SRC:.c=.o)
TEST_BSTREAM_OBJ := $(TEST_BSTREAM_SRC:.c=.o)

.PHONY: all test clean run

all: mrock

mrock: $(MAIN_OBJ)
	$(CC) $(LDFLAGS) -o $@ $^

test-common: $(TEST_COMMON_OBJ)
	$(CC) $(LDFLAGS) -o $@ $^

test-bstream: $(TEST_BSTREAM_OBJ)
	$(CC) $(LDFLAGS) -o $@ $^

test: test-common test-bstream
	./test-common
	./test-bstream

run: mrock
	./mrock

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

DEPS := $(MAIN_OBJ:.o=.d) $(TEST_COMMON_OBJ:.o=.d) $(TEST_BSTREAM_OBJ:.o=.d)
-include $(wildcard $(DEPS))

clean:
	rm -f mrock test-common test-bstream \
	      $(MAIN_OBJ) $(TEST_COMMON_OBJ) $(TEST_BSTREAM_OBJ) $(DEPS)