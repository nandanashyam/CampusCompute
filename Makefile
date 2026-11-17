CC      = gcc
CFLAGS  = -Wall -Wextra -Wpedantic -std=c11 -D_POSIX_C_SOURCE=200809L -g -O0
LDLIBS  = -pthread -lrt
TARGET  = campuscompute
SRCS    = main.c util.c job.c scheduler.c logger.c ipc.c banker.c worker.c resources.c
OBJS    = $(SRCS:.c=.o)
HDRS    = $(wildcard *.h)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $@ $(LDLIBS)

%.o: %.c $(HDRS)
	$(CC) $(CFLAGS) -c $< -o $@

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET) $(OBJS) campuscompute.log

.PHONY: all run clean
