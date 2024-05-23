#include "linklayer.h"

#ifndef DEBUG
#define DEBUG 1
#endif

#define FLAG 0x5c
#define A_TX 0x01
#define A_RX 0x03
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

#define FRAME_MAX_SIZE 2007 // Worst case scenario frame size

/*
 * File Descriptor is not present on struct linklayer {}, so we have to
 * create a global variable for it
 * Given that: This API can't handle two open connections at the same time
 */
static int fd, res, s = 0, num_tries = MAX_RETRANSMISSIONS_DEFAULT, time_out = TIMEOUT_DEFAULT;
static struct termios oldtio,newtio;

static int stats_transmitted_bytes = 0;
static int stats_aux_escaped_bytes = 0;
static int stats_received_bytes = 0;

#if DEBUG
    static int bcc2_tracker = 0;
#endif

static ssize_t send_cframe(unsigned char A,unsigned char C) {
    unsigned char buf[5] = {FLAG, A, C, A^C, FLAG};
    #if DEBUG
    printf("            [send_cframe] %02x %02x %02x %02x %02x --> \n",buf[0],buf[1],buf[2],buf[3],buf[4]);
    #endif
    return write(fd,buf,5);
}

static void bytestuff(unsigned char byte, unsigned char *frame, int *n) {
    if(byte == FLAG || byte == ESC) {
        frame[(*n)++] = ESC;
        frame[(*n)++] = byte ^ ESC_XOR;
        stats_aux_escaped_bytes++;
    } else {
        frame[(*n)++] = byte;
    }
    return;
}

