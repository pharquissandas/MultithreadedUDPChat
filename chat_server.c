#define _XOPEN_SOURCE 700    // for pthread_rwlock
#include <stdio.h>           // for printf
#include <stdlib.h>          // for malloc, free
#include <string.h>          // for strlen, strcpy, strcmp
#include <pthread.h>         // for pthreads
#include <unistd.h>          // for close, sleep
#include "udp.h"             // for UDP socket functions

#define MAX_NAME_LEN 50      // maximum length of client name
#define MAX_MUTED 50         // maximum number of muted clients per client

// client linked list node structure
typedef struct client_node{
    struct sockaddr_in addr; // client IP and port
    char name[MAX_NAME_LEN]; // client chat name
    char muted_clients[MAX_MUTED][MAX_NAME_LEN]; // list of muted client names
    int muted_count;
    time_t last_active_time;
    int ping; // 0 = no ping sent, 1 = ping sent
    struct client_node* next;
} client_node_t;

// head of the client linked list --> points to first client node
client_node_t* client_list_head = NULL;

// reader-writer lock for the client list --> allows only one thread to write at a time
pthread_rwlock_t client_list_lock = PTHREAD_RWLOCK_INITIALIZER;

char history[15][BUFFER_SIZE]; // for storing last 15 messages
int history_count = 0;
int history_start = 0; // index of the oldest message

// function declarations
void store_in_history(const char* msg);
void remove_client(struct sockaddr_in addr);
client_node_t* find_client_by_name(const char* name);
client_node_t* find_client_by_addr(struct sockaddr_in addr);

// utility: add a new client --> locks list for writing, creates new node and adds to head of list, unlocks
void add_client(struct sockaddr_in addr, const char* name){
    pthread_rwlock_wrlock(&client_list_lock);
    client_node_t* new_client = malloc(sizeof(client_node_t));
    new_client->addr = addr;
    strncpy(new_client->name, name, MAX_NAME_LEN-1);
    new_client->name[MAX_NAME_LEN - 1] = '\0';
    new_client->muted_count = 0;
    new_client->next = client_list_head;
    new_client->last_active_time = time(NULL);
    new_client->ping = 0;
    client_list_head = new_client;
    pthread_rwlock_unlock(&client_list_lock);
}

