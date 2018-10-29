
sudo docker run \
    --net mynet \
    --ip "192.168.69.5" \
    --hostname foo.bar.1 \
    --add-host foo.bar.2:192.168.69.6 \
    --add-host foo.bar.3:192.168.69.7 \
    -a stdout -a stderr -a stdin \
    dist_docker \
    ./test -p 55555 -h hosts_local -t 1 &

sleep 5

sudo docker run \
    --net mynet \
    --ip "192.168.69.6" \
    --hostname foo.bar.2 \
    --add-host foo.bar.1:192.168.69.5 \
    --add-host foo.bar.3:192.168.69.7 \
    -a stdout -a stderr -a stdin \
    dist_docker \
    ./test -p 55555 -h hosts_local -t 1 &


sleep 10

sudo docker container kill $(sudo docker ps -q | cut -d " " -f 1)
