#include "linklayer.h"

#ifndef DEBUG
#define DEBUG 1
#endif

#define FLAG 0x5c
#define SET  0x07
#define DISC 0x0a
#define UA   0x06
#define ESC  0x5d
#define ESC_XOR 0x20
#define RR_0 0x01
#define RR_1 0x11
#define REJ_0 0x05
#define REJ_1 0x15
#define R_XOR 0x10

#define I_0  0x80
#define I_1  0xc0
#define I_XOR 0x40

#define FRAME_MAX_SIZE 1000

/*
 * File Descriptor is not present on struct linklayer {}, so we have to 
 * create a global variable for it
 * Given that: This API can't handle two open connections at the same time 
 */
static int fd, s = 0;
static struct termios oldtio,newtio;

// Opens a conection using the "port" parameters defined in struct linkLayer, returns "-1" on error and "1" on sucess
int llopen(linkLayer connectionParameters) {
    #if DEBUG 
        printf("[linklayer] llopen() opening socket\n");
    #endif
    
    int res;
    unsigned char buf[5];

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
        #if DEBUG
            printf("            [1] %02x %02x %02x %02x %02x --> \n",buf[0],buf[1],buf[2],buf[3],buf[4]);
        #endif
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
        sleep(0.1);
        switch(state) {
            case 1:
                res = read(fd,&buf[0],1);
                #if DEBUG 
                    printf("            [%d] <-- %02x\n",state,buf[0]);
                #endif
                if(buf[0] == FLAG)
                    state = 2;
            break;
            case 2:
                res = read(fd,&buf[1],1); 
                #if DEBUG 
                    printf("            [%d] <-- %02x\n",state,buf[1]);
                #endif
                if(buf[1] == 0x01)
                    state = 3;
                else if(buf[1] == FLAG)
                    state = 2;
                else
                    state = 1;
            break;
            case 3:
                res = read(fd,&buf[2],1); 
                #if DEBUG 
                    printf("            [%d] <-- %02x\n",state,buf[2]);
                #endif
                if(buf[2] == SET || buf[2] == UA)
                    state = 4;
                else if(buf[2] == FLAG)
                    state = 2;
                else
                    state = 1;
            break;
            case 4:
                res = read(fd,&buf[3],1); 
                #if DEBUG 
                    printf("            [%d] <-- %02x\n",state,buf[3]);
                #endif
                if(buf[3] == buf[2]^buf[1])
                    state = 5;
                else if(buf[3] == FLAG)
                    state = 2;
                else
                    state = 1;
            break;
            case 5:
                res = read(fd,&buf[4],1); 
                #if DEBUG 
                    printf("            [%d] <-- %02x\n",state,buf[4]);
                #endif
                if(buf[4] == FLAG) {
                    if(buf[2] == SET) { // Answer SET with UA
                        buf[0] = FLAG;
                        buf[1] = 0x01;
                        buf[2] = UA;
                        buf[3] = buf[1]^buf[2];
                        buf[4] = FLAG;
                        res = write(fd,buf,5);
                        #if DEBUG
                            printf("            [5] %02x %02x %02x %02x %02x --> \n",buf[0],buf[1],buf[2],buf[3],buf[4]);
                        #endif
                    }
                    state = 0;
                }
                else
                    state = 1;
            break;
        }
    }

    return 1;
}

