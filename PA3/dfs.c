#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <pthread.h>

#define TYPE_USAGE                  0
#define TYPE_FORMAT                 1
#define TYPE_SOCKET                 2
#define TYPE_LISTEN                 3
#define TYPE_BIND                   4
#define TYPE_CONNECT                5
#define TYPE_CONFIG                 6

#define CMD_GET                     1
#define CMD_LIST                    0
#define CMD_PUT                     2
#define CMD_ERR                     -1

#define MSG_TYPE_OK                 0
#define MSG_TYPE_USRINFO            1
#define MSG_TYPE_FILENOTEXIST       2
#define MSG_TYPE_GET_OK             3
#define MSG_TYPE_PUT_OK             4
#define MSG_TYPE_LIST_OK            5
#define MSG_TYPE_ERROR              6
#define MSG_TYPE_FAKE               7
#define MSG_TYPE_DIR                8

#define MSG_OK                      "OK"
#define MSG_FAILED                  "FAILED"
#define MSG_USRINFO                 "WRONG USER INFORMATION"
#define MSG_FILENOTEXIST            "FILE DOESN'T EXIST"
#define MSG_ERROR                   "SYSTEM ERROR"
#define MSG_FAKE                    "THIS IS FAKE DATA"
#define MSG_DIR                     "NOT ABLE TO CREATE DIRECTORY"

#define MAXREQNUM                   10
#define MAXREADLINE                 1024
#define MAXBUFFER                   1024

typedef struct {
    char username[24];
    char password[24];
} userinfo;

typedef struct {
    userinfo usr_info;
    char cmd[10];
    char* filename;
    int pid;
    long psize;
} filesystem_header;

struct spot {
    char state;
    int fd;
};

const char* cfg_filename = "dfs.conf";
const char* wpath;
int port;

struct spot spots[MAXREQNUM+5];
unsigned init_findworker = 0;


int listenRoutine(int sa_len);
int recvHeader(int fd, filesystem_header* fs_hdr);
void respondHeader(int fd, filesystem_header* fs_hdr, int type);
void dfsRoutines(int fd);
int getFilePiece(int fd, filesystem_header* fs_hdr, const char* filename);
int setupGet_Header(filesystem_header* fs_hdr, char* filename);
int putFile(int fd, filesystem_header* fs_hdr);
int listRoutine(int fd, const char* username);
void* work_routine(void* data);
void assignWorker(int fd);
int findSpot();
void initialize(int argc, char** argv);
int setupWorkDirectory(const char* username);
void printErr(int type);
int validUser(const char* usrname, const char* passwd);

int main(int argc, char** argv) {
    int listenfd, clientfd;
    int sa_len;
    struct sockaddr_in client;

    initialize(argc, argv);
    sa_len = sizeof(struct sockaddr_in);

    listenfd = listenRoutine(sa_len);

    printf("dfs is running....\n");

    while(1) {
        if((clientfd = accept(listenfd, (struct sockaddr *)&client, &sa_len)) < 0) {
            if(listenfd >= 0) close(listenfd);
            printErr(TYPE_CONNECT);
        }
        /*
        printf("Connection: %s:%d\n", 
                inet_ntoa(client.sin_addr), ntohs(client.sin_port));
        */
        // start work
        assignWorker(clientfd);
    }

    if(listenfd >= 0) close(listenfd);
    return 0;
}

