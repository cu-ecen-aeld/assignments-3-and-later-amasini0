#!/bin/bash

# Requires two arguments
if [[ $# -lt 2 ]]; then
    printf "error: too few arguments to script $0: 2 required, $# provided\n"
    exit 1
fi

# Name command line arguments
writefile=$1
writestr=$2

# Business logic
mkdir -p $(dirname $writefile)
echo $writestr > $writefile

if [[ $? -ne 0 ]]; then
    printf "error: failed to write on file $writefile"
    exit 1
fi

