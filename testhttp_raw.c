#include "err.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdbool.h>
#include <ctype.h>

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

bool is_equal(const char *a, const char *b) {
  if (strlen(a) != strlen(b)) {
    return 0;
  }
  for (int i = 0; i < strlen(a); i++) {
    if (tolower(a[i]) != tolower(b[i])) {
      return 0;
    }
  }
  return 1;
}

bool is_n_equal(const char *a, const char *b, int n) {
  if (strlen(a) < n || strlen(b) < n) {
    return 0;
  }
  for (int i = 0; i < n; i++) {
    if (tolower(a[i]) != tolower(b[i])) {
      return 0;
    }
  }
  return 1;
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

void add_cookies(char **message, FILE *file, unsigned *size, unsigned *current) {
  char buffer[4096];
  memset(buffer, 0, sizeof(buffer));
  bool empty = 1;
  while (fgets(buffer, sizeof(buffer), file)) {
    if (empty) {
      append(message, "Cookie: ", size, current);
      empty = 0;
    }
    buffer[strlen(buffer) - 1] = ';';
    buffer[strlen(buffer)] = ' ';
    append(message, buffer, size, current);
    memset(buffer, 0, sizeof(buffer));
  }
  append(message, "\r\n", size, current);
}

char *request(const char *cookies, const char *test, const char *host) {
  char *message = NULL;
  unsigned size = 0;
  unsigned current = 0;
  /* append essential request headers */
  append(&message, "GET ", &size, &current);
  append(&message, test, &size, &current);
  append(&message, " HTTP/1.1\r\n\0", &size, &current);
  append(&message, "Host: ", &size, &current);
  append(&message, host, &size, &current);
  append(&message, "\r\n", &size, &current);
  /* add cookies from given file */
  FILE *file = fopen(cookies, "r");
  if (file == NULL) {
    syserr("file opening");
  }
  add_cookies(&message, file, &size, &current);
  append(&message, "Connection: close\r\n\0", &size, &current);
  append(&message, "\r\n", &size, &current);
  /* erase extra part */
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
      *size = 2;
    }
    *current = realloc(*current, *size);
  }
  (*current)[*used] = c;
  (*used)++;
  (*current)[*used] = '\0';
}

bool complete(const char *line, const unsigned used) {
  return used >= 2 && line[used - 1] == '\n' && line[used - 2] == '\r';
}

bool ok_response(const char *line) {
  return is_equal(line, "HTTP/1.1 200 OK\r\n");
}

void check_for_cookie(const char *line) {
  if (is_n_equal(line, "Set-Cookie: ", strlen("Set-Cookie: "))) {
    int i = strlen("Set-Cookie: ");
    while (i < strlen(line) - 2 && line[i] != ';') {
      putchar(line[i]);
      i++;
    }
    putchar('\n');
  }
}

bool headers_end(const char *line) {
  return is_equal(line, "\r\n");
}

bool chunked(const char *line) {
  return is_equal(line, "transfer-encoding: chunked\r\n")
  || is_equal(line, "transfer-encoding:chunked\r\n");
}

void printline(const char *line) {
  int i = 0;
  while (line[i] != '\r' || line[i + 1] != '\n') {
    putchar(line[i]);
    i++;
  }
  putchar('\n');
}

int chunked_length(int socked_fd, char *buffer, int i, int n) {
  char *current = NULL;
  unsigned size = 0;
  unsigned used = 0;
  int read_so_far = -1;
  int body_length = 0;
  do {
    if (n < 0) {
      syserr("reading from socket");
    }
    for (; i < n; i++) {
      if (read_so_far > 0) {
        read_so_far--;
        continue;
      }
      add(buffer[i], &current, &size, &used);
      if (complete(current, used)) {
        int _size;
        sscanf(current, "%x", &_size);
        body_length += _size;
        read_so_far = _size + 2;
        free(current);
        current = NULL;
        size = used = 0;
      }
    }
    i = 0;
  } while ((n = read(socked_fd, buffer, BUFFER_SIZE)));
  return body_length;
}

int non_chunked(int socked_fd, char *buffer) {
  int n;
  int length = 0;
  while ((n = read(socked_fd, buffer, BUFFER_SIZE))) {
    if (n < 0) {
      syserr("reading from socket");
    }
    length += n;
  }
  return length;
}

bool parse_headers(int socket_fd, char **error, int *result) {
  char *current = NULL;
  char buffer[BUFFER_SIZE];
  unsigned size = 0;
  unsigned used = 0;
  bool first_header = 1;
  bool enc_chunk = 0;
  bool headers_finish = 0;
  int content_length = 0;
  int n, i;
  memset(buffer, 0, sizeof(buffer));
  while (!headers_finish && (n = read(socket_fd, buffer, BUFFER_SIZE))) {
    if (n < 0) {
      syserr("reading from socket");
    }
    for (i = 0; i < n; i++) {
      add(buffer[i], &current, &size, &used);
      if (complete(current, used)) {
        if (first_header && !ok_response(current)) {
          *error = current;
          return false;
        }
        if (headers_end(current)) {
          if (enc_chunk) {
            content_length = chunked_length(socket_fd, buffer, i, n);
          } else {
            content_length = non_chunked(socket_fd, buffer) + (n - i - 1);
          }
          headers_finish = 1;
          break;
        }
        enc_chunk |= chunked(current);
        check_for_cookie(current);
        first_header = 0;
        free(current);
        current = NULL;
        size = used = 0;
      }
    }
  }
  *result = content_length;
  free(current);
  return 1;
}

void response(int socket_fd) {
  char *error;
  int result;
  if (!parse_headers(socket_fd, &error, &result)) {
    printline(error);
    free(error);
    return;
  }
  printf("Dlugosc zasobu: %d\n", result);
}

int main(int argc, char *argv[]) {
  if (argc != 4) {
    syserr("usage: %s <host>:<port> <cookies> <http address>", argv[0]);
  }
  /* split host and port */
  char *split = malloc(strlen(argv[1]) + 1);
  strcpy(split, argv[1]);
  char *host = strtok(split, ":");
  char *port = strtok(NULL, ":");
  if (port == NULL) {
    syserr("no port specified");
  }
  /* prepare http request */
  char *message = request(argv[2], argv[3], host);
  int socket_fd = connect_socket(host, port);
  if (write(socket_fd, message, strlen(message)) < 0) {
    syserr("writing to socket");
  }
  free(message);
  free(split);
  /* response is ready */
  response(socket_fd);
  close(socket_fd);
  return 0;
}