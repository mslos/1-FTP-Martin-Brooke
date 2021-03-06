#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdio.h>
#include <netinet/in.h>
#include <netinet/ip.h> /* superset of previous */
#include <string.h>
#include <errno.h>
#include <sys/select.h>
#include <sys/time.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <wordexp.h>
#include <fcntl.h>
#include <pwd.h>
#include <sys/sendfile.h>
#include <sys/stat.h> 


#include "FTPserver.h"


#define BUFFER_SIZE 500
#define MAX_NUM_OF_CLIENTS 5 //if changed need to add names for new clients
#define MAX_NAME_SIZE 10
#define MAX_PASS_SIZE 10
#define NUM_OF_USERS 5
#define FILE_TRANSFER_PORT 7000
#define MAX_PATH_SIZE 500

int main (int argc, char ** argv) {

	 // TCP protocol, same as opening a file
	int port, listener_sock, file_port, file_transfer_sock, len, client_fd;
	char * ip_addr;
	struct sockaddr_in server_addr, client_addr, file_transfer_addr;
	char buffer[BUFFER_SIZE];
	len = sizeof(client_addr); //necessary since accept requires lvalue - why?

	// Define array of users and initiate
	user authorized_users[NUM_OF_USERS];
	set_up_authorized_list(authorized_users);

	// Select() variables
	fd_set master_fds;
	fd_set temp_fds;
	int connection_fd_range;
	FD_ZERO(&master_fds);
	FD_ZERO(&temp_fds);


	// Read in port number and address from args
	ip_addr = argv[1];
	port = atoi(argv[2]);

	file_port = FILE_TRANSFER_PORT;
	// // Create socket descriptor
	// open_socket(&server_addr, &port, ip_addr, &listener_sock);
	

	// // Bind socket to port and address
	// if (bind(listener_sock, &server_addr, sizeof(server_addr)) < 0) {
 //      perror("Could not bind socket");
 //   	}

	// Opens Port for clients
	openTCPport(&server_addr, &port, ip_addr, &listener_sock);

	// Open port for file transfer
	openTCPport(&file_transfer_addr, &file_port, ip_addr, &file_transfer_sock);


	// Listen for clients, max number specified by MAX_NUM_OF_CLIENTS, block until first connection
	if ( listen(listener_sock,MAX_NUM_OF_CLIENTS) < 0)
		perror("Error in listening on  listener socket");
	

	memset(buffer,0,sizeof(buffer));

	// Add listener into fd set for select
	FD_SET(listener_sock, &master_fds);
	connection_fd_range = listener_sock;

	// Keep track of weather a file connection has been opened
	int first_connection = 1;

	// File descriptor list for file transfer
		// Select() variables
	fd_set file_transfer_fds;
	fd_set temp_file_fds;
	int file_fd_range;
	FD_ZERO(&file_transfer_fds);
	FD_ZERO(&temp_file_fds);

	struct timeval tv;
	tv.tv_usec = 200;

	while(1) {

		// Select() with error checking
		// Add connection to temp_fd before validating 
		temp_fds = master_fds;
		if( select(connection_fd_range+1, &temp_fds, NULL, NULL, &tv) == -1) {
			perror("Select() failed");
		}
		tv.tv_sec = 1;

		int fd, new_connection;
		// Iterate through all fds
		for(fd=3; fd<= connection_fd_range; fd++){

			// If there is data on a connection to read
			if((int)( FD_ISSET(fd, &temp_fds) )) {

				// Check for new connections
				if (fd==listener_sock){
					// accept_connection(new_connection, listener_sock, client_addr, master_fds, connection_fd_range,len);			
					len = sizeof(client_addr);
					// Accept new connection
					if((new_connection =accept(listener_sock,( struct sockaddr * restrict) &client_addr,&len))<0){
						perror("Server cannot accept connection\n");
					} else {
						// Add to master set
						FD_SET(new_connection, &master_fds);
						if (new_connection>connection_fd_range){
							connection_fd_range=new_connection;
							printf("New connection successfuly added into fd set\n");
						}
					}
				}
				// Event not on listener
				else {
					memset(buffer,0,BUFFER_SIZE);
					int num_of_bytes = read(fd, buffer, BUFFER_SIZE);

					if( num_of_bytes < 0)
						perror("Error reading incoming stream\n");
					else if (num_of_bytes == 0) {
						int j;
						for (j = 0; j<NUM_OF_USERS; j++) {
							if(authorized_users[j].usrFD == fd) {
								authorized_users[j].auth = 0;
								authorized_users[j].usrFD = -1;
							}
						}
						printf("Socket %d closed\n",fd);
						FD_CLR(fd, &master_fds);
						close(fd);
					}
					else {
						char command[100]; 
						char params[100]; 
						memset(command,0,sizeof(command));
						memset(params,0,sizeof(params));
						parse_command(command, params, buffer, fd);

						if (strcmp(command, "USER") == 0) {
							user_command(authorized_users, params, fd);
						}

					    else if (strcmp(command, "PASS") == 0) {
					    	pass_command(authorized_users, params, fd);
						}

						else if(strcmp(command, "PUT")==0){
							int j;
							for (j = 0; j<NUM_OF_USERS; j++) {
								if(authorized_users[j].usrFD == fd && authorized_users[j].auth == 1) {
									char msg1[] = "File upload request received.\n";
									write(fd, msg1, strlen(msg1) +1);
									put_command(&file_transfer_sock, &first_connection, &file_transfer_fds, 
										&file_fd_range, &file_transfer_addr, &(authorized_users[j]));
									printf("returned to while loop\n");
									char path [MAX_PATH_SIZE];
									memset(path,0,sizeof(path));
									strcat(path,authorized_users[j].current_directory);
									strcat(path,"/");
									strcat(path,params);
									if ((authorized_users[j].incoming_file = fopen(path,"a"))==NULL)
									{
										perror("Cannot create file");
									}
									memset(command,0,sizeof(command));
									memset(params,0,sizeof(params));
									break;
								}
							}
							if (j == NUM_OF_USERS) {
								char msg5[] = "File upload request: Authenticate first!\n";
								printf("%s",msg5);
								write(fd, msg5, strlen(msg5) +1);
							}
						}


						else if (strcmp(command, "LS") == 0) {
							list_server_files(authorized_users, params, fd);
						}
						else if (strcmp(command, "CD") == 0) {
							char tmp_cur[MAX_PATH_SIZE];
							memset(tmp_cur,0,sizeof(tmp_cur));
							strcpy(tmp_cur,"");
							int j;
							for (j = 0; j<NUM_OF_USERS; j++) {
								if((authorized_users[j].usrFD == fd)&& authorized_users[j].auth == 1) {
									// strcpy(tmp_cur, authorized_users[j].current_directory);
									break;
								}
							}
							// User is not authenticated
							// if (strcmp(tmp_cur,"")==0) {
							if (j==NUM_OF_USERS) {
								char msg5[] = "Authenticate yourself please\n";
								printf("%s",msg5);
								write(fd, msg5, strlen(msg5) +1);
								break;
							}
							// 
							else if(change_directory(authorized_users[j].current_directory, params)==0){
								char msg6[] = "Directory changed to ";
								strcat(msg6,authorized_users[j].current_directory);
								strcat(msg6, "\n");
								printf("%s",msg6);
								write(fd, msg6, strlen(msg6) +1);
							}
							else{
								char msg6[] = "Change directory failed, wrong directory\n";
								printf("%s",msg6);
								write(fd, msg6, strlen(msg6) +1);

							}
						}

						else if (strcmp(command, "PWD") == 0) {
							int authenticated =0;
							int j;
							for (j = 0; j<NUM_OF_USERS; j++) {
								if(authorized_users[j].usrFD == fd && authorized_users[j].auth == 1) {
									char msg5[MAX_PATH_SIZE];
									strcpy(msg5,authorized_users[j].current_directory);
									strcat(msg5,"\n");
									printf("%s",msg5);
									write(fd, msg5, strlen(msg5) +1);
									authenticated = 1;
									break;
								}

							}
							if(!authenticated){
								char msg5[] = "Authenticate yourself please\n";
								printf("%s",msg5);
								write(fd, msg5, strlen(msg5) +1);
							}
						}

						else if(strcmp(command, "GET")==0){
							int j;
							for (j = 0; j<NUM_OF_USERS; j++) {
								if(authorized_users[j].usrFD == fd && authorized_users[j].auth == 1) {
									char path [MAX_PATH_SIZE];
									memset(path,0,sizeof(path));
									strcat(path,authorized_users[j].current_directory);
									strcat(path,"/");
									strcat(path,params);

									int src = open(path, O_RDONLY); 
									//opens file here for error checking, closes in function get_command()
	
									if( src < 0 || (strcmp(params,"") == 0)) {
										char msg [] = "Error opening file / File not found\n";
										printf("%s",msg);
										write(fd, msg, strlen(msg));
										break;
									} else {
										char msg [] = "Success, file open.\n";
										write(fd, msg, strlen(msg));
									}
									get_command(&file_transfer_sock, &first_connection, &file_transfer_fds, 
										&file_fd_range, &file_transfer_addr, path, src);
									printf("returned from get_command into main\n");
									memset(command,0,sizeof(command));
									memset(params,0,sizeof(params));
									break;
								}
							}
							if (j == NUM_OF_USERS) {
								char msg5[] = "File download request: Authenticate first!\n";
								printf("%s",msg5);
								write(fd, msg5, strlen(msg5) +1);
							}

						}
						else {
						  	printf("An invalid FTP command.\n");
						}

					}
					

				}

			}
		}
		
		if(!first_connection){
			printf("in file transfer loop\n");
			// Select for file transfers
			temp_file_fds = file_transfer_fds;
			if( select(file_fd_range+1, &temp_file_fds, NULL, NULL, &tv) == -1) {
				perror("Select() failed\n");
			}
			tv.tv_usec = 200;

			// Iterate through all fds
			for(fd=3; fd<= file_fd_range; fd++){

				// If there is data on a connection to read
				if((int)( FD_ISSET(fd, &temp_file_fds) )) {

					// Check for new connections
					if (fd==file_transfer_sock){
					}
					// Event not on listener
					else {
						//memset(buffer,0,BUFFER_SIZE);
						printf("in else\n");

						int num_of_bytes = read(fd, buffer, BUFFER_SIZE);

						// printf("Buffer is %s\n", buffer);
						int j;

						if( num_of_bytes < 0)
							perror("Error reading incoming stream\n");
						else if (num_of_bytes == 0) {
							printf("Socket %d closed\n",fd);
							FD_CLR(fd, &file_transfer_fds);
							close(fd);
							for (j = 0; j<NUM_OF_USERS; j++) {
								if(authorized_users[j].transFD == fd) {
									printf("Closing the file transfer\n");
									fclose(authorized_users[j].incoming_file);
									break;
								}
							}						
						}
						else { 
							for (j = 0; j<NUM_OF_USERS; j++) {
								if(authorized_users[j].transFD == fd) {
									printf("TransFD found\n");
									fputs(buffer,authorized_users[j].incoming_file);
								}
							}						
						}
						

					}
					printf("After else statement\n");

				}
			}
		}
	}
}


