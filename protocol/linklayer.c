#include "linklayer.h"
#include <time.h>

#ifndef DEBUG
#define DEBUG 1
#endif

#ifndef RANDOM_ERROR_GENERATION
#define RANDOM_ERROR_GENERATION 0
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
time_t start,end;

struct Statistics {
    int received_i_frames;
    int transmitted_i_frames;
    int received_rej_frames;
    int transmitted_rej_frames;
    int timeout_counter;
    int escaped_bytes;
    int transmitted_bytes;
    int received_bytes;
    time_t total_time;
    time_t average_frame_time;
    time_t fastest_frame;
    time_t slowest_frame;
};

static struct Statistics stats;

#if DEBUG
    static int bcc2_tracker = 0;
#endif

static float read_timeout(unsigned char *byte) {
    float time = 0;
    while(read(fd,byte,1) < 0 || time > time_out) {
        time += 0.1;
        sleep(0.1);
    }
    return time > time_out ? -1 : time;
}

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
        stats.escaped_bytes++;
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

    stats.received_i_frames = 0;
    stats.transmitted_i_frames = 0;
    stats.received_rej_frames = 0;
    stats.transmitted_rej_frames = 0;
    stats.timeout_counter = 0;
    stats.escaped_bytes = 0;
    stats.transmitted_bytes = 0;
    stats.received_bytes = 0;

    stats.average_frame_time = 0;
    stats.total_time = 0;
    stats.fastest_frame = 9999999;
    stats.slowest_frame = -1;

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

    unsigned char byte = 0, address_byte = 0, control_byte = 0;
    int retrasmission_counter = 0, state = 1;
    while(state) {
        if(read_timeout(&byte) < 0) {
            retrasmission_counter++;
            if(retrasmission_counter > num_tries)
                return -1;
            if(connectionParameters.role == 0)
                send_cframe(A_TX,SET);
            state = 1;
        }
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
                #if DEBUG
                printf("            [%d] received %02x and expected %02x\n",state,byte,address_byte^control_byte);
                #endif
                if(byte == (address_byte^control_byte))
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
    start = time(0);
    #if DEBUG
    printf("[linklayer] llwrite() write data to socket\n");
    #endif

    // Populate frame array
    int frame_size;
    unsigned char bcc2, frame[FRAME_MAX_SIZE];
    frame[0] = FLAG;
    frame[1] = A_TX;
    frame[2] = s ? I_1 : I_0;
    frame[3] = frame[1]^frame[2];

    #if DEBUG
    printf("            [1] parity %d\n",s);
    printf("            [1] constructing packet");
    printf("            [1] %02x %02x %02x %02x --> \n",frame[0],frame[1],frame[2],frame[3]);
    #endif

    frame_size = 4;
    bcc2 = 0;
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

    // Send frame
    res = write(fd,frame,frame_size);
    #if DEBUG
    printf("            [1] sending %d bytes of data\n",frame_size - 6);
    #endif

    // Start state machine
    int state = 1;
    int retransmission_counter = 0;
    unsigned char byte = 0, address_byte = 0, control_byte = 0;
    while(state) {
        if(read_timeout(&byte) < 0) {
            retransmission_counter++;
            if(retransmission_counter > num_tries) {
                stats.total_time += end - start;
                if(end - start > stats.slowest_frame)
                    stats.slowest_frame = end - start;
                if(end - start < stats.fastest_frame)
                    stats.fastest_frame = end - start;
                return -1;
            }

            res = write(fd,frame,frame_size);
            stats.transmitted_i_frames++;
            #if DEBUG
            printf("            [%d] Retransmitting %d bytes of data\n",state, frame_size - 6);
            #endif

            state = 1;
        }
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
                if(byte == FLAG) {
                    state = 2;
                    break;
                }

                if(byte == (frame[2] == I_0 ? RR_0 : RR_1)) {
                    control_byte = byte;
                    state = 4;
                }
                else if(byte == (frame[2] == I_0 ? REJ_0 : REJ_1)) {
                    stats.received_rej_frames++;
                    // TODO: Retransmit only after receiving the full control packet
                    control_byte = byte;
                    retransmission_counter++;
                    if(retransmission_counter > num_tries)
                        return -1;

                    res = write(fd,frame,frame_size);
                    stats.transmitted_i_frames++;
                    #if DEBUG
                    printf("            [%d] Retransmitting %d bytes of data\n",state, frame_size - 6);
                    #endif

                    state = 1;
                } else {
                    state = 1;
                }
            break;
            case 4:
                #if DEBUG
                printf("            [%d] received %02x and expected %02x\n",state,byte,address_byte^control_byte);
                #endif
                if(byte == (address_byte^control_byte))
                    state = 5;
                else if(byte == FLAG)
                    state = 2;
                else
                    state = 1;
            break;
            case 5:
                if(byte == FLAG) {
                    s = !s; // change parity
                    state = 0;
                }
                else
                    state = 1;
            break;
        }
    }

    stats.transmitted_bytes += frame_size - 2;
    end = time(0);
    stats.total_time += end - start;
    if(end - start > stats.slowest_frame)
        stats.slowest_frame = end - start;
    if(end - start < stats.fastest_frame)
        stats.fastest_frame = end - start;
    return 1;
};