// Opens a conection using the "port" parameters defined in struct linkLayer, returns "-1" on error and "1" on sucess
int llopen(linkLayer connectionParameters) {
    #if DEBUG
        printf("[linklayer] llopen() opening socket\n");
    #endif

    unsigned char buf[5];

    if(connectionParameters.timeOut)
        time_out = connectionParameters.timeOut;

    if(connectionParameters.numTries)
        num_tries = connectionParameters.numTries;

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

    if(connectionParameters.role == 0)
        send_cframe(A_TX,SET);

    unsigned byte = 0, address_byte = 0, control_byte = 0;
    int state = 1;
    while(state) {
        sleep(0.1);
        read(fd,&byte,1);
        #if DEBUG
        printf("            [%d] <-- %02x\n",state,byte);
        #endif
        switch(state) {
            case 1:
                if(byte == FLAG)
                    state = 2;
            break;
            case 2:
                if(byte == A_TX || byte == A_RX) {
                    address_byte = byte;
                    state = 3;
                }
                else if(byte == FLAG)
                    state = 2;
                else
                    state = 1;
            break;
            case 3:
                if(byte == SET || byte == UA) {
                    control_byte = byte;
                    state = 4;
                }
                else if(byte == FLAG)
                    state = 2;
                else
                    state = 1;
            break;
            case 4:
                if(byte == address_byte^control_byte)
                    state = 5;
                else if(byte == FLAG)
                    state = 2;
                else
                    state = 1;
            break;
            case 5:
                if(byte == FLAG) {
                    if(control_byte == SET) // Answer SET with UA
                        send_cframe(address_byte,UA);
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
    int frame_size;
    unsigned char bcc2, frame[FRAME_MAX_SIZE];
    // Frame
    frame[0] = FLAG;
    frame[1] = 0x01;
    frame[2] = s ? I_1 : I_0;
    frame[3] = frame[1]^frame[2];

    #if DEBUG
        printf("            [1] parity %d\n",s);
    #endif

    frame_size = 4;
    bcc2 = 0;
    #if DEBUG
        printf("            [1] constructing packet");
        printf("            [1] %02x %02x %02x %02x --> \n",frame[0],frame[1],frame[2],frame[3]);
    #endif
    for(int i = 0; i < bufSize; i++) {
        bcc2 = bcc2 ^ buf[i]; // The generation of BCC considers only the original octets (before stuffing)
        bytestuff(buf[i],frame,&frame_size);
        #if DEBUG
            if(buf[i] == FLAG || buf[i] == ESC)
                printf("ESCAPE ");
            printf("%02x(%02x) ",frame[frame_size - 1], bcc2);
            if((frame_size - 4) % 16 == 0)
                printf("\n");
        #endif
    }
    #if DEBUG
        printf("%02x %02x\n",bcc2,FLAG);
    #endif
    bytestuff(bcc2,frame,&frame_size);

    frame[frame_size++] = FLAG;

    int state = 11;
    int transmission_counter = 0;

    unsigned char control_buf[6];
    while(state) {
        sleep(0.1);
        switch(state) {
            case 11:
                res = write(fd,frame,frame_size);
                #if DEBUG
                    printf("            [1] sending %d bytes of data\n",frame_size - 6);
                #endif
                transmission_counter++;
                if(transmission_counter > num_tries) {
                    return -1;
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
                if(control_buf[2] == (frame[2] == I_0 ? RR_0 : RR_1)) {
                    state = 4;
                }
                else if(control_buf[2] == REJ_0 || control_buf[2] == REJ_1) { // (frame[2] == I_0 ? REJ_0 : REJ_1) // Should this be used ?
                    state = 11; // Retransmission
                    // TODO: Change to state 11 only after receiving the full control packet
                    #if DEBUG
                        printf("            [%d] Retransmitting data\n",state);
                    #endif
                } else if(control_buf[2] == FLAG) {
                    state = 2;
                } else {
                    state = 1;
                }
            break;
            case 4:
                res = read(fd,&control_buf[3],1);
                #if DEBUG
                    printf("            [%d] <-- %02x\n",state,control_buf[3]);
                    printf("            [%d] received %02x and expected %02x\n",state,control_buf[3],control_buf[2]^control_buf[1]);
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
                    s = !s; // change parity
                    state = 0;
                }
                else
                    state = 1;
            break;
        }
    }

    stats_transmitted_bytes += frame_size - 6 - stats_aux_escaped_bytes;
    stats_aux_escaped_bytes = 0;
    return 1;
};

// Receive data in packet
int llread(unsigned char* packet) {
    #if DEBUG
        printf("[linklayer] llread() reading socket data\n");
    #endif
    int res;
    size_t frame_size = 0;
    unsigned char buf[6], frame[FRAME_MAX_SIZE];

    int state = 1;
    while(state) {
        sleep(0.1);
        switch(state) {
            case 1:
                res = read(fd,&frame[0],1);
                #if DEBUG
                    printf("            [%d] <-- %02x\n",state,frame[0]);
                #endif
                if(frame[0] == FLAG)
                    state = 2;
            break;
            case 2:
                res = read(fd,&frame[1],1);
                #if DEBUG
                    printf("            [%d] <-- %02x\n",state,frame[1]);
                #endif
                if(frame[1] == 0x01)
                    state = 3;
                else if(frame[1] == FLAG)
                    state = 2;
                else
                    state = 1;
            break;
            case 3:
                res = read(fd,&frame[2],1);
                #if DEBUG
                    printf("            [%d] <-- %02x\n",state,frame[2]);
                #endif
                if( (s == 0)*(frame[2] == I_0) || (s == 1)*(frame[2] == I_1) )
                    state = 4;
                else if(frame[2] == FLAG)
                    state = 2;
                else
                    state = 1;
            break;
            case 4:
                res = read(fd,&frame[3],1);
                #if DEBUG
                    printf("            [%d] <-- %02x\n",state,frame[3]);
                #endif
                if(frame[3] == frame[2]^frame[1])
                    state = 5;
                else if(frame[3] == FLAG)
                    state = 2;
                else
                    state = 1;
            break;
            case 5:
                frame_size = 4;
                #if DEBUG
                    bcc2_tracker = 0;
                #endif
                while(1) {
                    res = read(fd,&frame[frame_size++],1);

                    if(frame[frame_size - 1] == ESC) { // byte destuffing (note that this also destuff bcc2)
                        #if DEBUG
                            printf("ESCAPE ");
                        #endif
                        res = read(fd,&frame[frame_size - 1],1);
                        frame[frame_size - 1] ^= ESC_XOR;
                    } else if(frame[frame_size - 1] == FLAG) { // end-of-frame
                        state = 6;
                        #if DEBUG
                            printf("%02x \n",FLAG);
                        #endif
                        break;
                    }

                    #if DEBUG
                        bcc2_tracker ^= frame[frame_size - 1];
                        printf("%02x(%02x) ",frame[frame_size - 1],bcc2_tracker);
                        if((frame_size - 4) % 16 == 0)
                            printf("\n");
                    #endif
                }
            break;
            case 6:
                unsigned char bcc2_local = 0;
                for(int i = 4; i < frame_size - 2; i++) {
                    bcc2_local ^= frame[i];
                }
                #if DEBUG
                    printf("\n            [%d] received %02x and expected %02x\n",state,bcc2_local, frame[frame_size - 2]);
                #endif

                if(frame[frame_size - 2] == bcc2_local) {
                    buf[2] = s ? RR_1 : RR_0;
                    s = !s; // change parity
                    for(int i = 4; i < frame_size - 2; i++)
                        packet[i - 4] = frame[i];
                    state = 0;
                } else {
                    buf[2] = s ? REJ_1 : REJ_0;
                    state = 1;
                }

                #if DEBUG
                    printf("            [%d] parity %d\n",state,s);
                #endif
                send_cframe(A_TX,buf[2]);
            break;
        }
    }

    stats_received_bytes += frame_size - 6;
    return frame_size - 6;
};

// Closes previously opened connection; if showStatistics==TRUE, link layer should print statistics in the console on close
int llclose(linkLayer connectionParameters, int showStatistics) {
    #if DEBUG
        printf("[linklayer] llclose() closing socket\n");
    #endif

    int res;
    unsigned char buf[5];

    if(connectionParameters.role == 0)
        send_cframe(A_TX,DISC);

    int state = 1;
    unsigned char byte = 0, addres_byte = 0, control_byte = 0;
    while(state) {
        sleep(0.1);
        res = read(fd,&byte,1);
        switch(state) {
            case 1:
                if(byte == FLAG)
                    state = 2;
            break;
            case 2:
                if(byte == A_TX || byte == A_RX) {
                    addres_byte = byte;
                    state = 3;
                }
                else if(byte == FLAG)
                    state = 2;
                else
                    state = 1;
            break;
            case 3:
                if(byte == DISC || byte == UA) {
                    control_byte = byte;
                    state = 4;
                }
                else if(byte == FLAG)
                    state = 2;
                else
                    state = 1;
            break;
            case 4:
                if(byte == addres_byte^control_byte)
                    state = 5;
                else if(byte == FLAG)
                    state = 2;
                else
                    state = 1;
            break;
            case 5:
                if(byte != FLAG) {
                    state = 1;
                    break;
                }

                if(control_byte == DISC) {
                    control_byte = connectionParameters.role == 0 ? UA : DISC;
                    send_cframe(addres_byte,control_byte);
                    state = 1;
                }

                if(control_byte == UA) { // sent or received UA
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

    if(showStatistics) {
        printf("[linklayer] llclose() statistics\n");
        printf("            bytes received: %d\n", stats_received_bytes);
        printf("            bytes sent: %d\n", stats_transmitted_bytes);
    }
    return 1;
};