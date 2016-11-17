/*
 * Program Assignment #2
 * Weipeng Cao
 * See README
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
//#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <signal.h>

#include <pthread.h>

// strings
#define HTTP1_0 "HTTP/1.0"
#define HTTP1_1 "HTTP/1.1"
#define STATUS200 "200 OK"
#define STATUS400 "400 Bad Request"
#define STATUS404 "404 Not Found"
#define STATUS500 "500 Internal Server Error: cannot allocate memory"
#define STATUS501 "501 Not Implemented"

// charset for checking url validation
#define INVALIDCHARS    ""

// constants
#define MAXFILENAME             256
#define MAXPATH                 4096
#define MAXBUFFER               4096
#define MAXERRCONTENT           2048
#define MAXLINE                 1024
#define MINLINE                 256
#define MAXREQBUFFER            1024
#define MAXREADLINE             1024
#define MAXATTRNAME             32
#define MAXTYPELENGTH           32
#define MIN_CONTENT_TYPE        10
#define MAXREQUEST              32
#define MAXMETHOD               32
#define MAXVERSION              16
#define MAXKEEPALIVE            16
#define MAXREQNUM               9

// server setting (not used)
#define THREADSLEEPTIME_MS      9
#define MAIN_UF_SLEEPTIME_MS    2

// structs
struct spot {
    char state;
    int fd;
};

typedef enum http_methods {GET, POST, ANY} http_method;
typedef enum req_status 
    {S200, S400, S401, S500, S501, S404} status;
typedef struct server_congf_ex {
    int port;
    unsigned short num_content_type;

    char* root_dir;
    char* dir_index;
    char** suffixes;
    char** content_types;
} server_config;

// global
const char* config_file_path = "ws.conf";
int port;
int listenfd;
server_config *s_confg = NULL;
struct spot spots[MAXREQNUM+5];
unsigned init_findworker = 0;

// prototypes
void server_routine(int);
int evalMethod(const char *str, int len);
int constructErrMsg(int num, char buffer[], char** http_reql, int type);
int sendBadRequst(int connfd, char** http_reql, int type);
int methodGet(int connfd, char** http_reql);
int respondMethodOk(int connfd, char** http_reql, FILE *source, 
        off_t fsize, const char *ctype);
int isUrlValid(const char* str);
int isVersionValid(const char* str);
const char* getFullPath(char **fullpath, char* url);
const char* getSuffix(char **suffix, const char* fullpath);
const char* getContentType(const char* suffix);
void* work_routine(void *);
void assignWorker(int fd);
int findSpot();
void freeServerCfg();
void configureServerEx();
int setContentType(FILE *fstream, int num);
int setServerDirPort(FILE *fstream, int *counter); 

// Interruption signal handler
void hSIGINT(int sig) {
    // free calloc
    freeServerCfg();
    if(listenfd >= 0) {
        close(listenfd);
    }

    exit(0);
}

int main() {
    // local variables
    int clientfd, sa_len;
    struct sockaddr_in server, client;

    // install signal(s)
    signal(SIGINT, hSIGINT);

    // Start

    // initialize global vars, then local vars
    listenfd = -1;

    configureServerEx();

    sa_len = sizeof(client);
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = htons(s_confg->port);

    // create a TCP socket and set up server socket
    if((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        fprintf(stdout, "Error: failed to create socket.\n");
        freeServerCfg();
        exit(1);
    }

    // bind socket
    if(bind(listenfd, (struct sockaddr *) &server, sa_len) < 0) {
        fprintf(stdout, "Error: failed to bind socket.\n");
        freeServerCfg();
        exit(1);
    }

    if(listen(listenfd, MAXREQNUM)) {
        fprintf(stdout, "Error: failed to bind socket.\n");
        freeServerCfg();
        exit(1);
    }

    printf("Server: <%s, %d>\n", 
            inet_ntoa(server.sin_addr),
            ntohs(server.sin_port));

    //start server routines
    while(1) {
        clientfd = accept(listenfd, (struct sockaddr *)&client, &sa_len);

        printf("Connection: %s:%d\n", 
                inet_ntoa(client.sin_addr), ntohs(client.sin_port));
        if(clientfd < 0) {
            fprintf(stdout, "Error: failed to create connection.\n");
            freeServerCfg();
            hSIGINT(0);
            exit(1);
        }
        assignWorker(clientfd);
    }

    // End -- Not reachable
    if(listenfd >= 0) close(listenfd);
    freeServerCfg();
    return 0;
}

/*
 * start server routines, only GET
 * return none
 */
