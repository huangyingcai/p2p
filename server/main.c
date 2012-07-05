/*
	Authors: Justin Hill, Gordon Keesler, Matt Layher
	Date:	 3/21/12
	Updated: 4/1/12
	Project: p2pd
	Module:	 main.c

	Description:
	A generic TCP server written using sockets programming.  This server is capable of operating
	with a variety of command line parameters, and is also capable of running as a daemon.

	The server is terminated by sending it a SIGINT signal, typically by one of these methods:
		1) Ctrl+C on server console
		2) 'stop' on server console
		3) Sending SIGINT from another process (e.g. kill -2 (pid))
*/

//------------------------ C LIBRARIES -----------------------

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

//----------------------- CUSTOM LIBRARIES -------------------

#include "config.h"
#include "functions.h"
#include "main.h"
#include "p2p.h"
#include "thpool.h"

//----------------------- GLOBAL VARIABLES -------------------

//----------------------- SOCKET VARIABLES -------------------

// Utilize global socket file descriptors, so that we may close them via signal handlers
int loc_fd, inc_fd;

// Storage for incoming socket address
struct sockaddr_storage inc_addr;

// Define length of incoming socket using the socklen_t primitive
socklen_t inc_len = sizeof(inc_addr);

//----------------------- THREADING --------------------------

// Globally declared threadpool, so it may be destroyed in the signal handler
thpool_t *threadpool;

// Globally declared network handler thread, so it may be canceled by the signal handler
pthread_t net_thread;

//----------------------- SERVER CONFIGURATION --------------

// Define a flag which controls daemonization of the server
int daemonized = 0;

// Define name of lockfile used in daemonized mode
char *lock_location = LOCKFILE;

// Initialize number of threads in pool to the default number
int num_threads = NUM_THREADS;

// Globally declared pidfile, so it may be closed by signal handler
int pidfile;

// Initialize server port number to the generic default port
char *port = (char *)DEFAULT_PORT;

// Initialize connection queue length to the default number
int queue_length = QUEUE_LENGTH;

//------------------------ MISCELLANEOUS --------------------

// Create buffer for storing client's IP address
char clientaddr[128] = { '\0' };

// SQLite database access struct
sqlite3 *db;

// Create a start time clock
time_t start_time;

// Keep track of the terminal on which the server was started, for stats purposes
char *term;

//----------------------- SIGNAL HANDLERS --------------------

//----------------------- STAT HANDLER -----------------------

// Stats handler, which causes a daemonized server to report its health to the console when presented with SIGUSR1/SIGUSR2
void stat_handler()
{
	// Open stdout to the terminal
	freopen(term, "w", stdout);

	// Write out server information
	print_stats();

	// Return stdout to /dev/null
	freopen("/dev/null", "w", stdout);
}

//----------------------- SHUTDOWN HANDLER -------------------

