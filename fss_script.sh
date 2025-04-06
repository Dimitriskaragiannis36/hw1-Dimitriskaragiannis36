#!/bin/bash

#ελέγχω τα ορίσματα
while getopts "p:c:" opt; do
  case $opt in
    p) path="$OPTARG" ;;
    c) command="$OPTARG" ;;
    *) echo "Usage: $0 -p <path> -c <command>"; exit 1 ;;
  esac
done

if [ -z "$path" ] || [ -z "$command" ]; then
  echo "Usage: $0 -p <path> -c <command>"
  exit 1
fi

case "$command" in
    purge)
        echo "Purging $path..."
        if [ -d "$path" ] || [ -f "$path" ]; then
            rm -rf "$path"
            echo "Purge complete."
        else
            echo "Error: '$path' not found."
        fi
        ;;

    listAll)
        awk '
        /\[.*\] \[\/.*\] \[\/.*\] \[[0-9]+\] \[.*\]/ {
            source = $2
            target = $3
            timestamp = $1 " " $2
            getline
            status = $1
            print source, "->", target, "[Last Sync:", timestamp, "]", "[" status "]"
        }
        ' "$path"
        ;;

    listMonitored)
        grep "Monitoring started for" "$path" | while read -r line; do
            dir=$(echo "$line" | grep -oP "/home/[^ ]+")
            line=$(grep "Sync completed $dir" "$path" | tail -n 1)
            last_sync=$(echo "$line" | grep -oP '\[\K[^\]]+')
            target=$(echo "$line" | awk -F'-> ' '{print $2}' | awk '{print $1}')
            echo "$dir -> $target [Last Sync: $last_sync]"
        done
        ;;

    listStopped)
        grep "Command cancel|Monitoring stopped" "$path" | while read -r line; do
            dir=$(echo "$line" | grep -oP "/home/[^ ]+")
            target=$(grep -m 1 "$dir" "$path" | grep -oP -- "-> /backup/[^ ]+")
            last_sync=$(grep -B1 "$dir -> $target" "$path" | grep "Sync completed" | tail -1 | cut -d']' -f1 | tr -d '[')
            echo "$dir -> $target [Last Sync: $last_sync]"
        done
        ;;

    *)
        echo "Invalid command: $command"
        print_usage
        ;;
esac



