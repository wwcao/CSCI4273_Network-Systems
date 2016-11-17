#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/time.h>
#include <stdlib.h>
#include <memory.h>
#include <errno.h>
#include <dirent.h> // enable DIR* and dirent
#include <sys/types.h> // this and below for stat() and macro  S_ISREG(m)
#include <sys/stat.h>


// basic macros, type define, and data structures, same to client
#define MAXBUFFERSIZE 1024
#define PORT 5000

#define REQ_OK 			1
#define REQ_FAILED 		2

typedef char req_state;

typedef enum {
	EXIT, ANY, LS, GET, PUT
} command;

struct req_hdr {
	char cmd;
	req_state state;
	char msg[255];
};

struct data_hdr {
	char isLast;
	int size;
};

// function prototypes
int getconnection(int, const struct sockaddr_in);
int sendFile(int sockfd, FILE* readfrom, 
					const struct sockaddr_in remote, int len);
int recvFile(int sockfd, FILE* writeto, struct sockaddr *remote, int *sa_len);
void constructReqHeader(struct req_hdr *hdr, command cmd, req_state state, const char* msg, size_t msg_len);
command evalCmd(size_t *start_pt, const char *src, size_t src_len);
int lsSetupRoutine(FILE* file);
int recvReqHdr(int sockfd, struct req_hdr* hdr, size_t hdr_len , struct sockaddr* from_addr, socklen_t* sa_len);
int isFileWritable(const char* filename);

int main(int argc, char** argv) {
	struct sockaddr_in sin;
	int sockfd;
	
	if (argc != 2)
	{
		printf ("USAGE:  <port>\n");
		exit(1);
	}
	
	/********** From Example ********
	  This code populates the sockaddr_in struct with
	  the information about our socket
	 ******************/
	bzero(&sin,sizeof(sin));                    //zero the struct
	sin.sin_family = AF_INET;                   //address family
	sin.sin_port = htons(atoi(argv[1]));        //htons() sets the port # to network byte order
	sin.sin_addr.s_addr = INADDR_ANY;           //supplies the IP address of the local machine
	
	if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0))< 0)
	{
		printf("Unable to create socket");
		exit(0);
	}
	
	
	if(bind(sockfd, (struct sockaddr*)&sin, sizeof(sin)) < 0) {
		fprintf(stdout, "Unable to bind: %s", strerror(errno));
		exit(0);
	}
	
	printf("Running...\n");
	while(1) {
		
		if(!getconnection(sockfd, sin)) {
			break;
		}
		printf("---------------- Completed ----------------\n");
	}
	close(sockfd);
}

/*
 * start server routines after binding
 * 
 * Parameters: 	int sockfd, 
 * 				const struct sockaddr_in sin
 * 
 */
