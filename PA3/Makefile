CC=gcc
FLAGS_P=-pthread 
FLAGS_CRYPT=-lssl -lcrypto 
FLAGS_W=-Wextra -Wall
all:dfs dfc

dfs: dfs.o
	$(CC) $(FLAGS_W) $(FLAGS_P) dfs.o -o dfs

dfc: dfc.o
	$(CC) $(FLAGS_W) $(FLAGS_P) dfc.o -o dfc $(FLAGS_CRYPT)

dfs.o: dfs.c
	 $(CC) dfs.c -c
dfc.o: dfc.c
	 $(CC) dfc.c -c

clean:
	rm -rf dfc.o dfs.o dfc dfs 

