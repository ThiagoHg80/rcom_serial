# RCOM Laboratory 3

## Folder Structure

```
.
├── app                 # Application layer
│   └── main.c
├── cable               # Virtual serial port
│   └── cable.c
├── makefile
├── penguin.gif         # File to be transmitted through the linklayer
├── protocol            # Link layer
│   ├── linklayer.c
│   └── linklayer.h
└── README.md
```

## How to run the application with the linklayer library

To compile the binaries you can execute the run script with no parameters:

```
./run.sh
```

After that you should have a ./bin folder with all the binaries that are necessary to run the application.

- `./run.sh cable` Runs the cable virtual port, you need to activate it by writing `on` in the cable terminal.
- `./run.sh tx` Runs the transmitter, it will try sending penguin.gif through the cable virtual port.
- `./run.sh rx` Runs the receiver, that will try to read from the cable virtual port and create the file penguin-received.gif.

To send files through real serial ports you will need to execute the binaries directly on the ports you want to use:

- `./bin/main /dev/ttyS<port-number> tx penguin.gif` Transmitter
- `./bin/main /dev/ttyS<port-number> rx penguin-received.gif` Receiver