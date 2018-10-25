#!/bin/bash

sudo docker run \
    --net mynet \
    --ip "192.168.69.5" \
    --hostname "foo.bar.1" \
    -a stdout -a stderr -a stdin \
    dist_docker \
    ./test -p 55555 -h hosts_local &

sudo docker run \
    --net mynet \
    --ip "192.168.69.6" \
    --hostname "foo.bar.2" \
    -a stdout -a stderr -a stdin \
    dist_docker \
    ./test -p 55555 -h hosts_local &

