CC = gcc
CFLAGS = -Wall -g

all: fss_manager fss_console fss_script.sh

fss_manager: fss_manager.c utils.c
	$(CC) $(CFLAGS) -o fss_manager fss_manager.c utils.c

fss_console: fss_console.c utils.c
	$(CC) $(CFLAGS) -o fss_console fss_console.c utils.c

fss_script.sh:
	chmod +x fss_script.sh

run: fss_manager
	./fss_manager -l manager_logfile -c config_file -n 2 

console: fss_console
	./fss_console -l console_logfile

clean:
	rm -f fss_manager fss_console *.o manager_logfile console_logfile
