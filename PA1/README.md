4273 Network System
Program Assignment 1
by Weipeng Cao
SID: 810833265
09/19/2016

Create a server program and a client program with UNIX UDP socket. The sever and client are able to communicate each other by command entered on client side. The server should evaluate the command and respond correctly with operation GET, PUT, LS, EXIT; both server and client need to receive/send data based on the operation. client need to print out the message from server when command is not able to be evaluated.

------ Sever Operation ------
GET <file>: open file, read all of conten, and sen
PUT <file>: create/truncate file and wait for data until all of data are transmited 
			note: empty file can be created if file is not exist in the client directory
EXIT: terminate the program
default: echo the command to client and request state set to REQ_FAILED

------ Client Operation ------ 
GET <file>: REQ_OK, receive all content of file
			REQ_FAILED, display error message
PUT <file>: REQ_OK, send all content of file; if file is not exist, empty file will be created 
					in server
			REQ_FAILED, display error message
EXIT: terminate server
default: print ount message from server

------ prepare to make ------ 
None

------ Makefile ------ 
make: make both server and client execution
make server or make client: make individual execution
make clean: remove client, server, and buffer file of server

------ Run ------ 
server: ./server <port>
client: ./client <ip> <port>

