CC      = cc
CFLAGS  = -std=c99 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -O2
TARGET  = vi
SRCS    = main.c terminal.c buffer.c screen.c modes.c
OBJS    = $(SRCS:.c=.o)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

%.o: %.c vi.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(TARGET) $(OBJS)

.PHONY: clean
