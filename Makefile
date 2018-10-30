all: prj2

prj2: src/main.c src/serialize.c src/logging.c src/failure.c
	gcc -Wall -g -o prj2 src/main.c src/serialize.c src/logging.c src/failure.c

clean:
	$(RM) prj2
