#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <openssl/md5.h>
#include <sys/select.h>
#include <sys/time.h>

#define MAXFILENAME                 32
#define MAXREADLINE                 1024
#define MINREADLINE                 256
#define MAXBUFFER                   1024
#define MAXREADFILE                 4096


#define CMD_ERROR                   -1
#define CMD_GET                     1
#define CMD_LIST                    0
#define CMD_PUT                     2

#define ERR_TYPE_USAGE              0
#define ERR_TYPE_FORMAT             1
#define ERR_TYPE_SOCKET             2
#define ERR_TYPE_CONNECT            3
#define ERR_TYPE_CONFIG             4

////////////////////
// define type 
////////////////////
typedef struct {
    char addr[16];
    int port;
} dfs_info;

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

typedef struct node {
    char *filename;
    int piece[4];
    struct node* next;
} file_node;

dfs_info dests[4];
userinfo usrinfo;
int connfds[4];
char fptos_patterns[4][4][2];
char stofp_patterns[4][4][2] = {
                        {{1,2}, {2,3}, {3,4}, {4,1}},
                        {{4,1}, {1,2}, {2,3}, {3,4}},
                        {{3,4}, {4,1}, {1,2}, {2,3}},
                        {{2,3}, {3,4}, {4,1}, {1,2}}
                    };

int connectServers();
int connectRoutine(dfs_info server_info);
int connectServerAt(int index);
void startRoutines(const char* command, int cmd_num);
void routineGET(const char* command);
int copyFiles(FILE* dest, FILE* src);
int getFile(FILE* tmp, char* filename, char x);
int getFilePiece(FILE* tmp, filesystem_header* fs_hdr, char s_index);
void routineLIST(const char* command);
int getListData(int fd, FILE* ftmp);
void analyzeListDataLL();
void createNode(file_node* n, const char* filename, int pid);
int isComplete(file_node* n);
file_node* searchFilename(file_node* f_list, const char* filename, int* state);
int addFilename(file_node* f_list, const char* filename, int pid);
void routinePUT(const char* command);
void putFile(FILE* file, char* filename, off_t fsize, int x);
int sendFilePiece(FILE* file, filesystem_header* fs_hdr, int s_index);
int sendHeader(filesystem_header* fs_hdr, int connfd);
int copyFiles(FILE* dest, FILE* src);
int SELECT(int fd);
void allocatePieceSize(off_t* stopat, off_t fsize);
void initialize(int argc, char** argv);
void convertPatterns();
void readConfig(FILE* cfg_src);
int evalCommand(const char* input, int next);
void printErr(int type);
char md5hashFile(FILE* file, off_t fsize);
char* getFileNameFromCmd(const char* command, int type);
int getFileSize(const char* filename, off_t* size);
void close_resetConnfds();

int main(int argc, char** argv) {
    int cmd;
    char buf[MINREADLINE];
    initialize(argc, argv);

    while(1) {
        printf("> ");
        fgets(buf, MINREADLINE, stdin);
        cmd = evalCommand(buf, 0);

        startRoutines(buf, cmd);
        memset(buf, 0, MINREADLINE);
    }

    printf("Exit ....\n");
    return 0;
}


// connect the servers at once
// not used
int connectServers() {
    int connfd;
    int i, res;
    int server_num = 4;
    res = 0;
    for(i = 0; i < server_num; i++ ) {
        connfd = -1;
        if((connfd = connectRoutine(dests[i])) > 0) {
            connfds[i] = connfd;
            res++;
        }
    }
    return res;
}

