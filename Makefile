CC      ?= gcc
CFLAGS  ?= -Wall -Wextra -Wpedantic -std=c99 -O2
TARGET   = tail
SRC      = tail.c

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

test: $(TARGET)
	@bash tests/run_tests.sh

clean:
	rm -f $(TARGET)
