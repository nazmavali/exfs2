CC = gcc
CFLAGS = -Wall -Wextra -g
TARGET = exfs2
SRCS = exfs2.c
OBJS = $(SRCS:.c=.o)

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET) $(OBJS)
	rm -f inode_seg_* data_seg_*  # Clean up segment files

.PHONY: test
test: $(TARGET)
	@echo "Running basic initialization test..."
	./$(TARGET) -l