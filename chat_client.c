#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include "udp.h"


// Essentially this code opens a UDP socket on a dynamic port
// Spawns two threads:
// 1) Sender thread: reads user input and sends it to the server
// 2) Listener thread: receives messages from the server and prints them
// Supports commands like conn$, say$, sayto$, mute$, etc.
// Keeps running until the user disconnects with disconn

#define SERVER_PORT 12000
#define BUFFER_SIZE 1024

// structure to pass multiple arguments to threads
typedef struct{
    int sd;
    struct sockaddr_in server_addr;
} thread_args_t;
    
// listener thread function --> loops to receive messages from the server, prints them immediately without blocking reader
void* listener_thread(void* arg){
    thread_args_t* args = (thread_args_t*) arg;
    char buffer[BUFFER_SIZE];
    struct sockaddr_in responder_addr;

    while(1){
        // wait for incoming messages from the server
        int n = udp_socket_read(args->sd, &responder_addr, buffer, BUFFER_SIZE);
        if(n>0){
            buffer[n] = '\0'; // null-terminate the message
            // print server message without overwriting user input
            printf("\n[Server]: %s\n>", buffer);
            fflush(stdout);
        }
    }
    return NULL;
}

// sender thread function -> loops to read user input, sends raw string to the server, detects disconn to disconnect
void* sender_thread(void* arg){
    thread_args_t* args = (thread_args_t*) arg;
    char input[BUFFER_SIZE];

    while(1){
        printf("> "); // show the prompt
        fflush(stdout);

        // read user input
        if(fgets(input, BUFFER_SIZE, stdin)!=NULL){
            // remove newline character
            input[strcspn(input, "\n")] = '\0';

            // ignore empty input
            if(strlen(input)==0){
                continue;
            }

            // sends the full input string to the server
            int rc = udp_socket_write(args->sd, &args->server_addr, input, strlen(input)+1);
            if(rc<0){
                perror("udp_socket_write failed");
            }

            // if the user wants to disconnect, exit the client
            if(strncmp(input, "disconn$", 8)==0){
                printf("Disconnecting...\n");
                exit(0);
            }
        }
    }
    return NULL;
}

// main function
int main(){
    // open UDP socket with dynamic port (let OS assign a free port)
    int sd = udp_socket_open(0);
    if(sd<0){
        perror("Failed to open socket");
        return -1;
    }

    // initialise server address (local host, port 12000) - sets IP and port of the chat server
    struct sockaddr_in server_addr;
    if(set_socket_addr(&server_addr, "127.0.0.1", SERVER_PORT)<0){
        perror("Invalid server IP");
        return -1;
    }

    // prepare arguments for threads
    thread_args_t args;
    args.sd = sd;
    args.server_addr = server_addr;

    // create listener and sender threads
    pthread_t listener_tid, sender_tid;
    if(pthread_create(&listener_tid, NULL, listener_thread, &args)!=0){
        perror("Failed to create listener thread");
        return -1;
    }

    if(pthread_create(&sender_tid, NULL, sender_thread, &args)!=0){
        perror("Failed to create sender thread");
        return -1;
    }

    // wait for the threads to finish (they run indefinitely)
    pthread_join(listener_tid, NULL);
    pthread_join(sender_tid, NULL);

    // clock socket (rarely reached)
    close(sd);

    return 0;
}