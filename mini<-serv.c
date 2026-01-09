#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct s_client{
	int id;
	char msg[500000 - 20];
} t_client;

t_client clients[1024];
fd_set readfds, writefds, activefds;
char buffread[500000], buffwrite[500000];
int max = 0, nextid = 0;

void err(char *str){
	write(2, str, strlen(str));
	exit(1);
}

void sendMSG(int sendfd){
	for (int fd = 0; fd <= max; fd++){
		if (FD_ISSET(fd, &writefds) && fd != sendfd)
			write(fd, buffwrite, strlen(buffwrite));
	}
}

int main(int ac, char **av){
	if (ac != 2)
		err("Wrong number of arguments\n");
	int sockfd = max = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd == -1)
		err("Fatal error\n");
	FD_ZERO(&activefds); // initialize fd_set and put sockfd on fd_set
	FD_SET(sockfd, &activefds);

	struct sockaddr_in servaddr;
	servaddr.sin_family = AF_INET; 
	servaddr.sin_addr.s_addr = htonl(2130706433); //127.0.0.1
	servaddr.sin_port = htons(atoi(av[1]));
	if ((bind(sockfd, (const struct sockaddr *)&servaddr, sizeof(servaddr))) != 0)
		err("Fatal error\n");
	if (listen(sockfd, 10) != 0)
		err("Fatal error\n");

	while (1){
		readfds = writefds = activefds;
		if (select(max + 1, &readfds, &writefds, 0, 0) < 0) continue;

		for (int fd = 0; fd <= max; fd++){
			if (!FD_ISSET(fd, &readfds)) continue;
			if (fd == sockfd){ // client arrived - accept new connection and put clientsock on fd_set
				int clientsock = accept(fd, 0, 0);
				max = (clientsock > max) ? clientsock : max;
				clients[clientsock].id = nextid++;
				bzero(clients[clientsock].msg, strlen(clients[clientsock].msg));
				FD_SET(clientsock, &activefds);
				sprintf(buffwrite, "server: client %d just arrived\n", clients[clientsock].id);
				sendMSG(clientsock);
			}else{
				int read = recv(fd, buffread, sizeof(buffread), 0);
				if (read <= 0){ // client left - clear and close fd
					sprintf(buffwrite, "server: client %d just left\n", clients[fd].id);
					sendMSG(fd);
					FD_CLR(fd, &activefds);
					close(fd);
				}else{ // client message - handle client message
					for (int i = 0, j = strlen(clients[fd].msg); i < read; i++, j++){
						clients[fd].msg[j] = buffread[i];
						if (clients[fd].msg[j] == '\n'){
							clients[fd].msg[j] = 0;
							sprintf(buffwrite, "client %d: %s\n", clients[fd].id, clients[fd].msg);
							sendMSG(fd);
							bzero(clients[fd].msg, strlen(clients[fd].msg));
							j = -1;
						}
					}
				}
			}
			break;
		}
	}
}