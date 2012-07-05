/*
	Authors: Justin Hill, Gordon Keesler, Matt Layher
	Date:    3/21/12
	Updated: 4/1/12
	Project: p2pd
	Module:	 functions.h

	Description:
	A header containing prototypes used in functions.c
*/

//------------------------ C LIBRARIES -----------------------

#include <netinet/in.h>

//------------------------ PROTOTYPES ------------------------

// Prototype for client_count(), which keeps track of the number of clients connected
int client_count(int);

// Prototype for clean_string(), which parses out all bad input characters from a string in memory
void clean_string(char *);

// Prototype for console_help(), which prints out all console command information
void console_help();

// Prototype for get_in_addr() (borrowed from Beej's Guide to Network Programming)
// Used to get IP addresses in string format
void *get_in_addr(struct sockaddr *);

// Prototype for recv_msg(), a wrapper for the recv() system call
int recv_msg(int, char *);

// Prototype for send_msg(), a wrapper for the send() system call
int send_msg(int, char *);

// Prototype for validate_int(), which checks if a string is a valid integer
int validate_int(char *);
