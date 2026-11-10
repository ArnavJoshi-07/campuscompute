CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -D_GNU_SOURCE -g
LDLIBS = -pthread -lrt

SRC = $(wildcard src/*.c)
OBJ = $(SRC:.c=.o)
HDR = $(wildcard src/*.h)

all: campus

campus: $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LDLIBS)

# rebuild everything if any header changes
$(OBJ): $(HDR)

clean:
	rm -f campus src/*.o logs/*.log

zip:
	@if [ -z "$(SRN)" ]; then echo "usage: make zip SRN=xxxx"; exit 1; fi
	rm -f $(SRN)_src.zip
	zip -r $(SRN)_src.zip src tests Makefile README.md -x '*.o'

.PHONY: all clean zip
