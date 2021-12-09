#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <sys/types.h>
#include <signal.h>

#define MAX_CLIENTS 100
#define BUFFER_SZ 2048
#define NAME_LEN 32

static _Atomic unsigned int cli_count = 0;
static int uid =10;

typedef struct{
    struct sockaddr_in address;
    int sockfd; //socket file descriptor
    int uid; //user id
    char name[NAME_LEN]; //name
} client_t;

client_t *clients[MAX_CLIENTS];

pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER; //required to send the messages

void str_overwrite_stdout(){
    printf("\r%s", "> ");
    fflush(stdout);
}

void str_trim_lf(char* arr, int length){
    for(int i=0; i<length; i++){
        if(arr[i] == '\n'){
            arr[i]='\0';
            break;
        }
    }
}

void queue_add(client_t *cl){
    pthread_mutex_lock(&clients_mutex);
    for(int i=0; i<MAX_CLIENTS; i++){
        if(!clients[i]){
            clients[i] =cl;
            break;
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

void queue_remove(int uid){
    pthread_mutex_lock(&clients_mutex);

    for(int i=0; i<MAX_CLIENTS; i++){
        if(clients[i]){
            if(clients[i]->uid == uid){
                clients[i] = NULL;
                break;
            }
        }
    }

    pthread_mutex_unlock(&clients_mutex);
}

void print_ip_addr(struct sockaddr_in addr){
    printf("%d.%d.%d.%d",
            addr.sin_addr.s_addr & 0xff,
            (addr.sin_addr.s_addr & 0xff00) >> 8,
            (addr.sin_addr.s_addr & 0xff0000) >>16,
            (addr.sin_addr.s_addr & 0xff000000) >> 24);
}

void send_message(char *s, int uid){
    pthread_mutex_lock(&clients_mutex);

    for(int i=0; i<MAX_CLIENTS; i++){
        if(clients[i]){
            if(clients[i]->uid != uid){
                if(write(clients[i]->sockfd, s, strlen(s)) < 0){
                    printf("ERROR: Write to descriptor failed\n");
                    break;
                }
            }
        }
    }

    pthread_mutex_unlock(&clients_mutex);
}

void *handle_client(void *arg){
    char buffer[BUFFER_SZ];
    char name[NAME_LEN];
    int leave_flag = 0;
    cli_count++;

    client_t *cli = (client_t*)arg;

//RECIEVE NAME FROM CLIENT
    if(recv(cli->sockfd, name, NAME_LEN, 0) <= 0  || strlen(name) < 2 || strlen(name) >= NAME_LEN -1){
        printf("Enter the correct name\n");
        leave_flag = 1;
    } else{
        strcpy(cli->name, name);
        sprintf(buffer, "%s has joined\n", cli->name);
        printf("%s", buffer);
        //send_message(buffer, cli->uid);
    }
    bzero(buffer, BUFFER_SZ);
    while(1){
        if(leave_flag){
            break;
        }
        int recieve = recv(cli->sockfd, buffer, BUFFER_SZ, 0);
        if(recieve > 0){
            if(strlen(buffer) > 0){
                send_message(buffer, cli->uid);
                str_trim_lf(buffer, strlen(buffer));
                printf("%s -> %s", buffer, cli->name);
            }
        } else if(recieve == 0 || strcmp(buffer, "exit") == 0){
            sprintf(buffer, "%s has left\n", cli->name);
            printf("%s", buffer);
            //send_message(buffer, cli->uid);
            leave_flag = 1;
        } else{
            printf("ERROR: -1\n");
            leave_flag = 1;
        }
        bzero(buffer, BUFFER_SZ);
    }
    close(cli->sockfd);
    queue_remove(cli->uid);
    free(cli);
    cli_count--;
    pthread_detach(pthread_self());

    return NULL;
}

int main(int argc, char **argv){
    if(argc !=2){
        printf("Usage: %s <port>\n", argv[0]);
        return EXIT_FAILURE;
    }
    char *ip = "127.0.0.1";
    int port = atoi(argv[1]);

    int option =1;
    int listenffd =0, connfd =0;
    struct sockaddr_in serv_addr;
    struct sockaddr_in client_addr;
    pthread_t tid;

//SOCKET SETTINGS
    listenffd = socket(AF_INET, SOCK_STREAM, 0);
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = inet_addr(ip);
    serv_addr.sin_port = htons(port);

//SIGNALS
    signal(SIGPIPE, SIG_IGN);
    
    if(setsockopt(listenffd, SOL_SOCKET, (SO_REUSEPORT | SO_REUSEADDR), (char*)&option, sizeof(option)) <0){
        printf("ERROR: setsockopt\n");
        return EXIT_FAILURE;
    }

//BIND
    if(bind(listenffd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0){
        printf("ERROR: bind\n");
        return EXIT_FAILURE;
    }

//LISTEN
    if(listen(listenffd, 10) < 0){
        printf("ERROR: listen\n");
        return EXIT_FAILURE;
    }

    printf("WELCOME TO THE SERVER\n");

    while(1){
        socklen_t clilen = sizeof(client_addr);
        connfd = accept(listenffd, (struct sockaddr*)&client_addr, &clilen);
    

//CHECK # OF CLIENTS
    if((cli_count +1) == MAX_CLIENTS){
        printf("Maximum clients is connected");
        print_ip_addr(client_addr);
        close(connfd);
        continue;
    }
//CLIENT SETTINGS
    client_t *cli = (client_t *)malloc(sizeof(client_t));
    cli->address = client_addr;
    cli->sockfd = connfd;
    cli->uid = uid++;

//ADD CLIENT TO THE QUEUE
    queue_add(cli);
    pthread_create(&tid, NULL, &handle_client, (void*)cli);

//CPU USAGE
    sleep(1);
}

    return EXIT_SUCCESS;
}