// connect the individual server
// return file descriptor on success; otherwise -1
int connectRoutine(dfs_info server_info) {
    int sock;
    int sa_len;
    struct sockaddr_in server;

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        //printf("Error: Unable to connect server, %s:%d...\n", server_info.addr, server_info.port);
        return -1;
    }

    sa_len = sizeof(server);
    memset(&server, 0, sa_len);
    server.sin_family       = AF_INET;
    server.sin_addr.s_addr  = inet_addr(server_info.addr);
    server.sin_port         = htons(server_info.port);

    if(connect(sock, (struct sockaddr *) &server, sa_len) < 0) {
        //printf("Error: Unable to connect server, %s:%d...\n", server_info.addr, server_info.port);
        return -1;
    }
    return sock;
}

// connect a server
// return same as connectRoutine
int connectServerAt(int index) {
    int connfd;

    connfd = connectRoutine(dests[index]);
    return connfd;
}

// start client routines
void startRoutines(const char* command, int cmd_num) {
    switch(cmd_num) {
        case CMD_GET:
            routineGET(command);
            break;
        case CMD_LIST:
            routineLIST(command);
            break;
        case CMD_PUT:
            routinePUT(command);
            break;
        default:
            printf("Error: Failed to evaluate %s\r\n", command);
    }
    close_resetConnfds();
}

// handle GET command
void routineGET(const char* command) {
    int x;
    char* filename;
    FILE* ftmp;
    FILE* file;

    filename = getFileNameFromCmd(command, CMD_GET);

    // open a file to store piece
    ftmp = tmpfile();
    if(!ftmp) {
        fprintf(stderr, "Error: Unable to open tmp file\n");
        return;
    }

    if(getFile(ftmp, filename, 0) < 0) {
        free(filename);
        fclose(ftmp);
        return;
    }

    file = fopen(filename, "w");
    if(!file) {
        free(filename);
        fclose(ftmp);
        return;
    }

    rewind(ftmp);
    copyFiles(file, ftmp);

    free(filename);
    fclose(ftmp);
    fclose(file);
}

// copy data from src file to dest file
int copyFiles(FILE* dest, FILE* src) {
    int nb_read;
    char *buffer[MAXREADFILE];

    while((nb_read = fread(buffer, 1, MAXREADFILE, src))>0) {
        fwrite(buffer, 1, nb_read, dest);
    }
    return 0;
}

// Get all file pieces in order from each server
// and prepare message that will be send to server
// return 0 on getting all pieces; 1 on incomplete
int getFile(FILE* tmp, char* filename, char x) {
    int i;
    char fptos_pattern[4][2];
    int server_num;
    int file_piece;
    filesystem_header fs_hdr;

    file_piece = 0;
    memset(&fs_hdr, 0, sizeof(fs_hdr));
    fs_hdr.usr_info = usrinfo;
    fs_hdr.filename = filename;
    sprintf(fs_hdr.cmd, "GET");

    memcpy(fptos_pattern, fptos_patterns[x], sizeof(char)*4*2);
    // loop for each piece
    while(file_piece < 4) {
        int status = 0;
        int success = 0;
        fs_hdr.pid = file_piece;
        fs_hdr.psize = 0;

        // loop for requesting nth piece of file from all servers
        for(i = 0; i < 4; i++) {
            success = getFilePiece(tmp, &fs_hdr, i);
            status += success > 0? success-status:success;
            if(success >= 0) break;
        }
        if(status < -3) {
            printf("Error: Failed to get a complete file\n");
            return -1;
        }
        file_piece++;
    }
    return 0;
}

