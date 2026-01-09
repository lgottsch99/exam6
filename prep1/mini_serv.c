#include <unistd.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h> //struct sockaddr
#include <string.h>
#include <stdio.h>

//Globals to handle poll logic with select
int next = 0; // simple counter: first to connect gets ID 0, the next gets 1, and so on. increment this every time accept() is successful.
int ids[65536]; // Maps FD -> Client ID. lookup table. using fd as index. When a client sends a message, the computer only tells you their File Descriptor (e.g., "Socket #5"). But the subject says you must display their ID (e.g., "client 2").
char *msgs[65536]; // Buffer for partial messages. lookup table. FD has its own "bucket" in this array. We store the Hel in msgs[5], and when the rest arrives, we join it together. We only "broadcast" the message when we finally see that \n.
fd_set active; //Master Record. It contains every socket you are currently watching (the server + all clients).
fd_set ready_read; // temporary copy used for select. After select runs, this set is filtered to show only people who sent something.
fd_set ready_write; // temporary copy used for select. This shows you which clients are ready to receive data. (The subject says if they are "lazy," you shouldn't disconnect them, so we check this before sending).
int max_fd = 0; // select is old-fashioned; it doesn't know how many FDs you have. You must tell it the highest FD number currently in use so it knows where to stop searching.
char buf_read[424242]; // massive temporary "tray." When recv gets data, we dump it here first before processing it.
char buf_write[424242]; // massive temporary "tray" for outgoing mail. We use sprintf to write things like "server: client 1 just arrived\n" into this buffer before passing it to the send() function.


void send_to_all(int sender_fd, char *msg, int sockfd)
{
	for (int fd = 0; fd < max_fd + 1; fd++)
	{
		if (FD_ISSET(fd, &ready_write) && fd != sender_fd && fd != sockfd)
		{
			//send(fd, msg, strlen(msg), 0);
			write(fd, msg, strlen(msg)); //both ok but i find write more easy...
		}
	}
}
char *str_join(char *buf, char *add)
{
	char	*newbuf;
	int		len;

	if (buf == 0)
		len = 0;
	else
		len = strlen(buf);
	newbuf = malloc(sizeof(*newbuf) * (len + strlen(add) + 1));
	if (newbuf == 0)
		fatal_error();
	newbuf[0] = 0;
	if (buf != 0)
		strcat(newbuf, buf);
	free(buf);
	strcat(newbuf, add);
	return (newbuf);
}


void fatal_error(void)
{
	write(2, "Fatal error\n", 12); //stderr = fildes 2
	exit(1);
}

int main(int argc, char *argv[])
{
	/*
	Step A: Setup
	Create the socket, bind to 127.0.0.1, and listen.
	Add the server socket FD to the active set and set max_fd.
	*/
	if (argc != 2) // ./mini_serv port_to_bind
	{
		write(2, "Wrong number of arguments\n", 26); //stderr = fildes 2
		exit(1);
	}

	//create server socket
	int sockfd, port;
	struct sockaddr_in servaddr;
	port = atoi(argv[1]);

	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd == -1)
		fatal_error();

	// assign IP, PORT 
	bzero(&servaddr, sizeof(servaddr));

	servaddr.sin_family = AF_INET; 
	servaddr.sin_addr.s_addr = htonl(2130706433); //127.0.0.1
	servaddr.sin_port = htons(port); 

	//bind
	if (bind(sockfd,(const struct sockaddr *)&servaddr, sizeof(servaddr)) != 0)
		fatal_error();

	//listen
	if (listen(sockfd, 128) != 0)
		fatal_error();

	//init sleect stuff
	FD_ZERO(&active);
	FD_SET(sockfd, &active);
	max_fd = sockfd;

	//poll like loop using sleect
	/*
	Step B: The Main Loop
		Copy active into ready_read and ready_write.
		Call select(max_fd + 1, &ready_read, &ready_write, NULL, NULL).
		Iterate from 0 to max_fd:
			If FD is the Server Socket & in ready_read:
				accept the new connection.
				Assign the next ID.
				Add new FD to active.
				Broadcast "client %d just arrived".
			If FD is a Client & in ready_read:
				recv data into a temporary buffer.
				If 0 returned: Client disconnected. Broadcast "left", close FD, and remove from active.
				If > 0 returned: Append to that client's buffer. Check for \n. If found, format the message "client %d: ..." and broadcast it to everyone except the sender.
	*/
	while (1)
	{
		//copy active into sets. select is "destructive." When you pass a set to select, it looks at all the FDs you gave it, and then wipes out any FD that isn't doing anything.
		ready_read = active;
		ready_write = active;

		if (select(max_fd + 1, &ready_read, &ready_write, NULL, NULL) < 0) //maxfd + 1 bc f your highest client is at FD 10, you tell select to check $10 + 1$ (the count includes 0).
			continue; //Think of select as a gatekeeper. If no one is talking and no one is connecting, your program sits at the select line and consumes 0% CPU. The moment a packet arrives over the internet, the Kernel "wakes" your program, and it moves to the next line to process the data.

		//loop thru all active fds
		for (int fd = 0; fd < max_fd + 1; fd++)
		{
			//check if ready to read
			if (!FD_ISSET(fd, &ready_read))
				continue;
			
			//Check if new connection
			if (fd == sockfd)
			{
				struct sockaddr_in client;
				socklen_t len = sizeof(client);

				int client_fd = accept(sockfd, (struct sockaddr *) &client, &len); //achtung ptr cast
				if (client_fd == -1)
					continue;

				//update maxfd to incl new client
				max_fd = (client_fd > max_fd) ? client_fd : max_fd;
				//assign id and incr counter
				ids[client_fd] = next++;
				//assign empty str to msg table
				msgs[client_fd] = calloc(1,1);
				//add client to active
				FD_SET(client_fd, &active);

				//broadcast that new client arrived
				sprintf(buf_write, "server: client %d just arrived\n", ids[client_fd]);
				//broadcast
				send_to_all(client_fd, buf_write, sockfd);
				// Restart loop to refresh select sets
				break;
			}
			else //NEW DATA TO READ coming from client
			{
				int bytes = recv(fd, buf_read, 424242 - 1, 0);
				//check if client disconnected
				if (bytes <= 0)
				{
					//broadcast msg
					sprintf(buf_write, "server: client %d just left\n", ids[fd]);
					send_to_all(fd, buf_write, sockfd);

					//remove client and free mem
					FD_CLR(fd, &active);
					close(fd); //!!!
					if (msgs[fd])
						free(msgs[fd]);
					msgs[fd] = NULL;
					break;
				}
				else // client still there
				{
					//terminate buf read at pos bytes
					buf_read[bytes] = '\0';
					//append to msgs table
					msgs[fd] = str_join(msgs[fd], buf_read);

					//process msg line by line
					char *msg_ptr;
					while (msgs[fd] && (msg_ptr = strstr(msgs[fd], "\n")))
					{
						//temp terminate at new linw
						*msg_ptr = '\0';
						sprintf(buf_write, "client %d: %s\n", ids[fd], msgs[fd]);
						send_to_all(fd, buf_write, sockfd);

						//shift remaining buf
						char *remaining = msg_ptr + 1;
						char *new_buf = malloc(strlen(remaining) + 1);
						if (!new_buf)
							fatal_error();
						strcpy(new_buf, remaining);

						free(msgs[fd]);
						msgs[fd] = new_buf;
					}
				}

			}

		}
		
	}

}