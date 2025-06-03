#!/bin/bash

# Requires two arguments
if [[ $# -lt 2 ]]; then
    printf "error: too few arguments to script $0: 2 required, $# provided\n"
    exit 1
fi

# Name command line arguments
filesdir=$1
searchstr=$2

# Check that filesdir is a valid directory
if [[ ! -d $filesdir ]]; then
    printf "error: $filesdir is not a valid directory name\n"
    exit 1
fi

# Business logic
numfiles=$(find $filesdir/ -type f | wc -l)

numlines=0
for file in $(find $filesdir/ -type f ); do
    lines=$(grep --count $searchstr $file)
    count=$((numlines + lines))
    numlines=$count
done

printf "The number of files are $numfiles and the number of matching lines are $numlines\n"

