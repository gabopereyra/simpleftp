#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <err.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define BUFSIZE 512

/**
 * function: receive and analize the answer from the server
 * sd: socket descriptor
 * code: three leter numerical code to check if received
 * text: normally NULL but if a pointer if received as parameter
 *       then a copy of the optional message from the response
 *       is copied
 * return: result of code checking
 **/
bool recv_msg(int sd, int code, char *text) {
    char buffer[BUFSIZE], message[BUFSIZE];
    int recv_s, recv_code;

    // receive the answer
    recv_s = recv(sd, buffer, BUFSIZE, 0);

    // error checking
    if (recv_s < 0) warn("Error receiving data");
    if (recv_s == 0) errx(1, "Connection closed by host");

    // parsing the code and message receive from the answer
    sscanf(buffer, "%d %[^\r\n]\r\n", &recv_code, message);
    printf("%d %s\n", recv_code, message);
    // optional copy of parameters
    if (text) strcpy(text, message);
    // boolean test for the code
    return (code == recv_code) ? true : false;
}

/**
 * function: send command formated to the server
 * sd: socket descriptor
 * operation: four letters command
 * param: command parameters
 **/
void send_msg(int sd, char *operation, char *param) {
    char buffer[BUFSIZE] = "";

    // command formatting
    if (param != NULL)
        sprintf(buffer, "%s %s\r\n", operation, param);
    else
        sprintf(buffer, "%s\r\n", operation);

    // send command and check for errors
    if (send(sd, buffer, strlen(buffer), 0) < 0) {  
        warn("ERROR: sending command");
    }
}

/**
 * function: simple input from keyboard
 * return: input without ENTER key
 **/
char * read_input() {
    char *input = malloc(BUFSIZE);
    if (fgets(input, BUFSIZE, stdin)) {
        return strtok(input, "\n");
    }
    return NULL;
}

/**
 * function: login process from the client side
 * sd: socket descriptor
 **/
void authenticate(int sd) {
    char *input, desc[100];
    int code;

    // ask for user
    printf("Username: ");
    input = read_input();

    // send the command to the server
    send_msg(sd, "USER", input);

    // release memory
    free(input);

    // wait to receive password requirement and check for errors
    if (!recv_msg(sd, 331, NULL)) {
        errx(1, "Invalid username");
    }

    // ask for password
    printf("Password: ");
    input = read_input();

    // send the command to the server
    send_msg(sd, "PASS", input);

    // release memory
    free(input);

    // wait for answer and process it and check for errors
    if (!recv_msg(sd, 230, NULL)) {
        errx(1, "Authentication failed");
    }
}

int send_port_command(int sd) {
    //Create a new socket for data connection
    int data_sd = socket(AF_INET, SOCK_STREAM, 0);
    if (data_sd < 0) {
        perror("ERROR: failed to create data socket");
        exit(EXIT_FAILURE);
    }

    //assign a random port
    struct sockaddr_in data_addr;
    socklen_t addr_len = sizeof(data_addr);
    data_addr.sin_family = AF_INET;
    data_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    data_addr.sin_port = htons(0); 

    if (bind(data_sd, (struct sockaddr*)&data_addr, sizeof(data_addr)) < 0) {
        perror("ERROR: failed to bind data socket");
        close(data_sd);
        exit(EXIT_FAILURE);
    }

    // get assigned port
    if (getsockname(data_sd, (struct sockaddr*)&data_addr, &addr_len) < 0) {
        perror("ERROR: failed to get data socket name");
        close(data_sd);
        exit(EXIT_FAILURE);
    }

    unsigned char* ip = (unsigned char*)&data_addr.sin_addr.s_addr;
    unsigned char* port = (unsigned char*)&data_addr.sin_port;

    char port_param[BUFSIZE];
    sprintf(port_param, "%d,%d,%d,%d,%d,%d",
            ip[0], ip[1], ip[2], ip[3], ntohs(data_addr.sin_port) / 256, ntohs(data_addr.sin_port) % 256);

    // send port command
    send_msg(sd, "PORT", port_param);

    // listenig socket
   if (listen(data_sd, 1) < 0) {
        perror("ERROR: failed to listen on data socket");
        close(data_sd);
        exit(EXIT_FAILURE);
    }
    
   return data_sd;
}

