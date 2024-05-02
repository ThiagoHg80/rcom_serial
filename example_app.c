#include "linklayer.h"

int main(int argc, char **argv) {
    if(
        (argc != 3) ||
        strncmp("/dev/ttyS", argv[1], 9) != 0 ||
        strlen(argv[1]) > 50 || // linkayer configuration serialPort max size
        strspn(argv[1] + 9, "0123456789") != strlen(argv[1] + 9) ||     // Match [0-9]+$ after /dev/ttyS
        ((strcmp("w", argv[2]) != 0 && strcmp("r", argv[2]) != 0))
    ) {
        printf("Usage:\tnserial SerialPort mode\n\tex: nserial /dev/ttyS1 write\n");
        exit(1);
    }

    struct linkLayer ll_cfg;
    strcpy(ll_cfg.serialPort, argv[1]);
    ll_cfg.role = strcmp("w", argv[2]) == 0 ? 0 : 1;
    ll_cfg.baudRate = 9600;

    llopen(ll_cfg);
    sleep(1);
    llclose(ll_cfg, 0);
}

