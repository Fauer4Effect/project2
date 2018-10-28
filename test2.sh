#!/bin/bash

echo "Starting leader"

sudo docker run \
    --net mynet \
    --ip "192.168.69.5" \
    --hostname foo.bar.1 \
    --add-host foo.bar.2:192.168.69.6 \
    --add-host foo.bar.3:192.168.69.7 \
    --name cont1 \
    --rm \
    dist_docker \
    ./test -p 55555 -h hosts_local &

echo "Waiting for second process"
sleep 2

sudo docker run \
    --net mynet \
    --ip "192.168.69.6" \
    --hostname foo.bar.2 \
    --add-host foo.bar.1:192.168.69.5 \
    --add-host foo.bar.3:192.168.69.7 \
    --name cont2 \
    --rm \
    dist_docker \
    ./test -p 55555 -h hosts_local &

echo "Waiting for third process"
sleep 2

sudo docker run \
    --net mynet \
    --ip "192.168.69.7" \
    --hostname foo.bar.3 \
    --add-host foo.bar.1:192.168.69.5 \
    --add-host foo.bar.2:192.168.69.6 \
    --rm \
    --name cont3 \
    dist_docker \
    ./test -p 55555 -h hosts_local &

sleep 7
echo "Kill process 3"
sudo docker container kill cont3

sleep 10
echo "kill process 2"
sudo docker container kill cont2

sleep 15

sudo docker container kill `sudo docker ps | cut -d " " -f 1`