void server_routine(int connfd) {
    char* http_reql[5];
    char buffer[MAXLINE];
    char method[MAXMETHOD];
    char url[MAXLINE];
    char version[MAXVERSION];
    char host[MAXLINE];
    char keep_alive[MAXKEEPALIVE];
    
    // set aaray for parameter
    http_reql[0] = method;
    http_reql[1] = url;
    http_reql[2] = version;
    http_reql[3] = host;
    http_reql[4] = keep_alive;
    //set zero
    bzero(method, MAXMETHOD);
    bzero(url, MAXLINE);
    bzero(version, MAXLINE);
    bzero(host, MAXLINE);
    bzero(keep_alive, MAXKEEPALIVE);
    bzero(buffer, MAXLINE);

    while(1) { // not used
        recv(connfd, buffer, MAXLINE, 0);
        sscanf(buffer, "%s %s %s %*s %s %*s %s", 
                method, url, version, host, keep_alive);

        switch(evalMethod(method, strlen(method))) {
            case GET:
                printf("REQUEST: %s\n", buffer);
                methodGet(connfd, http_reql);
                break;
            case POST:
            case ANY:
                sendBadRequst(connfd, http_reql, 0);
                break;
        }
        break;
        // not going to do extra pts
    }
    close(connfd);
}

/*
 * eval method 
 * return status
 */
int evalMethod(const char *str, int len) {
    http_method m = ANY;
    if(!len) return m;
    if(!strcmp(str, "POST")) m = POST;
    if(!strcmp(str, "GET")) m = GET;
    return m;
}

/*
 * Construct error message bodys
 * return strlen(buffer)
 */
int constructErrMsg(int num, char buffer[], char** http_reql, int type) {

    if(num == 404) {
        sprintf(buffer, "%s<html><body>404 Not Found Reason URL does not exist:",
                buffer);
        sprintf(buffer, "%s %s</body></html>", buffer, http_reql[1]);
    }
    else if( num = 400) {
        switch(type) {
            case 0:
                sprintf(buffer, 
                        "%s<html><body>400 Bad Request Reason: Invalid Method:",
                        buffer);

                sprintf(buffer, "%s %s</body></html>", buffer, http_reql[0]);
                break;
            case 1:
                sprintf(buffer, 
                        "%s<html><body>400 Bad Request Reason: Invalid Url:",
                        buffer);
                sprintf(buffer, "%s %s</body></html>", buffer, http_reql[1]);
                break;
            case 2:
                sprintf(buffer, 
                        "<html><body>400 Bad Request Reason: Invalid HTTP-Version:",
                        buffer);
                sprintf(buffer, "%s %s</body></html>", buffer, http_reql[2]);
                break;
        }
    }
    else if(num == 501) {
         sprintf(buffer, 
                 "%s<html><body>501 Not Implemented %s:", buffer,http_reql[0]);
         sprintf(buffer, "%s %s</body></html>", buffer, http_reql[1]);
    }
    else {
        return 0;
    }

    return  strlen(buffer);
}

// send 400
int sendBadRequst(int connfd, char** http_reql, int type) {
    char buffer[MAXBUFFER];
    char content[MAXERRCONTENT];
    int clen;

    bzero(buffer,MAXBUFFER);

    switch(type) {
        case 0:
            sendNotImplemented(connfd, http_reql);
            return 0;
            break;
        case 1:
            if(http_reql[1]) printf("%s is invalid\n", http_reql[1]);
            clen = constructErrMsg(400, content, http_reql, 1);
            break;
        case 2:
            if(http_reql[2]) printf("%s is invalid\n", http_reql[2]);
            clen = constructErrMsg(400, content, http_reql, 2);
            break;
    }
    sprintf(buffer, "%s %s\r\n", HTTP1_1, STATUS400);
    sprintf(buffer, "%sServer: PA2-Server\r\n", buffer);
    sprintf(buffer, "%sContent-length: %d\r\n", buffer, clen);
    sprintf(buffer, "%sContent-type: %s\r\n\r\n", buffer, "text/html");
    sprintf(buffer, "%s%s", buffer, content);
    write(connfd, buffer, strlen(buffer));
    return 0;
}

