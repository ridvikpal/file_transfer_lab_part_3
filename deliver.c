#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>

#define BUFFER_SIZE 1024
#define MAX_PACKET_SIZE 1000

struct packet {
    unsigned int total_frag;
    unsigned int frag_no;
    unsigned int size;
    char* filename;
    char filedata[1000];
};


int main(int argc,char *argv[])
{
    //Check to see if there are the correct number of arguments
    if (argc != 3){
        printf ("Error\n");
        return -1;
    }
    char *str = NULL;
    str = (char *) malloc(BUFFER_SIZE);
    printf ("Please input a message as follows: ftp <file name>\n");
    fgets(str, BUFFER_SIZE, stdin);

    if (strncmp (str, "ftp ", 4) != 0){
        printf ("Command must start with \"ftp\" \n");
        return -1;
    }
    char *file_name = str + 4;
    size_t len = strlen(file_name);

    // Remove trailing spaces and newline characters
    while (len > 0 && (file_name[len - 1] == ' ' || file_name[len - 1] == '\t' || file_name[len - 1] == '\n')) {
        len--;
    }

    // Set the null terminator at the last non-whitespace character
    file_name[len] = '\0';
    printf ("%s\n", file_name);

    if (access(file_name, F_OK) != 0) {
        // file doesn't exists
        printf("File with name: \"%s\" does not exist\n", file_name);
        return -1;
    }

    //Fill in server information
    int port = atoi(argv[2]);
    if (port == 0 && strcmp(argv[2], "0") !=0){
        //Failed string to int conversion
        printf("Invalid port number: %s\n", argv[2]);
        return -1;
    }

    struct sockaddr_in server_address;
    memset(&server_address, 0, sizeof (server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(port);
    server_address.sin_addr.s_addr = inet_addr(argv[1]);


    //Make the socket
    int sockfd;
    if ((sockfd = socket(PF_INET, SOCK_DGRAM, 0))<0){
        printf ("Error making the socket\n");
        return -1;
    }
    clock_t start_time = clock();

    //Send "ftp"
    if (sendto(sockfd, "ftp", strlen("ftp"), MSG_CONFIRM, (struct sockaddr *) &server_address, sizeof (server_address))<0){
        printf ("Sending ftp failed\n");
        close(sockfd);
        return -1;
    }
    printf ("ftp sent to the server\n");

    //Receive message from the server
    ssize_t msg_len;
    socklen_t client_address_size = sizeof(server_address);
    char buffer [BUFFER_SIZE];
    msg_len = recvfrom(sockfd, (char*) buffer, BUFFER_SIZE, 0, (struct sockaddr*) &server_address, &client_address_size);


    if (msg_len == -1){
        printf("recvfrom failed");
        close(sockfd);
        return -1;
    }

    clock_t end_time = clock();
    double rtt = ((double)(end_time - start_time))/ CLOCKS_PER_SEC;
    printf("Round Trip Time (RTT): %fs\n", rtt);

    buffer[msg_len] = '\0';

    if (strcmp(buffer, "yes") == 0){
        printf ("A file transfer can start.\n");
    }else {
        printf ("Exiting");
        return -1;
    }

    //Try to open the file
    FILE *file = fopen (file_name, "rb");
    if (!file){
        printf("Error opening file with name: %s\n", file_name);
        return -1;
    }

    //Find the size of the file
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    //Find total number of fragments
    int total_fragment = ((int)file_size / MAX_PACKET_SIZE) + 1;

    struct timeval timeout;
    timeout.tv_sec = 2; //2 second timeout
    timeout.tv_usec = 0;

    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0){
        printf ("Error in setsockopt\n");
        return -1;
    }
    size_t bytes_read;

    //Create the packet
    struct packet outgoing_packet;
    outgoing_packet.total_frag = total_fragment;
    outgoing_packet.frag_no = 1;

    while ((bytes_read = fread(outgoing_packet.filedata, 1, MAX_PACKET_SIZE, file)) > 0) {
        int ack = 0;

        outgoing_packet.filename = malloc(strlen(file_name)+1);
        if (!outgoing_packet.filename){
            printf("Error in allocating memory in packet\n");
            return -1;
        }
        strcpy(outgoing_packet.filename, file_name);
        outgoing_packet.size = bytes_read;
        int header_size = sprintf(buffer, "%u:%u:%u:%s:", outgoing_packet.total_frag, outgoing_packet.frag_no, outgoing_packet.size,
                              outgoing_packet.filename);
        memcpy(buffer + header_size, outgoing_packet.filedata, outgoing_packet.size);

        while (!ack){
            if (sendto(sockfd, buffer, BUFFER_SIZE, MSG_CONFIRM, (struct sockaddr *) &server_address, sizeof (server_address))<0){
                printf ("Sending packet failed\n");
                fclose(file);
                close(sockfd);
                return -1;
            }
            msg_len = recvfrom(sockfd, (char*) buffer, BUFFER_SIZE, 0, (struct sockaddr*) &server_address, &client_address_size);
            if (msg_len == -1){
                printf("recvfrom failed when trying to ACK\n");
                close(sockfd);
                return -1;
            }
            if (msg_len > 0 && strncmp(buffer, "ACK", 3) == 0){
                ack = 1;
                printf("ACK received for fragment number %u\n", outgoing_packet.frag_no);
            }else {
                printf ("Waiting for ACK for fragment number %u\n", outgoing_packet.frag_no);
            }
        }
        free(outgoing_packet.filename);
        outgoing_packet.frag_no++;
    }
    printf ("Successfully sent file: %s\n", file_name);
    fclose(file);
    free(str);
    close (sockfd);
    return 0;
}