#!/bin/sh
set -e

./check.py
./write.py idm 012E0123456789AB
./write.py sys 88B4
./write.py ser 0000 000B

# clear data blocks 0-B (block numbers are hex)
for b in 0 1 2 3 4 5 6 7 8 9 A B; do
    ./write.py $b 00000000000000000000000000000000
done
