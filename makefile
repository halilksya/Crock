CC      ?= cc
CFLAGS  ?= -std=c11 -O2 -g
CFLAGS  += -Iinclude -Wall -Wextra -Wshadow -Wconversion -Wsign-conversion \
           -Wpointer-arith -Wstrict-prototypes -D_GNU_SOURCE
CFLAGS  += -MMD -MP
LDFLAGS ?=
LDLIBS  := -lz

ifdef SAN
CFLAGS  += -fsanitize=address,undefined -fno-omit-frame-pointer
LDFLAGS += -fsanitize=address,undefined
endif

SRC := src/util/log.c \
       src/util/bstream.c \
       src/net/addr.c \
       src/raknet/server.c \
       src/raknet/offline.c \
       src/raknet/frame.c \
       src/raknet/ack.c \
       src/raknet/session.c \
       src/raknet/recv.c \
       src/raknet/send.c \
       src/game/compress.c \
       src/game/batch.c \
	   src/game/connection.c \
	   src/game/handler.c 

MAIN_SRC         := $(SRC) src/main.c
TEST_COMMON_SRC  := $(SRC) tests/test_common.c
TEST_BSTREAM_SRC := $(SRC) tests/test_bstream.c
TEST_FRAME_SRC   := $(SRC) tests/test_frame.c
TEST_ACK_SRC     := $(SRC) tests/test_ack.c

MAIN_OBJ         := $(MAIN_SRC:.c=.o)
TEST_COMMON_OBJ  := $(TEST_COMMON_SRC:.c=.o)
TEST_BSTREAM_OBJ := $(TEST_BSTREAM_SRC:.c=.o)
TEST_FRAME_OBJ   := $(TEST_FRAME_SRC:.c=.o)
TEST_ACK_OBJ     := $(TEST_ACK_SRC:.c=.o)

.PHONY: all test clean run

all: mrock

mrock: $(MAIN_OBJ)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

test-common: $(TEST_COMMON_OBJ)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

test-bstream: $(TEST_BSTREAM_OBJ)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

test-frame: $(TEST_FRAME_OBJ)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

test-ack: $(TEST_ACK_OBJ)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

test: test-common test-bstream test-frame test-ack
	./test-common
	./test-bstream
	./test-frame
	./test-ack

run: mrock
	./mrock

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

DEPS := $(MAIN_OBJ:.o=.d) $(TEST_COMMON_OBJ:.o=.d) \
        $(TEST_BSTREAM_OBJ:.o=.d) $(TEST_FRAME_OBJ:.o=.d) $(TEST_ACK_OBJ:.o=.d)
-include $(wildcard $(DEPS))

clean:
	rm -f mrock test-common test-bstream test-frame test-ack
	find . -name '*.o' -delete
	find . -name '*.d' -delete