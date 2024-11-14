/** 
 * @author Burton O Sumner
 * @brief  Created for homework 1 for CSCE416: Creating a two-way chat server
 *         and client for communication between server and single client, using 
 *         TCP.
 * @credit Professor Nelakuditi for providing the template sample code for 
 *         creating a one-way message server and client with TCP using the
 *         POSIX Socket (BSD Socket) API.
 *         */

// For my variadic error handling function, errprintf_and_exit
#include <errno.h>
#include <stdarg.h>

// For network connection establishment functions and socket file descriptor
// syscalls like read and write.
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>

// For creating and parsing timestamps to send to and receive from server 
// respectively.
#include <time.h>

// Mem manip functions.
#include <string.h>

// Ensured sizes for integer data types.
#include <stdint.h>

// Console IO
#include <stdio.h>

// For several libc standard functions, like
#include <stdlib.h>




#define MAX_MSG_LEN 1023
#define MSG_BUF_LEN 1024
#define MSG_HEADER_LEN 12

#define DATA_BUF_LEN 1036

#define MSGLEN_HDRFLD_OFFSET 8
#define TIMESTAMP_HDRFIELD_LEN 8
#define MSGLEN_HDRFLD_LEN 4

void errprintf_and_exit(const char *restrict fmt, int exit_value, ...) 
  __attribute__ ((noreturn));

void errprintf_and_exit(const char *restrict fmt, int exit_val, ...) {
  va_list args;
  fputs("\x1b[1;31m[Error]:\x1b[0m ", stderr);
  va_start(args, exit_val);
  vfprintf(stderr, fmt, args);
  va_end(args);
  exit(exit_val);
}

/** 
 * @brief Parse out port number, but also exit program if the port number given
 * is invalid.
 * @return parsed-out port number.*/
int validate_portnumber(char *port_str) {
  int i;
  char c;
  for (c = port_str[i = 0]; c; c = port_str[++i]) {
    if (c < '0' || c > '9')
      errprintf_and_exit("Invalid port number passed through.\nPort number arg,"
          " \"%s\", contained non-numeric, '%c'.\nPort number arg should be a "
          "whole number within the interval (1000, 65535].\n", 1, port_str, c);
  }
  
  i = atoi(port_str);
  if (i <= 1000 || (i > 65535)) {
    errprintf_and_exit("Invalid port number passed through.\nPort number given,"
        " %d, was outside of valid range, (1000, 65535]. Port number must be\n"
        "within this range, as port numbers only have 16b to be encoded with"
        "\nand port numbers below 1000 run the risk of being an already-"
        "\nreserved port number.\n", 1, i);
  }

  return i;
}

int establish_connection(char *server_name, struct sockaddr_in *server_addr, 
    int port_number) {
  struct hostent *host_entry;
  int ret_sockfd, outcome;
  ret_sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (0 > ret_sockfd) {
    outcome = errno;
    errprintf_and_exit("Failed to open socket.\nDetails from \x1b[1;34msocket"
        "\x1b[0m syscall: %s\n", 1, strerror(outcome));

  }

  server_addr->sin_family = AF_INET;
  server_addr->sin_port = htons(port_number);
  
  host_entry = gethostbyname(server_name);
  if (!host_entry) {
    outcome = errno;
    errprintf_and_exit("Failed to get host address from given server host name"
        ".\nDetails from \x1b[1;34mgethostbyname\x1b[0m: %s\n", 1, 
        strerror(outcome));
  }


  memcpy(((void*) &(server_addr->sin_addr)), ((void*) (host_entry->h_addr)),
      host_entry->h_length);

  puts("Requesting to connect to server ... ");
  if (0 > connect(ret_sockfd, ((struct sockaddr*) server_addr), 
        sizeof(*server_addr))) {
    outcome = errno;
    errprintf_and_exit("Failed to connect to server at specified host name.\n"
        "Details from \x1b[1;34mconnect\x1b[0m syscall: %s\n", 1, 
        strerror(outcome));
  }


  return ret_sockfd;


}


int receive_msg(int sfd, uint8_t *databuf) {
  int len, outcome;
  
  memset((void*)databuf, 0x0, DATA_BUF_LEN);
  outcome = read(sfd, (void*)databuf, MSG_HEADER_LEN);
  
  if (0 >= outcome) {
    return outcome;
  }
  
  len = *((int32_t*) (databuf + MSGLEN_HDRFLD_OFFSET));
  return read(sfd, ((void*) (databuf+MSG_HEADER_LEN)), len);
}

void send_display_name(int sfd, char *name, int len, 
    char *server_display_name) {
  uint8_t header[MSG_HEADER_LEN] = {0};
  time_t *timestamp_field = (time_t*)header;
  int32_t *msglen_field = ((int32_t*) (header + MSGLEN_HDRFLD_OFFSET));
  int outcome;
  if (0 > len) {
    len = strlen(name);
  }

  if (len > MAX_MSG_LEN) {
    fprintf(stderr, "\x1b[1;33m[Warning]:\x1b[0m Display name is too "
        "long to fit in data communication buffer.\nTruncating message from "
        "length %d down to %d\n", len, MAX_MSG_LEN);
    len = MAX_MSG_LEN;
  }

  *msglen_field = len;
  time(timestamp_field);

  outcome = write(sfd, (void*)header, MSG_HEADER_LEN);

  if (0 > outcome) {
    goto WRITE_ERROR;
  } else if (!outcome) {
    goto SERVER_DISCONNECTED;
  }

  outcome = write(sfd, (void*)name, len);
  
  if (0 > outcome) {
    goto WRITE_ERROR;
  } else if (!outcome) {
    goto SERVER_DISCONNECTED;
  }
  return;

WRITE_ERROR:
  outcome = errno;
  close(sfd);
  errprintf_and_exit("An error occurred during display name exchange. Failed "
      "to send display name to server.\nDetails from \x1b[1;31mwrite\x1b[0m"
      " syscall: %s\n", 1, strerror(outcome));
SERVER_DISCONNECTED:
  fprintf(stderr, "\x1b[1;31m[Error]:\x1b[34m %s\x1b[0m (server) unexpectedly"
      " during display name exchange.\n", server_display_name);
  close(sfd);
  free((void*)server_display_name);
  exit(EXIT_FAILURE);
}

