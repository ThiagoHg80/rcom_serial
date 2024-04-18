#include "linklayer.h"

#define FLAG 0x5c
#define SET  0x07
#define UA   0x06

/*
 * File Descriptor is not present on struct linklayer {}, so we have to 
 * create a global variable for it
 * Given that: This API can't handle two open connections at the same time 
 */
int fd;

// Opens a conection using the "port" parameters defined in struct linkLayer, returns "-1" on error and "1" on sucess
int llopen(linkLayer connectionParameters) {
    int c, res;
    struct termios oldtio,newtio;
    unsigned char buf[255];
    int i, sum = 0, speed = 0;
    
    fd = open(connectionParameters.serialPort, O_RDWR | O_NOCTTY );
    if (fd < 0) { perror(connectionParameters.serialPort); exit(-1); }
    if ( tcgetattr(fd,&oldtio) == -1) { /* save current port settings */
        perror("tcgetattr");
        exit(-1);
    }

    bzero(&newtio, sizeof(newtio));
    newtio.c_cflag = connectionParameters.baudRate | CS8 | CLOCAL | CREAD;
    newtio.c_iflag = IGNPAR;
    newtio.c_oflag = 0;

    newtio.c_lflag = 0;
    newtio.c_cc[VTIME]    = 0;   /* inter-character timer unused */
    newtio.c_cc[VMIN]     = 1;   /* blocking read until 1 char received */

    tcflush(fd, TCIOFLUSH);

    if (tcsetattr(fd,TCSANOW,&newtio) == -1) {
        perror("tcsetattr");
        exit(-1);
    }

    if(connectionParameters.role == 0) {
        buf[0] = FLAG;
        buf[1] = 0x01;
        buf[2] = SET;
        buf[3] = buf[1]^buf[2];
        buf[4] = FLAG;
        res = write(fd,buf,5);
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
                printf("state %d : recv %02x\n",state,buf[0]);
                if(buf[0] == FLAG)
                    state = 2;
            break;
            case 2:
                res = read(fd,&buf[1],1); 
                printf("state %d : recv %02x\n",state,buf[1]);
                if(buf[1] == 0x01)
                    state = 3;
                else if(buf[1] == FLAG)
                    state = 2;
                else
                    state = 1;
            break;
            case 3:
                res = read(fd,&buf[2],1); 
                printf("state %d : recv %02x\n",state,buf[2]);
                if(buf[2] == SET || buf[2] == UA)
                    state = 4;
                else if(buf[2] == FLAG)
                    state = 2;
                else
                    state = 1;
            break;
            case 4:
                res = read(fd,&buf[3],1); 
                printf("state %d : recv %02x\n",state,buf[3]);
                if(buf[3] == buf[2]^buf[1])
                    state = 5;
                else if(buf[3] == FLAG)
                    state = 2;
                else
                    state = 1;
            break;
            case 5:
                res = read(fd,&buf[4],1); 
                printf("state %d : recv %02x\n",state,buf[4]);
                if(buf[4] == FLAG) {
                    if(buf[2] == SET) { // Answer SET with UA
                        buf[0] = FLAG;
                        buf[1] = 0x01;
                        buf[2] = UA;
                        buf[3] = buf[1]^buf[2];
                        buf[4] = FLAG;
                        res = write(fd,buf,5);
                        printf("state 1 : sent %02x\nstate 1 : sent %02x\nstate 1 : sent %02x\nstate 1 : sent %02x\nstate 1 : sent %02x\n", buf[0], buf[1], buf[2], buf[3], buf[4]);
                    }
                    state = 0;
                }
                else
                    state = 1;
            break;
        }
    }

    return fd;
}

// Sends data in buf with size bufSize
int llwrite(unsigned char* buf, int bufSize) {

};

// Receive data in packet
int llread(unsigned char* packet) {

};

// Closes previously opened connection; if showStatistics==TRUE, link layer should print statistics in the console on close
int llclose(linkLayer connectionParameters, int showStatistics) {

};