// connect server and receive data
// received data would be stored in tmp
// return 0 on success; 1 on failure
int getFilePiece(FILE* tmp, filesystem_header* fs_hdr, char s_index) {
    char buffer[MAXBUFFER];
    int read_size;
    ssize_t byte_read;
    ssize_t byte_sent;
    off_t p_size;
    int connfd;
    int pid;

    connfd = connectServerAt(s_index);

    if(connfd < 0) return -1;

    byte_sent = sendHeader(fs_hdr, connfd);
    if(byte_sent < 0) return -1;
    
    p_size = fs_hdr->psize;
    while(p_size > 0) {
        memset(buffer, 0, MAXBUFFER);
        read_size = p_size > MAXBUFFER? MAXBUFFER:p_size;
        byte_read = recv(connfd, buffer, read_size, 0);
        p_size -= fwrite(buffer, 1, read_size, tmp);
    }
    close(connfd);
    return 0;
}
// handle PUT command
void routinePUT(const char* command) {
    char x;
    FILE* file;
    char* filename;
    off_t fsize;

    filename = getFileNameFromCmd(command, CMD_PUT);
    fsize = 0;
    if(getFileSize(filename, &fsize) < 0) {
        printf("Error: Failed to read status, file:%s\n", filename);
        free(filename);
        return;
    }
    
    file = fopen(filename, "r");
    if(!file) {
        printf("Error: Failed to open %s\n", filename);
        free(filename);
        return;
    }

    x = md5hashFile(file, fsize);
    putFile(file, filename, fsize, x);
    free(filename);
    fclose(file);
}

// setup and send message, split file into 4 depending on the pattern
// send two copies of each piece of the file to relevant servers
void putFile(FILE* file, char* filename, off_t fsize, int x) {
    char fptos_pattern[4][2];
    int server_num;
    off_t stopat[4];
    int file_piece;
    int i;
    filesystem_header fs_hdr;

    // probes
    /*
    server_num = connectServers();
    if(server_num < 4) {
        printf("Error: one or more server(s) is not online.\n\tTerminate PUT operation\r\n");
        return;
    }
    */

    file_piece = 0;
    memset(&fs_hdr, 0, sizeof(fs_hdr));
    fs_hdr.usr_info = usrinfo;
    fs_hdr.filename = filename;
    sprintf(fs_hdr.cmd, "PUT");

    memcpy(fptos_pattern, fptos_patterns[x], sizeof(char)*4*2);
    allocatePieceSize(stopat, fsize);
    while(file_piece < 4) {
        int status = 0;
        fpos_t pos;
        fs_hdr.pid = file_piece;
        fs_hdr.psize = stopat[file_piece];

        fgetpos(file, &pos);
        for(i = 0; i < 2; i++) {
            // set file position back
            fsetpos(file, &pos);
            status = sendFilePiece(file, &fs_hdr, fptos_pattern[file_piece][i]);
        }
        if(status == -2) break;
        file_piece++;
    }
}

// connect server and send data of file
// return -2 on no connection, 0 on success
int sendFilePiece(FILE* file, filesystem_header* fs_hdr, int s_index) {
    char buffer[MAXBUFFER];
    ssize_t byte_sent;
    off_t read_size;
    off_t p_size;
    int connfd;

    connfd = connectServerAt(s_index);

    if(connfd < 0) return -2;

    byte_sent = sendHeader(fs_hdr, connfd);
    if(byte_sent < 0) return byte_sent;
    
    p_size = fs_hdr->psize;
    while(p_size > 0) {
        memset(buffer, 0, MAXBUFFER);
        read_size = p_size > MAXBUFFER? MAXBUFFER:p_size;
        fread(buffer, 1, read_size, file);
        byte_sent = send(connfd, buffer, read_size, 0);
        p_size -= byte_sent;
    }
    close(connfd);
    return 0;
}

// send message to the server
// return positive integer on success; negative on failure
int sendHeader(filesystem_header* fs_hdr, int connfd) {
    char buffer[256];
    char status[10];
    char msg[64];
    int nbyte;
    memset(buffer, 0, 256);
    sprintf(buffer, "%s %s %s %s %d %ld", 
            fs_hdr->usr_info.username, fs_hdr->usr_info.password,
            fs_hdr->cmd, fs_hdr->filename, fs_hdr->pid, fs_hdr->psize);
    //ssize_t send(int sockfd, const void *buf, size_t len, int flags);
    nbyte = send(connfd, buffer, 256, 0);
    if(nbyte < 0) return -1;

    memset(msg, 0, 64);

    //ssize_t recv(int sockfd, void *buf, size_t len, int flags);
    nbyte = recv(connfd, msg, 64, 0);
    sscanf(msg, "%s %*s", status);
    if(strcmp(status, "OK")) {
        if(strcmp(fs_hdr->cmd, "GET")) 
            return -1;

        printf("Error:\r\nMessage: %s\r\n", msg+7);
        return -2;
    }

    // set up for GET command
    if(!strcmp(fs_hdr->cmd, "GET")) {
        sscanf(msg, "%*s %d %ld", &fs_hdr->pid, &fs_hdr->psize);
    }
    
    return nbyte;
}

