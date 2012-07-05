p2p
===

A peer-to-peer file sharing server written in C, and client written in Java.  Developed in collaboration with Justin Hill (https://github.com/justindhill).

p2p uses a centralized directory server approach.  Clients connect to the central server in order to retrieve a list of files which exist among peers in the network.  Once a client requests to download a file, the connection is negotiated between peers, and the client can begin to download the file.
