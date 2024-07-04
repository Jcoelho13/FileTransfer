#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <netdb.h>
#include <unistd.h>
#include <string.h>
#include <regex.h>
#include <termios.h>
#include <stdbool.h>

#define MAX_LENGTH 100

struct URL {
    char host[MAX_LENGTH];      // 'netlab1.fe.up.pt'
    char user[MAX_LENGTH];      // 'username'
    char password[MAX_LENGTH];  // 'password'
    char ip[MAX_LENGTH];      // 193.137.29.15
    char path[MAX_LENGTH]; // 'path/to/file
    char file[MAX_LENGTH]; // 'file'
};

#define h_addr h_addr_list[0]	//The first address in h_addr_list.


int main(int argc, char *argv[]) {

    if (argc != 2) {
        printf("Usage: %s <URL>\n", argv[0]);
        return -1;
    }

//----------------------- PARSING ARGUMENT -----------------------------
    struct URL url;
    memset(&url, 0, sizeof(url));
    char *input = argv[1];
    
    if (strncmp(input, "ftp://", 6) == 0) {
        input += 6;
    }

    bool hasCredentials = strchr(input, '@') != NULL;

    // With user and password -- ex: ftp://rcom:rcom@netlab1.fe.up.pt/pipe.txt
    if (hasCredentials) {
        sscanf(input, "%[^:]:%[^@]@%[^/]/%s", url.user, url.password, url.host, url.path);
    } 
    // No credentials/anonymous -- ex: ftp://netlab1.fe.up.pt/pub.txt
    else {
    	strcpy(url.user, "anonymous");
    	strcpy(url.password, "anonymous");
        sscanf(input, "%[^/]/%s", url.host, url.path);
    }

    // Parsing file path
    sscanf(input, "%*[^/]//%*[^/]/%s", url.path);
    strcpy(url.file, strrchr(input, '/') + 1);

    // hostname resolution - hostent holds name, ip address, etc.
    struct hostent *h;
    // gethostbyname: DNS lookup to resolve hostname to IP address
    if ((h = gethostbyname(url.host)) == NULL) {
        printf("Error: gethostbyname()\n");
        return -1;
    }
    // IP address is stored in the h_addr;
    // inet_ntoa converts from 32 bit int to dotted decimal notation; struct in_addr represents an IPv4 address; h->h_addr is a char* to the first address in the array of network addresses
    strcpy(url.ip, inet_ntoa(*((struct in_addr *) h->h_addr)));
    
    printf("IP Address : %s\n", url.ip);
    printf("User : %s\n", url.user);
    printf("Password : %s\n", url.password);
    printf("Host : %s\n", url.host);
    printf("File : %s\n", url.file);
    printf("Path : %s\n", url.path);

    int sockfd;
    struct sockaddr_in server_addr;
    
    // Server address handling
    bzero((char *) &server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET; // IPv4
    server_addr.sin_addr.s_addr = inet_addr(url.ip);    /*32 bit Internet address network byte ordered*/
    server_addr.sin_port = htons(21);             /*server TCP port must be network byte ordered; port 21 by convention*/

    // Open a TCP socket, SOCK_STREAM: socket type stream-oriented (TCP)
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        printf("Error opening socket\n");
        return -1;
    }

    // Connect to the server
    if (connect(sockfd, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0) {
        printf("Error connecting to server\n");
        return -1;
    }

    // Receive response for successful connection
    char buf[MAX_LENGTH];
    int response = responseCode(sockfd, buf);
    if(response != 220) { // 220 Service ready for new user.
        printf("Could not get correct response code\n");
        return -1;
    }


//-------------------------- AUTHENTICATION -----------------------------------------------

    char userField[5+strlen(url.user)+1]; 
    sprintf(userField, "USER %s\n", url.user);

    char passwordField[5+strlen(url.password)+1]; 
    sprintf(passwordField, "PASS %s\n", url.password);

    char log_buf[100];
    
    write(sockfd, userField, strlen(userField)); // writes username to server using socket
    if (responseCode(sockfd, log_buf) != 331) { // 331 User name okay, need password.
        printf("Could not login. '%s' doesn't exist.\n", url.user);
        return -1;
    }

    write(sockfd, passwordField, strlen(passwordField)); // writes password to server using socket
    if (responseCode(sockfd, log_buf) != 230) { // 230 User logged in, proceed.
        printf("Could not login as '%s' - Password is incorrect.\n", url.user);
        return -1;
    }

//------------------- ENTERING PASSIVE MODE -------------------------------------
// waits for the client to establish a connection

    int port;
    char ip[MAX_LENGTH];
    if (pMode(sockfd, ip, &port) != 227) { // 227 Entering Passive Mode (193,137,29,15,7,228).
        printf("Passive mode failed\n");
       return -1;
    }

//-------------------------- CREATING B SOCKET ------------------------------------
// Connects data_sockfd to server IP and port for passive data reception.
    int data_sockfd;
    struct sockaddr_in data_server_addr;

    bzero((char *) &data_server_addr, sizeof(data_server_addr));
    data_server_addr.sin_family = AF_INET;
    data_server_addr.sin_addr.s_addr = inet_addr(ip);
    data_server_addr.sin_port = htons(port);

    if ((data_sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        printf("Error opening data socket\n");
        return -1;
    }

    if (connect(data_sockfd, (struct sockaddr *) &data_server_addr, sizeof(data_server_addr)) < 0) {
        printf("Error connecting to server - data socket\n");
        return -1;
    }

    printf("Connected to socket\n");

//----------------------------- RETRIEVING FILE USING FILE PATH -------------------------------------------------------

    char fileField[5+strlen(url.path)+1];
    char request_buf[100];

    sprintf(fileField, "retr %s\n", url.path);
    write(sockfd, fileField, sizeof(fileField));

    printf("Wrote path to socket\n");

    if(responseCode(sockfd, request_buf)!= 150) { // 150 File status okay; about to open data connection.
        printf("Could not locate file\n");
        return -1;
    }


//---------- OPENING FILE DESCRIPTOR AND TRANSFERING FILE ------------

    FILE *file = fopen(url.file, "wb");

    printf("File Descriptor opened\n");

    if (file == NULL) {
        printf("Error opening file\n");
        return -1;
    }

    char data_buf[MAX_LENGTH];
    int bytes_read;
    while ((bytes_read = read(data_sockfd, data_buf, sizeof(data_buf))) > 0) { // reads from socket up to buffer size or until EOF
        fwrite(data_buf, 1, bytes_read, file); // writes to file byte by byte
    }

//------------------------- CLOSING FD AND TERMINATING ---------------------------

    fclose(file);

    printf("File Descriptor closed\n");

    if(responseCode(sockfd, request_buf) != 226) { // 226 Successfully transferred
        printf("Error downloading file\n");
        return -1;
    }

    char quitField[MAX_LENGTH];

    write(sockfd, "quit\n", 5);
    if (responseCode(sockfd, quitField) != 221) { // 221 Successful Quit
        printf("Error quitting\n");
        return -1;
    }

    if((close(sockfd) || close(data_sockfd)) != 0) {
        printf("Error closing socket\n");
        return -1;
    }

    return 0;

}

//reads socket byte by byte until it finds a \n or reaches buffer end, gets response code with sscanf
int responseCode(const int socket, char* buf) {

    char* backup = buf;
    int response;
    do {

        unsigned char byte = 0;

        while (read(socket, &byte, 1) > 0 && byte != '\n') {
            *buf = byte;
            buf++;
        }

        buf = backup;


    } while (buf[3] == '-');


    sscanf(buf, "%d", &response);

    return response;
}

// sends PASV command to FTP server; extracts IP address and port number from response
int pMode(const int socket, char *ip, int *port) {

    char passiveMode[MAX_LENGTH];
    int ipnetwork;
    int ipsubnet;
    int ipdiv;
    int ipdevice;
    int port1;
    int port2;
    write(socket, "pasv\n", 5);
    if (responseCode(socket, passiveMode) != 227) return -1;

    // buffer = 227 Entering Passive Mode (192,168,109,136,186,218).
    sscanf(passiveMode, "%*[^(](%d,%d,%d,%d,%d,%d)%*[^\n$)]", &ipnetwork, &ipsubnet, &ipdiv, &ipdevice, &port1, &port2);
    *port = port1 * 256 + port2;
    sprintf(ip, "%d.%d.%d.%d", ipnetwork, ipsubnet, ipdiv, ipdevice);

    return 227;
}

/*
ftp://ftp.up.pt/pub/kodi/timestamp.txt

Necessário VPN
ftp://netlab1.fe.up.pt/pub.txt
ftp://rcom:rcom@netlab1.fe.up.pt/pipe.txt
ftp://rcom:rcom@netlab1.fe.up.pt/files/archlinux-2023.03.01-x86_64.iso
ftp://rcom:rcom@netlab1.fe.up.pt/files/crab.mp4
ftp://rcom:rcom@netlab1.fe.up.pt/files/pic1.jpg
ftp://rcom:rcom@netlab1.fe.up.pt/files/pic2.png
*/