// Receive data in packet
int llread(unsigned char* packet) {
    start = time(0);
    #if DEBUG
    printf("[linklayer] llread() reading socket data\n");
    #endif
    int res;
    size_t frame_size = 0;
    unsigned char frame[FRAME_MAX_SIZE];

    unsigned char byte = 0, address_byte = 0, control_byte = 0;
    int state = 1;
    while(state) {
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
                if( ((s == 0) && (byte == I_0)) || ((s == 1) && (byte == I_1)) ) {
                    control_byte = byte;
                    state = 4;
                }
                else if(byte == FLAG)
                    state = 2;
                else
                    state = 1;
            break;
            case 4:
                if(byte == FLAG) {
                    state = 2;
                    break;
                }

                #if DEBUG
                printf("            [%d] received %02x and expected %02x\n",state,byte,address_byte^control_byte);
                #endif

                if(byte != (address_byte^control_byte)) {
                    state = 1;
                    break;
                }

                #if DEBUG
                bcc2_tracker = 0;
                printf("            [%d] reading frame\n",state);
                #endif
                for(frame_size = 0; frame_size < FRAME_MAX_SIZE; frame_size++) {
                    read(fd,&frame[frame_size],1);
                    #if RANDOM_ERROR_GENERATION
                    if(rand() % 200 == 0) {
                        frame[frame_size] ^ 0x01; // Jam the first bit
                    }
                    #endif
                    if(frame[frame_size] == ESC) { // byte destuffing (note that this also destuff bcc2)
                        stats.escaped_bytes++;
                        #if DEBUG
                        printf("ESCAPE ");
                        #endif
                        res = read(fd,&frame[frame_size],1);
                        frame[frame_size] ^= ESC_XOR;
                    } else if(frame[frame_size] == FLAG) { // end-of-frame
                        #if DEBUG
                        printf("%02x \n",FLAG);
                        #endif
                        break;
                    }

                    #if DEBUG
                    bcc2_tracker ^= frame[frame_size];
                    printf("%02x(%02x) ",frame[frame_size],bcc2_tracker);
                    if((frame_size - 4) % 16 == 0)
                        printf("\n");
                    #endif
                }
                #if DEBUG
                printf("\n            [%d] finished reading frame\n",state);
                #endif
                stats.received_i_frames++;
                unsigned char bcc2_local = 0;
                for(int i = 0; i < frame_size - 1; i++) {
                    bcc2_local ^= frame[i];
                }
                #if DEBUG
                printf("            [%d] received %02x and expected %02x\n",state,bcc2_local, frame[frame_size - 1]);
                #endif

                if(frame[frame_size - 1] == bcc2_local) {
                    control_byte = s ? RR_1 : RR_0;
                    s = !s; // change parity
                    for(int i = 0; i < frame_size - 1; i++)
                        packet[i] = frame[i];
                    state = 0;
                } else {
                    stats.transmitted_rej_frames++;
                    control_byte = s ? REJ_1 : REJ_0;
                    state = 1;
                }

                #if DEBUG
                    printf("            [%d] parity %d\n",state,s);
                #endif
                send_cframe(address_byte,control_byte);
            break;
        }
    }

    stats.received_bytes += frame_size - 1;
    end = time(0);
    stats.total_time += end - start;
    if(end - start > stats.slowest_frame)
        stats.slowest_frame = end - start;
    if(end - start < stats.fastest_frame)
        stats.fastest_frame = end - start;
    return frame_size - 1;
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

    int retransmission_counter = 0, state = 1;
    unsigned char byte = 0, address_byte = 0, control_byte = 0;
    while(state) {
        if(read_timeout(&byte) < 0) {
            retransmission_counter++;
            if(retransmission_counter > num_tries)
                return -1;
            if(connectionParameters.role == 0)
                send_cframe(A_TX,DISC);
            state = 1;
        }
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
                if(byte == (address_byte^control_byte)) {
                    #if DEBUG
                    printf("            [%d] received %02x and expected %02x\n",state,byte,address_byte^control_byte);
                    #endif
                    state = 5;
                }
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
                    send_cframe(address_byte,control_byte);
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

    if(stats.received_i_frames)
        stats.average_frame_time = stats.total_time / (stats.received_i_frames);
    if(showStatistics) {    
        printf("[linklayer] llclose() Statistics\n");
        printf("Baudrate:%d\n",connectionParameters.baudRate);
        
        printf("            bytes received: %d\n", stats.received_bytes);
        printf("            bytes sent: %d\n", stats.transmitted_bytes);
        printf("            bytes escaped: %d\n", stats.escaped_bytes);

        printf("            trasmitted frames: %d\n", stats.transmitted_i_frames);
        printf("            trasmitted rejection frames : %d\n", stats.transmitted_rej_frames);
        
        printf("            received frames: %d\n", stats.received_i_frames);
        printf("            received rejection frames : %d\n", stats.received_rej_frames);
        
        
        printf("            Total Time : %ld\n", stats.total_time);
        printf("            fastest received frame : %ld\n", stats.fastest_frame);
        printf("            slowest received frame : %ld\n", stats.slowest_frame);
        printf("            average time for received frames : %ld\n", stats.average_frame_time);

    }
    return 1;
};
