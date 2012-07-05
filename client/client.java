/*
	Authors: Justin Hill, Gordon Keesler, Matt Layher
	Date:    4/1/12
	Updated: 4/15/12
	Project: p2p
	Module:  client.java

	Description:
	The Java client implementation of our peer-to-peer protocol client.
*/

import java.io.*;
import java.net.*;

// Apache Commons Codec used for easy hashing via MD5 algorithm
// Borrowed from: http://commons.apache.org/codec/
import org.apache.commons.codec.digest.DigestUtils;

class Global
{
	public static String path = "";
}

class peer_server implements Runnable
{
	// Implement a basic file server, so that we may send files when requested by another peer
	public void run()
	{
		try
		{
			// For now, hardcode to define share path
			String path = Global.path;

			// Create ServerSocket objects, so we can accept multiple connections
			ServerSocket comServSock = new ServerSocket(6601);
			ServerSocket fileServSock = new ServerSocket(6602);
			
			// Loop forever, accepting connections
			while(true)
			{
				// Create socket to listen for incoming connections
				Socket socket = comServSock.accept();
				
				// Set up input/output streams on the opened socket
				BufferedReader in = new BufferedReader(new InputStreamReader(socket.getInputStream()));
				PrintWriter out = new PrintWriter(socket.getOutputStream(), false);
				
				// Create strings and arrays to do I/O with client
				String response = "";
				String[] respArray;

				// Loop until the peer sends the OPEN or CLOSE handshakes
				while(!response.equals("OPEN") && !response.equals("CLOSE"))
				{
					// Read in response from peer
					response = in.readLine();
					
					// If OPEN is sent, confirm handshake with peer via HELLO message
					if(response.equals("OPEN"))
					{
						out.println("HELLO");
						out.flush();
					}
				}

				// Loop until peer sends the CLOSE handshake
				while(!response.equals("CLOSE"))
				{
					// Read in response from peer
					response = in.readLine();
					
					// Split response into fields
					respArray = response.split(" ");
					
					// Process commands as specified in p2p protocol
					
					// GET - Transfer a file to a peer
					// syntax: GET [filename]
					if(respArray[0].equals("GET"))
					{
						// If a filename wasn't specified, an exception will be thrown here.  Catch it
						try
						{
							// Check if filename is not empty
							if(!respArray[1].isEmpty())
							{
								// If filename isn't empty, we are ready to begin transfer
								// Open a new socket for file transfer
								Socket fileSocket = fileServSock.accept();

								// Open specified file for transfer
								File peerfile = new File(path + respArray[1]);
								
								// Create a byte array of the file's size
								byte[] buffer = new byte[(int)peerfile.length()];

								// Create I/O streams
								BufferedInputStream fileIn = new BufferedInputStream(new FileInputStream(peerfile));

								// Read file in to byte array
								fileIn.read(buffer, 0, buffer.length);

								// Create buffered output stream to send the buffer
								BufferedOutputStream fileOut = new BufferedOutputStream(fileSocket.getOutputStream());

								// Send file over socket
								fileOut.write(buffer, 0, buffer.length);
								fileOut.flush();
								
								// Close I/O streams, close file socket
								fileIn.close();
								fileOut.close();
								fileSocket.close();
								
								// Send OK to peer, to confirm transfer success
								out.println("OK");
								out.flush();
							}
						}
						catch (IndexOutOfBoundsException e)
						{
							out.print("ERROR G0");
							out.flush();
						}
						catch (IOException e)
						{
							out.print("ERROR G1");
							out.flush();
						}
					}
					// CLOSE - Initiate closing handshake with peer
					// syntax: CLOSE
					else if(response.equals("CLOSE"))
					{
						// Break loop and begin closing connection
						continue;
					}
				}

				// On CLOSE, send client the GOODBYE message
				out.print("GOODBYE");
				out.flush();
				
				// Close socket
				socket.close();
			}
		}
		catch (IOException e)
		{
			System.out.println("[error] IOException in peer server");
			System.exit(-1);
		}
	}
}

public class client
{
	// Error handler method, which prints an error and exits the client
	public static void error_handler(String err)
	{
		// Store error text in a string
		String ret;

		// Check for all possible errors, as defined in P2P protocol
		if(err.equals("ERROR A0"))
			ret = "a database error occurred while indexing files with tracker";
		else if(err.equals("ERROR A1"))
			ret = "a null file name was encountered while indexing files with tracker";
		else if(err.equals("ERROR A2"))
			ret = "a null file hash was encountered while indexing files with tracker";
		else if(err.equals("ERROR A3"))
			ret = "a null file size was encountered while indexing files with tracker";
		else if(err.equals("ERROR A4"))
			ret = "a duplicate file from your machine was encountered while indexing files with tracker";
		else if(err.equals("ERROR C0"))
			ret = "tracker received an unknown command";
		else if(err.equals("ERROR D0"))
			ret = "a database error occurred while deleting files from the tracker";
		else if(err.equals("ERROR D1"))
			ret = "a null file name was encountered while deleting files from tracker";
		else if(err.equals("ERROR D2"))
			ret = "a null file hash was encountered while deleting files from tracker";
		else if(err.equals("ERROR G0"))
			ret = "a null file name was encountered when attempting file transfer";
		else if(err.equals("ERROR G1"))
			ret = "file transfer with peer failed";
		else if(err.equals("ERROR L0"))
			ret = "a database error occurred while retrieving a list of files from the tracker";
		else if(err.equals("ERROR R0"))
			ret = "a database error occurred while requesting peer addresses from the tracker";
		else if(err.equals("ERROR R1"))
			ret = "a null file name was encountered while requesting peer addresses from the tracker";
		else
			ret = "an unknown error occurred: " + err;

		// Print the error and exit
		System.out.println("[error] " + ret);
		System.exit(-1);
	}

