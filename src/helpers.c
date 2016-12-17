#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include "helpers.h"
#include <unistd.h>

void trim(char *str) {
  int i;
  int begin = 0;
  int end = (int) strlen(str) - 1;

  while (isspace((unsigned char) str[begin]))
    begin++;

  while ((end >= begin) && isspace((unsigned char) str[end]))
    end--;

  // Shift all characters back to the start of the string array.
  for (i = begin; i <= end; i++)
    str[i - begin] = str[i];

  str[i - begin] = '\0'; // Null terminate string.
}

unsigned short parse_pasv_port(const char *str, unsigned int len) {
  unsigned short port = 0;
  int index_end = 0;
  char digits[4];
  int state = 0;
  for (int i = len - 1; i >= 0; --i) {
    if (str[i] == ')') {
      state = 1;
      index_end = i;
    }
    else if (str[i] == ',') {
      if (state == 1) {
        memcpy(digits, str + i + 1, (size_t) (index_end - (i + 1)));
        digits[(index_end - (i + 1))] = 0;
        port += atoi(digits);
        state = 2;
        index_end = i;
      }
      else if (state == 2) {
        memcpy(digits, str + i + 1, (size_t) (index_end - (i + 1)));
        digits[(index_end - (i + 1))] = 0;
        port += 256 * atoi(digits);
        break;
      }
    }
  }
  return port;
}

void parse_command(char *command, const char *str, unsigned int len) {
  assert(len > 4);
  memcpy(command, str, 4);
  for (int i = 0; i < 4; ++i) {
    command[i] = (char) tolower(str[i]);
  }
  command[4] = 0;
}
#include <stdarg.h>
#include <stdio.h>
#include <defs.h>
#include <arpa/inet.h>
#include <errno.h>

void send_response(int fd, int code, char *format_string, ...) {
  char buf[TCP_MTU];
  va_list args;
  va_start(args, format_string);
  int off = sprintf(buf, "%d ", code);
  off += vsprintf(buf + off, format_string, args);
  va_end(args);

  buf[off] = '\n';
  buf[off+1] = 0;

  write(fd, buf, (size_t) (off + 1));
}

void create_srv_sock(int *fd, const char *ip, unsigned short *port) {
  struct sockaddr_in sa;
  memset(&sa, 0, sizeof(sa));
  sa.sin_family = AF_INET;
  sa.sin_port = htons(*port);
  inet_pton(AF_INET, ip, &(sa.sin_addr));

  int srv_sock = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
  if (srv_sock <= 0) {
    fprintf(stderr, "socket: %s\n", strerror(errno));
    exit(1);
  }

  if (bind(srv_sock, (struct sockaddr*) &sa, sizeof(sa)) < 0) {
    fprintf(stderr, "bind: failed\n");
    exit(1);
  }

  if (listen(srv_sock, 1) < 0) {
    fprintf(stderr, "listen(): failed\n");
    exit(1);
  }

  struct sockaddr_in sin;
  socklen_t len = sizeof(sin);
  if (getsockname(srv_sock, (struct sockaddr *)&sin, &len) == -1) {
    fprintf(stderr, "getsockname(): failed\n");
    exit(1);
  }
  *port = ntohs(sin.sin_port);
  *fd = srv_sock;
}

