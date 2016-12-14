CC=gcc
FLAGS_P=-pthread
FLAGS_CRYPT=-lssl -lcrypto 
CFLAGS=-Wall

all:webproxy

webproxy: webproxy.o
	$(CC) $(CFLAGS) $(FLAGS_P) webproxy.o -o webproxy $(FLAGS_CRYPT)
webproxy.o: webproxy.c
	 $(CC) webproxy.c -c

clean:
	rm -rf webproxy.o webproxy