//send 501
int sendNotImplemented(int connfd, char** http_reql) {
    char buffer[MAXBUFFER];
    char content[MAXERRCONTENT];
    int clen;
    if(http_reql[0]) printf("%s is NOT implemented\n", http_reql[0]);
    bzero(buffer, MAXBUFFER);
    clen = constructErrMsg(501, content, http_reql, 0);
    sprintf(buffer, "%s %s\r\n", HTTP1_1, STATUS501);
    sprintf(buffer, "%sServer: PA2-Server\r\n", buffer);
    sprintf(buffer, "%sContent-length: %d\r\n", buffer, clen);
    sprintf(buffer, "%sContent-type: %s\r\n\r\n", buffer, "text/html");
    sprintf(buffer, "%s%s", buffer, content);
    write(connfd, buffer, strlen(buffer));
    return 0;
}

// send 404
int sendNotFound(int connfd, char **http_reql) {
    char buffer[MAXBUFFER];
    char content[MAXERRCONTENT];
    int clen;
    
    if(http_reql[1]) printf("%s is NOT found\n", http_reql[1]);
    bzero(buffer, MAXBUFFER);
    
    clen = constructErrMsg(404, content, http_reql, 0);
    printf("content:\n%s\n", content);
    clen = strlen(content);
    sprintf(buffer, "%s %s\r\n", HTTP1_1, STATUS404);
    sprintf(buffer, "%sServer: PA2-Server\r\n", buffer);
    sprintf(buffer, "%sContent-length: %d\r\n", buffer, clen);
    sprintf(buffer, "%sContent-type: %s\r\n\r\n", buffer, "text/html");
    sprintf(buffer, "%s%s", buffer, content);
    write(connfd, buffer, strlen(buffer));
    return 0;
}

// send 500 
int sendInternalError(int connfd) {
    char buffer[MAXBUFFER];
    sprintf(buffer, "%s %s\r\n", HTTP1_1, STATUS500);
    write(connfd, buffer, strlen(buffer));
    return 0;
}

/*
 * Method GET routine
 * parameters: int connfd
 *              char** http_reql 
 * return 0 for complete; 1 for error
 */
int methodGet(int connfd, char** http_reql){
    char *fullpath = NULL;
    char *suffix = NULL;
    int path_len, suf_len;
    off_t file_size;
    struct stat fstat;
    FILE* file;
    status r_status;

    // check validation of url and HTTP Version
    if(!isUrlValid(http_reql[1]))  {
        printf("url invalid\n");
        sendBadRequst(connfd, http_reql, 1);
        return 1;
    }

    if(!isVersionValid(http_reql[2])) {
        printf("version invalid\n");
        sendBadRequst(connfd, http_reql, 2);
        return 1;
    }

    r_status = S200;

    // get full path
    getFullPath(&fullpath, http_reql[1]);
    // check file type
    if(stat(fullpath, &fstat) < 0) {
        // file not exist
        r_status = S404;
    }
    else {
        if(S_ISREG(fstat.st_mode)) {
            // can read 
            if(fstat.st_size == 0){
                r_status = S501;
            }
            else {
                r_status = S200;
                file_size = fstat.st_size;
                getSuffix(&suffix, fullpath);
            }
        }
        else {
            r_status = S404;
        }
    }

    switch(r_status) {
        case S404:
            sendNotFound(connfd, http_reql);
            break;
        case S200:
            file = fopen(fullpath, "r");
            respondMethodOk(connfd, http_reql, file, 
                    fstat.st_size, getContentType(suffix));
            fclose(file);
            break;
        case S500:
            sendInternalError(connfd);
            break;
        case S501:
            sendNotImplemented(connfd, http_reql);
            break;
    }
    // free callocs
    if(fullpath != NULL) free(fullpath);
    if(suffix != NULL) free(suffix);
    return 0;
}

/*
 * respond GET with 200 OK header and file information
 * and send data of the file 
 * return 0
 */
