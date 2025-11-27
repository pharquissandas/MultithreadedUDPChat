#ifndef UDP_H
#define UDP_H

#include <sys/socket.h>
#include <netinet/in.h>

#define BUFFER_SIZE 1024
#define SERVER_PORT 12000
#define ADMIN_PORT 6666

// Function declarations
int set_socket_addr(struct sockaddr_in *addr, const char *ip, int port);
int udp_socket_open(int port);
int udp_socket_read(int sd, struct sockaddr_in *addr, char *buffer, int n);
int udp_socket_write(int sd, struct sockaddr_in *addr, char *buffer, int n);

#endif // UDP_H