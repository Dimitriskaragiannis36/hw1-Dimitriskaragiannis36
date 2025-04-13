#!/bin/bash

#συνάρτηση σφάλματος
print_usage() {
    echo "Usage: $0 -p <logfile|directory> -c <purge|listAll|listMonitored|listStopped>"
}

#ελέγχω τα ορίσματα για ./fss_script.sh -p <path> -c <command>
while getopts "p:c:" opt; do
  case $opt in
    p) path="$OPTARG" ;; #ανάθεση τιμής
    c) command="$OPTARG" ;;
    *) print_usage; exit 1 ;;
  esac
done

#εάν έστω και ένα από τα 2 δεν δοθεί
if [ -z "$path" ] || [ -z "$command" ]; then
  echo "Usage: $0 -p <path> -c <command>"
  exit 1
fi

#αν έχω φάκελο για path, θα ακολουθεί purge οπωσδήποτε
if [ -d "$path" ] && [ "$command" != "purge" ]; then
    echo "Error: Can only run 'purge' with directory paths"
    exit 1
fi

#ένα switch case για τις 4 διαφορετικές εντολές
case "$command" in
    purge)
        echo "Deleting $path..."
        if [ -d "$path" ] || [ -f "$path" ]; then
            rm -rf "$path"  #διαγράφω
            echo "Purge complete."
        else
            echo "Error: '$path' not found."
        fi
        ;;
                #awk για parsing, timestamp, source, target κτλ
                #gensub αντικαθιστά την κανονική έκφραση με κείμενο
                #getline η επόμενη γραμμή
                #print για να εκτυπώσει
    listAll)
        awk '
        /^\[[0-9]{4}-[0-9]{2}-[0-9]{2}/ && /\[[\/]/ && /FULL|ADDED|MODIFIED|DELETED/ {
            timestamp = gensub(/^\[([^\]]+)\].*/, "\\1", "g", $0)
            source = gensub(/^.*\[([^\]]+)\] \[([^\]]+)\].*/, "\\1", "g", $0)
            target = gensub(/^.*\[([^\]]+)\] \[([^\]]+)\].*/, "\\2", "g", $0)
            getline
            status = gensub(/^\[([A-Z]+)\].*/, "\\1", "g", $0)
            print source " -> " target " [Last Sync: " timestamp "] [" status "]"
        }
        ' "$path"
        ;;

    listMonitored)
        declare -A monitored    #πίνακας συσχετισμού (piazza) 
                                #IFS έτσι ώστε το read να μη σπάει τη γραμμή σε λέξεις με βάση κενά/tabs
        while IFS= read -r line; do  #-r για να διαάσει την γραμμή όπως είναι
            if [[ "$line" =~ Monitoring\ started\ for\ (.+) ]]; then
                dir="${BASH_REMATCH[1]}"  #=~για regex matching
                monitored["$dir"]=1     #ο φάκελος παρακολουθείται
            elif [[ "$line" =~ Monitoring\ stopped\ for\ (.+) ]]; then
                dir="${BASH_REMATCH[1]}"
                unset monitored["$dir"]  #ο φάκελος σταμάτησε να παρακολουθείται
                                        #τον βγάζω από τον πίνακα
            fi
        done < "$path"

        for dir in "${!monitored[@]}"; do #παίρνει όλα τα κλειδιά 
            #grep για να βρει όλες τις γραμμές που περιέχουν
            #tail από το τέλος -1 την τελευταία
            sync_line=$(grep "Sync completed $dir" "$path" | tail -1)
            #regex για να βγάλω το timestamp
            last_sync=$(echo "$sync_line" | grep -oP '^\[\K[^]]+')
            #-F για διαχωρισμό γραμμής για να πάρω 2ο πεδίο
            #cut -d' ' -f2 εξαγωγή 2ου πεδίου
            target=$(echo "$sync_line" | awk -F'->' '{print $2}' | cut -d' ' -f2)
            echo "$dir -> $target [Last Sync: $last_sync]"
        done
        ;;

    listStopped)
        declare -A last_state  #πίνακας συσχετισμού (piazza) 

        #περνάμε από το log και θυμόμαστε την τελευταία κατάσταση για κάθε dir
        while IFS= read -r line; do
            if [[ "$line" =~ Monitoring\ started\ for\ (.+) ]]; then
                dir="${BASH_REMATCH[1]}"
                last_state["$dir"]="started"
            elif [[ "$line" =~ Monitoring\ stopped\ for\ (.+) ]]; then
                dir="${BASH_REMATCH[1]}"
                last_state["$dir"]="stopped"
            fi
        done < "$path"

         for dir in "${!last_state[@]}"; do
            if [ "${last_state[$dir]}" = "stopped" ]; then
                sync_line=$(grep "Sync completed $dir" "$path" | tail -1)
                last_sync=$(echo "$sync_line" | grep -oP '^\[\K[^]]+')
                target=$(echo "$sync_line" | awk -F'->' '{print $2}' | cut -d' ' -f2)
                echo "$dir -> $target [Last Sync: $last_sync]"
            fi
        done
        ;;
            #μήνυμα σφάλματος για άκυρη εντολή
    *)
        echo "Invalid command: $command"
        print_usage
        ;;
esac



