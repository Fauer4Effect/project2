all: test

test: src/main.c src/serialize.c src/logging.c src/failure.c
	gcc -Wall -g -o test src/main.c src/serialize.c src/logging.c src/failure.c

clean:
	$(RM) test
