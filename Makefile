all: test

test: src/main.c src/serialize.c src/logging.c
	gcc -Wall -o test src/main.c src/serialize.c src/logging.c

clean:
	$(RM) test