int open_socket(struct sockaddr_in * myaddr, int * port, char * addr, int * sock) {
	*sock = socket(AF_INET, SOCK_STREAM,0);

	if (*sock < 0) {
      perror("Error opening socket\n");
   }

	myaddr->sin_family = AF_INET;
	myaddr->sin_port = htons(*port);
	
	if( inet_aton(addr, &(myaddr->sin_addr))==0 ) {
		perror("Error, cannot translate IP into binary\n");
	}

	return 0;
}

void openTCPport(struct sockaddr_in * myaddr, int *port, char * ip_addr, int * sock){
	// Create filetransfer socket descriptor
	open_socket(myaddr, port, ip_addr, sock);

	if (bind(*sock, (const struct sockaddr *) myaddr, sizeof(*myaddr)) < 0) {
	      perror("Could not bind socket");
	}
}

//Sets up the list of permitted users (they still need to authorize)
void set_up_authorized_list(user * usr) {
	for (int i = 0; i < NUM_OF_USERS; i++) {
		usr[i].name = (char *) malloc(MAX_NAME_SIZE);
		usr[i].pass = (char *) malloc(MAX_PASS_SIZE);
		usr[i].current_directory = (char *) malloc(MAX_PATH_SIZE);
		// strcpy(usr[i].current_directory, "/home/");
		getcwd(usr[i].current_directory, MAX_PATH_SIZE);
		usr[i].auth = 0;
		usr[i].usrFD = -1;
	}

	strcpy(usr[0].name, "Nabil");
	strcpy(usr[0].pass, "1234");
	strcpy(usr[1].name, "Brooke");
	strcpy(usr[1].pass, "qwer");
	strcpy(usr[2].name, "Martin");
	strcpy(usr[2].pass, "iluvnet");
	strcpy(usr[3].name, "Yasir");
	strcpy(usr[3].pass, "ethernet");
	strcpy(usr[4].name, "Stefan");
	strcpy(usr[4].pass, "~!:?");
}

