#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include "udp.h"

#define SERVER_PORT 12000
#define BUFFER_SIZE 1024
#define MAX_NAME_LEN 50
#define MAX_MUTED 50
#define ADMIN_PORT 6666

// client linked list node
typedef struct client_node{
    struct sockaddr_in addr; // client IP and port
    char name[MAX_NAME_LEN]; // client chat name
    char muted_clients[MAX_MUTED][MAX_NAME_LEN]; // list of muted client names
    int muted_count;
    struct client_node* next;
} client_node_t;

// head of the client linked list --> points to first client node
client_node_t* client_list_head = NULL;

// reader-writer lock for the client list --> allows only one thread to write at a time
pthread_rwlock_t client_list_lock = PTHREAD_RWLOCK_INITIALIZER;

// utility: adding client --> locks the list for writing, creates new node 
// inserts it at head and unlocks list
void add_client(struct sockaddr_in addr, const char* name){
    pthread_rwlock_wrlock(&client_list_lock);

    client_node_t* new_client = malloc(sizeof(client_node_t));
    new_client->addr = addr;
    strncpy(new_client->name, name, MAX_NAME_LEN);
    new_client->muted_count = 0;
    new_client->next = client_list_head;
    client_list_head = new_client;

    pthread_rwlock_unlock(&client_list_lock);
}

// utility: remove a client --> locks list for writing, searches for client using IP and port
// removes the node from the list and frees memory, and unlocks
void remove_client(struct sockaddr_in addr){
    pthread_rwlock_wrlock(&client_list_lock);

    client_node_t* prev = NULL;
    client_node_t* curr = client_list_head;

    while(curr){
        if(curr->addr.sin_port==addr.sin_port & curr->addr.sin_addr.s_addr==addr.sin_addr.s_addr){
            if(prev){
                prev->next = curr->next;
            }
            else{
                client_list_head = curr->next;
            }
            free(curr);
            break;
        }
        prev = curr;
        curr = curr->next;
    }
    pthread_rwlock_unlock(&client_list_lock);
}

// finding the client by name --> searches client by name, uses read lock
// returns pointer to client or NULL
client_node_t* find_client_by_name(const char* name){
    pthread_rwlock_rdlock(&client_list_lock);

    client_node_t* curr = client_list_head;
    while(curr){
        if(strcmp(curr->name,name)==0){
            pthread_rwlock_unlock(&client_list_lock);
            return curr;
        }
        curr = curr->next;
    }

    pthread_rwlock_unlock(&client_list_lock);
    return NULL;
}

// broadcast message to all clients (except muted) --> loops through all clients
// skips sender so they receives their own message, checks if sender is muted by client, if yes, skip sending to that client
// uses read lock 
void broadcast_message(int sd, const char* msg, struct sockaddr_in* sender_addr) {
    pthread_rwlock_rdlock(&client_list_lock);
    client_node_t* curr = client_list_head;
    while(curr){
        // skip sender
        if(sender_addr && curr->addr.sin_port == sender_addr->sin_port && curr->addr.sin_addr.s_addr == sender_addr->sin_addr.s_addr){
            curr = curr->next;
            continue;
        }

        // skip if sender is muted by this client
        if(sender_addr){
            client_node_t* sender = client_list_head;
            while(sender){
                if(sender->addr.sin_port == sender_addr->sin_port && sender->addr.sin_addr.s_addr == sender_addr->sin_addr.s_addr){
                    break;
                }                    
                sender = sender->next;
            }

            if(sender){
                int muted = 0;
                for(int i=0;i<curr->muted_count;i++){
                    if(strcmp(curr->muted_clients[i],sender->name)==0){
                        muted = 1;
                        break;
                    }
                }
                if(muted){
                    curr = curr->next;
                    continue;
                }
            }
        }

        udp_socket_write(sd, &curr->addr, (char*)msg, strlen(msg)+1);
        curr = curr->next;
    }

    pthread_rwlock_unlock(&client_list_lock);
}

