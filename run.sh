#!/bin/bash

# 1 - Ensure binaries exist

if [ ! -d "bin/" ]; then
    echo "Creating directory ./bin"
    mkdir bin
fi

APP_BINARY_PATH="./bin/main"
CABLE_BINARY_PATH="./bin/cable"

if [[ ! -f "$APP_BINARY_PATH" ]]; then
    make build_app
fi

if [[ ! -f "$CABLE_BINARY_PATH" ]]; then
    make build_cable
fi

case $1 in
    tx)
        valgrind --track-origins=yes --leak-check=yes ./bin/main /dev/ttyS10 tx penguin.gif
    ;;

    rx)
        valgrind --track-origins=yes --leak-check=yes ./bin/main /dev/ttyS11 rx penguin-received.gif
    ;;

    cable)
        ./bin/cable
    ;;

    *)
        echo "./run.sh tx        # Transmitter";
        echo "./run.sh rx        # Receiver";
        echo "./run.sh cable     # Virtual serial port"
    ;;
esac