void parse_command(char *command, char * params, char * buffer, int fd){

	printf("Client fd %d, says: %s\n",fd,buffer);
	sscanf(buffer,"%s %s", command , params);
	printf("Command is %s, params are %s\n", command, params);
}


void user_command(user * authorized_users, char * params, int fd){

	printf("USER command activated.\n");
	for (int j = 0; j<NUM_OF_USERS; j++) {

		printf("Username check: %s \n", (authorized_users[j].name));

		if(strcmp(authorized_users[j].name, params) == 0) {
			printf("User found in database.\n");
			authorized_users[j].usrFD = fd;
			char msg1[] = "Username OK, password required\n";
			write(fd, msg1, strlen(msg1) +1);
			break; // Why this break?
		}

		else if (j == NUM_OF_USERS-1) {
			char msg2[] = "Username does not exist\n";
			printf("%s",msg2);
			write(fd, msg2, strlen(msg2) +1);
		}
	}
}

void pass_command(user * authorized_users, char * params, int fd){
	int j;

	for (j = 0; j<NUM_OF_USERS; j++) {
		if(authorized_users[j].usrFD == fd) {
			if(strcmp(authorized_users[j].pass, params) == 0) {
				char msg3[] = "Authentication complete\n";
				printf("%s",msg3);
				write(fd, msg3, strlen(msg3) +1);
				authorized_users[j].auth = 1;
				break;
			}
			else {
				char msg4[] = "Password incorrect, try again!\n";
				printf("%s",msg4);
				write(fd, msg4, strlen(msg4) +1);
				break;
			}

		}

	}

	if (j == NUM_OF_USERS) {
		char msg5[] = "Set USER first\n";
		printf("%s",msg5);
		write(fd, msg5, strlen(msg5) +1);
	}

}