int getconnection(int sockfd, const struct sockaddr_in sin) {
	struct sockaddr_in remote;
	short doConnect = 1;
	int sa_len = sizeof(remote);
	int nbytes;
	size_t msg_startpt = 0;
	char buffer[MAXBUFFERSIZE];
	FILE *f;
	command cmd;
	struct req_hdr r_hdr;
	req_state rs = REQ_OK;
	char *msg;
	
	// initializing
	bzero(buffer,MAXBUFFERSIZE);
	bzero(&r_hdr, sizeof(r_hdr));
	
	// receive command
	nbytes = recvfrom(sockfd, buffer, MAXBUFFERSIZE, 0, 
		(struct sockaddr*)&remote,(socklen_t *)&sa_len);
	
	printf("Received packet from %s:%d\nData: %s\n", 
		inet_ntoa(remote.sin_addr), ntohs(remote.sin_port), buffer);
	
	// command evaluation and set a start point of the message that is in the command
	cmd = evalCmd(&msg_startpt,buffer, MAXBUFFERSIZE);
	msg = buffer+msg_startpt;
	switch(cmd) {
		case GET:
			// GET routine
			printf("> command: get\tpath: %s\n", msg);
			if(msg[0] != '/' || msg[0] != '.') {
				f = fopen(msg, "r+");
				if(!f) {
					printf("Failed to open %s\n", msg);
					rs = REQ_FAILED;
				}
				else {
					printf("Enable to open %s\n", msg);
				}
			}
			else {
				printf("Failed to open %s\n", msg);
				rs = REQ_FAILED;
			}
			
			constructReqHeader(&r_hdr, cmd, rs, msg, MAXBUFFERSIZE - msg_startpt);
			sendto(sockfd, &r_hdr, sizeof(r_hdr), 0, (struct sockaddr *)&remote, sa_len);
			
			if(f) {
				printf("---------- sent %d byte(s)----------\n", sendFile(sockfd, f, remote, sa_len));
				fclose(f);
			}
			
			break;
		case PUT:
			// PUT routine
			printf("> command: put\tpath: %s\n", msg);
			
			if(!isFileWritable(msg)) {
				printf("%s: tried to write to a non-regular file...\n", msg);
				rs = REQ_FAILED;
			}
			else {
				printf("Receiving file: %s\n", msg);
			}
			
			constructReqHeader(&r_hdr, cmd, rs, msg, MAXBUFFERSIZE - msg_startpt);
			sendto(sockfd, &r_hdr, sizeof(r_hdr), 0, (struct sockaddr *)&remote, sa_len);
			if(rs == REQ_FAILED) {
				break;
			}
			bzero(&r_hdr, sizeof(r_hdr));
			
			recvReqHdr(sockfd, &r_hdr, sizeof(r_hdr), (struct sockaddr*)&remote, (socklen_t*)&sa_len);
			if(r_hdr.state == REQ_OK) {
				f = fopen(r_hdr.msg, "w+");
				if(f) {
					printf("receiving data ... \n");
					printf("---------- received %d byte(s)----------\n", recvFile(sockfd, f, (struct sockaddr *)&remote, &sa_len));
					fclose(f);
				}
				else {
					// wouldn't reach here
					printf("failed to open file %s", r_hdr.msg);
				}
			}
			else {
				printf("Client: Terminate transmission...\n"); 
			}
			break;
		case LS:
			// LS routine
			printf("> command: ls\n");
			f = fopen(".dir_buf", "w+");
			lsSetupRoutine(f);
			
			if(!f) {
				rs = REQ_FAILED;
			}
			else {
				printf("Operating ls request...\n");
				lsSetupRoutine(f);
			}
			
			constructReqHeader(&r_hdr, cmd, rs, msg, MAXBUFFERSIZE - msg_startpt);
			sendto(sockfd, &r_hdr, sizeof(r_hdr), 0, (struct sockaddr *)&remote, sa_len);
			
			if(rs == REQ_OK) {
				
				sendFile(sockfd, f, remote, sa_len); 
				fclose(f);
			}
			
			break;
		case EXIT:
			// EXIT routine
			printf("> command: exit\n");
			printf("%s command is received.\n\nTerminating server\n", msg);
			constructReqHeader(&r_hdr, cmd, REQ_OK, msg, MAXBUFFERSIZE - msg_startpt);
			sendto(sockfd, &r_hdr, sizeof(r_hdr), 0, (struct sockaddr *)&remote, sa_len);
			// break while loop
			doConnect = 0;
			break;
		case ANY:
			// ECHO routine
			printf("> command: INVALID\n");
			constructReqHeader(&r_hdr, cmd, REQ_FAILED, msg, MAXBUFFERSIZE - msg_startpt);
			sendto(sockfd, &r_hdr, sizeof(r_hdr), 0, (struct sockaddr *)&remote, sa_len);
			break;
		default:
			// can not reach here...
			printf("> Error: %s\n", msg);
	}
	return doConnect;
}


/*
 * send udp data that read from readfrom with sockfd
 * parameters: 	int sockfd,
 * 			   	FILE* readfrom
 * 				const struct sockaddr remote
 * 				int sa_len
 * return: number of byte sent
 */
int sendFile(int sockfd, FILE* readfrom, const struct sockaddr_in remote, int sa_len) {
	FILE *log;
	char buffer[MAXBUFFERSIZE];
	int nbyte;
	int data_len;
	struct data_hdr d_hdr;
	size_t buf_size = sizeof(buffer);
	size_t hdr_size = sizeof(d_hdr);
	int counter;
	/*
	log=fopen(".log_server", "w+");
	if(log) {
		printf("server_log is open....\n");
	}
	*/
	counter = 0;
	do {
		nbyte = 0;
		d_hdr.isLast = 0;
		data_len = buf_size - hdr_size;
		bzero(buffer,buf_size);
		nbyte = fread(buffer+hdr_size,sizeof(char)*1,data_len,readfrom);
		d_hdr.size = data_len;
		if(nbyte < data_len) {
			d_hdr.isLast = 1;
			d_hdr.size = nbyte;
		}
			
		memcpy(buffer, &d_hdr, hdr_size);
		sendto(sockfd, buffer, buf_size, 0, 
				(struct sockaddr*)&remote, (socklen_t)sa_len);
		//fwrite(buffer+hdr_size, 1,d_hdr.size, log);
		counter += nbyte;
	}while(!feof(readfrom));
	
	// handle special case 
	// server doesn't need to handle null file
	if(nbyte == data_len) {
		d_hdr.isLast = 1;
		d_hdr.size = 0;
		bzero(buffer,buf_size);
		memcpy(buffer, &d_hdr, hdr_size);
		sendto(sockfd, buffer, buf_size, 0, 
				(struct sockaddr*)&remote, (socklen_t)sa_len);
	}
	
	//fclose(log);
	return counter;
}