// Shutdown handler, which catches signals and performs shutdown routines to cleanly terminate the server
void shutdown_handler()
{
	// Cancel the network handling thread, stopping all incoming connections
	pthread_cancel(net_thread);

	// If in daemon mode, unlock, close, and remove the lockfile
	if(daemonized == 1)
	{
		// Attempt to unlock lockfile
		if(lockf(pidfile, F_ULOCK, 0) == -1)
		{
			fprintf(stderr, "%s: %s failed to unlock lockfile\n", SERVER_NAME, ERROR_MSG);
			exit(-1);
		}

		// Attempt to close lockfile
		if(close(pidfile) == -1)
		{
			fprintf(stderr, "%s: %s failed to close daemon lockfile\n", SERVER_NAME, ERROR_MSG);
			exit(-1);
		}

		// Attempt to remove lockfile
		if(remove(lock_location) == -1)
		{
			fprintf(stderr, "%s: %s could not remove lockfile %s\n", SERVER_NAME, ERROR_MSG, lock_location);
			exit(-1);
		}
	}

	// Print newline to clean up output
	fprintf(stdout, "\n");

	// Close SQLite database
	if(sqlite3_close(db) != SQLITE_OK)
	{
		// On failure, print error to console and exit
		fprintf(stderr, "%s: %s sqlite: failed to close database\n", SERVER_NAME, ERROR_MSG);
		exit(-1);
	}

        // Attempt to shutdown the local socket
        if(shutdown(loc_fd, 2) == -1)
        {
                // Print error and exit if socket fails to shutdown
                fprintf(stderr, "%s: %s failed to shutdown local socket\n", SERVER_NAME, ERROR_MSG);
                exit(-1);
        }

        // Attempt to close local socket to end program
        if(close(loc_fd) == -1)
        {
                // Print error and exit if socket fails to close
                fprintf(stderr, "%s: %s failed to close local socket\n", SERVER_NAME, ERROR_MSG);
                exit(-1);
        }

	// Destroy created threadpool.  If 0 clients are connected, rejoin the threads.  Else, cancel the threads (force destroy).
	if(client_count(0) == 0)
		thpool_destroy(threadpool, 0);
	else
		thpool_destroy(threadpool, 1);

	// Print final termination message
        fprintf(stdout, "%s: %s kicked %d client(s), server terminated\n", SERVER_NAME, OK_MSG, client_count(0));

        // Exit, returning success
        exit(0);
}

//----------------------- PRINT STATS ------------------------

// Statistics function, which can be called via console or signal
void print_stats()
{
	// Create variables to calculate elapsed time
	int hours, minutes, seconds;

	// Create a buffer to store total elapsed time
	char runtime[32] = { '\0' };

	// Create a buffer to store thread pool usage calculations
	char tpusage[32] = { '\0' };

	//------------------ CALCULATE RUNTIME ---------------------

	// Calculate total number of seconds since program start
	seconds = (int)difftime(time(NULL), start_time);
	
	// Calculate minutes as the number of seconds divided by 60
	minutes = seconds / 60;

	// Calculate hours as the number of minutes divided by 60
	hours = minutes / 60;

	// Recalculate minutes as the remainder after hours are taken out
	minutes = minutes % 60;

	// Recalculate seconds as the remainder after minutes are taken out
	seconds = seconds % 60;

	// Combine all time information into a single string
	sprintf(runtime, "%02d:%02d:%02d", hours, minutes, seconds);

	//----------------- CALCULATE THREAD POOL USAGE -------------

	// Begin calculating thread pool usage, setting message type accordingly
	// If thread pool is under-capacity...
	if(client_count(0) < (num_threads * TP_UTIL))
	{
		// Set message type to OK
		fprintf(stdout, "%s: %s ", SERVER_NAME, OK_MSG);

		// Format users string as normal
		sprintf(tpusage, "[users: %d/%d]", client_count(0), num_threads);
	}
	// If thread pool is near or at-capacity...
	else if(((double)client_count(0) >= ((double)num_threads * TP_UTIL)) && client_count(0) <= num_threads)
	{
		// Set message type to WARN
		fprintf(stdout, "%s: %s ", SERVER_NAME, WARN_MSG);

		// Format users string with warning color
		sprintf(tpusage, "\033[1;33m[users: %d/%d]\033[0m", client_count(0), num_threads);
	}
	// Finally, if thread pool is over-capacity...
	else
	{
		// Set message type to ERROR
		fprintf(stdout, "%s: %s ", SERVER_NAME, ERROR_MSG);

		// Format users string with error color
		sprintf(tpusage, "\033[1;31m[users: %d/%d]\033[0m", client_count(0), num_threads);
	}
	
	//----------------- PRINT STATISTICS ------------------------

	// Print out server statistics, differ slightly if daemonized
	if(daemonized == 1)
		fprintf(stdout, "daemon running [PID: %d] [time: %s] [lock: %s] [port: %s] [queue: %d] %s\n", getpid(), runtime, lock_location, port, queue_length, tpusage);
	else
		fprintf(stdout, "server running [PID: %d] [time: %s] [port: %s] [queue: %d] %s\n", getpid(), runtime, port, queue_length, tpusage);
}

