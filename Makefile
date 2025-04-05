CC = gcc
CFLAGS = -Wall -g

all: fss_manager fss_console

fss_manager: fss_manager.c utils.c
	$(CC) $(CFLAGS) -o fss_manager fss_manager.c utils.c

fss_console: fss_console.c
	$(CC) $(CFLAGS) -o fss_console fss_console.c

run: fss_manager
	./fss_manager -l manager.log -c config.txt -n 5

console: fss_console
	./fss_console

clean:
	rm -f fss_manager fss_console *.o manager.log
