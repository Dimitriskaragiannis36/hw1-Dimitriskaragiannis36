#!/bin/bash

print_usage() {
    echo "Usage: $0 -p <logfile|directory> -c <purge|listAll|listMonitored|listStopped>"
}

#ελέγχω τα ορίσματα
while getopts "p:c:" opt; do
  case $opt in
    p) path="$OPTARG" ;;
    c) command="$OPTARG" ;;
    *) print_usage; exit 1 ;;
  esac
done

if [ -z "$path" ] || [ -z "$command" ]; then
  echo "Usage: $0 -p <path> -c <command>"
  exit 1
fi

if [ -d "$path" ] && [ "$command" != "purge" ]; then
    echo "Error: Can only run 'purge' with directory paths"
    exit 1
fi

case "$command" in
    purge)
        echo "Deleting $path..."
        if [ -d "$path" ] || [ -f "$path" ]; then
            rm -rf "$path"
            echo "Purge complete."
        else
            echo "Error: '$path' not found."
        fi
        ;;

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
        declare -A monitored

        while IFS= read -r line; do
            if [[ "$line" =~ Monitoring\ started\ for\ (.+) ]]; then
                dir="${BASH_REMATCH[1]}"
                monitored["$dir"]=1
            elif [[ "$line" =~ Monitoring\ stopped\ for\ (.+) ]]; then
                dir="${BASH_REMATCH[1]}"
                unset monitored["$dir"]
            fi
        done < "$path"

        for dir in "${!monitored[@]}"; do
            sync_line=$(grep "Sync completed $dir" "$path" | tail -1)
            last_sync=$(echo "$sync_line" | grep -oP '^\[\K[^]]+')
            target=$(echo "$sync_line" | awk -F'->' '{print $2}' | cut -d' ' -f2)
            echo "$dir -> $target [Last Sync: $last_sync]"
        done
        ;;

    listStopped)
        declare -A last_state

        # Περνάμε από το log και θυμόμαστε την τελευταία κατάσταση για κάθε dir
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

    *)
        echo "Invalid command: $command"
        print_usage
        ;;
esac



