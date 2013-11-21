#!/bin/bash -e
if [ -z "$2" ]; then
echo "Useage: $0 dir1 dir2"
exit 1
fi
./stop.sh || true
./start.sh
for i in {1..100}; do
./test-lab-3-a $1 $2
./test-lab-3-b $1 $2
done