/*
 * receive udp data with sockfd and write to file writeto (same to client recvFile(...))
 * parameters: 	int sockfd,
 * 			   	FILE* writeto
 * 				struct sockaddr* remote
 * 				int* sa_len
 * return: byte received
 */
int recvFile(int sockfd, FILE* writeto, struct sockaddr *remote, int *sa_len) {
	char buffer[MAXBUFFERSIZE];
	struct data_hdr d_hdr;
	size_t buf_size = sizeof(buffer);
	size_t hdr_size = sizeof(d_hdr);
	int counter = 0;
	
	do {
		bzero(buffer,buf_size);
		bzero(&d_hdr, hdr_size);
		recvfrom(sockfd, buffer, buf_size, 0, 
				remote, (socklen_t*)sa_len);
		memcpy(&d_hdr, buffer, hdr_size);
		if(!d_hdr.size) {
			break;
		}
		fwrite(buffer+hdr_size, 1, d_hdr.size, writeto);
		counter+=d_hdr.size;
		if(d_hdr.isLast) break;
	}while(1);
	return counter;
}

/*
 * Command Evaluation 
 * parameters: 	size_t *start_pt, 
 * 				const char *src, 
 * 				size_t src_len
 * return position of space +1 if get and put command;
 * 			zero otherwise
 */
 
command evalCmd(size_t *start_pt, const char *src, size_t src_len) {
	command cmd = ANY;
	
	// ls and exist command
	if(!strcmp(src, "ls")) { cmd = LS;}
	if(!strcmp(src, "exit")) { cmd = EXIT;}
	
	if(cmd == ANY) {
		// get and put command
		int i;
		for(i = 0; i < src_len; i++) {
			if(src[i] == ' ')
				break;
		}
		
		// space must be within 0-src_len
		if(i < src_len) {
			char src_buf[src_len];
			bzero(src_buf, src_len);
			memcpy(src_buf, src, i);
			if(!strcmp(src_buf, "get")) {
				cmd = GET;
				*start_pt = i+1;
			}
			if(!strcmp(src_buf, "put")) {
				cmd = PUT;
				*start_pt = i+1;
			}
		}
	}
	
	return cmd;
}

/*
 * construct request header
 * parameters: 	struct req_hdr *hdr, 
 * 				command cmd, 
 * 				req_state state, 
 * 				const char* msg, 
 * 				size_t msg_len
 * return void
 */
void constructReqHeader(struct req_hdr *hdr, command cmd, req_state state, const char* msg, size_t msg_len) {
	// NOT sure why data of msg is gone after memcpy
	memcpy(hdr->msg, msg, msg_len);
	hdr->cmd = cmd;
	hdr->state = state;
}

/*
 * search the working directory for regular file; write the result into file
 * and set cursor position to 0
 * parameters: FILE* file
 * 
 * return number of regular files
 */
int lsSetupRoutine(FILE* file) {
	DIR *dir;
	struct dirent *d_entry;
	char *filename;
	int counter;
	counter = 0;
	dir = opendir("./"); // open current working directory 
	if(dir) {
		while(d_entry = readdir(dir)) {
			struct stat f_stat;
			filename = d_entry->d_name;
			if(filename && *filename == '.') continue; //  skip hidden files
			bzero(&f_stat, sizeof(struct stat));
			stat(d_entry->d_name, &f_stat);
			if(!S_ISREG(f_stat.st_mode)) continue; // skip ! regular files
			fputs(d_entry->d_name, file);  // write result to file and add trailing new line
			fputs("\n", file);
			counter++;
		}
	}
	rewind(file);
	return counter;
}

/*
 * check file, filename, is existed or is regular file
 * parameters: const char* filename
 * 
 * return 	1 if file is not exist or is regular file;
 * 			0 otherwise
 */
int isFileWritable(const char* filename) {
	struct stat f_stat;

	if(stat(filename, &f_stat))
		return 1;
	
	if(S_ISREG(f_stat.st_mode)) 
		return 1;
		
	return 0;
}

/*
 * receive the request header from server after a command is sent
 * parameters: 	int sockfd, 
 * 				struct req_hdr* hdr, 
 * 				size_t hdr_len , 
 * 				struct sockaddr* from_addr, 
 * 				socklen_t* sa_len
 * 
 * return: value from recvfrom
 */
int recvReqHdr(int sockfd, struct req_hdr* hdr, size_t hdr_len , struct sockaddr* from_addr, socklen_t* sa_len) {
	int nbytes;
	bzero(hdr, hdr_len);
	nbytes = recvfrom(sockfd, (void*)hdr, hdr_len, 0, from_addr, sa_len);
	return nbytes;
}