// utility: remove a client --> locks list for writing, searches for client by address, removes node, unlocks
void remove_client(struct sockaddr_in addr){
    pthread_rwlock_wrlock(&client_list_lock);

    client_node_t* prev = NULL;
    client_node_t* curr = client_list_head;

    while(curr){
        if(curr->addr.sin_port==addr.sin_port && curr->addr.sin_addr.s_addr==addr.sin_addr.s_addr){
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

// finding the client by name
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

// finding the client by address
client_node_t* find_client_by_addr(struct sockaddr_in addr){
    pthread_rwlock_rdlock(&client_list_lock);
    client_node_t* curr = client_list_head;
    while(curr){
        if(curr->addr.sin_port == addr.sin_port && curr->addr.sin_addr.s_addr == addr.sin_addr.s_addr){
            pthread_rwlock_unlock(&client_list_lock);
            return curr;
        }
        curr = curr->next;
    }
    pthread_rwlock_unlock(&client_list_lock);
    return NULL;
}

// broadcast message to all clients except sender and muted clients
void broadcast_message(int sd, const char* msg, struct sockaddr_in* sender_addr) {
    pthread_rwlock_rdlock(&client_list_lock);
    client_node_t* sender_node = NULL;
    if(sender_addr){
        client_node_t* temp = client_list_head;
        // find sender node
        while(temp){
            if(temp->addr.sin_port == sender_addr->sin_port && temp->addr.sin_addr.s_addr == sender_addr->sin_addr.s_addr){
                sender_node = temp;
                break;
            }
            temp = temp->next;
        }
    }

    client_node_t* curr = client_list_head;
    while(curr){
        // Skip sender
        if(sender_addr && curr->addr.sin_port == sender_addr->sin_port && curr->addr.sin_addr.s_addr == sender_addr->sin_addr.s_addr){
            curr = curr->next;
            continue;
        }

        // Skip if sender is muted by current client
        if(sender_node){
            int muted = 0;
            for(int i=0;i<curr->muted_count;i++){
                if(strcmp(curr->muted_clients[i], sender_node->name)==0){
                    muted = 1;
                    break;
                }
            }
            if(muted){
                curr = curr->next;
                continue;
            }
        }
        udp_socket_write(sd, &curr->addr, (char*)msg, strlen(msg)+1);
        curr = curr->next;
    }
    pthread_rwlock_unlock(&client_list_lock);
}

// add client to mute list --> adds to mute list
void mute_client(client_node_t* requester, const char* name){
    pthread_rwlock_wrlock(&client_list_lock);
    if(requester->muted_count<MAX_MUTED){
        strncpy(requester->muted_clients[requester->muted_count], name, MAX_NAME_LEN - 1);
        requester->muted_clients[requester->muted_count][MAX_NAME_LEN - 1] = '\0';
        requester->muted_count++;
    }
    pthread_rwlock_unlock(&client_list_lock);
}

// remove client from mute list --> removes from mute list
void unmute_client(client_node_t* requester, const char* name){
    pthread_rwlock_wrlock(&client_list_lock);
    int idx = -1;
    // find index of muted client
    for(int i=0;i<requester->muted_count;i++){
        if(strcmp(requester->muted_clients[i],name)==0){
            idx = i;
            break;
        }
    }
    // shift remaining muted clients down
    if(idx!=-1){
        for(int i=idx;i<requester->muted_count-1;i++){
            strncpy(requester->muted_clients[i], requester->muted_clients[i+1], MAX_NAME_LEN - 1);
            requester->muted_clients[i][MAX_NAME_LEN - 1] = '\0';
        }
        requester->muted_count--;
    }
    pthread_rwlock_unlock(&client_list_lock);
}

// store message in history --> keeps last 15 messages in circular buffer
void store_in_history(const char* msg){
    pthread_rwlock_wrlock(&client_list_lock);  
    // add message to history
    if(history_count < 15){
        strncpy(history[history_count], msg, BUFFER_SIZE - 1);
        history[history_count][BUFFER_SIZE - 1] = '\0';
        history_count++;
    }
    // circular buffer overwrite
    else{
        strncpy(history[history_start], msg, BUFFER_SIZE - 1); // replace oldest message
        history[history_start][BUFFER_SIZE - 1] = '\0';
        history_start = (history_start + 1) % 15; // increment circular buffer
    }
    pthread_rwlock_unlock(&client_list_lock);
}

// update last active time for client
void update_last_active(struct sockaddr_in addr){
    pthread_rwlock_wrlock(&client_list_lock);
    client_node_t* curr = client_list_head;
    // find client and update time
    while(curr){
        if(curr->addr.sin_port == addr.sin_port && curr->addr.sin_addr.s_addr == addr.sin_addr.s_addr){
            curr->last_active_time = time(NULL);
            curr->ping = 0;
            pthread_rwlock_unlock(&client_list_lock);
            return;
        }
        curr = curr->next;
    }
    pthread_rwlock_unlock(&client_list_lock);
}

// ping clients for inactivity --> sends ping message if inactive for 5 minutes, removes if no response in 10 seconds
void ping_clients(int sd){
    time_t now = time(NULL);
    pthread_rwlock_wrlock(&client_list_lock);

    client_node_t* curr = client_list_head; // head of last active list
    client_node_t* prev = NULL;             // previous node for removal

    // iterate through clients
    while(curr){
        double diff = difftime(now, curr->last_active_time);
        if(diff > 300 && curr->ping == 0){ // 5 minutes timeout
            char ping_msg[] = "PING - please type anything to stay connected";
            udp_socket_write(sd, &curr->addr, ping_msg, strlen(ping_msg)+1);
            curr->ping = 1; // mark ping sent

        }
        else if(diff > 310 && curr->ping == 1){ // 10 seconds after ping sent, no response
            char kick_msg[BUFFER_SIZE];
            snprintf(kick_msg, BUFFER_SIZE, "You have been removed from the chat for innactivity");
            udp_socket_write(sd, &curr->addr, kick_msg, strlen(kick_msg)+1);
            
            char broadcast_msg[BUFFER_SIZE]; // broadcast to all manually as lock is already held
            snprintf(broadcast_msg, BUFFER_SIZE, "[Server]: %s has been removed for inactivity", curr->name);
            
            // add to history manually as lock is already held
            if(history_count < 15){
                strncpy(history[history_count], broadcast_msg, BUFFER_SIZE - 1);
                history[history_count][BUFFER_SIZE - 1] = '\0';
                history_count++;
            }
            else{
                strncpy(history[history_start], broadcast_msg, BUFFER_SIZE - 1); // replace oldest message
                history[history_start][BUFFER_SIZE - 1] = '\0';
                history_start = (history_start + 1) % 15; // increment circular buffer
            }

            client_node_t* client = client_list_head;
            while(client){
                // broadcast to all clients except the one being removed
                if(client != curr){
                    udp_socket_write(sd, &client->addr, broadcast_msg, strlen(broadcast_msg)+1);
                }
                client = client->next;
            }

            // remove from last active list
            client_node_t* temp = curr;
            if(prev){
                prev->next = curr->next;
                curr = curr->next;
            } else {
                client_list_head = curr->next;
                curr = client_list_head;
            }
            free(temp);
            continue; // continue loop with new curr
        }
        prev = curr;
        curr = curr->next;
    }
    pthread_rwlock_unlock(&client_list_lock);
}

// ping thread --> loops forever, sleeps for 5 seconds, calls ping_clients
void* ping_thread(void* arg){
    int sd = *(int*)arg;
    while(1){
        sleep(5); // ping every 10 seconds
        ping_clients(sd);
    }
    return NULL;
}

// handle individual client request --> parses request and performs appropriate action
void* handle_request(void* arg){
    struct sockaddr_in client_addr;
    char* request = ((char**)arg)[0];
    memcpy(&client_addr, ((char**)arg)[1], sizeof(struct sockaddr_in));
        
    int sd = *(int*)((char**)arg)[2]; // main socket address as 3rd argument
    free(((char**)arg)[1]); 
    free(((char**)arg)[2]); 
    free(arg);

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

    client_node_t* client = find_client_by_addr(client_addr);

    if(strcmp(req_type, "conn") == 0){ // connection request
        if (find_client_by_name(req_content)){
            snprintf(response, BUFFER_SIZE, "Error: Name %s is already taken.", req_content);
            udp_socket_write(sd, &client_addr, response, strlen(response)+1);
        } else{
            add_client(client_addr, req_content);
            // broadcast join message
            char join_msg[BUFFER_SIZE];
            snprintf(join_msg, BUFFER_SIZE, "%s has joined the chat", req_content);
            broadcast_message(sd, join_msg, &client_addr);

            // send last 15 messages from history
            pthread_rwlock_rdlock(&client_list_lock);
            for(int i = 0; i < history_count; i++){
                int idx = (history_start + i) % 15;
                udp_socket_write(sd, &client_addr, history[idx], strlen(history[idx])+1);
            }
            snprintf(response, BUFFER_SIZE, "Hi %s, you have successfully connected to the chat", req_content);
            udp_socket_write(sd, &client_addr, response, strlen(response)+1);

            pthread_rwlock_unlock(&client_list_lock);
            // add join message to history
            store_in_history(join_msg);
            update_last_active(client_addr);
        }
    }
    else if(strcmp(req_type, "disconn") == 0){ // disconnect request
        if(client){
            char leave_msg[BUFFER_SIZE];
            snprintf(leave_msg, BUFFER_SIZE, "%s has disconnected", client->name);
            broadcast_message(sd, leave_msg, NULL);
            remove_client(client_addr);
            store_in_history(leave_msg);
        }
    }
    else if(strcmp(req_type, "say") == 0){ // broadcast message request
        if (client) {
            // display sent form user
            char my_msg[BUFFER_SIZE];
            snprintf(my_msg, BUFFER_SIZE, "[Me]: %s", req_content);
            udp_socket_write(sd, &client_addr, my_msg, strlen(my_msg)+1);
            // broadcast to everyone else
            char msg[BUFFER_SIZE];
            snprintf(msg, BUFFER_SIZE, "%s: %s", client->name, req_content);
            broadcast_message(sd, msg, &client_addr);
            // store in history
            store_in_history(msg);
            update_last_active(client_addr);
        }
        else{
            // If they are not connected, send an error and dont broadcast
            snprintf(response, BUFFER_SIZE, "Error: You must connect first (type: conn$<name>)");
            udp_socket_write(sd, &client_addr, response, strlen(response)+1);
        }
    }
    else if(strcmp(req_type, "mute") == 0){ // mute client request
        if(client && find_client_by_name(req_content)){
            mute_client(client, req_content);
            snprintf(response, BUFFER_SIZE, "You are now muting messages from %s", req_content);
        } 
        else{
            snprintf(response, BUFFER_SIZE, "Error: User %s not found to mute", req_content);
        }
        udp_socket_write(sd, &client_addr, response, strlen(response)+1);
        update_last_active(client_addr);
    }
    else if(strcmp(req_type, "unmute") == 0){ // unmute client request
        if(client){
            unmute_client(client, req_content);
            snprintf(response, BUFFER_SIZE, "You are no longer muting messages from %s", req_content);
        }
        else{
            snprintf(response, BUFFER_SIZE, "Error: User %s not found to unmute", req_content);
        }
        udp_socket_write(sd, &client_addr, response, strlen(response)+1);
        update_last_active(client_addr);
    }
    else if(strcmp(req_type, "sayto") == 0){ // private message request
        char* space = strchr(req_content, ' ');
        if(space && client){
            *space = '\0';
            char* target_name = req_content;
            char* private_msg = space + 1;

            // find target client
            client_node_t* target_client = find_client_by_name(target_name);
            if(target_client){
                char send_msg[BUFFER_SIZE];
                char receive_msg[BUFFER_SIZE];
                snprintf(send_msg, BUFFER_SIZE, "[To %s]: %s", target_client->name, private_msg);
                udp_socket_write(sd, &client_addr, send_msg, strlen(send_msg)+1);
                snprintf(receive_msg, BUFFER_SIZE, "[From %s]: %s", client->name, private_msg);
                udp_socket_write(sd, &target_client->addr, receive_msg, strlen(receive_msg)+1);
            }
            else{ 
                snprintf(response, BUFFER_SIZE, "User %s not found", target_name);
                udp_socket_write(sd, &client_addr, response, strlen(response)+1);
            }
        }
        update_last_active(client_addr);
    }
    else if(strcmp(req_type, "rename") == 0){ // rename client request
        if(client){
            if (find_client_by_name(req_content)){
                snprintf(response, BUFFER_SIZE, "Error: Name %s is already taken.", req_content);
            } else{
                pthread_rwlock_wrlock(&client_list_lock);
                strncpy(client->name, req_content, MAX_NAME_LEN - 1);
                client->name[MAX_NAME_LEN - 1] = '\0';
                pthread_rwlock_unlock(&client_list_lock);
                snprintf(response, BUFFER_SIZE, "You are now known as %s", req_content);
            }
            update_last_active(client_addr);
            udp_socket_write(sd, &client_addr, response, strlen(response)+1);
        }
    }
    else if(strcmp(req_type, "kick") == 0){ // kick client request (admin only)
        if(ntohs(client_addr.sin_port) == ADMIN_PORT){
            client_node_t* target_client = find_client_by_name(req_content);
            if(target_client){
                char kick_msg[BUFFER_SIZE];
                snprintf(kick_msg, BUFFER_SIZE, "You have been removed from the chat by an admin");
                udp_socket_write(sd, &target_client->addr, kick_msg, strlen(kick_msg)+1);
                remove_client(target_client->addr);
                char broadcast_kick_msg[BUFFER_SIZE];
                snprintf(broadcast_kick_msg, BUFFER_SIZE, "%s has been removed from the chat", req_content);
                broadcast_message(sd, broadcast_kick_msg, NULL);
                store_in_history(broadcast_kick_msg);
            } 
            else{
                snprintf(response, BUFFER_SIZE, "Error: User %s not found to kick", req_content);
                udp_socket_write(sd, &client_addr, response, strlen(response)+1);
            }
        }
        else{ 
            snprintf(response, BUFFER_SIZE, "You do not have permission to perform this action");
            udp_socket_write(sd, &client_addr, response, strlen(response)+1);
            update_last_active(client_addr);
        }
    }
    else if (strcmp(req_type, "ret-ping") == 0) { // reset timer
        update_last_active(client_addr);
    }

    free(request);
    return NULL;
}

// listener thread --> loops forever, reads incoming messages, spawns handler thread for each request
void* listener_thread(void* arg){
    int sd = *(int*)arg;
    char buffer[BUFFER_SIZE];
    struct sockaddr_in client_addr;

    // infinite loop to listen for incoming messages
    while(1){
        int n = udp_socket_read(sd, &client_addr, buffer, BUFFER_SIZE);
        if(n>0){
            buffer[n] = '\0';

            void** args = malloc(3*sizeof(void*));
            args[0] = strdup(buffer);               // message
            args[1] = malloc(sizeof(struct sockaddr_in));
            memcpy(args[1], &client_addr, sizeof(struct sockaddr_in));

            int* sd_ptr = malloc(sizeof(int)); // pass socket id
            *sd_ptr = sd;
            args[2] = sd_ptr;

            pthread_t req_tid;
            pthread_create(&req_tid, NULL, handle_request, args);
            pthread_detach(req_tid);
        }
    }
    return NULL;
}

// main function --> opens UDP socket, starts listener and ping threads, waits for them to finish
int main(){
    int sd = udp_socket_open(SERVER_PORT);
    if(sd < 0){
        return -1;
    }

    printf("Server listening on port %d...\n", SERVER_PORT);

    // start listener thread
    pthread_t listener_tid;
    pthread_create(&listener_tid, NULL, listener_thread, &sd);

    // start ping thread
    pthread_t ping_tid;
    pthread_create(&ping_tid, NULL, ping_thread, &sd);

    // wait for threads to finish (they won't in this infinite server)
    pthread_join(listener_tid, NULL);
    pthread_join(ping_tid, NULL);

    // close socket
    close(sd);
    return 0;
}