char *receive_server_display_name(int sfd, uint8_t *databuf) {
  void *msg_buf = ((void*) (databuf + MSG_HEADER_LEN));
  int32_t *msglen_field = ((int32_t*) (databuf + MSGLEN_HDRFLD_OFFSET));
  char *ret;
  int outcome, len;
  
  outcome = read(sfd, (void*)databuf, MSG_HEADER_LEN);
  
  if (0 > outcome) {
    goto READ_ERR;
  } else if (!outcome) {
    goto SERVER_DISCONNECTED;
  }

  outcome = read(sfd, msg_buf, MSG_BUF_LEN);

  ret = (char*)malloc((1+ (len = *msglen_field)));  // alloc room
                                                    // for the 
                                                    // server's
                                                    // display name

  memcpy((void*)ret, msg_buf, len);  // Copy in the server
                              // display name from the
                              // message received buff
                              // to the charblock 
                              // that we alloc'd.

  ret[len] = '\0';  // null terminate the server display name string
  return ret;
READ_ERR:
  outcome = errno;
  close(sfd);
  errprintf_and_exit("An error occurred during display name exchange. Failed "
      "to receive display name from server.\nDetails from \x1b[1;31mread\x1b[0m"
      " syscall: %s\n", 1, strerror(outcome));
SERVER_DISCONNECTED:
  close(sfd);
  errprintf_and_exit("Server unexpectedly disconnected during name exchange.\n", 
      1);
}

int prompt_and_send_msg(int sfd, uint8_t *buf) {
  char *msg_buf = ((char*) (buf + MSG_HEADER_LEN));
  
  time_t *timestamp_field = (time_t*)buf;
  
  int32_t *msglen_field = ((int32_t*) (buf + MSGLEN_HDRFLD_OFFSET));
  int len;
  
  memset((void*)buf, 0x0, DATA_BUF_LEN);
  
  fputs("Message > ", stdout);
  fgets(msg_buf, MAX_MSG_LEN, stdin);
  len = *msglen_field = strlen(msg_buf);
  time(timestamp_field);

  return write(sfd, (void*)buf, MSG_HEADER_LEN + len);
}


int main(int argc, char *argv[]) {
  // Initializing with {0} ensures that the buffer will start out with all bytes
  // set to zero.
  uint8_t databuf[DATA_BUF_LEN] = {0};
  struct sockaddr_in server_addr = {0};

  char *servername, *server_dispname = NULL, 
       *msg_buf = ((char*) (databuf + MSG_HEADER_LEN));

  time_t *timestamp_field = ((time_t*)databuf);


  struct tm *time_obj;

  int port, sfd, dispname_len, outcome;


  if (4 != argc) {
    errprintf_and_exit("Invalid amount of arguments. See below for usage.\n"
        "\x1b[1;34mUsage:\x1b[0m %s <server name> <server port> "
        "<your display name>\n", 1, argv[0]);
  }

  dispname_len = strlen(argv[3]);



  servername = argv[1];

  port = validate_portnumber(argv[2]);

  sfd = establish_connection(servername, &server_addr, port);

  printf("Connected to server at \x1b[1;34m%s:%hu\x1b[0m "
      "([IP address]:[port number])\n", servername, port);
 
  server_dispname = receive_server_display_name(sfd, databuf);
  time_obj = localtime(timestamp_field);  // Parse out the timestamp's value
                                          // and store the data of it in 
                                          // time_obj

  send_display_name(sfd, argv[3], dispname_len, server_dispname);

  printf("Info exchange complete: Server sent display name, \x1b[1;33m%s\x1b[0m"
      ", at \x1b[1;34m%s\x1b[0m\n", server_dispname, asctime(time_obj));
  
  while (1) {
    outcome = prompt_and_send_msg(sfd, databuf);
    if (0 > outcome) {
      goto ERR_WRITE_DISRUPTED;
    } else if (!outcome) {
      break;
    }
    puts("Waiting for response...");

    outcome = receive_msg(sfd, databuf);
    if (0 > outcome) {
      goto ERR_WRITE_DISRUPTED;
    } else if(!outcome) {
      break;
    }

    time_obj = localtime(timestamp_field);

    printf("\x1b[1;34m%s\t\x1b[33m%s:\x1b[0m\t%s\n",
        asctime(time_obj), server_dispname, msg_buf);
  }

  printf("\x1b[1;33m%s\x1b[0m disconnected.\n", server_dispname);
  close(sfd);
  free((void*)server_dispname);
  exit(0);
  return 0;
ERR_WRITE_DISRUPTED:
  outcome = errno;
  close(sfd);
  free((void*)server_dispname);
  errprintf_and_exit("Failed to write to server connection socket.\n"
      "Details from \x1b[1;34mwrite\x1b[0m syscall: %s\n", 1, 
      strerror(outcome));
  return 1;
}
