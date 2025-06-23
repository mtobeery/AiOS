CC = gcc
CFLAGS = -Wall -Wextra -O2 -std=c11 -Iinclude

SRCS = $(wildcard bootloader/*.c kernel/*.c ai/*.c security/*.c)
OBJS = $(SRCS:.c=.o)
TARGET = AiOSKernel.efi

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f $(TARGET) $(OBJS)
