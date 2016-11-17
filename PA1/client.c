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

// basic macros, type define, and data structures, same to server
#define MAXBUFFERSIZE 1024
#define MAXCMDSIZE 256

#define REQ_OK 			1
#define REQ_FAILED 		2

// define types
typedef char req_state;

typedef enum {
	EXIT, ANY, LS, GET, PUT
} command;

// data structures
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
int sendCmd(int sockfd, char *cmd, size_t cmd_size, 
				const struct sockaddr* s, socklen_t sa_len);
int clientProcedure(int sockfd, char *cmd);
int recvReqHdr(int sockfd, struct req_hdr* hdr, size_t hdr_len , struct sockaddr* from_addr, socklen_t* sa_len);
int recvFile(int sockfd, FILE* writeto, struct sockaddr *remote, int *sa_len);
int sendFile(int sockfd, FILE* readfrom, 
					const struct sockaddr_in remote, int len);
					void constructReqHeader(struct req_hdr *hdr, command cmd, req_state state, const char* msg, size_t msg_len);
void trimCmd(char* cmd, size_t len);

int main(int argc, char** argv) {
	
	struct sockaddr_in sin;
	char cmd[MAXCMDSIZE];
	int sockfd;
	
	if (argc != 3)
	{
		printf ("USAGE: <ip> <port>\n");
		exit(1);
	}
	
	/********** From Example ********
	  This code populates the sockaddr_in struct with
	  the information about our socket
	 ******************/
	bzero(&sin,sizeof(sin));                    //zero the struct
	sin.sin_family = AF_INET;                   //address family
	sin.sin_port = htons(atoi(argv[2]));
	sin.sin_addr.s_addr = inet_addr(argv[1]); //sets remote IP address

	//Causes the system to create a generic socket of type UDP (datagram)
	if ((sockfd = socket(PF_INET, SOCK_DGRAM,0)) < 0)
	{
		printf("unable to create socket");
		exit(1);
	}
	
	fflush(stdout);
	
	// start client routines
	while(1) {
		
		printf("> ");
		bzero(cmd, MAXCMDSIZE);
		fgets(cmd, MAXCMDSIZE, stdin);
		trimCmd(cmd, MAXCMDSIZE);
		sendCmd(sockfd, cmd, sizeof(cmd), (struct sockaddr *)&sin, sizeof(sin));
		clientProcedure(sockfd, cmd);
		fflush(stdout);
		//printf("\n");
		
	}
	
	close(sockfd);
	return 0;
}

/*
 * Send command entered 
 * 
 * return: always 0 (not used) 
 */
int sendCmd(int sockfd, char *cmd, size_t cmd_size, 
				const struct sockaddr* s, socklen_t sa_len) {
	sendto(sockfd, cmd, cmd_size, 0, 
				s, sa_len);
	return 0;
}

/*
 * Operate client procedures: send command, receive request header, analyze state of request
 * and match operation evaluated
 * Parameters: 	int sockfd, 
 * 				char *cmd
 * return: always 0 (not used since client is not required termation by command 'exit')
 */
