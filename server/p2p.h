/*
	Authors: Justin Hill, Gordon Keesler, Matt Layher
	Date:	 3/21/12
	Updated: 4/1/12
	Project: p2pd
	Module:	 p2p.h

	Description:
	A header containing prototypes and structs used in p2pd
*/

//------------------------ GLOBAL VARIABLES ------------------

// Reference externally defined SQLite database
extern sqlite3 *db;

//------------------------ PROTOTYPES ------------------------

//------------------------ STRUCTS ---------------------------

typedef struct
{
	// User's file descriptor
	int fd;

	// User's IP address
	char ipaddr[128];
} p2p_t;
