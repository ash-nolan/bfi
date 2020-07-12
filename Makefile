.POSIX:
.SUFFIXES:
.PHONY: all clean format
.SILENT: clean

C99_DBG = -O0 -g
C99_REL = -DNDEBUG

CC = c99
CFLAGS = $(C99_REL)
OBJS = bfi.o
TARGET = bfi

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) -o $@ $(OBJS) $(CFLAGS)

clean:
	rm -f $(TARGET) $(OBJS)

format:
	clang-format -i *.c

.SUFFIXES: .c .o
.c.o:
	$(CC) -o $@ $(CFLAGS) -c $<
