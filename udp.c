#include <sys/types.h>       // for data types
#include <sys/socket.h>      // for socket functions
#include <netinet/in.h>      // for sockaddr_in
#include <arpa/inet.h> 	     // for inet_pton
#include <unistd.h> 	     // for close
#include <string.h> 	     // for memset
#include <stdio.h>           // for perror

#include "udp.h"             // for udp socket function declarations

// utility: set up sockaddr_in structure with given IP and port
int set_socket_addr(struct sockaddr_in *addr, const char *ip, int port){
    memset(addr, 0, sizeof(*addr));
    addr->sin_family = AF_INET;
    addr->sin_port = htons(port);

    // If ip is NULL, bind to all interfaces
    if (ip == NULL){ 
        addr->sin_addr.s_addr = INADDR_ANY;
    }
    else{
        if(inet_pton(AF_INET, ip, &addr->sin_addr) <= 0){
            return -1;
        }
    }
    return 0;
}

// open UDP socket on given port, return socket descriptor
int udp_socket_open(int port){
    int sd = socket(AF_INET, SOCK_DGRAM, 0);
    if(sd < 0){
        perror("Error creating socket");
        return -1;
    }

    // Set socket option to allow address reuse
    int optval = 1;
    if (setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0) {
        perror("Error setting SO_REUSEADDR");
    }

    // Bind socket to the specified port
    struct sockaddr_in this_addr;
    set_socket_addr(&this_addr, NULL, port);

    // Only bind if port is non-zero
    if(port != 0){ 
        if(bind(sd, (struct sockaddr *)&this_addr, sizeof(this_addr)) < 0){
            perror("Error binding socket");
            close(sd);
            return -1;
        }
    }

    return sd;
}

// read data from UDP socket, fill in client address, return number of bytes read
int udp_socket_read(int sd, struct sockaddr_in *addr, char *buffer, int n){
    socklen_t len = sizeof(struct sockaddr_in);
    return recvfrom(sd, buffer, n, 0, (struct sockaddr *)addr, &len);
}

// write data to UDP socket, send to given client address, return number of bytes sent
int udp_socket_write(int sd, struct sockaddr_in *addr, char *buffer, int n){
    int addr_len = sizeof(struct sockaddr_in);
    return sendto(sd, buffer, n, 0, (struct sockaddr *)addr, addr_len);
}