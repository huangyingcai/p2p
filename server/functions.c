/*
	Authors: Justin Hill, Gordon Keesler, Matt Layher
	Date:    3/21/12
	Updated: 4/1/12
	Project: p2pd
	Module:  functions.c

	Description:
	A collection of helper functions and wrappers for various functions used by the server
*/

//------------------------ C LIBRARIES -----------------------

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//------------------------ CUSTOM LIBRARIES ------------------

#include "config.h"
#include "functions.h"

//------------------------ GLOBAL VARIABLES ------------------

// Client count, declared global so it may be manipulated via function and have only instance of itself
static int c_count = 0;

//------------------------ CLEAN STRING ----------------------

// clean_string() allows us to remove bad input characters from a string in memory
void clean_string(char *str)
{
	// Generic indexer variable
	int i = 0;

	// Create index to keep track of place in buffer
	int index = 0;

	// Keep buffer to copy in good characters
	char buffer[1024];

	// Iterate the string, removing any backspaces, newlines, and carriage returns
	for(i = 0; i < strlen(str); i++)
	{
		if(str[i] != '\b' && str[i] != '\n' && str[i] != '\r')
			buffer[index++] = str[i];
	}

	// Nullify the original input string
	memset(str, 0, sizeof(str));

	// Null terminate the buffer
	buffer[index] = '\0';

	// Copy the buffer back into the main string
	strcpy(str, buffer);
}

//------------------------ CLIENT COUNT ---------------------

// client_count() allows us to modify the client count for the server, and to simply return its value
//	1 - add one client
//	0 - return number of clients
//  -1 - remove one client
int client_count(int change)
{
	// Modify client counter by using change integer, return its value
	c_count += change;
	return c_count;
}

//------------------------ CONSOLE HELP ----------------------

// console_help() displays usage and information for console commands
void console_help()
{
	fprintf(stdout, "%s console commands:\n", SERVER_NAME);
	fprintf(stdout, "\tclear - clear the console\n");
	fprintf(stdout, "\t help - display available console commands\n");
	fprintf(stdout, "\t stat - display a quick server statistics summary\n");
	fprintf(stdout, "\t stop - terminate the server\n");
}

//------------------------ GET IN ADDR -----------------------

// get_in_addr() borrowed from Beej's Guide to Network Programming: http://beej.us/guide/bgnet/output/html/multipage/index.html
// This function allows us to easily create a string with the client's IP address
void *get_in_addr(struct sockaddr *sa)
{
        // In the case of IPv4, return the IPv4 address, else return the IPv6 address
        if (sa->sa_family == AF_INET)
                return &(((struct sockaddr_in*)sa)->sin_addr);
        else
                return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

//------------------------ RECV MSG --------------------------

// recv_msg() takes a file descriptor and a message, and abstracts the recv() sockets call
int recv_msg(int fd, char *message)
{
	// Keep track of number of bytes received, and total bytes received
	int b_received = 0;
	int b_total = 0;

	// Keep a buffer to gather input from recv() and copy it into the message
	char buffer[1024];

	// Nullify the input buffer before receiving data
	memset(buffer, '\0', sizeof(buffer));

	// Perform the recv() socket call, but handle the length and null termination for us, return the number of bytes
	b_received = recv(fd, buffer, sizeof(buffer), 0);
	b_total += b_received;

	// Copy buffer into message
	strcpy(message, buffer);

	// Return total bytes received
	return b_total;
}

//------------------------ SEND MSG --------------------------

// send_msg() takes a file descriptor and a message, and abstracts the send() sockets call
int send_msg(int fd, char *message)
{
	// Perform the send() socket call, but handle the length and null termination for us, return the number of bytes
	return send(fd, message, strlen(message), 0);
}

//------------------------- VALIDATE INT --------------------

// validate_int() ensures that an input string is a valid integer, and returns 1 on success, 0 on failure
int validate_int(char *string)
{
	// Flag to determine if integer is valid
	int isInt = 1;

	// Indexer variable
	int j = 0;

	// Loop through string, checking each digit to ensure it's an integer
        for(j = 0; j < strlen(string); j++)
        {
        	if(isInt == 1)
	        {
           	     if(!isdigit(string[j]))
                	     isInt = 0;
                }
        }

	// Return the value of isInt
	return isInt;
}
