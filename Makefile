CC ?= cc
CFLAGS ?= -O2 -std=c11 -Wall -Wextra -Wpedantic
LDFLAGS ?=

TARGET = snake
SRC = snake.c

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LDFLAGS)

clean:
	rm -f $(TARGET)