// prepare socket, bind, and listen
// return fd for the socket on success; otherwise exit
int listenRoutine(int sa_len) {
    int listenfd;
    struct sockaddr_in server;

    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = htons(port);

    // create a TCP socket and set up server socket
    if((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) printErr(TYPE_SOCKET);

    // bind socket
    if(bind(listenfd, (struct sockaddr *) &server, sa_len) < 0) printErr(TYPE_BIND);

    if(listen(listenfd, MAXREQNUM)) printErr(TYPE_LISTEN);

    return listenfd;
}


// receive message from client(s)
int recvHeader(int fd, filesystem_header* fs_hdr) {
    int size;
    char buffer[256];
    char filename[256];
    int nbyte;
    char* p_filename;

    nbyte = 0;
    nbyte = recv(fd, buffer, 256, 0);

    if(nbyte < 0) return -1;

    // read data in particular format
    sscanf(buffer, "%s %s %s %s %d %ld", (fs_hdr->usr_info).username, (fs_hdr->usr_info).password,
            fs_hdr->cmd, filename, &(fs_hdr->pid), &(fs_hdr->psize));

    size = strlen(filename)+1>0?strlen(filename)+1:10;
    p_filename = malloc(size);
    memset(p_filename, 0, size);
    memcpy(p_filename, filename, strlen(filename));

    fs_hdr->filename = p_filename;
    return 0;
}

// setup and send message
void respondHeader(int fd, filesystem_header* fs_hdr, int type) {
    int nb_sent;
    char buffer[64];
    switch(type) {
        case MSG_TYPE_GET_OK:
            sprintf(buffer, "%s %d %ld", MSG_OK, fs_hdr->pid, fs_hdr->psize);
            break;
        case MSG_TYPE_PUT_OK:
            sprintf(buffer, "%s %s", MSG_OK, MSG_OK);
            break;
        case MSG_TYPE_LIST_OK:
            sprintf(buffer, "%s %s", MSG_OK, MSG_OK);
            break;
        case MSG_TYPE_USRINFO:
            sprintf(buffer, "%s %s", MSG_FAILED, MSG_USRINFO);
            break;
        case MSG_TYPE_FILENOTEXIST:
            sprintf(buffer, "%s %s", MSG_FAILED, MSG_FILENOTEXIST);
            break;
        case MSG_TYPE_FAKE:
            sprintf(buffer, "%s %s", MSG_FAILED, MSG_FAKE);
            break;
        case MSG_TYPE_DIR:
            sprintf(buffer, "%s %s", MSG_FAILED, MSG_DIR);
            break;
        default:
            sprintf(buffer, "%s %s", MSG_FAILED, MSG_ERROR);
            break;
    }
    nb_sent = 0;
    nb_sent = send(fd, buffer, 64, 0);
}

// start server routines
void dfsRoutines(int fd) {
    int status;
    int cmd;
    char *username;
    char *filename;
    filesystem_header fs_hdr;
    
    if(recvHeader(fd, &fs_hdr)) {
        printf("Error: Failed to receive header\n");
        return;
    }

    // check user info
    username = fs_hdr.usr_info.username;
    if(!validUser((fs_hdr.usr_info).username, (fs_hdr.usr_info).password)) {
        printf("[LOG] %s: Having a wrong username or password\n", username);
        respondHeader(fd, &fs_hdr, MSG_TYPE_USRINFO);
        return;
    }

    //printf("User:%s\n", (fs_hdr.usr_info).username);
    // check working directory
    if(setupWorkDirectory(username)) {
        printf("[LOG] Error: Failed to create User Directory\n");
        respondHeader(fd, &fs_hdr, MSG_TYPE_DIR);
        return;
    }

    if(!strcmp(fs_hdr.cmd, "GET")) cmd = CMD_GET;
    else if(!strcmp(fs_hdr.cmd, "PUT")) cmd = CMD_PUT;
    else if(!strcmp(fs_hdr.cmd, "LIST")) cmd = CMD_LIST;
    else cmd = CMD_ERR;

    switch (cmd) {
        case CMD_GET:
            filename = malloc(256);
            memset(filename, 0, 256);
            if((setupGet_Header(&fs_hdr, filename)) < 0){
                respondHeader(fd, &fs_hdr, MSG_TYPE_FILENOTEXIST);
            }
            else {
                respondHeader(fd, &fs_hdr, MSG_TYPE_GET_OK);
                getFilePiece(fd, &fs_hdr, filename);
            }
            free(filename);
            break;
        case CMD_PUT:
            putFile(fd, &fs_hdr);
            break;
        case CMD_LIST:
            usleep(100);
            listRoutine(fd, username);
            break;
        default:
            send(fd, "FAILED", 7, 0);
            break;
    }
    if(fs_hdr.filename) free(fs_hdr.filename);
}

// read data of file piece
int getFilePiece(int fd, filesystem_header* fs_hdr, const char* filename) {
    FILE* file;
    int nb_read;
    char* buffer[MAXBUFFER];

    file = fopen(filename, "r");
    if(!file) return -1;
    while((nb_read = fread(buffer, 1, MAXBUFFER, file))) {
        send(fd, buffer, nb_read, 0);
    }
    return 0;
}

// setup response of GET command, fill the filesize into the message
int setupGet_Header(filesystem_header* fs_hdr, char* filename) {
    char re_path[256];
    struct stat buf;

    // construct path of the file piece
    sprintf(re_path, ".%s/%s/.%s.%d", 
            wpath, fs_hdr->usr_info.username, fs_hdr->filename, fs_hdr->pid);
    if(stat(re_path, &buf)) return -1;

    memcpy(filename, re_path, strlen(re_path));
    fs_hdr->psize = buf.st_size;

    return 0;
}
// handle PUT command
int putFile(int fd, filesystem_header* fs_hdr) {
    int nbwrite;
    int nbrecv;
    FILE* file;
    char buffer[MAXBUFFER];
    char re_path[256];

    sprintf(re_path, ".%s/%s/.%s.%d", wpath, fs_hdr->usr_info.username, fs_hdr->filename, fs_hdr->pid);

    if(!(file = fopen(re_path, "w"))) {
        fprintf(stderr, "Error: Unable to open file, %s\n", re_path);
        respondHeader(fd, NULL, MSG_TYPE_FILENOTEXIST);
        return -1;
    }

    respondHeader(fd, NULL, MSG_TYPE_PUT_OK);

    nbwrite = 0;
    memset(buffer, 0, MAXBUFFER);
    while (nbrecv = recv(fd, buffer, MAXBUFFER, 0)) {
        nbwrite += fwrite(buffer, 1, nbrecv, file);
        if(nbrecv < MAXBUFFER)
            break;
        memset(buffer, 0, MAXBUFFER);
    }

    fclose(file);
    return 0;
}

// handel list routine
int listRoutine(int fd, const char* username) {
    int len;
    DIR* dir;
    struct dirent *d_entry;
    char* filename;
    int counter;
    char path[256];
    char buffer[256];

    // get path
    memset(buffer, 0, 256);
    sprintf(path, ".%s/%s/", wpath, username);
    dir = opendir(path);

    if(!dir) {
        respondHeader(fd, NULL, MSG_TYPE_ERROR);
        return -1;
    }

    respondHeader(fd, NULL, MSG_TYPE_LIST_OK);

    // start reading directories
    len = 0;
    memset(buffer, 0, 256);
    while((d_entry = readdir(dir))) {
        filename = d_entry->d_name;
        if(!strcmp(filename, ".") ||
                !strcmp(filename, "..")) continue;
        sprintf(buffer, "%s", filename+1);
        len = strlen(buffer);
        buffer[len-2] = ' ';
        sprintf(buffer, "%s\n", buffer);
        send(fd, buffer, strlen(buffer), 0);
        //printf("------\n%s------\n", buffer);
        memset(buffer, 0, 256);
    }
    return 0;
}


// start the routine of a thread -- server_routine
// release spot
// return NULL
void* work_routine(void* data) {
    int fd;
    struct spot *s = (struct spot*) data;
    fd = s->fd;
    dfsRoutines(fd);
    close(fd);
    s->state = 0;
    return NULL;
}

// create a thread 
// return none
void assignWorker(int fd) {
    int rc;
    int s;
    pthread_t p;
    
    // find a unsed spot to avoid race condition
    s = findSpot();
    spots[s].fd = fd;

    // create thread and assign work_routine
    rc = pthread_create(&p, NULL, 
                          work_routine, (void*)&spots[s]);
    if(rc) {
        fprintf(stdout, 
            "Error: Failed to create thread, return value: %d\n", rc);
    }
    spots[s].state = 1;
}

// find a unsed spot from  spots
// return index of available spot
int findSpot() {
    int i;
    int total = MAXREQNUM+5;
    
    while(1) {
        i = init_findworker%total;
        if(!spots[i].state) break;
        init_findworker++;
    }
    init_findworker++;
    return i;
}

// initialize globals
void initialize(int argc, char** argv) {
    if(argc != 3) printErr(0);

    wpath = argv[1];
    port = atoi(argv[2]);
}

// create user directory
int setupWorkDirectory(const char* username) {
    char dir_name[256];
    struct stat st;

    sprintf(dir_name, ".%s/%s", wpath, username);
    if (stat(dir_name, &st) == -1) {
        if(mkdir(dir_name, 0700) < 0) {
            printf("Failed to create directory, ->%s<-\n", dir_name);
            return -1;
        }
    }
    return 0;
}


// error messages
void printErr(int type) {
    switch(type) {
        case TYPE_USAGE:
            fprintf(stderr, "./dfs <path> <port>\n");
            break;
        case TYPE_FORMAT:
            fprintf(stderr, "Error: failed to read configuration file\n"); break;
        case TYPE_SOCKET:
            fprintf(stderr, "Error: Failed to create socket\n");
            break;
        case TYPE_LISTEN:
            fprintf(stderr, "Error: Failed to Listen\n");
            break;
        case TYPE_BIND:
            fprintf(stderr, "Error: Failed to Bind\n");
            break;
        case TYPE_CONFIG:
            fprintf(stderr, "Error: Errors in configuration file\n");
            break;
        default:
            fprintf(stderr, "Error: Failed to Bind\n");
            break;
    }
    exit(1);
}

// read conf file and check user info
int validUser(const char* usrname, const char* passwd) {
    int res = 0;
    FILE* cfg_file = NULL;
    char buffer[MAXREADLINE];
    char uname[32];
    char pwd[32];

    if(!(cfg_file = fopen(cfg_filename, "r"))) return 0;

    while(fgets(buffer, MAXREADLINE, cfg_file)) {
        if(buffer[0] != usrname[0]) {
            memset(buffer, 0, MAXREADLINE);
            continue;
        }
        sscanf(buffer, "%s %s", uname, pwd);
        if(!strlen(uname) || !strlen(pwd)) {
            fclose(cfg_file);
            return 0;
        }
        if(!strcmp(usrname, uname)&&!strcmp(passwd, pwd)) return 1;
    }
    return 0;
}
