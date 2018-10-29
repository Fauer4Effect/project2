# Select base image
FROM ubuntu:16.04
# FROM centos:7.4.1708

#ENV https_proxy=<proxy>
#ENV http_proxy= <proxy>

# Set the current working directory
WORKDIR /home

# Expose out default ports for testing
EXPOSE 55555
EXPOSE 44444

# Update the system, download any packages essential for the project
RUN apt-get update && apt-get upgrade -y
RUN apt-get install -y git build-essential make gcc vim net-tools iputils-ping

# Download and build application code
RUN git clone https://github.com/Fauer4Effect/project2
WORKDIR /home/project2
RUN make

# Import any additional files into the environment (from the host)
ADD hosts_local .
