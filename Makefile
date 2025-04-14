#ο compiler
CC = gcc

#οι σημαίες για τα warnings, debugs
CFLAGS = -Wall -g

#make βασικός στόχος
all: fss_manager fss_console worker fss_script.sh

#manager με link στο utils
fss_manager: fss_manager.c utils.c
	$(CC) $(CFLAGS) -o fss_manager fss_manager.c utils.c

#console με link στο utils
fss_console: fss_console.c utils.c
	$(CC) $(CFLAGS) -o fss_console fss_console.c utils.c

#worker με link στο utils
worker: worker.c utils.c
	$(CC) $(CFLAGS) -o worker worker.c utils.c


#δικαιώματα εκτέλεσης στο script
fss_script.sh:
	chmod +x fss_script.sh

#εκτέλεση manager με στανταρ ορίσματα + -n 3 (worker_limit)
run: fss_manager
	./fss_manager -l manager_logfile -c config_file -n 3 

#εκτέλεση console με στανταρ όρισμα
console: fss_console
	./fss_console -l console_logfile

#καθαρισμός όλων των binaries και log_files
clean:
	rm -f fss_manager fss_console worker *.o manager_logfile console_logfile