void put_command(int * file_transfer_sock, int * first_connection, 
	fd_set * file_transfer_fds, int * file_fd_range, struct sockaddr_in * file_transfer_addr, user * usr){
	int new_connection, len;
	len = sizeof(file_transfer_addr);
	// Listen for clients, max number specified by MAX_NUM_OF_CLIENTS, block until first connection
	if(*first_connection){
		printf("file connection starting\n");
		if ( listen(*file_transfer_sock,MAX_NUM_OF_CLIENTS) < 0)
			perror("Error in listening on file transfer socket\n");
		else{
			printf("First connection\n");
			*first_connection = 0;
		}
			// Add listener into fd set for select
		FD_SET(*file_transfer_sock, file_transfer_fds);

		*file_fd_range = *file_transfer_sock;

	}

	if((new_connection = accept(*file_transfer_sock,( struct sockaddr * restrict) file_transfer_addr,&len))<0){
		perror("Server cannot accept file transfer connection");
	} 
	else {
		printf("Accepted connection\n");
		usr->transFD = new_connection;
		// Add to master set
		FD_SET(new_connection, file_transfer_fds);
		if (new_connection>*file_fd_range){
			*file_fd_range=new_connection;
			printf("New file transfer connection successfuly added into fd set\n");
		}
	}

}