int respondMethodOk(int connfd, char** http_reql, 
    FILE *source, off_t fsize, const char *ctype) {

    char buffer[MAXBUFFER];
    int nbyte;
    int t_read;
    int t_send;
    
    memset(buffer, '\0', MAXBUFFER); 

    sprintf(buffer, "%s %s\r\n", http_reql[2], STATUS200);
    sprintf(buffer, "%sServer: PA2-Server\r\n", buffer);
    sprintf(buffer, "%sContent-length: %d\r\n", buffer, (int)fsize);
    sprintf(buffer, "%sContent-type: %s\r\n", buffer, ctype);
    // check is Keep alive
    if(strlen(http_reql[4]))
        sprintf(buffer, "%sConnection: %s\r\n\r\n", buffer, http_reql[4]);
    else
        sprintf(buffer, "%s\r\n", buffer);
    // send repond header
    write(connfd, buffer, strlen(buffer));
    // send data
    bzero(buffer, MAXBUFFER);
    nbyte = 0;
    t_read = 0;
    t_send = 0;
    while(!feof(source)){
        nbyte = fread(buffer, sizeof(char), MAXBUFFER-1, source);
        t_send += write(connfd, buffer, nbyte);
        t_read += nbyte;
        //if(t_read == fsize) break;
        bzero(buffer, MAXBUFFER);
    }
    return 0;
}

// check url is valid, INVALIDCHARS is empty
int isUrlValid(const char* str) {
    int len = strlen(str);
    const char *invalidchars = INVALIDCHARS;
    int i,j;
    for(i = 0; i < strlen(invalidchars); i++ ) {
        for(j = 0; j < len; j++) {
            if(invalidchars[i] == str[j]) return 0;
        }
    }
    return 1;
}

// check http version
// return 1 for true, otherwise 0
int isVersionValid(const char* str) {
    if(str == NULL|| strlen(str) == 0) return 0;
    return !(strcmp(str, HTTP1_0)) || !(strcmp(str, HTTP1_1));
}

// construct fullpath, allowed www everywhere if permited 
// return string
const char* getFullPath(char **fullpath, char* url) {
    int root_dir_len;
    int url_len;
    int len;
    char* pre_res;
    char* suf_ptr;
    char* res;

    root_dir_len = strlen(s_confg->root_dir)+1;
    url_len = strlen(url);
    if(url_len < 3) {
        // get default index file from configuration
        suf_ptr = s_confg->dir_index;
        url_len = strlen(suf_ptr)+1;
    }
    else {
        // get fullpath as the request desired
        suf_ptr = url+1;
        url_len = strlen(suf_ptr)+1;
    }
    len = root_dir_len + url_len -1;
    res = calloc(len, sizeof(char));
    strcpy(res, s_confg->root_dir);
    strcpy(res+root_dir_len -1, suf_ptr);
    *fullpath = res;
    return res;
}

// find out the suffix of the target file
// return string on success, NULL on failure
const char* getSuffix(char **suffix, const char* fullpath) {
    int i;
    int url_len;
    char *url;
    char *res;

    url_len = strlen(fullpath);
    url = calloc(url_len+1,sizeof(char));
    strcpy(url, fullpath);

    // start from back to avoid non-suffix phrase in file name
    for(i = url_len-1; i > 0; i--) {
        if(url[i] == '.') break;
    }

    if(i <= 0) return NULL;
    res = calloc(url_len-i+1, sizeof(char));
    strcpy(res, url+i);
    *suffix = res;
    return res;
}

// retrieve content type with suffix
// return string on success, NULL on failure
const char* getContentType(const char* suffix) {
    int i;
    int total = s_confg->num_content_type;
    if(!suffix) return NULL; // no search if NULL
    for(i = 0; i < total; i++) {
        if(!strcmp(suffix, s_confg->suffixes[i])) break;
    }
    if(i < total) return s_confg->content_types[i];
    return NULL;
}