// Sends data in buf with size bufSize
int llwrite(unsigned char* buf, int bufSize) {
    #if DEBUG 
        printf("[linklayer] llwrite() write data to socket\n");
    #endif
    int res;

    // TODO: Implement FRAME into function?
    int buf_position, frameSize;
    unsigned char bcc2, frame[FRAME_MAX_SIZE];
    // Frame
    frame[0] = FLAG;
    frame[1] = 0x01;
    frame[2] = s ? I_1 : I_0;
    frame[3] = frame[1]^frame[2];

    buf_position = 0;
    frameSize = 4;
    bcc2 = 0;
    for(; frameSize < FRAME_MAX_SIZE - 2; frameSize++) {
        // The generation of BCC considers only the original octets (before stuffing)
        bcc2 = bcc2 ^ buf[buf_position];

        // Byte Stuffing
        if(buf[buf_position] == FLAG || buf[buf_position] == ESC) {
            // Check for the very specific case where you're escaping a character
            // at position FRAME_MAX_SIZE - 3 (you only have ony byte left to use in the frame)
            if(frameSize == FRAME_MAX_SIZE - 3) {
                bcc2 = bcc2 ^ buf[buf_position]; // Remove octet from BCC
                break;
            }
            frame[frameSize] = ESC;
            frameSize++;
            frame[frameSize] = buf[buf_position]^ESC_XOR;
        }

        frame[frameSize] = buf[buf_position];
        buf_position++;
    }
    frame[frameSize] = bcc2;
    frameSize++;
    frame[frameSize] = FLAG;

    int state = 11;
    int rej_counter = 0;
    
    unsigned char control_buf[6];
    while(state) {
        sleep(0.1);
        switch(state) {
            case 11:
                res = write(fd,frame,frameSize+1);
                printf("            [1] sending %d bytes of data",frameSize + 1);
                #if DEBUG
                    printf("            [1] %02x %02x %02x %02x --> \n",frame[0],frame[1],frame[2],frame[3]);
                    for(int i = 4; i < frameSize+1; i++) {
                        printf("%02x ",frame[i]);
                    }
                    printf("\n");
                #endif
                rej_counter++;
                if(rej_counter > 5) {
                    state = 0;
                } else {
                    state = 1;
                }
            case 1:
                res = read(fd,&control_buf[0],1);
                #if DEBUG 
                    printf("            [%d] <-- %02x\n",state,control_buf[0]);
                #endif
                if(control_buf[0] == FLAG)
                    state = 2;
            break;
            case 2:
                res = read(fd,&control_buf[1],1); 
                #if DEBUG 
                    printf("            [%d] <-- %02x\n",state,control_buf[1]);
                #endif
                if(control_buf[1] == 0x01)
                    state = 3;
                else if(control_buf[1] == FLAG)
                    state = 2;
                else
                    state = 1;
            break;
            case 3:
                res = read(fd,&control_buf[2],1); 
                #if DEBUG 
                    printf("            [%d] <-- %02x\n",state,control_buf[2]);
                #endif
                if(control_buf[2] == (frame[2] == I_0 ? RR_0 : RR_1))
                    state = 4;
                else if(control_buf[2] == FLAG)
                    state = 2;
                else
                    state = 1;
            break;
            case 4:
                res = read(fd,&control_buf[3],1); 
                #if DEBUG 
                    printf("            [%d] <-- %02x\n",state,control_buf[3]);
                #endif
                if(control_buf[3] == control_buf[2]^control_buf[1])
                    state = 5;
                else if(control_buf[3] == FLAG)
                    state = 2;
                else
                    state = 1;
            break;
            case 5:
                res = read(fd,&control_buf[4],1); 
                #if DEBUG 
                    printf("            [%d] <-- %02x\n",state,control_buf[4]);
                #endif
                if(control_buf[4] == FLAG) {
                    s = !s;
                    state = 0;
                }
                else
                    state = 1;
            break;
        }
    }
    
    return 1;
};

// Receive data in packet
int llread(unsigned char* packet) {
    #if DEBUG 
        printf("[linklayer] llread() reading socket data\n");
    #endif
    int res;
    size_t n_data_buf = 0;
    unsigned char buf[6], data_buf[FRAME_MAX_SIZE];

    int state = 1;
    while(state) {
        sleep(0.1);
        switch(state) {
            case 1:
                res = read(fd,&buf[0],1);
                #if DEBUG 
                    printf("            [%d] <-- %02x\n",state,buf[0]);
                #endif
                if(buf[0] == FLAG)
                    state = 2;
            break;
            case 2:
                res = read(fd,&buf[1],1); 
                #if DEBUG 
                    printf("            [%d] <-- %02x\n",state,buf[1]);
                #endif
                if(buf[1] == 0x01)
                    state = 3;
                else if(buf[1] == FLAG)
                    state = 2;
                else
                    state = 1;
            break;
            case 3:
                res = read(fd,&buf[2],1); 
                #if DEBUG 
                    printf("            [%d] <-- %02x\n",state,buf[2]);
                #endif
                if(buf[2] == I_0 | buf[2] == I_1)
                    state = 4;
                else if(buf[2] == FLAG)
                    state = 2;
                else
                    state = 1;
            break;
            case 4:
                res = read(fd,&buf[3],1); 
                #if DEBUG 
                    printf("            [%d] <-- %02x\n",state,buf[3]);
                #endif
                if(buf[3] == buf[2]^buf[1])
                    state = 5;
                else if(buf[3] == FLAG)
                    state = 2;
                else
                    state = 1;
            break;
            case 5:                
                while(1) {
                    res = read(fd,&buf[4],1);
                    #if DEBUG
                        printf("%02x ",buf[4]);
                    #endif
                    if(buf[4] == ESC) { // byte destuffing
                        res = read(fd,&buf[5],1);
                        buf[4] = buf[5]^ESC_XOR;
                    } else if(buf[4] == FLAG) { // end-of-frame
                        state = 6;
                        n_data_buf--;
                        break;
                    }
                    data_buf[n_data_buf] = buf[4];
                    n_data_buf++;
                }
            break;
            case 6:
                unsigned char bcc2_local = 0;
                for(int i = 0; i < n_data_buf; i++) {
                    bcc2_local = bcc2_local ^ data_buf[i];
                }
                #if DEBUG
                    printf("\n            [6] received %02x and expected %02x\n",bcc2_local, data_buf[n_data_buf]);
                #endif
                if(data_buf[n_data_buf] == bcc2_local) {
                    buf[0] = FLAG;
                    buf[1] = 0x01;
                    buf[2] = buf[2] == I_0 ? RR_0 : RR_1;
                    buf[3] = buf[1]^buf[2];
                    buf[4] = FLAG;
                    res = write(fd,buf,5);
                    #if DEBUG
                        printf("            [6] %02x %02x %02x %02x %02x --> \n",buf[0],buf[1],buf[2],buf[3],buf[4]);
                    #endif
                    data_buf[n_data_buf] = '\0';
                    strcpy(packet,data_buf);
                    state = 0;
                } else {
                    state = 1;
                }
            break;
        }
    }

    return 1;
};