// handle LIST command
void routineLIST(const char* command) {
    int nb_sent;
    int i;
    int connfd;
    FILE* ftmp;
    filesystem_header fs_hdr;
    char filename[5] = "NONE";

    // open a file to store info from each server
    ftmp = tmpfile();
    if(!ftmp) {
        fprintf(stderr, "Error: Unable to open tmp file\n");
        return;
    }

    // prepare massage
    memset(&fs_hdr, 0, sizeof(fs_hdr));
    fs_hdr.usr_info = usrinfo;
    fs_hdr.filename = filename;
    sprintf(fs_hdr.cmd, "LIST");
    fs_hdr.pid = 0;
    fs_hdr.psize = 0;

    // connect individual server
    for(i = 0; i < 4; i++) {
        connfd = connectServerAt(i);
        if((nb_sent = sendHeader(&fs_hdr, connfd)) < 0) continue;
        getListData(connfd, ftmp);
        close(connfd);
    }
    rewind(ftmp);
    analyzeListDataLL(ftmp);
    fclose(ftmp);
}

// receiving data from socket file decriptor fd
// and store into ftmp
// return not used
int getListData(int fd, FILE* ftmp) {
    int nb_recv;
    char buffer[MAXBUFFER];

    memset(buffer, 0, MAXBUFFER);

    while((nb_recv = recv(fd, buffer, MAXBUFFER, 0))) {
        fwrite(buffer, 1, nb_recv, ftmp);
        memset(buffer, 0, MAXBUFFER);
    }

    return 0;
}

// process file directory info of each server
void analyzeListDataLL(FILE* src) {
    int fnum;
    char filename[256];
    int pid;
    file_node* f_list;

    memset(filename, 0, 256);
    f_list = NULL;
    pid = -1;
    fnum = 0;
    while((fscanf(src, "%s %d", filename, &pid)) != EOF) {
        if(!f_list) {
            f_list = malloc(sizeof(file_node));
            memset(f_list, 0, sizeof(file_node));
            createNode(f_list, filename, pid);
            fnum++;
            continue;
        }
        fnum += addFilename(f_list, filename, pid)?0:1;
        memset(filename, 0, 256);
        pid = -1;
    }
    printf(">>>>>>>>>>>>%d<<<<<<<<<<<<\n", fnum);
    while(f_list) {
        file_node* node = NULL;
        if(!isComplete(f_list)) printf("%s [imcomplete]\n", f_list->filename);
        else printf("%s\n", f_list->filename);
        node = f_list;
        f_list = f_list->next;
        free(node->filename);
        free(node);
    }
    printf("\n");
}

// create linklist node
void createNode(file_node* n, const char* filename, int pid) {
    int len;
    char* str;
    len = strlen(filename);
    str = malloc(len);
    memset(str, 0, len+1);
    memcpy(str, filename, len);
    n->filename = str;
    n->piece[pid] = 1;
}

// set the file has enough pieces
int isComplete(file_node* n) {
    return n->piece[0]&&n->piece[1]&&n->piece[2]&&n->piece[3];
}

