/** 
 * @author Burton O Sumner
 * @brief  Created for homework 1 for CSCE416: Creating a two-way chat server
 *         and client for communication between server and single client, using
 *         TCP.
 * @credit Professor Nelakuditi for providing the template sample code for 
 *         creating a one-way message server and client with TCP using the
 *         POSIX Socket (BSD Socket) API.
 *         */

// For error handler (and subsequent exit) function, errprintf_and_exit
#include <errno.h>
#include <stdarg.h>

// For all networking functions and file descriptor syscalls used.
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <unistd.h>

// For timestamp encoding (time(time_t*)) and decoding 
// (localtime(time_t*) and asctime(struct tm*)).
#include <time.h>

// For buffer manip functions.
#include <string.h>


#include <stdio.h>

// For malloc of appropriate resources.
#include <stdlib.h>

// For access to types whose sizeof is ensured regardless of system 
// architecture.
#include <stdint.h>



#define MAX_MSG_LEN 1023
#define MSG_BUF_LEN 1024

#define MSG_HEADER_LEN 12

#define MSGLEN_HDRFIELD_OFFSET 8

#define DATA_BUFFER_LEN 1036


char *client_name = NULL;
int client_sockfd = -1;

void errprintf_and_exit(const char *restrict, int fmt, ...) 
  __attribute__ ((noreturn));

void errprintf_and_exit(const char *restrict fmt, int exit_val, ...) {
  va_list args;
  fputs("\x1b[1;31m[Error]:\x1b[0m ", stderr);
  va_start(args, exit_val);
  vfprintf(stderr, fmt, args);
  va_end(args);
  exit(exit_val);
}