// Closes previously opened connection; if showStatistics==TRUE, link layer should print statistics in the console on close
int llclose(linkLayer connectionParameters, int showStatistics) {
    #if DEBUG 
        printf("[linklayer] llclose() closing socket\n");
    #endif

    int res; 
    unsigned char buf[5];

    if(connectionParameters.role == 0) {
        buf[0] = FLAG;
        buf[1] = 0x01;
        buf[2] = DISC;
        buf[3] = buf[1]^buf[2];
        buf[4] = FLAG;
        res = write(fd,buf,5);
        #if DEBUG
            printf("            [1] %02x %02x %02x %02x %02x --> \n",buf[0],buf[1],buf[2],buf[3],buf[4]);
        #endif
    }

    int state = 1;
    while(state) {
        sleep(0.1);
        switch(state) {
            case 1:
                res = read(fd,&buf[0],1);
                #if DEBUG 
                    printf("            [%d] <-- %02x\n",state,buf[0]);
                #endif
                if(buf[0] == FLAG)
                    state = 2;
            break;
            case 2:
                res = read(fd,&buf[1],1); 
                #if DEBUG 
                    printf("            [%d] <-- %02x\n",state,buf[1]);
                #endif
                if(buf[1] == 0x01)
                    state = 3;
                else if(buf[1] == FLAG)
                    state = 2;
                else
                    state = 1;
            break;
            case 3:
                res = read(fd,&buf[2],1); 
                #if DEBUG 
                    printf("            [%d] <-- %02x\n",state,buf[2]);
                #endif
                if(buf[2] == DISC || buf[2] == UA)
                    state = 4;
                else if(buf[2] == FLAG)
                    state = 2;
                else
                    state = 1;
            break;
            case 4:
                res = read(fd,&buf[3],1); 
                #if DEBUG 
                    printf("            [%d] <-- %02x\n",state,buf[3]);
                #endif
                if(buf[3] == buf[2]^buf[1])
                    state = 5;
                else if(buf[3] == FLAG)
                    state = 2;
                else
                    state = 1;
            break;
            case 5:
                res = read(fd,&buf[4],1); 
                #if DEBUG 
                    printf("            [%d] <-- %02x\n",state,buf[4]);
                #endif
                if(buf[4] != FLAG)
                    break;

                if(buf[2] == DISC) {
                    buf[0] = FLAG;
                    buf[1] = 0x01;
                    buf[2] = connectionParameters.role == 0 ? UA : DISC;
                    buf[3] = buf[1]^buf[2];
                    buf[4] = FLAG;
                    res = write(fd,buf,5);
                    #if DEBUG
                        printf("            [5] %02x %02x %02x %02x %02x --> \n",buf[0],buf[1],buf[2],buf[3],buf[4]);
                    #endif
                    state = 1;
                }

                if(buf[2] == UA) { // sent or received UA
                    state = 0;
                }
            break;
        }
    }


    if ( tcsetattr(fd,TCSANOW,&oldtio) == -1) {
        perror("tcsetattr");
        exit(-1);
    }
    close(fd);
    return 1;
};
