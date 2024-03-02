#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define MESSAGE_SIZE 1024

struct packet {
    unsigned int total_frag;
    unsigned int frag_no;
    unsigned int size;
    char* filename;
    char filedata[1000];
};

int main(int argc, char* argv[]){

    /**** CHECK FOR INPUT VALIDATION ****/

    // input should be udp listen port, so only 1 argument
    if (argc != 1){
        // ensure only 2 arguments are passed
        if (argc != 2){
            printf("Error: Please enter only 2 arguments");
            exit(1);
        }

        /**** SETUP SERVER AND CLIENT ADDRESSES ****/

        // setup our server address
        struct sockaddr_in serverSockAddrIn;
        // fill the entire struct with zero to get rid of garbage values
        memset(&serverSockAddrIn, 0, sizeof(serverSockAddrIn));
        // initialize the required values of the server address
        serverSockAddrIn.sin_family = AF_INET;
        serverSockAddrIn.sin_addr.s_addr = INADDR_ANY;
        // set the port number to the input port number to the one provided in the input
        serverSockAddrIn.sin_port = htons(atoi(argv[1]));

        // create a new socket using socket(), returning socket fd
        int udpSocket = socket(AF_INET, SOCK_DGRAM, 0);
        // associate address with the socket via bind()
        bind(udpSocket, (struct sockaddr*) &serverSockAddrIn, sizeof(serverSockAddrIn));

        printf("Server started at port %s...\n", argv[1]);

        // declare our client address
        struct sockaddr_in clientSockAddrIn;
        socklen_t clientAddressSize = sizeof(clientSockAddrIn);
        memset(&clientSockAddrIn, 0, sizeof(clientSockAddrIn));

        /**** SETUP CONNECTION ****/

        // create buffer to store incoming message
        char message[MESSAGE_SIZE];
        // create file to store the file coming from the client
        FILE *file = NULL;

        while(1){

            // get initial connection handshake
            ssize_t message_length = recvfrom(udpSocket, (char*)message, MESSAGE_SIZE, 0,
                                              (struct sockaddr*)&clientSockAddrIn, &clientAddressSize);

            if (message_length < 0){
                printf("Failed to receive message.\n");
                exit(1);
            }

            // null terminate the string so we can print via printf() easily
            message[message_length] = '\0';

            printf("Received message: %s\n", message);

            // setup the response message
            if (strcmp(message, "ftp") == 0){
                strcpy(message, "yes");
            }else{
                strcpy(message, "no");
                continue;
            }

            printf("Sending message: %s\n", message);

            // send the response to the client
            if (sendto(udpSocket, message, strlen(message), 0, (struct sockaddr*)&clientSockAddrIn,
                       clientAddressSize)< 0) {
                printf("Failed to send message.\n");
                exit(1);
            }

            /**** TRANSFER THE FILE ****/

            int done = 0;

            while (done == 0){
                // receive incoming packet via UDP connection
                ssize_t message_length = recvfrom(udpSocket, message, MESSAGE_SIZE, 0,
                                                  (struct sockaddr*)&clientSockAddrIn, &clientAddressSize);

                if (message_length < 0){
                    printf("Failed to receive message.\n");
                    continue;
                }

                struct packet incomingPacket;

                // allocate space for the incoming packet filename
                incomingPacket.filename = malloc(256);
                if (!(incomingPacket.filename)){
                    printf("Malloc for incoming packet filename failed");
                    continue;
                }

                // store all data that is a regular string in our incomingPacket variable
                // store the header data
                sscanf(message, "%u:%u:%u:%255[^:]:", &incomingPacket.total_frag, &incomingPacket.frag_no,
                       &incomingPacket.size, incomingPacket.filename);

                // calculate the header size using snprintf()
                char tempString[MESSAGE_SIZE];
                int header_size = snprintf(tempString, sizeof(tempString), "%u:%u:%u:%s:", incomingPacket.total_frag,
                                           incomingPacket.frag_no, incomingPacket.size, incomingPacket.filename);

                // copy the filedata to the incomingPacket
                memcpy(incomingPacket.filedata, message + header_size, incomingPacket.size);

                //if the packet is received, open file stream
                // we keep the file stream open starting from when first fragment comes until last fragment comes
                // only open the file stream once
                if (incomingPacket.frag_no == 1){
                    if(file){ // if the file pointer exists, then close it
                        fclose(file);
                    }
                    file = fopen(incomingPacket.filename, "wb");
                    if (!file){ // if the file pointer is still null
                        printf("Opening the file failed");
                        free(incomingPacket.filename);
                        continue;
                    }
                }

                // write the data to the local machine
                size_t bytes_written = fwrite(incomingPacket.filedata, 1, incomingPacket.size, file);
                if (bytes_written != incomingPacket.size){
                    printf("Unable to write to local machine");
                    continue;
                }

                printf("Recieved fragment %u of %u\n", incomingPacket.frag_no, incomingPacket.total_frag);

                // if all fragments are received, only then close the file stream
                if (incomingPacket.frag_no == incomingPacket.total_frag){
                    done = 1;
                    fclose(file);
                    file = NULL;
                }

                /**** UPDATE THE CLIENT ****/

                char response[] = "ACK";

                // send the response to the client
                if (sendto(udpSocket, response, strlen(response), 0, (struct sockaddr*)&clientSockAddrIn,
                           clientAddressSize) < 0) {
                    printf("Failed to send ACK response.\n");
                    continue;
                }

                // free the char * we malloced earlier
                free(incomingPacket.filename);
            }
        }


        // close the file descriptor after it has been used
        close(udpSocket);

        return 0;
    }
}