void close_dynamic_assets(void) {
  if (-1 != client_sockfd) {
    close(client_sockfd);
    client_sockfd = -1;
  }
  if (client_name) {
    free((void*)client_name);
    client_name = NULL;
  }
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

void bind_addr_to_listening_socket(int socket_filedes, int port_no) {
  struct sockaddr_in sock_address;
  int errval;
  sock_address.sin_port = htons(port_no);
  sock_address.sin_addr.s_addr = htonl(INADDR_ANY);
  sock_address.sin_family = AF_INET;
  if (0 > bind(socket_filedes, (struct sockaddr*) &sock_address, 
        sizeof(struct sockaddr_in))) {
    errval = errno;  // Get value of errno before close syscall below has a 
                     // chance to potentially overwrite it if it fails too.
    close(socket_filedes);
    errprintf_and_exit("Failed to bind the port number to the listening socket"
        "\nDetails from \x1b[1;34mbind\x1b[0m: %s", 1, strerror(errval));
  }
}

int wait_for_and_accept_client(int listening_sfd, 
    struct sockaddr_in *client_addr_dest, socklen_t *client_addrlen_dest) {
  struct sockaddr_in client_addr;
  int errnoval, ret;
  *client_addrlen_dest = sizeof(*client_addr_dest);

  // Pass thru locally-scoped sockaddr_in reference's address, since the param
  // for it in the accept function's signature declared with restrict keyword.
  // To adhere to the restrictions imposed by the restrict keyword, I
  // pass thru address of the local reference, and then copy the contents of
  // the local reference to the parameterized struct sockaddr ptr, 
  // client_addr_dest.
  ret = accept(listening_sfd,((struct sockaddr*) (&client_addr)), 
      client_addrlen_dest);
  memcpy((void*)client_addr_dest, (void*) &client_addr, 
      sizeof(struct sockaddr_in));
  if (0 > ret) {
    errnoval = errno;
    close(listening_sfd);
    errprintf_and_exit("Failed to accept client's connection request.\n"
        "Details from \x1b[1;31maccept\x1b[0m syscall: %s\n", 1,
        strerror(errnoval));
  }
  close(listening_sfd);
  return ret;
}



void send_display_name(int sock_fd, char *name, int len) {
  uint8_t header[MSG_HEADER_LEN] = {0};  // Initializing with this syntax
                                         // ensures that the header is fully
                                         // zero'd out.
  time_t *timestamp_headerfield = (time_t*)header;
  int32_t *msglen_headerfield = ((int32_t*) (header + MSGLEN_HDRFIELD_OFFSET)),
      outcome;

  if (len > MAX_MSG_LEN) {
    fprintf(stderr, "\x1b[1;33m[Warning]:\x1b[0m Display name too large to fit "
        "into client communication buffer.\nTruncating display name down from "
        "length, %d, to %d.\n", len, MAX_MSG_LEN);
    len = MAX_MSG_LEN;
  }

  *msglen_headerfield = len;
  time(timestamp_headerfield);
  
  outcome = write(sock_fd, (void*)header, MSG_HEADER_LEN);
  if (0 > outcome) {
    goto UNEXPECTED_WRITE_FAILURE;
  } else if (!outcome) {
    goto DISCONNECTED_CLIENT;
  }
  
  outcome = write(sock_fd, (void*)name, len);
  if (0 > outcome) {
    goto UNEXPECTED_WRITE_FAILURE;
  } else if (!outcome) {
    goto DISCONNECTED_CLIENT;
  }

  // Explicit return statement here so that program doesn't step instruction
  // into error and disconnection handler label regions.
  return;

UNEXPECTED_WRITE_FAILURE:
  outcome = errno;
  errprintf_and_exit("Failed to send display name through server connection "
      "socket.\nDetails from \x1b[1;34mwrite\x1b[0m syscall: %s\n", 1,
      strerror(outcome));

DISCONNECTED_CLIENT:
    errprintf_and_exit("Client unexpectedly disconnected during display name "
        "exchange.\n", 1);

}

char *receive_client_display_name(int client_sfd, uint8_t *databuf) {
  int32_t *msglen_headerfield = ((int32_t*) (databuf + MSGLEN_HDRFIELD_OFFSET));
  void *msg_buf = ((void*) (databuf + MSG_HEADER_LEN));
  char *ret;
  int outcome, len;

  memset((void*)databuf, 0x0, DATA_BUFFER_LEN);
  
  outcome = read(client_sfd, (void*)databuf, MSG_HEADER_LEN);
  
  if (0 > outcome) {
    goto ERR_READ_FAILURE;
  } else if (!outcome) {
    goto CLIENT_DISCONNECTED;
  }

  outcome = read(client_sfd, msg_buf, (len = *msglen_headerfield));
  // Get message content whose length is discerned by the message header.

  if (0 > outcome) {
    goto ERR_READ_FAILURE;
  } else if (!outcome) {
    goto CLIENT_DISCONNECTED;
  }

  
  ret = (char*)malloc(len + 1);
  memcpy((void*)ret, (void*)msg_buf, len);
  ret[len] = '\0';

  return ret;

ERR_READ_FAILURE:
  outcome = errno;
  errprintf_and_exit("Unexpected failure to read from client socket during "
      "chat initialization data exchange.\nDetails from \x1b[1;31mread\x1b[0m"
      " syscall: %s\n", 1, strerror(outcome));
CLIENT_DISCONNECTED:
  close(client_sfd);
  errprintf_and_exit("Client unexpectedly disconnected during display name "
      "exchange.\n", 1);
}

int prompt_and_message_client(int client_sfd, uint8_t *databuf) {
  char *msg_buf = ((char*) (databuf + MSG_HEADER_LEN));
  int32_t *msglen_headerfield = ((int32_t*) (databuf + MSGLEN_HDRFIELD_OFFSET));
  int len;
  
  memset((void*)databuf, 0, DATA_BUFFER_LEN);

  fputs("Message > ", stdout);  // Use fputs with stdout, instead of puts, since
                                // puts always adds a newline char at the end,
                                // and I want the prompt to be on the same line
                                // as the entry written by user.

  fgets(msg_buf, MAX_MSG_LEN, stdin);
  len = *msglen_headerfield = strlen(msg_buf);
  time((time_t*)databuf);
  return write(client_sfd, (void*)databuf, MSG_HEADER_LEN + len);
}

int receive_client_message(int client_sfd, uint8_t *databuf) {
  void *msg_buf = ((void*) (databuf + MSG_HEADER_LEN)),
       *header = (void*)databuf;
  int32_t *msglen_headerfield = ((int32_t*) (databuf + MSGLEN_HDRFIELD_OFFSET));
  int outcome;

  memset((void*)databuf, 0, DATA_BUFFER_LEN);

  outcome = read(client_sfd, header, MSG_HEADER_LEN);
  if (0 >= outcome) {
    return outcome;
  } else {
    return read(client_sfd, (void*)msg_buf, *msglen_headerfield);
  }
}




int main(int argc, char *argv[]) {
  uint8_t databuf[DATA_BUFFER_LEN] = {0};
  struct sockaddr_in client_addr = {0x0};  // Initialize  client_addr
                                           // such that all bytes are set to
                                           // zero.
  char *msg_buf = ((char*) (databuf + MSG_HEADER_LEN));
  time_t *timestamp_headerfield = ((time_t*) databuf);
  struct tm *time_obj;
  socklen_t client_addrlen;
  int listening_sfd, port_number, client_sfd, outcome, server_namelen;

  if (3 != argc) {
    errprintf_and_exit("Invalid amount of arguments given. See below for "
        "correct usage.\n\x1b[1;34mUsage:\x1b[0m %s <port number> "
        "<display name>\n", 1, argv[0]);
  }

  server_namelen = strlen(argv[2]);

  port_number = validate_portnumber(argv[1]);

  listening_sfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

  if (0 > listening_sfd) {
    errprintf_and_exit("Failed to open a socket. Error details from \x1b[1;34m"
        "socket\x1b[0m syscall: %s\n", 1, strerror(errno));
  }

  bind_addr_to_listening_socket(listening_sfd, port_number);

  listen(listening_sfd, 5);

  client_sfd = client_sockfd = wait_for_and_accept_client(listening_sfd, 
      &client_addr, &client_addrlen);
  inet_ntop(AF_INET, &client_addr.sin_addr, msg_buf, MSG_BUF_LEN);

  printf("Connected to a client at \x1b[1;34m%s:%hu\x1b[0m "
      "([IP address]:[port number])\n", msg_buf, ntohs(client_addr.sin_port));
 
  send_display_name(client_sfd, argv[2], server_namelen);

  client_name = receive_client_display_name(client_sfd, databuf);

  time_obj = localtime(timestamp_headerfield);

  printf("Info exchange complete: Client sent display name, \x1b[1;33m%s\x1b[0m"
      ", at \x1b[1;34m%s\x1b[0m\n", msg_buf, asctime(time_obj));
   
  puts("Waiting for 1st message from client...");
  
  while (1) {
    outcome = receive_client_message(client_sfd, databuf);
    if (0 > outcome) {
      goto UNEXPECTED_ERR;
    } else if (!outcome) {
      goto DISCONNECTED_CLIENT;
    }

    time_obj = localtime(timestamp_headerfield);

    printf("\x1b[1;34m%s\t\x1b[33m%s:\x1b[0m\t%s\n",
        asctime(time_obj), client_name, msg_buf);
 
    outcome = prompt_and_message_client(client_sfd, databuf);
    
    if (0 > outcome) {
      goto UNEXPECTED_ERR;
    } else if (!outcome) {
      goto DISCONNECTED_CLIENT;
    }

    puts("Waiting for response...");
    continue;

UNEXPECTED_ERR:
    outcome = errno;
    close(client_sfd);
    free((void*)client_name);
    errprintf_and_exit("Unexpected failure to read from client socket file "
        "descriptor.\nDetails from \x1b[1;34mread\x1b[0m syscall: %s\n", 1,
        strerror(outcome));
    break;

DISCONNECTED_CLIENT:
    printf("\x1b[1;33m%s\x1b[0m disconnected.\n", client_name);
    break;
  }
  if (-1 != client_sockfd) {
    close(client_sfd);
    client_sfd = -1;
    client_sockfd = -1;
  }
  if (client_name) {
    free((void*)client_name);
    client_name = NULL;
  }

  return 0;
}
