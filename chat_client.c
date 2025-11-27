#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <ncurses.h> 
#include "udp.h"

// structure to pass multiple arguments to threads
typedef struct{
    int sd;
    struct sockaddr_in server_addr;
} thread_args_t;

// global Ncurses Windows and synchronization lock
WINDOW *chat_win;
WINDOW *input_win;
pthread_mutex_t ui_lock = PTHREAD_MUTEX_INITIALIZER; 

// Initialize Ncurses and set up the two windows
void setup_ncurses(){
    initscr();           
    cbreak();           
    // We *must* enable echo here so the user can see their typing.
    // The previous design was fighting Ncurses's default input handling.
    echo();            
    curs_set(1);         
    
    int chat_height = LINES - 3; 
    
    chat_win = newwin(chat_height, COLS, 0, 0); 
    input_win = newwin(3, COLS, chat_height, 0); 

    scrollok(chat_win, TRUE); 

    box(input_win, 0, 0);
    mvwprintw(input_win, 1, 1, "> "); // Prompt moved slightly left for input space
    
    refresh();
    wrefresh(chat_win);
    wrefresh(input_win);
    wmove(input_win, 1, 3); // Move cursor to the starting position
}

// Restore terminal
void cleanup_ncurses() {
    endwin(); 
}

// Utility function to print messages to the chat window safely
void print_chat_message(const char* msg){
    pthread_mutex_lock(&ui_lock);
    
    // 1. Save cursor position in the input window
    int cur_y, cur_x;
    getyx(input_win, cur_y, cur_x);
    
    // 2. Clear the input line (not the whole window)
    wmove(input_win, 1, 3);
    wclrtoeol(input_win);

    // 3. Print message to chat window
    wprintw(chat_win, "%s\n", msg);
    wrefresh(chat_win);
    
    // 4. Redraw input prompt and box
    box(input_win, 0, 0);
    mvwprintw(input_win, 1, 1, "> "); 
    
    // 5. Restore the cursor
    wmove(input_win, cur_y, cur_x);
    wrefresh(input_win);
    
    pthread_mutex_unlock(&ui_lock);
}

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
            print_chat_message(buffer);
        }
    }
    return NULL;
}

// sender thread function -> loops to read user input, sends raw string to the server, detects disconn to disconnect
void* sender_thread(void* arg){
    thread_args_t* args = (thread_args_t*) arg;
    char input[BUFFER_SIZE];

    while(1){
        pthread_mutex_lock(&ui_lock);
        
        // Move the cursor back to the start of the input area (column 3, row 1)
        wmove(input_win, 1, 3); 
        wrefresh(input_win); 

        pthread_mutex_unlock(&ui_lock);
        
        // Read input - this is a blocking call, waiting for Enter
        int rc_input = wgetnstr(input_win, input, BUFFER_SIZE - 1);
        
        // Acquire lock immediately after input read completes
        pthread_mutex_lock(&ui_lock);

        // Clear the line after Enter is pressed (since echo() is now on)
        wmove(input_win, 1, 3);
        wclrtoeol(input_win);
        box(input_win, 0, 0);
        mvwprintw(input_win, 1, 1, "> ");
        wrefresh(input_win);
        
        pthread_mutex_unlock(&ui_lock);
        
        if (rc_input != ERR && strlen(input) > 0){
                
            // Check for disconnect command
            if(strncmp(input, "disconn", 7) == 0){
                udp_socket_write(args->sd, &args->server_addr, "disconn$", 9);
                print_chat_message("Disconnecting...");
                usleep(500000); 
                cleanup_ncurses();
                exit(0);
            }
            
            // Check if the input is already a command
            if (strchr(input, '$') != NULL){
                udp_socket_write(args->sd, &args->server_addr, input, strlen(input)+1);
            } else{
                 // Default to chat message: prepend "say$"
                 char full_message[BUFFER_SIZE + 5]; 
                 snprintf(full_message, sizeof(full_message), "say$%s", input);
                 udp_socket_write(args->sd, &args->server_addr, full_message, strlen(full_message)+1);
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
    
    setup_ncurses();
    print_chat_message("Client loaded. Type 'conn$<name>' to connect to the server.");

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