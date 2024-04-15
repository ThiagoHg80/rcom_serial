/*Non-Canonical Input Processing*/

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define BAUDRATE B9600
#define MODEMDEVICE "/dev/ttyS1"
#define _POSIX_SOURCE 1
#define FALSE 0
#define TRUE 1

#define FLAG 0x5c
#define SET  0x07
#define UA   0x06

volatile int STOP=FALSE;

int main(int argc, char** argv)
{
    int fd,c, res;
    struct termios oldtio,newtio;
    unsigned char buf[255];
    int i, sum = 0, speed = 0;

    if(
        (argc != 3) ||
        strncmp("/dev/ttyS", argv[1], 9) != 0 ||
        strspn(argv[1] + 9, "0123456789") != strlen(argv[1] + 9) ||     // Match [0-9]+$ after /dev/ttyS
        ((strcmp("write", argv[2]) != 0 && strcmp("recv", argv[2]) != 0))
    ) {
        printf("Usage:\tnserial SerialPort mode\n\tex: nserial /dev/ttyS1 write\n");
        exit(1);
    }

    /*
    Open serial port device for reading and writing and not as controlling tty
    because we don't want to get killed if linenoise sends CTRL-C.
    */
    fd = open(argv[1], O_RDWR | O_NOCTTY );
    if (fd < 0) { perror(argv[1]); exit(-1); }

    if ( tcgetattr(fd,&oldtio) == -1) { /* save current port settings */
        perror("tcgetattr");
        exit(-1);
    }

    bzero(&newtio, sizeof(newtio));
    newtio.c_cflag = BAUDRATE | CS8 | CLOCAL | CREAD;
    newtio.c_iflag = IGNPAR;
    newtio.c_oflag = 0;

    /* set input mode (non-canonical, no echo,...) */
    newtio.c_lflag = 0;

    newtio.c_cc[VTIME]    = 0;   /* inter-character timer unused */
    newtio.c_cc[VMIN]     = 1;   /* blocking read until 1 char received */



    /*
    VTIME e VMIN devem ser alterados de forma a proteger com um temporizador a
    leitura do(s) próximo(s) caracter(es)
    */


    tcflush(fd, TCIOFLUSH);

    if (tcsetattr(fd,TCSANOW,&newtio) == -1) {
        perror("tcsetattr");
        exit(-1);
    }

    printf("New termios structure set\n");

    /* Protocol Implementation starts here */
    /*
     * If the mode (argv[2]) is "write":
     * Send the message
     */
    if(strcmp("write",argv[2]) == 0) {
        buf[0] = FLAG;
        buf[1] = 0x01;
        buf[2] = SET;
        buf[3] = buf[1]^buf[2];
        buf[4] = FLAG;
        res = write(fd,buf,5);
        printf("%d bytes written: %02x %02x %02x %02x %02x\n", res, buf[0], buf[1], buf[2], buf[3], buf[4]);
    }

    /* State Machine
     * 0 STOP
     * 1 Start
     * 2 FLAG_RCV
     * 3 A_RCV
     * 4 C_RCV
     * 5 BCC OK
     */
    int state = 1;
    while(state) {
        switch(state) {
            case 1:
                res = read(fd,&buf[0],1); 
                printf("%d:%02x\n",state,buf[0]);
                if(buf[0] == FLAG)
                    state = 2;
            break;
            case 2:
                res = read(fd,&buf[1],1); 
                printf("%d:%02x\n",state,buf[1]);
                if(buf[1] == 0x01)
                    state = 3;
                else if(buf[1] == FLAG)
                    state = 2;
                else
                    state = 1;
            break;
            case 3:
                res = read(fd,&buf[2],1); 
                printf("%d:%02x\n",state,buf[2]);
                if(buf[2] == SET || buf[2] == UA)
                    state = 4;
                else if(buf[2] == FLAG)
                    state = 2;
                else
                    state = 1;
            break;
            case 4:
                res = read(fd,&buf[3],1); 
                printf("%d:%02x\n",state,buf[3]);
                if(buf[3] == buf[2]^buf[1])
                    state = 5;
                else if(buf[3] == FLAG)
                    state = 2;
                else
                    state = 1;
            break;
            case 5:
                res = read(fd,&buf[4],1); 
                printf("%d:%02x\n",state,buf[4]);
                if(buf[4] == FLAG) {
                    if(buf[2] == SET) { // Answer SET with UA
                        buf[0] = FLAG;
                        buf[1] = 0x01;
                        buf[2] = UA;
                        buf[3] = buf[1]^buf[2];
                        buf[4] = FLAG;
                        res = write(fd,buf,5);
                        printf("%d bytes written: %02x %02x %02x %02x %02x\n", res, buf[0], buf[1], buf[2], buf[3], buf[4]);
                    }
                    state = 0;
                }
                else
                    state = 1;
            break;
        }
    }
    /* Protocol Implementation ends here */

    if ( tcsetattr(fd,TCSANOW,&oldtio) == -1) {
        perror("tcsetattr");
        exit(-1);
    }


    close(fd);
    return 0;
}
