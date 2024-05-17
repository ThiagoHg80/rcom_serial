all: linklayer_obj cable app

linklayer_obj:
	gcc ./protocol/linklayer.c ./protocol/linklayer.h -o ./protocol/linklayer.o

cable:
	gcc -w ./cable/cable.c -o ./bin/cable

app:
	gcc -w ./app/main.c ./protocol/*.o -o ./bin/main

clean:
	rm ./protocol/linklayer.o ./bin/cable ./bin/main