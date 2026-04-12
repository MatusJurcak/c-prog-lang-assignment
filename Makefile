TARGET_FILE = mytar

SRC_FILES = mytar.c

.PHONY: all clean cstyle

all: $(TARGET_FILE)

$(TARGET_FILE): $(SRC_FILES)
	$(CC) -Wall -Wextra -std=c99 $(SRC_FILES) -o $(TARGET_FILE)

clean:
	rm -f $(TARGET_FILE) *.o

cstyle:
	find . -name '*.[ch]' -exec clang-format -style=file -i {} \;