// search link list
// return the last node of the list
file_node* searchFilename(file_node* f_list, const char* filename, int* state) {
    // filename is in the list
    if(!strcmp(filename, f_list->filename)) {
        *state = 0;
        return f_list;
    }
    // reaching the end
    if(!f_list->next) {
        *state = 1;
        return f_list;
    }
    // searching
    return searchFilename(f_list->next, filename, state);
}

// add a filename to a list
// return 0 when added
int addFilename(file_node* f_list, const char* filename, int pid) {
    file_node* f_node;
    int state;
    f_node = searchFilename(f_list, filename, &state);
    if(!state) {
        if(!f_node->piece[pid]) f_node->piece[pid] = 1;
        return 1;
    }
    // create a node for new filename
    f_node->next = malloc(sizeof(file_node));
    memset(f_node->next, 0, sizeof(file_node));
    createNode(f_node->next, filename, pid);
    return 0;
}

// set up fd timeout 
// return positive on success; neagive on failure
// Not used
int SELECT(int fd) {
    int res;
    fd_set set;
    struct timeval tval = {.tv_sec = 1, .tv_usec = 0};
    return 1;
    FD_SET(fd, &set);
    res = select(0, &set, &set, NULL, &tval);
    if(FD_ISSET(fd, &set)) FD_CLR(fd, &set);
    return res;
}

// calculate size of file piece
void allocatePieceSize(off_t* stopat, off_t fsize) {
    int i;
    off_t temp;
    temp = fsize/4;
    fsize -= temp*4;
    for(i=0;i<4;i++) {
        stopat[i] = temp;
        if((fsize--) > 0)
            stopat[i]++;
    }
}

// Initialize program
void initialize(int argc, char** argv) {
    FILE* cfg_src;
    int i = 0;

    if(argc != 2) printErr(ERR_TYPE_USAGE);

    cfg_src = fopen(argv[1], "r");
    if(cfg_src==NULL) printErr(ERR_TYPE_CONFIG);
    memset(&dests, 0, sizeof(dfs_info)*4);
    memset(&usrinfo, 0, sizeof(usrinfo));
    readConfig(cfg_src);
    fclose(cfg_src);

    for(i = 0; i < 4; i++) {
        connfds[i] = -1;
    }

    for(i = 0; i < 4; i++) {
        printf("addr: %s, port: %d\n", dests[i].addr, dests[i].port);
    }
    // convert patterns
    convertPatterns();
}

void convertPatterns() {
    int i, j, k;
    for(i = 0; i < 4; i++) {
        for(j = 0; j < 4; j++) {
            for(k = 0; k < 2; k++) {
                int value = stofp_patterns[i][j][k];
                fptos_patterns[i][value-1][k]=j;
            }
        }
    }
}


// read configuration file 
// exit on wrong format
void readConfig(FILE* cfg_src) {
    int counter;
    int s_cnt;
    char buffer[MAXREADLINE];
    counter = 0;
    s_cnt = 4;

    while(fgets(buffer, MAXREADLINE, cfg_src)) {
        char initial = buffer[0];
        if(initial=='S') {
            char addr[16];
            char port[8];
            int i;
            for(i = 0; i < strlen(buffer); i++) {
                if(buffer[i] == ':') {
                    buffer[i] = ' ';
                }
                if(buffer[i] == '\n') {
                    buffer[i] = '\0';
                    break;
                }
            }
            sscanf(buffer, "%*s %*s %s %s", addr, port);
            if(!s_cnt) continue;
            memcpy(&dests[4-s_cnt].addr, &addr, strlen(addr));
            dests[4-s_cnt].port = atoi(port);
            s_cnt--;
        }
        else if (initial == 'U') {
            char username[24];
            sscanf(buffer, "%*s%s",username);
            memcpy(&usrinfo.username, username, strlen(username));
        }
        else if (initial == 'P') {
            char password[24];
            sscanf(buffer, "%*s%s",password);
            memcpy(&usrinfo.password, password, strlen(password));
        }
        else {
            fclose(cfg_src);
            printf("Error at line %d\n", counter);
            printErr(ERR_TYPE_FORMAT);
        }
        counter++;
    }
}

