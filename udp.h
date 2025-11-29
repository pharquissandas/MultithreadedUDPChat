// utilitues for UDP socket operations
#ifndef UDP_H
#define UDP_H

// system headers for socket programming
#include <sys/socket.h>
#include <netinet/in.h>

// configuration constants
#define BUFFER_SIZE 1024    // size of UDP message buffer
#define SERVER_PORT 12000   // default server port
#define ADMIN_PORT 6666     // admin port

// Function declarations
// sets up a sockaddr_in structure with given IP and port
int set_socket_addr(struct sockaddr_in *addr, const char *ip, int port);

// opens a UDP socket on the specified port, returns socket descriptor or -1 on error
int udp_socket_open(int port);

// reads a UDP message from the socket, fills addr with sender's address, returns number of bytes read
int udp_socket_read(int sd, struct sockaddr_in *addr, char *buffer, int n);

// writes a UDP message to the specified address, returns number of bytes sent
int udp_socket_write(int sd, struct sockaddr_in *addr, char *buffer, int n);

#endif // UDP_H