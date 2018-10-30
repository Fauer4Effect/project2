
sudo docker run \
    --net mynet \
    --ip "192.168.69.5" \
    --hostname foo.bar.1 \
    --add-host foo.bar.2:192.168.69.6 \
    --add-host foo.bar.3:192.168.69.7 \
    --add-host foo.bar.4:192.168.69.8 \
    --name cont1 \
    -a stdout -a stderr -a stdin \
    dist_docker \
    ./test -p 55555 -h hosts_local -t 4 &

sleep 5

sudo docker run \
    --net mynet \
    --ip "192.168.69.6" \
    --hostname foo.bar.2 \
    --add-host foo.bar.1:192.168.69.5 \
    --add-host foo.bar.3:192.168.69.7 \
    --add-host foo.bar.4:192.168.69.8 \
    --name cont2 \
    -a stdout -a stderr -a stdin \
    dist_docker \
    ./test -p 55555 -h hosts_local -t 4 &

sleep 7

sudo docker run \
    --net mynet \
    --ip "192.168.69.7" \
    --hostname foo.bar.3 \
    --add-host foo.bar.1:192.168.69.5 \
    --add-host foo.bar.2:192.168.69.6 \
    --add-host foo.bar.4:192.168.69.8 \
    --name cont3 \
    -a stdout -a stderr -a stdin \
    dist_docker \
    ./test -p 55555 -h hosts_local -t 4 &

sleep 10

sudo docker run \
    --net mynet \
    --ip "192.168.69.8" \
    --hostname foo.bar.4 \
    --add-host foo.bar.1:192.168.69.5 \
    --add-host foo.bar.2:192.168.69.6 \
    --add-host foo.bar.3:192.168.69.7 \
    --name cont4 \
    -a stdout -a stderr -a stdin \
    dist_docker \
    ./test -p 55555 -h hosts_local -t 4 &

sleep 10

sudo docker container kill cont1

sleep 15

sudo docker container kill cont2

sleep 15

sudo docker container kill $(sudo docker ps -q | cut -d " " -f 1)
sudo docker container prune -f
