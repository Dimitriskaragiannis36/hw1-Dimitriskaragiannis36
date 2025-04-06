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

