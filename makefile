.PHONY: all

all: build_linklayer_obj build_cable build_app

build_linklayer_obj: ./protocol/linklayer.c ./protocol/linklayer.h
	gcc -c ./protocol/linklayer.c -o ./protocol/linklayer.o

build_cable: ./cable/cable.c
	gcc -w ./cable/cable.c -o ./bin/cable

build_app: ./app/main.c build_linklayer_obj
	gcc -w ./app/main.c ./protocol/*.o -o ./bin/main

clean:
	rm -f ./protocol/linklayer.o ./bin/cable ./bin/main