//----------------------- MAIN -------------------------------

// Main function, the base TCP server
int main(int argc, char *argv[])
{
	//-------------------- SET UP VARIABLES -----------------------
	
	// Set up all required variables for sockets programming, starting with addrinfo "hints" struct and pointers to results list
	struct addrinfo hints, *result;

	// Integer variable used to set socket options
	int yes = 1;

	// Create command buffer, to send commands directly to the server
	char command[512] = { '\0' };

	// Define a generic indexer variable for loops
	int i = 0;

	// Set up SQLite statement struct
	sqlite3_stmt *stmt;

	// Create a buffer to store queries to process on the database
	char query[256] = { '\0' };

	//------------------ INITIALIZE SIGNAL HANDLERS ---------------

	// Install signal handlers for graceful shutdown
	// Install SIGHUP signal handler
	signal(SIGHUP, shutdown_handler);

	// Install SIGINT signal handler
	signal(SIGINT, shutdown_handler);

	// Install SIGTERM signal handler
	signal(SIGTERM, shutdown_handler);

	// Install signal handlers for statistics output
	// Install SIGUSR1 signal handler
	signal(SIGUSR1, stat_handler);

	// Install SIGUSR2 signal handler
	signal(SIGUSR2, stat_handler);

	//------------------ BEGIN SERVER INITIALIZATION --------------

	// Read in terminal on which server was started
	term = strdup(ttyname(1));

	// Print initialization message
	fprintf(stdout, "%s: %s %s - Justin Hill, Gordon Keesler, Matt Layher (CS5550 Spring 2012)\n", SERVER_NAME, INFO_MSG, SERVER_NAME);

	// Capture initial start time
	start_time = time(NULL);

	//------------------ PARSE COMMAND LINE ARGUMENTS -------------

	// Iterate through all argv command line arguments, parsing out necessary flags
	for(i = 1; i < argc; i++)
	{
		// '-d' or '--daemon' flag: daemonize the server, and run it in the background
		if(strcmp("-d", argv[i]) == 0 || strcmp("--daemon", argv[i]) == 0)
		{
			// Set daemon flag to true, so we may daemonize later
			daemonized = 1;
		}
		// '-h' or '--help' flag: print help and usage for this server, then exit
		else if(strcmp("-h", argv[i]) == 0 || strcmp("--help", argv[i]) == 0)
		{
			// Print usage message
			fprintf(stdout, "usage: %s [-d | --daemon] [-h | --help] [-l | --lock lock_file] [-p | --port port] [-q | --queue queue_length] [-t | --threads thread_count]\n\n", SERVER_NAME);

			// Print out all available flags
			fprintf(stdout, "%s flags:\n", SERVER_NAME);
			fprintf(stdout, "\t-d | --daemon:     daemonize - start server as a daemon, running it in the background\n");
			fprintf(stdout, "\t-h | --help:            help - print usage information and details about each flag the server accepts\n");
			fprintf(stdout, "\t-l | --lock:       lock_file - specify the location of the lock file utilized when the server is daemonized (default: %s)\n", LOCKFILE);
			fprintf(stdout, "\t-p | --port:            port - specify an alternative port number to run the server (default: %s)\n", DEFAULT_PORT);
			fprintf(stdout, "\t-q | --queue:   queue_length - specify the connection queue length for the incoming socket (default: %d)\n", QUEUE_LENGTH);
			fprintf(stdout, "\t-t | --threads: thread_count - specify the number of threads to generate (max number of clients) (default: %d)\n", NUM_THREADS);
			fprintf(stdout, "\n");

			// Print out all available console commands via the common console_help() function
			console_help();

			// Exit the server
			exit(0);
		}
		// '-l' or '--lock' flag: specify an alternate lock file location
		else if(strcmp("-l", argv[i]) == 0 || strcmp("--lock", argv[i]) == 0)
		{
			// Make sure that another argument exists, specifying the lockfile location
			if(argv[i+1] != NULL)
			{
				// Set lock file location as specified on the command line
				lock_location = argv[i+1];
				i++;
			}
			else
			{
				// Print error and use default location if no lockfile was specified after the flag
				fprintf(stderr, "%s: %s no lockfile location specified, defaulting to %s\n", SERVER_NAME, ERROR_MSG, LOCKFILE);
			}
		}
		// '-p' or '--port' flag: specifies an alternative port number to run the server
		else if(strcmp("-p", argv[i]) == 0 || strcmp("--port", argv[i]) == 0)
		{
			// Make sure that another argument exists, specifying the port number
			if(argv[i+1] != NULL)
			{
				// Ensure this port is a valid integer
				if(validate_int(argv[i+1]))
				{
					// Set this port to be used if it's within the valid range, else use the default
					if(atoi(argv[i+1]) >= 0 && atoi(argv[i+1]) <= MAX_PORT)
					{
						port = argv[i+1];
						i++;
					}
					else
						fprintf(stderr, "%s: %s port lies outside valid range (0-%d), defaulting to %s\n", SERVER_NAME, ERROR_MSG, MAX_PORT, DEFAULT_PORT);
				}
				else
				{
					// Print error and use default port if an invalid port number was specified
					fprintf(stderr, "%s: %s invalid port number specified, defaulting to %s\n", SERVER_NAME, ERROR_MSG, DEFAULT_PORT);
				}
			}
			else
			{
				// Print error and use default port if no port number was specified after the flag
				fprintf(stderr, "%s: %s no port number specified after flag, defaulting to %s\n", SERVER_NAME, ERROR_MSG, DEFAULT_PORT);
			}
		}
		// '-q' or '--queue' flag: specify the connection queue length
		else if(strcmp("-q", argv[i]) == 0 || strcmp("--queue", argv[i]) == 0)
		{
			// Make sure another argument exists, specifying the queue length
			if(argv[i+1] != NULL)	
			{
				// Ensure this is a valid integer for queue length
				if(validate_int(argv[i+1]))
				{
					// Set connection queue length to the number specified on the command line, if it's a number more than 0, else use the default
					if(atoi(argv[i+1]) >= 1)
					{
						queue_length = atoi(argv[i+1]);
						i++;
					}
					else
						fprintf(stderr, "%s: %s cannot use negative or zero queue length, defaulting to length %d\n", SERVER_NAME, ERROR_MSG, QUEUE_LENGTH);
				}
				else
				{
					// Print error and use default queue length if an invalid number was specified
					fprintf(stderr, "%s: %s invalid queue length specified, defaulting to length %d\n", SERVER_NAME, ERROR_MSG, QUEUE_LENGTH);
				}
			}
			else
			{
				// Print error and use default queue length if no length was specified after the flag
				fprintf(stderr, "%s: %s no queue length specified after flag, default to length %d\n", SERVER_NAME, ERROR_MSG, QUEUE_LENGTH);
			}
		}
		// '-t' or '--threads' flag: specify the number of threads to generate in the thread pool
		else if(strcmp("-t", argv[i]) == 0 || strcmp("--threads", argv[i]) == 0)
		{
			// Make sure next argument exists, specifying the number of threads
			if(argv[i+1] != NULL)
			{
				// Ensure this number is a valid integer
				if(validate_int(argv[i+1]))
				{
					// Set number of threads to the number specified on the command line, if it's a number more than 0, else use the default
					if(atoi(argv[i+1]) >= 1)
					{
						num_threads = atoi(argv[i+1]);
						i++;
					}
					else
						fprintf(stderr, "%s: %s cannot use negative or zero threads, defaulting to %d threads\n", SERVER_NAME, ERROR_MSG, NUM_THREADS);
				}
				else
				{
					// Print error and use default number of threads if an invalid number was specified
					fprintf(stderr, "%s: %s invalid number of threads specified, defaulting to %d threads\n", SERVER_NAME, ERROR_MSG, NUM_THREADS);
				}
			}
			else
			{
				// Print error and use default number of threads if no count was specified after the flag
				fprintf(stderr, "%s: %s no thread count specified after flag, defaulting to %d threads\n", SERVER_NAME, ERROR_MSG, NUM_THREADS);
			}
		}
		else
		{
			// Else, an invalid flag or parameter was specified; print an error and exit
			fprintf(stderr, "%s: %s unknown parameter '%s' specified, please run '%s -h' for help and usage\n", SERVER_NAME, ERROR_MSG, argv[i], SERVER_NAME);
			exit(-1);
		}
	}

	//------------------------ OPEN SQLITE DATABASE ----------------

	// Open database file, as specified in config header; check for success
	sqlite3_open(DB_FILE, &db);
	if(db == NULL)
	{
		// Print an error message and quit if database fails to open
		fprintf(stderr, "%s: %s sqlite: could not open database %s\n", SERVER_NAME, ERROR_MSG, DB_FILE);
		exit(-1);
	}

	// Create a query to truncate the files table in the database
	sprintf(query, "DELETE FROM files");

	// Prepare, evaluate, and finalize SQLite query
	sqlite3_prepare_v2(db, query, strlen(query) + 1, &stmt, NULL);
	if(sqlite3_step(stmt) != SQLITE_DONE)
	{
		// On query failure, print an error and exit
		fprintf(stderr, "%s: %s sqlite: could not truncate files table\n", SERVER_NAME, ERROR_MSG);
		exit(-1);
	}
	sqlite3_finalize(stmt);

	//------------------------ INITIALIZE TCP SERVER ---------------

	// Clear the hints struct using memset to nullify it
	memset(&hints, 0, sizeof(hints));

	// Set options for hints to use IPv4, a TCP connection, and the machine's current IP address
	hints.ai_family = AF_INET;          // IPv4
	hints.ai_socktype = SOCK_STREAM;    // Reliable TCP connection
	hints.ai_flags = AI_PASSIVE;        // Use my ip address

	// now populate our result addrinfo
	if((getaddrinfo(NULL, port, &hints, &result)) != 0)
	{ 
		// If getaddrinfo() fails, print an error and quit the program, since setup cannot continue.
		fprintf(stderr, "%s: %s getaddrinfo() call failed\n", SERVER_NAME, ERROR_MSG);
		exit(-1);
	}

	// Attempt to instantiate the local socket, using values set by getaddrinfo()
	if((loc_fd = socket(result->ai_family, result->ai_socktype, result->ai_protocol)) == -1)
	{
		// On socket creation failure, print an error and exit
		fprintf(stderr, "%s: %s local socket creation failed\n", SERVER_NAME, ERROR_MSG);
		exit(-1);
	}

	// Allow the system to free and re-bind the socket if it is already in use
	if(setsockopt(loc_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1)
	{
		// If setting a socket option fails, terminate the program after printing an error
		fprintf(stderr, "%s: %s failed to set socket option: SO_REUSEADDR\n", SERVER_NAME, ERROR_MSG);
		exit(-1);
	}

	// Attempt to bind the local socket
	if((bind(loc_fd, result->ai_addr, result->ai_addrlen)) == -1)
	{
		// If socket binding fails, it's typically one of two scenarios:
		// 1) Check if socket is on a privileged port, and permission is denied
		if(atoi(port) < PRIVILEGED_PORT)
			fprintf(stderr, "%s: %s failed to bind local socket (permission denied?)\n", SERVER_NAME, ERROR_MSG);
		// 2) Else, the socket is probably already bound
		else
			fprintf(stderr, "%s: %s failed to bind local socket (socket already in use?)\n", SERVER_NAME, ERROR_MSG);

		// Exit on failure
		exit(-1);
	}
	
	// Free the results struct, as it is no longer needed
	freeaddrinfo(result);

	// Begin listening on the local socket, and set connection queue length as defined above
	if((listen(loc_fd, queue_length)) == -1)
	{
		// Print error message if socket fails to begin listening
		fprintf(stderr, "%s: %s failed to begin listening on local socket\n", SERVER_NAME, ERROR_MSG);
	}
    
	//-------------------------- DAEMONIZATION ------------------

	// If server is being daemonized, do so now.
	if(daemonized == 1)
		daemonize();
	else
	{	
		// Initialize a thread pool, using number of threads as defined earlier
		threadpool = thpool_init(num_threads);

		// Initialize the network thread to handle all incoming connections
		pthread_create(&net_thread, NULL, &tcp_listen, NULL);
	
		// Print out server information and ready message
		fprintf(stdout, "%s: %s server initialized [PID: %d] [port: %s] [queue: %d] [threads: %d]\n", SERVER_NAME, OK_MSG, getpid(), port, queue_length, num_threads);

		// If server is not being daemonized, use the default console interface
		fprintf(stdout, "%s: %s type 'stop' or hit Ctrl+C (SIGINT) to stop server\n", SERVER_NAME, INFO_MSG);
	}

	//------------------------- CONSOLE COMMAND ---------------

	// Loop continuously until 'stop' is provided on the console
	while(1)
	{
		// Read in user input, clean it up
		fgets(command, sizeof(command), stdin);
		clean_string((char *)&command);

		// 'clear' - Clear the console
		if(strcmp(command, "clear") == 0)
			system("clear");
		// 'help' - Display the common console help menu
		else if(strcmp(command, "help") == 0)
			console_help();
		// 'stat' - Print out server statistics
		else if(strcmp(command, "stat") == 0)
			print_stats();
		// 'stop' - Stop the server, breaking this loop
		else if(strcmp(command, "stop") == 0)
			break;
		// Else, print console error stating command does not exist
		else
			fprintf(stderr, "%s: %s unknown console command '%s', type 'help' for console command help\n", SERVER_NAME, ERROR_MSG, command);
	}

	// Send SIGINT to the server so that it will gracefully terminate via signal handler
	kill(getpid(), SIGINT);
}

// ----------------------- TCP LISTEN --------------------------

// Function called by network thread, used to separate the TCP listener from the console thread
void *tcp_listen()
{
	// Set up p2p_t struct, to pass variables into the thread function
	p2p_t params;

	// Create output buffer, in case the base server must communicate directly to the client
	char out[512] = { '\0' };

	// Loop infinitely until Ctrl+C SIGINT is caught by the signal handler
   	while(1)
	{
		// Attempt to accept incoming connections using the incoming socket
		if((inc_fd = accept(loc_fd, (struct sockaddr *)&inc_addr, &inc_len)) == -1)
		{
			// Print an error message and quit if server cannot accept connections
			fprintf(stderr, "%s: %s failed to accept incoming connections\n", SERVER_NAME, ERROR_MSG);
			return (void *)-1;
		}
		else
		{
			// If a connection is accepted, continue routines.
			// Capture client's IP address for logging.
			inet_ntop(inc_addr.ss_family, get_in_addr((struct sockaddr *)&inc_addr), clientaddr, sizeof(clientaddr));

			// Print message when connection is received, and increment client counter
			fprintf(stdout, "%s: %s client connected from %s [fd: %d] [users: %d/%d]\n", SERVER_NAME, OK_MSG, clientaddr, inc_fd, client_count(1), num_threads);

			// If client count reaches the utilization threshold, print a warning
			if(((double)client_count(0) >= ((double)num_threads * TP_UTIL)) && (client_count(0) <= num_threads))
			{
				// Print warning to server console, alter wording slightly if utilization is maxed out
				if(client_count(0) == num_threads)
					fprintf(stdout, "%s: %s thread pool exhausted [users: %d/%d]\n", SERVER_NAME, WARN_MSG, client_count(0), num_threads);
				else
					fprintf(stdout, "%s: %s thread pool nearing exhaustion [users: %d/%d]\n", SERVER_NAME, WARN_MSG, client_count(0), num_threads);
			}
			// If client count exceeds the number of threads in the pool, print an error
			else if((client_count(0)) > num_threads)
			{
				// Print error to console
				fprintf(stderr, "%s: %s thread pool over-exhausted [users: %d/%d]\n", SERVER_NAME, ERROR_MSG, client_count(0), num_threads);

				// Generate message to send to client
				sprintf(out, "%s: %s server has currently reached maximum user capacity, please wait\n", SERVER_NAME, USER_MSG);
				send_msg(inc_fd, out);
			}

			// Store user's file descriptor and IP address in the params struct
			params.fd = inc_fd;
			strcpy(params.ipaddr, clientaddr);

			// On client connection, add work to the threadpool, pass in params struct
			thpool_add_work(threadpool, &p2p, (void*)&params);
		}
	}
}

//------------------------ DAEMONIZE -------------------------

// The function which handles all the heavy lifting for daemonization of the server
void daemonize()
{
	// Declare PID and SID to create a forked process, and detach from the parent
	pid_t pid, sid;

	// Create buffer to store PID in lockfile
	char pidstr[6];

	// Check if we are already daemonized.  If yes, return
	if(getppid() == 1)
		return;

	// Fork off the parent process, die on failure
	if((pid = fork()) < 0)
	{
		// Print an error to the console and die
		fprintf(stderr, "%s: %s failed to fork child process and daemonize\n", SERVER_NAME, ERROR_MSG);
		exit(-1);
	}
	
	// End the parent process, as it is no longer needed, but wait just a moment first to print any errors which occur below
	if(pid > 0)
	{
		usleep(250);
		exit(0);
	}

	// Now executing as child process.

	// Set the file mode mask
	umask(0);
	
	// Create a new SID for child process, die on failure
	if((sid = setsid()) < 0)
	{
		// Print an error to the console and die
		fprintf(stderr, "%s: %s failed to set new session for child process\n", SERVER_NAME, ERROR_MSG);
		exit(-1);
	}

	// Change current working directory to root, to prevent locking
	if(chdir("/") < 0)
	{
		fprintf(stderr, "%s: %s failed to change working directory\n", SERVER_NAME, ERROR_MSG);
		exit(-1);
	}
	
	// Open pidfile using the defined location
	if((pidfile = open(lock_location, O_RDWR|O_CREAT, 0600)) < 0)
	{
		fprintf(stderr, "%s: %s failed to open lock file (permission denied?)\n", SERVER_NAME, ERROR_MSG);
		exit(-1);
	}

	// Try to lock the pidfile
	if(lockf(pidfile, F_TLOCK, 0) == -1)
	{
		fprintf(stderr, "%s: %s failed to lock PID file (daemon already running?)\n", SERVER_NAME, ERROR_MSG);
		exit(-1);
	}

	// Format PID and store in string
	sprintf(pidstr, "%d\n", getpid());

	// Write PID to lockfile
	write(pidfile, pidstr, strlen(pidstr));

	// Print success message
	fprintf(stdout, "%s: %s daemonization complete [PID: %d] [lock: %s] [term: %s] [port: %s] [queue: %d] [threads: %d]\n", SERVER_NAME, OK_MSG, getpid(), lock_location, term, port, queue_length, num_threads);

	// Redirect standard streams to /dev/null
	freopen("/dev/null", "r", stdin);
	freopen("/dev/null", "w", stdout);
	freopen("/dev/null", "w", stderr);	

	// When daemonizing, we must initialize the threadpool and listener thread here.	
	// Initialize a thread pool, using number of threads as defined earlier
	threadpool = thpool_init(num_threads);

	// Initialize the network thread to handle all incoming connections
	pthread_create(&net_thread, NULL, &tcp_listen, NULL);

	// Loop and sleep infinitely until death.  This is the end of the line for the main thread.
	while(1)
		sleep(60);
}
