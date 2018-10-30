# project2

## Distributed Membership

### Author: Kyle Fauerbach

For general use, code can be run as:

`./prj2 -p <port> -h <host file>`

Scripts for the 4 test cases are in `test<n>.sh`. The scripts rely on the `dist_docker` docker image which can be built using the provided Dockerfile. The top of each test script has commented lines to specify the building of the docker image and setting up the private network for the containers. For testing purposes these container are given garbage host names, which can be found in the `hosts_local` file. 

To implement the tests independently, there is the optional `-t` flag along with a number for which test case. If the `-t` flag is not included then all functionality will be enabled.

`./prj2 -p <port> -h <host file> -t <1-4>`

`-t 1`: disables all parts of the failure detector

`-t 2`: turns on the failure detector, but membership is not changed

`-t 3`: membership is altered according to failure detector

`-t 4`: enables support for leader crashes, instructs process 2 to ignore the req for process 4's join to force pending operation recovering on leader crash.

If there is a leader crash but there is no pending operation, nothing will explicity be printed to the screen regarding the new view. Each process will know the leader has failed and who the new leader will be automatically. They will quietly remove the old leader from membership and update their own view id.

Similarly, each process will verify their current leader id whenever they receive a new view. This is to ensure that processes that were trying to join in the middle of a leader crash are aware of the new leader.

Verboseness of the logging can be change by updating the `LOG_LEVEL` macro in `main.c`. Possible values for this are `DEBUG`, `INFO`, and `SILENT` and are explained in more detail in `logging.h`.