	// Main method
	public static void main(String[] args)
	{
		try
		{
			// Print header
			System.out.println("p2p client - Justin Hill, Gordon Keesler, Matt Layher (CS5550 Spring 2012)");

			// Initialize variables required for socket communication with tracker
			Socket socket;
			BufferedReader in;
			PrintWriter out;
			
			// Initialize stdin so we can read input from user
			BufferedReader stdin = new BufferedReader(new InputStreamReader(System.in));

			// Initialize variables to store a server and port number, and a share path
			String server;
			int port;
			String path;

			// Prompt user to enter a server address, port number, and share folder
			System.out.print("server >> ");
			server = stdin.readLine();
			System.out.print("  port >> ");
			port = Integer.parseInt(stdin.readLine());
			System.out.print(" share >> ");
			path = stdin.readLine();
			Global.path = path;

			// Instantiate socket to connect to specified tracker
			socket = new Socket(server, port);

			// Set up input/output streams on the opened socket
			in = new BufferedReader(new InputStreamReader(socket.getInputStream()));
			out = new PrintWriter(socket.getOutputStream(), false);

			// Initialize strings and arrays to store the user's request and the server's response
			String request = "";
			String[] reqArray;
			String response;
			String[] respArray;

			// Read in the server's information header, print it to the screen
			System.out.println(in.readLine());

			// Perform the necessary handshake with the server, get its response
			out.print("CONNECT");
			out.flush();
			response = in.readLine();

			// Ensure that the HELLO response was received
			if(!response.equals("HELLO"))
			{
				// If the server manages to send an incorrect handshake response, print an error and exit
				System.out.println("[error] tracker did not properly reply to handshake");
				System.exit(-1);	
			}
			else
			{
				// If the handshake succeeded, print success message
				System.out.println("[info] successfully connected to tracker at " + server + ":" + port);
			}

			// Open up files in a directory called "share"
			File folder = new File(path);
			File[] files = folder.listFiles();
			FileInputStream f_stream;

			// Store file name, hash, and size
			String filename;
			String filehash;
			String filesize;

			// Print message to state that we are beginning to add files to the directory
			System.out.println("[info] indexing files from " + path + " with tracker...");

			// Keep count of number of files indexed
			int index_total = 0;

			// Iterate all files in the directory
			for(int i = 0; i < files.length; i++)
			{
				// Ensure the listing is an actual file
				if(files[i].isFile())
				{
					// Store the file's name
					filename = files[i].getName();

					// Open file input stream, hash file by contents, close file input stream
					f_stream = new FileInputStream(files[i]);
					filehash = DigestUtils.md5Hex(f_stream);
					f_stream.close();

					// Store the file's size
					filesize = String.valueOf(files[i].length());

					// Format these three obtained parameters into an ADD command, to send to the directory
					out.print("ADD " + filename + " " + filehash + " " + filesize);
					out.flush();

					// Read server's response
					response = in.readLine();

					// Ensure that the server returned OK, quit and print error if it didn't
					if(!response.equals("OK"))
						error_handler(response);
					else
					{
						// On success, print a dot to indicate a file (provides progress)
						System.out.print(". ");

						// Increment number of files indexed
						index_total++;
					}
				}
			}

			// Print success message once all files are indexed
			System.out.println("\n[info] successfully indexed " + index_total + " files with tracker");

			// Start network listener thread, so that we may serve files from the share folder
			Runnable run = new peer_server();
			Thread thread = new Thread(run);
			thread.start();
			
			// Tell user that we are awaiting input
			System.out.println("[info] ready for user input");

			// Loop until the user asks to quit
			do
			{
				// Get input from user
				System.out.print(">> ");
				request = stdin.readLine();

				// Split request into array of strings
				reqArray = request.split(" ");

				// Perform actions using the given input
				// list - send tracker the LIST command; receive, format, and print output
				if(request.equals("list"))
				{
					// Print message to user
					System.out.println("[info] requesting list of files from tracker...");

					// Send server the LIST command
					out.print("LIST");
					out.flush();

					// Keep a count of number of files which arrive in listing
					int list_total = 0;

					// Read input from server
					response = in.readLine();

					// Split input into fields by space separator
					respArray = response.split(" ");

					// Loop and receive input, until server replies OK or with ERROR
					while((!respArray[0].equals("OK")) && (!respArray[0].equals("ERROR")))
					{
						// Increment total files listed
						list_total++;

						// Print formatted file and size
						System.out.println(String.format("file [%2d]: %20s [size: %10s]", new Object[] { new Integer(list_total), respArray[0], respArray[1] }));

						// If files were listed, read more input from the server
						response = in.readLine();

						// Split response into pieces by space separator
						respArray = response.split(" ");
					}

					// Print out total number of files listed
					System.out.println("[info] received list of " + list_total + " files from tracker");

					// Ensure that the server returned OK, quit and print error if it didn't
					if(!response.equals("OK"))
						error_handler(response);
				}
				// request - send server the REQUEST command; initiate file transfer
				else if(reqArray[0].equals("request"))
				{
					// If a file wasn't specified after the request, we'll get an exception.  Catch it.
					try
					{
						// Ensure that the second field in the array was set, so we have a filename to send
						if(!reqArray[1].isEmpty())
						{
							// Send server the REQUEST command, with the given filename
							out.print("REQUEST " + reqArray[1]);
							out.flush();

							// Read input from the server
							response = in.readLine();

							// Split input into fields by space separator
							respArray = response.split(" ");						

							// If OK was immediately returned, the file does not exist on the tracker.  Inform peer
							if(respArray[0].equals("OK"))
								System.out.println("[error] file '" + reqArray[1] + "' was not found on the tracker");

							// Loop and receive input, until server replies OK or with ERROR
							while((!respArray[0].equals("OK")) && (!respArray[0].equals("ERROR")))
							{
								// Open communications socket with peer
								Socket comSocket = new Socket(respArray[0], 6601);	
								
								// Keep string to capture communication responses
								String comResponse;
								
								// Open I/O streams for communication with peer
								BufferedReader comIn = new BufferedReader(new InputStreamReader(comSocket.getInputStream()));
								PrintWriter comOut = new PrintWriter(comSocket.getOutputStream(), false);

								// Send peer the OPEN handshake
								comOut.println("OPEN");
								comOut.flush();
								
								// Read the peer's communication
								comResponse = comIn.readLine();
								
								// Ensure we received the HELLO confirmation
								if(!comResponse.equals("HELLO"))
								{
									// If peer does not return handshake properly, print error and exit
									System.out.println("[error] peer did not properly reply to handshake");
									System.exit(-1);
								}
	
								// Open socket for file transfer with peer
								Socket fileSocket = new Socket(respArray[0], 6602);

								// Send peer the GET command, sending the specified filename as well
								comOut.println("GET " + reqArray[1]);
								comOut.flush();
								
								// Open input stream for file transfer with peer
								InputStream fileIn = fileSocket.getInputStream();

								// Open output stream to write file to disk
								BufferedOutputStream fileOut = new BufferedOutputStream(new FileOutputStream(path + reqArray[1]));
								
								// Initiate variables for file transfer
								int bytesRead,current = 0;

								// Create a byte array large enough to store the file
								byte[] buffer = new byte[Integer.parseInt(respArray[1])];
								
								// Initialize byte array cursors
								bytesRead = fileIn.read(buffer, 0, buffer.length);
								current = bytesRead;

								System.out.println("[info] initiating file transfer");

								// Loop through and keep adding bytes to buffer
								do
								{
									System.out.print(". ");

									// Read in more bytes
									bytesRead = fileIn.read(buffer, current, (buffer.length - current));

									// If byte counter was positive, shift it forward
									if(bytesRead >= 0)
										current += bytesRead;
								} while(bytesRead > -1 && buffer.length != current);

								// Write the byte array to a file
								fileOut.write(buffer, 0, current);
								fileOut.flush();

								System.out.println("\n[info] file transfer complete");

								// Close all I/O streams, close socket
								fileIn.close();
								fileOut.close();
								fileSocket.close();

								// Got file, exit loop
								respArray[0] = "OK";

								// Read more input
								//response = in.readLine();

								// Split response into pieces
								//respArray = response.split(" ");
							}

							// Ensure that the server returned OK, quit and print error if it didn't
							if(!respArray[0].equals("OK"))
								error_handler(response);
						}
					}
					catch (Exception e)
					{
						// If no file was specified after the request, print an error.
						System.out.println("[error] please specify a file after the request command");
					}
				}
			} while(!request.equals("quit"));

			// Once user wants to quit, send the disconnect handshake
			out.print("QUIT");
			out.flush();

			// Ensure the termination handshake was successful
			response = in.readLine();
			if(!response.equals("GOODBYE"))
			{
				System.out.println("[error] tracker did not properly reply to exit handshake: " + response);
				System.exit(-1);
			}
			else
			{
				// On success, print success message
				System.out.println("[info] successfully closed connection to tracker");
			}

			// Close I/O streams on socket
			in.close();
			out.close();

			// Close socket
			socket.close();
		}
		// Catch any IOExceptions which may occur
		catch (IOException e)
		{
			System.out.println("IOException occurred!");
		}
	}
}
