/*
	Authors: Justin Hill, Gordon Keesler, Matt Layher
	Date:    3/20/12
	Updated: 4/1/12
	Project: p2pd
	Module:  p2p.c

	Description:
	The basic p2p directory service application.  New users on the network connect to this
	server to request a listing of files present on the network.  Once a user requests a file,
	this server points them to the correct peer(s) which possess the file.
*/

//------------------------ C LIBRARIES -----------------------

#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

//------------------------ CUSTOM LIBRARIES ------------------

#include "config.h"
#include "functions.h"
#include "p2p.h"

//------------------------ P2P -------------------------------

void *p2p(void *args)
{
	// Create and clear input and output buffers
	char in[512],out[512] = { '\0' };

	// Create p2p_t params struct from thread arguments
	p2p_t params = *((p2p_t *)(args));

	// Create buffers to store filename, file hash, and file size, and a long integer to store file size
	char *filename, *filehash, *filesize;
	long int f_size = 0;

	// Create buffer to store peer's IP address, pull it from args struct
	char peeraddr[128] = { '\0' };
	strcpy(peeraddr, params.ipaddr);

	// Keep track of user's file descriptor, so we can send and receive messages
	int user_fd = params.fd;

	// Create buffer to store SQLite queries
	char query[256];

	// Check SQLite return status
	int status;

	// Create SQLite statement struct
	sqlite3_stmt *stmt;

	// Send user a message to describe the server
	sprintf(out, "%s: %s Justin Hill, Gordon Keesler, and Matt Layher\n", SERVER_NAME, USER_MSG);
	send_msg(user_fd, out);

	// Loop until the user sends in the CONNECT handshake, or QUIT (which would cause them to fall
	// right through the following loop, to disconnect routines
	while((strcmp(in, "CONNECT")) != 0 && (strcmp(in, "QUIT") != 0))
	{
		// Receive user's message, clean input
		recv_msg(user_fd, (char *)&in);
		clean_string((char *)&in);

		// If CONNECT is sent, confirm handshake with client via HELLO message
		if(strcmp(in, "CONNECT") == 0)
		{
			fprintf(stdout, "%s: %s received handshake from peer %s [fd: %d]\n", SERVER_NAME, OK_MSG, peeraddr, user_fd);

			sprintf(out, "HELLO\n");
			send_msg(user_fd, out);
		}
	}

	// Loop until the user sends in the QUIT command
	while(strcmp(in, "QUIT") != 0)
	{
		// Clean buffers
		memset(in, 0, sizeof(in));
		memset(out, 0, sizeof(out));
		memset(query, 0, sizeof(query));

		// Receive user's message, clean input
		recv_msg(user_fd, (char *)&in);
		clean_string((char *)&in);

		// Process commands as specified in p2pd protocol

		// ADD - Add a file to the directory listing
		// syntax: ADD [filename] [filehash] [filesize]
		if(strncmp(in, "ADD", 3) == 0)
		{
			// Use strtok to grab the filename, skipping first ADD command
			strtok(in, " ");
			filename = strtok(NULL, " ");

			// Ensure that a filename was set
			if(filename != NULL)
			{
				// Use strtok to grab the filehash
				filehash = strtok(NULL, " ");

				// Ensure that a filehash was set
				if(filehash != NULL)
				{
					// Use strtok to grab the filesize
					filesize = strtok(NULL, " ");

					// Ensure that a filesize was set, and that it's a valid integer
					if((filesize != NULL) && (validate_int(filesize) == 1))
					{
						// Copy filesize into an integer
						f_size = atoi(filesize);

						// Insert filename, hash, size, and peer address into files table
						sprintf(query, "INSERT INTO files VALUES('%s', '%s', '%ld', '%s')", filename, filehash, f_size, peeraddr);

						// Prepare, evaluate, finalize SQLite query
						sqlite3_prepare_v2(db, query, strlen(query) + 1, &stmt, NULL);
						if((status = sqlite3_step(stmt)) != SQLITE_DONE)
						{
							// Check if user is attempting to insert a duplicate file
							if(status == SQLITE_CONSTRAINT)
							{	
								// Send error A4 (duplicate entry) to client
								sprintf(out, "ERROR A4\n");
								send_msg(user_fd, out);
							}
							else
							{
								// Else, an internal error must have occurred
								// Print an error to console
								fprintf(stderr, "%s: %s sqlite: ADD file insert failed\n", SERVER_NAME, ERROR_MSG);

								// Send error A0 (database error) to client
								sprintf(out, "ERROR A0\n");
								send_msg(user_fd, out);

								// Break loop, begin disconnect
								break;
							}
						}
						sqlite3_finalize(stmt);

						// If status is OK, print success
						if(status == SQLITE_DONE)
						{
							// Print confirmation of file add to console
							fprintf(stdout, "%s: %s peer %s added %20s [hash: %20s] [size: %10ld]\n", SERVER_NAME, OK_MSG, peeraddr, filename, filehash, f_size);

							// Return 'OK' to client
							sprintf(out, "OK\n");
							send_msg(user_fd, out);
						}
					}
					else
					{
						// On failure, return message with error A3 (null/invalid filesize) to client
						sprintf(out, "ERROR A3\n");
						send_msg(user_fd, out);
					}
				}
				else
				{
					// On failure, return message with error A2 (null filehash) to client
					sprintf(out, "ERROR A2\n");
					send_msg(user_fd, out);
				}
			}
			else
			{
				// On failure, return message with error A1 (null filename) to client
				sprintf(out, "ERROR A1\n");
				send_msg(user_fd, out);
			}
		}
		// DELETE - Delete a file from the directory server listing
		// syntax: DELETE [filename] [filehash]
		else if(strncmp(in, "DELETE", 6) == 0)
		{
			// Use strtok to grab the filename, skipping first DELETE command
			strtok(in, " ");
			filename = strtok(NULL, " ");

			// Ensure that a filename was set
			if(filename != NULL)
			{
				// Use strtok to grab the filehash
				filehash = strtok(NULL, " ");

				// Ensure that a filehash was set
				if(filehash != NULL)
				{
					// If all the previous commands succeeded, remove file from the database

					// Delete file with the specified filename, hash, and peer address from the database
					sprintf(query, "DELETE FROM files WHERE file='%s' AND hash='%s' AND peer='%s'", filename, filehash, peeraddr);

					// Prepare, evaluate, and finalize SQLite query
					sqlite3_prepare_v2(db, query, strlen(query) + 1, &stmt, NULL);
					if(sqlite3_step(stmt) != SQLITE_DONE)
					{
						// Print an error to console
						fprintf(stderr, "%s: %s sqlite: DELETE file delete failed\n", SERVER_NAME, ERROR_MSG);

						// Send error D0 (database error) to client
						sprintf(out, "ERROR D0\n");
						send_msg(user_fd, out);

						// Break loop, begin disconnect
						break;
					}
					sqlite3_finalize(stmt);

					// Print confirmation of file delete to console
					fprintf(stdout, "%s: %s peer %s removed file '%s' with hash '%s'\n", SERVER_NAME, OK_MSG, peeraddr, filename, filehash);

					// Send user 'OK' to confirm success
					sprintf(out, "OK\n");
					send_msg(user_fd, out);
				}
				else
				{
					// On failure, print message with error D2 (null filehash) to client
					sprintf(out, "ERROR D2\n");
					send_msg(user_fd, out);
				}
			}
			else
			{
				// On failure, print message with error D1 (null filename) to client
				sprintf(out, "ERROR D1\n");
				send_msg(user_fd, out);
			}
		}
		// LIST - Request listing of all files tracked by the directory server
		// syntax: LIST
		else if(strcmp(in, "LIST") == 0)
		{
			// Query for a list of all files in the database
			sprintf(query, "SELECT DISTINCT file,size FROM files ORDER BY file ASC");

			// Prepare, evaluate, and loop SQLite query results
			sqlite3_prepare_v2(db, query, strlen(query) + 1, &stmt, NULL);
			while((status = sqlite3_step(stmt)) != SQLITE_DONE)
			{
				// Check for errors
				if(status == SQLITE_ERROR)
				{
					// On error, print message to console
					fprintf(stderr, "%s: %s sqlite: failed to retrieve listing of files tracked by server\n", SERVER_NAME, ERROR_MSG);

					// Print message with error L0 (database error) to client
					sprintf(out, "ERROR L0\n");
					send_msg(user_fd, out);

					// Break loop
					break;
				}
				else
				{
					// On success, print file and its size
					sprintf(out, "%s %d\n", sqlite3_column_text(stmt, 0), sqlite3_column_int(stmt, 1));
					send_msg(user_fd, out);
				}
			}
			sqlite3_finalize(stmt);

			// If an SQLite error occurred, break the loop and disconnect
			if(status == SQLITE_ERROR)
				break;
			else
			{
				// Else, send user OK to confirm success
				sprintf(out, "OK\n");
				send_msg(user_fd, out);
			}
		}
		// QUIT - End communication with directory server
		// syntax: QUIT
		else if(strcmp(in, "QUIT") == 0)
		{
			// Break loop, end communication
			continue;
		}
		// REQUEST - Request information from server about which peers possess a file
		// syntax: REQUEST [filename]
		else if(strncmp(in, "REQUEST", 7) == 0)
		{
			// Use strtok to grab the filename, skipping first REQUEST command
			strtok(in, " ");
			filename = strtok(NULL, " ");

			// Ensure that a filename was set
			if(filename != NULL)
			{
				// Query for peers which possess this file in the files table
				sprintf(query, "SELECT peer,size FROM files WHERE file='%s' ORDER BY peer ASC", filename);

				// Prepare, evaluate, and loop SQLite query results
				sqlite3_prepare_v2(db, query, strlen(query) + 1, &stmt, NULL);
				while((status = sqlite3_step(stmt)) != SQLITE_DONE)
				{
					// Check for errors
					if(status == SQLITE_ERROR)
					{
						// On error, print message to console
						fprintf(stderr, "%s: %s sqlite: failed to retrieve listing of peers for file '%s'\n", SERVER_NAME, ERROR_MSG, filename);

						// Print message with error R0 (database error) to client
						sprintf(out, "ERROR R0\n");
						send_msg(user_fd, out);

						// Break loop	
						break;
					}	
					else
					{
						// On success, print peer addresses, and a file size
						sprintf(out, "%s %ld\n", sqlite3_column_text(stmt, 0), (long int)sqlite3_column_int(stmt, 1));
						send_msg(user_fd, out);
					}
				}
				sqlite3_finalize(stmt);

				// If an SQLite error occurred, break the loop and disconnect
				if(status == SQLITE_ERROR)
					break;
				else
				{
					// Else, send user OK to confirm success
					sprintf(out, "OK\n");
					send_msg(user_fd, out);
				}
			}
			else
			{
				// On failure, print message with error R1 (null filename) to client
				sprintf(out, "ERROR R1\n");
				send_msg(user_fd, out);
			}
		}
		else
		{
			// Else, command is invalid. (error C0)
			sprintf(out, "ERROR C0\n");
			send_msg(user_fd, out);
		}
	}

	// Once loop ends, begin disconnect routines

	// Clean output buffer
	memset(out, 0, sizeof(out));

	// Send goodbye message to user
	sprintf(out, "GOODBYE\n");
	send_msg(user_fd, out);

	// Decrement client counter, print message to console
	fprintf(stdout, "%s: %s client disconnected from %s [fd: %d] [users: %d/%d]\n", SERVER_NAME, OK_MSG, peeraddr, user_fd, client_count(-1), NUM_THREADS);

	// Run query to purge all files belonging to this user from the database
	sprintf(query, "DELETE FROM files WHERE peer='%s'", peeraddr);

	// Prepare, evaluate, and finalize SQLite query
	sqlite3_prepare_v2(db, query, strlen(query) + 1, &stmt, NULL);
	if(sqlite3_step(stmt) != SQLITE_DONE)
	{
		// On failure, print a console error, exit thread
		fprintf(stderr, "%s: %s failed to purge files belonging to peer %s [fd: %d]\n", SERVER_NAME, ERROR_MSG, peeraddr, user_fd);
		return (void *)-1;
	}
	sqlite3_finalize(stmt);

	// Attempt to close user socket
	if(close(user_fd) == -1)
	{
		// On failure, print error to console, exit
		fprintf(stderr, "%s: %s failed to close user socket [fd: %d]\n", SERVER_NAME, ERROR_MSG, user_fd);
		return (void *)-1;
	}

	// Exit with success
	return (void *)0;
}