// start the routine of a thread -- server_routine
// release spot
// return NULL
void* work_routine(void* data) {
    int fd;
    struct spot *s = (struct spot*) data;
    fd = s->fd;
    server_routine(fd);
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

// free memory created by calloc in server_config
void freeServerCfg() {
    int i;
    if(!s_confg) return;
    if(s_confg->root_dir) free(s_confg->root_dir);
    if(s_confg->dir_index) free(s_confg->dir_index);
    if(s_confg->suffixes && s_confg->content_types) { 
        for(i = 0; i < s_confg->num_content_type; i++) {
            if(s_confg->suffixes[i]) free(s_confg->suffixes[i]);
            if(s_confg->content_types[i]) free(s_confg->content_types[i]);
        }
        free(s_confg->suffixes);
        free(s_confg->content_types);
    }
}

// read from ws.config and  fill struct server_confi_ex
void configureServerEx() {
    FILE* config_file;
    int counter;
    int res = 0;
    // read configuration file
    // exit if failed to open
    config_file = fopen(config_file_path, "r");
    if(!config_file) {
        fprintf(stderr, "Error: failed to open %s\n", config_file_path);
        exit(1);
    }
    // initialize server configuration structure
    if(!s_confg) {
        s_confg = malloc(sizeof(server_config));
    }
    
    // read attributes excep content type
    counter = 0;
    if((res = setServerDirPort(config_file, &counter))) {
        fclose(config_file);
        exit(res);
    }
    // reread the file for content type
    rewind(config_file);
    if((res = setContentType(config_file, counter))) {
        fclose(config_file);
        exit(res);
    }

    fclose(config_file);
}

// read server configuration excep content types
// return 0 on success, 1 on failure
int setServerDirPort(FILE *fstream, int *counter) {

    int linenum;
    int len;
    char buffer[MAXLINE];
    char attr[MAXLINE];
    char value[MAXLINE];
    char *str;

    port = 0;
    linenum = 0;
    len = 0;
    memset(buffer, 0, MAXLINE);
    
    while(fgets(buffer, MAXLINE, fstream)) {
        switch(buffer[0]) {
            case ' ':
            case '#':
            case '\n':
                break;
            case '.':
                // accumulate number of content types 
                (*counter)++;
                break;
            default: {
                char *str;
                memset(attr, 0, MAXLINE);
                memset(value, 0, MAXLINE);
                sscanf(buffer, "%s %s", attr,value);
                if(!(attr&&value)) {
                    fprintf(stdout, "Error: ws.config has wrong format at %d\n",
                                linenum);
                    return 1;
                }
                if(!strcmp(attr, "Listen")) {
                    if((port = atoi(value)) <= 1024) {
                        fprintf(stderr, 
                                "Error: port number cannot be below 1024.\n");
                        return 1;
                    }
                    s_confg->port = port;
                }

                // 
                str = NULL;
                len = strlen(value) +1;
                if(!strcmp(attr, "DirectoryIndex")) {
                    s_confg->dir_index =  calloc(len, 1);
                    memset(s_confg->dir_index, 0, len);
                    strcpy(s_confg->dir_index, value);
                }

                if(!strcmp(attr, "DocumentRoot")) {
                    s_confg->root_dir = calloc(len-2,1);
                    strcpy(s_confg->root_dir, value+1);
                    s_confg->root_dir[len-3] = '\0';
                }
            }
            break;
        }

        memset(attr, 0, MAXLINE);
        memset(value, 0, MAXLINE);
        memset(buffer, 0, MAXLINE);
        linenum++;
    }
    return 0;
}

// read all content type and have fomat suffixes[i]->content_types[i]
// return 0 on success, 1 on failure
int setContentType(FILE *fstream, int num) {
    int line, counter;
    char buffer[MAXLINE];
    char attr[MAXLINE];
    char value[MAXLINE];
    char **content_types;
    char **suffixes;

    // read file for content type
    line = 0;
    counter = num;
    s_confg->num_content_type = num;

    content_types = (char**) malloc(num*sizeof(void*));
    suffixes = (char**) malloc(num*sizeof(void*));

    while(fgets(buffer, MAXLINE, fstream)&&counter) {
        int len;
        if(buffer[0] == '.') {
            bzero(attr, MAXLINE);
            bzero(value, MAXLINE);
            sscanf(buffer, "%s %s", attr, value);
            if(strlen(value) <= 2) {
                int i;
                fprintf(stderr, "Error: line %d: %s %s\n", line, attr, value);
                for(i = 0; i < s_confg->num_content_type - counter; i++) {
                    free(content_types[i]);
                    free(suffixes[i]);
                }
                return 1;
            }

            len = strlen(attr);
            suffixes[counter-1] = calloc(len, 0);
            strcpy(suffixes[counter-1], attr);
            content_types[counter-1] = calloc(len, 0);
            strcpy(content_types[counter-1],value);
            counter--;
        }
        line++;
    }
    s_confg->suffixes = suffixes;
    s_confg->content_types = content_types;
    return 0;
}

