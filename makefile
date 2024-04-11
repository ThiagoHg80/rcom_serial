
lab3_1_transmitter:
	gcc -o bin/writenoncanonical example_code/writenoncanonical.c

lab3_1_5_transmitter:
	gcc -o bin/writenoncanonical_5 example_code/lab3_1/writenoncanonical.c

lab3_1_receiver:
	gcc -o bin/readnoncanonical example_code/noncanonical.c

lab3_1_5_receiver:
	gcc -o bin/readnoncanonical_5 example_code/lab3_1/noncanonical.c