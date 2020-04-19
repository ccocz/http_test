#include <stdio.h>
#include "err.h"
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdbool.h>

#define BUFFER_SIZE 4096

int connect_socket(const char *host, const char *port) {
  /* split host and port */

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

char *request(const char *cookies, const char *test, const char *host) {
  char *message = NULL;
  unsigned size = 0;
  unsigned current = 0;
  append(&message, "GET ", &size, &current);
  append(&message, test, &size, &current);
  append(&message, " HTTP/1.1\r\n\0", &size, &current);
  append(&message, "Host: ", &size, &current);
  append(&message, host, &size, &current);
  append(&message, "\r\n", &size, &current);
  FILE *file = fopen(cookies, "r");
  if (file == NULL) {
    syserr("file opening");
  }
  char buffer[4096];
  memset(buffer, 0, sizeof(buffer));
  bool empty = 1;
  while (fgets(buffer, sizeof(buffer), file)) {
    if (empty) {
      append(&message, "Cookie: ", &size, &current);
      empty = 0;
    }
    buffer[strlen(buffer) - 1] = ';';
    buffer[strlen(buffer)] = ' ';
    append(&message, buffer, &size, &current);
    memset(buffer, 0, sizeof(buffer));
  }
  append(&message, "\r\n", &size, &current);
  append(&message, "Connection: close\r\n\0", &size, &current);
  append(&message, "\r\n", &size, &current);
  for (unsigned i = current; i < size; i++) {
    message[i] = 0;
  }
  return message;
}

void add(const char c, char **current, unsigned *size, unsigned *used) {
  if (*used + 2 > *size) {
    if (*size) {
      *size *= 2;
    } else {
      *size = 1;
    }
    *current = realloc(*current, *size);
  }
  (*current)[*used] = c;
  (*used)++;
  (*current)[*used] = '\0';
}

bool is_comlete(const char *line, const unsigned used) {
  return line[used - 1] == '\n' && line[used - 2] == '\r';
}

bool is_ok(const char *line) {
  return !strcmp(line, "HTTP/1.1 200 OK\r\n");
}

void check_line(const char *line, bool *body) {
  if (!strcmp(line, "\r\n")) {
    *body = 1;
    return;
  }
  if (!strncmp(line, "Set-Cookie: ", strlen("Set-Cookie: "))) {
    // todo: check cookie
    int i = strlen("Set-Cookie: ");
    while (i < strlen(line) - 2 && line[i] != ';') {
      putchar(line[i]);
      i++;
    }
    putchar('\n');
  }
}

void printline(const char *line) {
  int i = 0;
  while (line[i] != '\r' || line[i + 1] != '\n') {
    putchar(line[i]);
    i++;
  }
  putchar('\n');
}

void response(int socket_fd) {
  char buffer[BUFFER_SIZE];
  memset(buffer, 0, sizeof(buffer));
  int n;
  char *current = NULL;
  unsigned size = 0;
  unsigned used = 0;
  bool first_header = 1;
  bool ok = 1;
  bool body = 0;
  int content_length = 0;
  memset(buffer, 0, sizeof(buffer));
  while (ok && (n = read(socket_fd, buffer, BUFFER_SIZE))) {
    if (n < 0) {
      syserr("reading from socket");
    }
    for (int i = 0; i < n; i++) {
      if (body) {
        content_length += n - i;
        break;
      }
      add(buffer[i], &current, &size, &used);
      bool complete = is_comlete(current, used);
      if (complete && first_header && !is_ok(current)) {
        ok = 0;
        printline(current);
        free(current);
        break;
      }
      if (complete) {
        check_line(current, &body);
        first_header = 0;
        free(current);
        current = NULL;
        size = used = 0;
      }
    }
    memset(buffer, 0, sizeof(buffer));
  }
  if (ok) {
    printf("Dlugosc zasobu: %d\n", content_length);
  }
}

int main(int argc, char *argv[]) {
  if (argc != 4) {
    syserr("usage: %s <host>:<port> <cookies> <http address>", argv[0]);
  }
  char *split = malloc(strlen(argv[1]) + 1);
  strcpy(split, argv[1]);
  char *host = strtok(split, ":");
  char *port = strtok(NULL, ":");
  char *message = request(argv[2], argv[3], host);
  int socket_fd = connect_socket(host, port);
  if (write(socket_fd, message, strlen(message)) < 0) {
    syserr("writing to socket");
  }
  free(message);
  free(split);
  response(socket_fd);
  close(socket_fd);
  return 0;
}