// evaluate input
int evalCommand(const char* input, int next) {
    char buf[10];
    int i;
    int j;

    if(input[0] == '\n') return CMD_ERROR;
    i = 0;
    j = 0;
    while(1) {
        if(input[i] == '\0'){
            break;
        }
        if(input[i] == ' ') {
            j++;
        }
        i++;
    }

    if(j > 1) return CMD_ERROR;
    
    sscanf(input, "%s%*s", buf);
    if(!strcmp(buf, "GET")) {
        if(j==0) return CMD_ERROR;
        return CMD_GET;
    }
    if(!strcmp(buf, "LIST")) {
        return CMD_LIST;
    }
    if(!strcmp(buf, "PUT")) {
        if(j==0) return CMD_ERROR;
        return CMD_PUT;
    }

    return CMD_ERROR;
}

// errors and exit
void printErr(int type) {
    switch(type) {
        case ERR_TYPE_USAGE:
            fprintf(stderr, "./dfc <configure file>\n");
            break;
        case ERR_TYPE_CONFIG:
            fprintf(stderr, "Error: failed to read configuration file\n");
            break;
        case ERR_TYPE_FORMAT:
            fprintf(stderr, "Error: configuration file: wrong format\n");
            break;
        case ERR_TYPE_SOCKET:
            fprintf(stderr, "Error: socket < 0\n");
            break;
    }
    exit(1);
}

// compute md5sum of the file
char md5hashFile(FILE* file, off_t fsize) {
    char* buffer;
    int buf_size, nb_read, i;
    unsigned char md5sum[MD5_DIGEST_LENGTH];
    MD5_CTX mdcxt;
    int num;

    buf_size = 1024;
    if(fsize > 2048) buf_size = 2048;

    buffer = malloc(buf_size);
    memset(buffer, 0, buf_size);

    MD5_Init(&mdcxt);
    while((nb_read = fread(buffer, 1, buf_size, file)))
        MD5_Update(&mdcxt, buffer, nb_read);

    if(!MD5_Final(md5sum, &mdcxt)) {
        fprintf(stderr, "Error: Failed to compute md5sum, set to 0...\n");
        free(buffer);
        rewind(file);
        return 0;
    }
    for(i = 0; i < MD5_DIGEST_LENGTH; i++) {
        //printf("%02x", md5sum[i]);
        num += md5sum[i];
    }
    //num = (long)strtol(md5sum, NULL, MD5_DIGEST_LENGTH);
    free(buffer);
    rewind(file);
    return num%4;
}

// get file name from input
char* getFileNameFromCmd(const char* command, int type) {
    int i;
    int start_at;
    int cmd_len;
    char* filename;

    start_at = 0;
    switch(type) {
        case CMD_PUT:
                start_at = 4;
            break;
        case CMD_GET:
                start_at = 4;
            break;
        case CMD_LIST:
                if(command[4] == '\0') return NULL;
                start_at = 5;
            break;
    }
    cmd_len = strlen(command);
    filename = malloc(cmd_len);
    memset(filename, 0, cmd_len);
    memcpy(filename, command+start_at, cmd_len);
    for(i = 0;;i++) {
        if(filename[i] == '\0') break;
        if(filename[i] == '\n') {
            filename[i] = '\0';
            break;
        }
    }
    return filename;
}

// get file size
int getFileSize(const char* filename, off_t* size) {
    struct stat buf;

    if(stat(filename, &buf)) return -1;

    *size = buf.st_size;
    return 0;
}

// close the socket file descriptors
// NOT USED
void close_resetConnfds() {
    int i;

    for(i = 0; i < 4; i++) {
        int fd = connfds[i];
        if(fd > 0) {
            //printf("close fd(%d)....\n", fd);
            close(fd);
            connfds[i] = -1;
        }
    }
}