/**
 * function: operation get
 * sd: socket descriptor
 * file_name: file name to get from the server
 **/
void get(int sd, char *file_name) {
    char desc[BUFSIZE], buffer[BUFSIZE];
    int f_size, recv_s, r_size = BUFSIZE;
    FILE *file;
    struct sockaddr_in data_addr;
    socklen_t data_addr_len = sizeof(data_addr);

    // send the PORT command to the server
    int data_socket = send_port_command(sd);

    // send the RETR command to the server
    send_msg(sd, "RETR", file_name);

    // check for the response
    if (!recv_msg(sd, 299, buffer)) {
        errx(1, "Failed to retrieve file");
    }

    // parsing the file size from the answer received
    // "File %s size %ld bytes"
    sscanf(buffer, "File %*s size %d bytes", &f_size);

    // open the file to write
    file = fopen(file_name, "w");
    if (file == NULL) {
        err(1, "Failed to open file");
    }

    //accepting connection from the server
    int data_sd = accept(data_socket, (struct sockaddr*)&data_addr, &data_addr_len);
    if (data_sd < 0) {
        perror("ERROR: failed to accept data connection");
        fclose(file);
        return;
    }

    // receive the file
    while ((recv_s = recv(sd, buffer, r_size, 0)) > 0) {
        fwrite(buffer, 1, recv_s, file);
    }

    if (recv_s < 0) {
        err(1, "Error receiving file");
    }

    // close the file
    fclose(file);

    //close data socket
    close(data_sd);

    // receive the OK from the server
    if (recv_msg(sd, 226, NULL)) {
        printf("File transfer successfully.");
    } else {
        errx(1, "File transfer not completed successfully");
    }
}

/**
 * function: operation quit
 * sd: socket descriptor
 **/
void quit(int sd) {
    // send command QUIT to the client
    send_msg(sd, "QUIT", NULL);

    // receive the answer from the server
    recv_msg(sd, 221, NULL);
}

/**
 * function: make all operations (get|quit)
 * sd: socket descriptor
 **/
void operate(int sd) {
    char *input, *op, *param;

    while (true) {
        printf("Operation: ");
        input = read_input();
        if (input == NULL)
            continue; // avoid empty input
        op = strtok(input, " ");
        // free(input);
        if (strcmp(op, "get") == 0) {
            param = strtok(NULL, " ");
            get(sd, param);
        } else if (strcmp(op, "quit") == 0) {
            quit(sd);
            break;
        } else {
            // new operations in the future
            printf("TODO: unexpected command\n");
        }
        free(input);
    }
    free(input);
}

/**
 * Run with
 *         ./myftp <SERVER_IP> <SERVER_PORT>
 **/
int main(int argc, char *argv[]) {
    int sd;
    struct sockaddr_in addr;

    // arguments checking
    if (argc != 3) {
        errx(1, "ERROR: not enough data, check the arguments and try again.");
    }

    // create socket and check for errors
    if ((sd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        err(1, "Socket creation failed");
    }

    // set socket data
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(atoi(argv[2]));
    if (inet_pton(AF_INET, argv[1], &(addr.sin_addr)) <= 0) {
        err(1, "ERROR: check the IP address and try again");
    }

    // connect and check for errors
    if (connect(sd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {     // Ver pag 13 donde menciona el casteo
        err(1, "Connection failed");
    }

    // if receive hello proceed with authenticate and operate if not warning
    if (recv_msg(sd, 220, NULL)) {
        authenticate(sd);
        operate(sd);
    } else {
        errx(1, "Failed to receive 'hello message' from server.");
    }

    // close socket
    close(sd);

    return 0;
}