int change_directory(char * current_directory, char * new_directory){
	char new_path[MAX_PATH_SIZE]; 
	if(new_directory[0] == '/') {
		strcpy(new_path,new_directory);
	}
	else if (new_directory[0]=='~') {
	   	wordexp_t p;
	   	wordexp(new_directory, &p, 0);
	    strcpy(new_path,p.we_wordv[0]);
	    wordfree(&p);
	}
	else
		strcat(strcat(strcpy(new_path,current_directory),"/"),new_directory);
		// printf("Trying to resolve %s\n", new_path);
	DIR* dir = opendir(new_path);

	if (dir){
		realpath(new_path,current_directory);
		printf("Changed directory to %s\n", new_directory);
	    closedir(dir);
	    return 0;
	}
	else if (ENOENT == errno){
	    //Directory does not exist.
	    printf("Directory does not exist. \n");
	    return 1;
	}
	else {
	    printf("CD failed.\n");
	    return 2;
	}
}


int list_server_files(user * authorized_users, char * path, int fd){

	printf("List command called\n");
	char tmp_cur[MAX_PATH_SIZE];
	memset(tmp_cur,0,sizeof(tmp_cur));
	strcpy(tmp_cur,"");

	// Check if user is authenticated
	int j;
	for (j = 0; j<NUM_OF_USERS; j++) {
		if(authorized_users[j].usrFD == fd && authorized_users[j].auth == 1) {
			strcpy(tmp_cur, authorized_users[j].current_directory);
			break;
		}

	}

	// User is not authenticated
	if (strcmp(tmp_cur,"")==0) {
		char msg5[] = "Authenticate yourself please\n";
		printf("%s",msg5);
		write(fd, msg5, strlen(msg5) +1);
		return 0;
	}
	else{
		printf("Current path %s\n", tmp_cur);

		if(strcmp(path,"")) {
			if(change_directory(tmp_cur, path) != 0) {
				
				char msg2[] = "Error, directory does not exist!\n";
				printf("%s",msg2);
				write(fd, msg2, strlen(msg2) +1);
				return -1;
			}
		}

		DIR *directory_path;
		struct dirent *file_pointer;     
		directory_path = opendir (tmp_cur);

		char files[2000];
		memset(files,0,sizeof(files));


		if (directory_path != NULL){
			while (file_pointer = readdir (directory_path)){
				if(strlen(files) + strlen(file_pointer->d_name) > 2000) break;
			  	strcat(files,file_pointer->d_name);
				strcat(files,"\n");
			}
			(void) closedir (directory_path);
		}
		else{
			char msg5[] = "Couldn't open the directory\n";
			printf("%s",msg5);
			write(fd, msg5, strlen(msg5) +1);
			// perror ("Couldn't open the directory\n");
			return -1;
		}

		write(fd, files, strlen(files) +1);
		return 0;
	}
}


void get_command(int * file_transfer_sock, int * first_connection, 
	fd_set * file_transfer_fds, int * file_fd_range, struct sockaddr_in * file_transfer_addr, 
	char * path, int src) {

	struct stat st; // Information about the file
	int new_connection, len;
	len = sizeof(file_transfer_addr);

	fstat(src, &st);
	printf("Size of file is %d\n",(int)st.st_size);
	int bytes_sent;

	if(*first_connection){
			printf("file connection starting\n");
			if ( listen(*file_transfer_sock,MAX_NUM_OF_CLIENTS) < 0)
				perror("Error in listening on file transfer socket\n");
			else{
				printf("First connection\n");
				*first_connection = 0;
			}
					// Add listener into fd set for select
		FD_SET(*file_transfer_sock, file_transfer_fds);

		*file_fd_range = *file_transfer_sock;

	}
	
	if((new_connection = accept(*file_transfer_sock,( struct sockaddr * restrict) file_transfer_addr,&len))<0){
		perror("Server cannot accept file transfer connection");
	} 
	else {
		char buf[20];
		read(*file_transfer_sock, buf, 20);
		printf("About to send the file\n");
		bytes_sent = sendfile(new_connection,src,NULL,st.st_size);

		if(bytes_sent <= 0) {
			printf("Error, send file failed.\n");
		}
		else if (bytes_sent < st.st_size) {
			printf("Warning: Did not PUT all bytes of the file.\n");
		}

		close(new_connection);
		close(src);

	}
	printf("returned to while loop\n");
}