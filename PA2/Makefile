CC=gcc
FLAGS=-pthread -Wall

all:web_server

web_server: web_server.o
	$(CC) $(FLAGS) web_server.o -o web_server
web_server.o: web_server.c
	 $(CC) web_server.c -c

clean:
	rm -rf web_server.o web_server
