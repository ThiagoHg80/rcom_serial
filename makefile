
lab3_1_transmitter:
	gcc -o bin/writenoncanonical example_code/writenoncanonical.c

lab3_1_5_transmitter:
	gcc -o bin/writenoncanonical_5 example_code/lab3_1/writenoncanonical.c

lab3_1_receiver:
	gcc -o bin/readnoncanonical example_code/noncanonical.c

lab3_1_5_receiver:
	gcc -o bin/readnoncanonical_5 example_code/lab3_1/noncanonical.c

milestone_1:
	gcc -o bin/conn_sm milestones/connection_state_machine.c

example_app:
	gcc -o bin/app -D linklayer.h linklayer.c example_app.c

linklayer_object:
	gcc -c linklayer.c -o linklayer.o