int clientProcedure(int sockfd, char *cmd) {
	int res = 0;
	struct req_hdr hdr;
	struct sockaddr_in remote;
	int sa_len = sizeof(remote);
	FILE *f;
	char *msg;
	size_t msg_len;
	recvReqHdr(sockfd, &hdr, sizeof(hdr), (struct sockaddr*)&remote, (socklen_t*)&sa_len);
	//printf("%d, %d\n", hdr.cmd, hdr.state);
	
	switch(hdr.cmd) {
		
		case GET:
			// GET routine
			printf("Server > get: %s\n", hdr.msg);
			if(hdr.state == REQ_FAILED) {
				printf("server > Failed to open %s\n", hdr.msg);
				break;
			}
			printf("GET: ------OK------\n");
			f = fopen(hdr.msg, "w+");
			if(!f) {
				printf("local > Failed to open %s\n", hdr.msg);
				break;
			}
			
			printf("---------- received %d bytes ----------\n", recvFile(sockfd, f, (struct sockaddr *)&remote, &sa_len));
			fclose(f);
			break;
			
		case PUT:
			// PUT routine
			printf("Server > put: %s\n", hdr.msg);
			if(hdr.state == REQ_FAILED) {
				printf("server > Failed to open %s\n", hdr.msg);
				break;
			}
			
			printf("PUT: ------OK------\n");
			
			f = fopen(hdr.msg, "r");
			if(!f) {
				printf("local > Failed to open %s\n", hdr.msg);
				hdr.state = REQ_FAILED;
			}
			
			sendto(sockfd, &hdr, sizeof(hdr), 0, (struct sockaddr *)&remote, sa_len);
			
			if(hdr.state==REQ_OK) {
				printf("---------- sent %d bytes ----------\n", sendFile(sockfd, f, remote, sa_len));
				fclose(f);
			}
			
			break;
		case EXIT:
			// EXIT routine
			printf("Server > %s\n", hdr.msg);
			break;
		case ANY:
			printf("Server > %s\n", hdr.msg);
			break;
		case LS:
			// LS routine
			if(hdr.state == REQ_OK){
				printf("files in current directory:\n");
				printf("--------------------\n");
				recvFile(sockfd, stdout, (struct sockaddr *)&remote, &sa_len);
				printf("--------------------\n\n");
			}
			else {
				printf("Server> Command ls failed...\n");
			}
			break;
		default:
			// won't reach here
			printf("Error: %d, %s\n", hdr.cmd, hdr.state==REQ_OK?"OK":"FAILED");
	}
	return res;
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


/*
 * remove return charactor of an input
 * 
 * return none
 */
void trimCmd(char* cmd, size_t len) {
	int i;
	for(i = 0; i < len; i++) {
		if(cmd[i]=='\n'){
			cmd[i] = '\0';
			break;
		}
	}
}


/*
 * Send the content of a open file readfrom to address stored in remote
 * parameters: 	int sockfd, 
 * 				FILE* readfrom, 
 * 				const struct sockaddr_in remote, 
 * 				int sa_len
 * return number of byte sent
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
	
	//initialize local variables
	nbyte = 0;
	data_len = 0;
	d_hdr.isLast = 0;
	counter = 0;
	
	if(readfrom) {
		// send file
		do {
			
			data_len = buf_size - hdr_size;
			bzero(buffer,buf_size);
			nbyte = fread(buffer+hdr_size,sizeof(char)*1,data_len,readfrom);
			d_hdr.size = data_len;
			if(nbyte < data_len) {
				// reach to end of file
				d_hdr.isLast = 1;
				d_hdr.size = nbyte;
			}
			
			memcpy(buffer, &d_hdr, hdr_size);
			sendto(sockfd, buffer, buf_size, 0, 
					(struct sockaddr*)&remote, (socklen_t)sa_len);
			counter += d_hdr.size;
			nbyte = 0;
		}while(!feof(readfrom));
	}
	
	// Handle special case nbyte == data_len and 
	// Null file
	if(nbyte == data_len) {
		d_hdr.isLast = 1;
		d_hdr.size = 0;
		bzero(buffer,buf_size);
		memcpy(buffer, &d_hdr, hdr_size);
		sendto(sockfd, buffer, buf_size, 0, 
				(struct sockaddr*)&remote, (socklen_t)sa_len);
	}
	
	return counter;
}


/*
 * receive udp data with sockfd and write to file writeto
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
	
	// write to file
	do {
		bzero(buffer,buf_size);
		bzero(&d_hdr, hdr_size);
		recvfrom(sockfd, buffer, buf_size, 0, 
				remote, (socklen_t*)sa_len);
		memcpy(&d_hdr, buffer, hdr_size);
		if(!d_hdr.size) break; // no content
		fwrite(buffer+hdr_size, 1, d_hdr.size, writeto);
		counter+=d_hdr.size;
		if(d_hdr.isLast) break; // no more coming packets
	}while(1);
	return counter;
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
	memcpy(hdr->msg, msg, msg_len);
	hdr->cmd = cmd;
	hdr->state = state;
}

