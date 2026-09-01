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

SRC := src/util/log.c

MAIN_SRC := $(SRC) src/main.c
TEST_SRC := $(SRC) tests/test_bstream.c

MAIN_OBJ := $(MAIN_SRC:.c=.o)
TEST_OBJ := $(TEST_SRC:.c=.o)

.PHONY: all test clean run

all: mrock

mrock: $(MAIN_OBJ)
	$(CC) $(LDFLAGS) -o $@ $^

test-bin: $(TEST_OBJ)
	$(CC) $(LDFLAGS) -o $@ $^

test: test-bin
	./test-bin

run: mrock
	./mrock -v

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $

DEPS := $(MAIN_OBJ:.o=.d) $(TEST_OBJ:.o=.d)
-include $(DEPS)

clean:
	rm -f mrock test-bin $(MAIN_OBJ) $(TEST_OBJ) $(DEPS)