// add client to mute list --> add's clients name to requester's mute list
// uses write lock
void mute_client(client_node_t* requester, const char* name){
    pthread_rwlock_wrlock(&client_list_lock);
    if(requester->muted_count<MAX_MUTED){
        strncpy(requester->muted_clients[requester->muted_count++], name, MAX_NAME_LEN);
    }
    pthread_rwlock_unlock(&client_list_lock);
}

// remove client from mute list --> removes from mute list
// uses write lock
void unmute_client(client_node_t* requester, const char* name){
    pthread_rwlock_wrlock(&client_list_lock);
    int idx = -1;
    for(int i=0;i<requester->muted_count;i++){
        if(strcmp(requester->muted_clients[i],name)==0){
            idx = i;
            break;
        }
    }
    if(idx!=-1){
        for(int i=idx;i<requester->muted_count-1;i++){
            strncpy(requester->muted_clients[i], requester->muted_clients[i+1], MAX_NAME_LEN);
        }
        requester->muted_count--;
    }
    pthread_rwlock_unlock(&client_list_lock);
}

// handle a single request --> parses request string, splits string into command and message/target name
// each request has its own detached thread, handles client request independently
void* handle_request(void* arg){
    struct sockaddr_in client_addr;
    char* request = ((char**)arg)[0];
    memcpy(&client_addr, ((char**)arg)[1], sizeof(struct sockaddr_in));
    free(arg); // free the allocated array

    char response[BUFFER_SIZE];

    // parse request type
    char* dollar = strchr(request, '$');
    if(!dollar){
        free(request);
        return NULL;
    }
    *dollar = '\0';
    char* req_type = request;
    char* req_content = dollar + 1;

    if(strcmp(req_type, "conn")==0){
        add_client(client_addr, req_content);
        snprintf(response, BUFFER_SIZE, "Hi %s, you have successfully connected to the chat", req_content);
        udp_socket_write(udp_socket_open(0), &client_addr, response, strlen(response)+1);
    }
    else if(strcmp(req_type, "say")==0){
        client_node_t* sender = find_client_by_name("unknown"); // Attention required: map IP to name
        char msg[BUFFER_SIZE];
        snprintf(msg, BUFFER_SIZE, "%s: %s", sender ? sender->name : "Someone", req_content);
        broadcast_message(udp_socket_open(0), msg, &client_addr);
    }
    // Attention required: implement sayto$, disconn$, mute$, unmute$, rename$, kick$

    free(request);
    return NULL;
}

// listener thread --> loops forever, reading messages from UDP socket
// for each message, copies message and client address into heap memory
// spwans new handle_request thread to process it
void* listener_thread(void* arg){
    int sd = *(int*)arg;
    char buffer[BUFFER_SIZE];
    struct sockaddr_in client_addr;

    while(1){
        int n = udp_socket_read(sd, &client_addr, buffer, BUFFER_SIZE);
        if(n>0){
            buffer[n] = '\0';

            // Prepare arguments for request thread
            void** args = malloc(2*sizeof(void*));
            args[0] = strdup(buffer);               // message
            args[1] = malloc(sizeof(struct sockaddr_in));
            memcpy(args[1], &client_addr, sizeof(struct sockaddr_in));

            pthread_t req_tid;
            pthread_create(&req_tid, NULL, handle_request, args);
            pthread_detach(req_tid);
        }
    }
    return NULL;
}

// main function --> opens UDP socket on port 12000, starts listener thread, waits for listener thread indefinitely
// closes socket when server shuts down
int main() {
    int sd = udp_socket_open(SERVER_PORT);
    if (sd < 0) {
        perror("Failed to open server socket");
        return -1;
    }

    printf("Server listening on port %d...\n", SERVER_PORT);

    pthread_t listener_tid;
    pthread_create(&listener_tid, NULL, listener_thread, &sd);

    pthread_join(listener_tid, NULL);

    close(sd);
    return 0;
}