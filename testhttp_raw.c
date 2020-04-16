#include <stdio.h>
#include "err.h"
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

int connect_socket(const char *address) {
  /* split host and port */
  char *split = malloc(strlen(address) + 1);
  strcpy(split, address);
  char *host = strtok(split, ":");
  char *port = strtok(NULL, ":");
  int sock;
  struct addrinfo addr_hints;
  struct addrinfo *addr_result;
  int err;
  /* 'converting' host/port in string to struct addrinfo */
  memset(&addr_hints, 0, sizeof(struct addrinfo));
  addr_hints.ai_family = AF_INET; // IPv4
  addr_hints.ai_socktype = SOCK_STREAM;
  addr_hints.ai_protocol = IPPROTO_TCP;
  err = getaddrinfo(host, port, &addr_hints, &addr_result);
  if (err == EAI_SYSTEM) { // system error
    syserr("getaddrinfo: %s", gai_strerror(err));
  }
  else if (err != 0) { // other error (host not found, etc.)
    fatal("getaddrinfo: %s", gai_strerror(err));
  }
  /* initialize socket according to getaddrinfo results */
  sock = socket(addr_result->ai_family, addr_result->ai_socktype, addr_result->ai_protocol);
  if (sock < 0) {
    syserr("socket");
  }
  /* connect socket to the server */
  if (connect(sock, addr_result->ai_addr, addr_result->ai_addrlen) < 0) {
    syserr("connect");
  }
  freeaddrinfo(addr_result);
  return sock;
}

int connect_socket_old(const char *address) {
  int socket_fd;
  int portno;
  int n;
  struct sockaddr_in serv_addr;
  struct hostent *server;
  /* split host and port */
  char *split = malloc(strlen(address) + 1);
  strcpy(split, address);
  char *host = strtok(split, ":");
  char *port = strtok(NULL, ":");
  portno = atoi(port);
  /* open socket */
  socket_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (socket_fd < 0) {
    syserr("opening socket");
  }
  /* Takes a name as an argument and returns a pointer to a hostent containing information about that host. */
  server = gethostbyname(host);
  if (server == NULL) {
    syserr("no such host");
  }
  /* configure address */
  memset(&serv_addr, 0, sizeof(serv_addr));
  serv_addr.sin_family = AF_INET;
  memcpy((char *)server->h_addr, (char *)&serv_addr.sin_addr.s_addr, server->h_length);
  serv_addr.sin_port = htons(portno);
  /* connect socket to the address */
  if (connect(socket_fd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
    syserr("connecting");
  }
  return socket_fd;
}

void append(char **dest, const char *src, unsigned *size, unsigned *current) {
  int m = (int)strlen(src);
  while (*current + m > *size) {
    if (*size) {
      *size *= 2;
    }
    else {
      *size = 1;
    }
  }
  (*dest) = realloc(*dest, *size);
  for (int i = 0; i < m; i++) {
    (*dest)[*current] = src[i];
    (*current)++;
  }
}

char *create_request(const char *cookies, const char *test) {
  char *message = NULL;
  unsigned size = 0;
  unsigned current = 0;
  append(&message, "GET ", &size, &current);
  append(&message, test, &size, &current);
  append(&message, " HTTP/1.1\r\n\0", &size, &current);
  FILE *file = fopen(cookies, "r");
  if (file == NULL) {
    syserr("file opening");
  }
  char buffer[4096];
  memset(buffer, 0, sizeof(buffer));
  while (fgets(buffer, sizeof(buffer), file)) {
    append(&message, "Cookie: ", &size, &current);
    buffer[strlen(buffer) - 1] = '\r';
    buffer[strlen(buffer)] = '\n';
    append(&message, buffer, &size, &current);
    memset(buffer, 0, sizeof(buffer));
  }
  append(&message, "Connection: close\r\n\0", &size, &current);
  append(&message, "\r\n", &size, &current);
  for (unsigned i = current; i < size; i++) {
    message[i] = 0;
  }
  return message;
}

int main(int argc, char *argv[]) {
  if (argc < 4) {
    syserr("usage: %s <host>:<port> <cookies> <http address>", argv[0]);
  }
  char *message = create_request(argv[2], argv[3]);
  int socket_fd = connect_socket(argv[1]);
  int n;
  n = write(socket_fd, message, strlen(message));
  if (n < 0) {
    syserr("writing to socket");
  }
  char buff[4096];
  memset(buff, 0, sizeof(buff));
  int total_size = 0;
  while (read(socket_fd, buff, 4095)) {
    printf("%s\n", buff);

    memset(buff, 0, sizeof(buff));
  }
  printf("%d\n", total_size);
  close(socket_fd);
  return 0;
}
