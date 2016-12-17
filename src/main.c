#include <client.h>
#include <server.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int main(int argc, char **argv) {
  if (argc == 4) {
    if (strcmp(argv[1], "s") == 0)
      return server_main(argv[2], (unsigned short) atoi(argv[3]));
    else if (strcmp(argv[1], "c") == 0)
      return client_main(argv[2], (unsigned short) atoi(argv[3]));
  }

  fprintf(stderr, "Wrong argument\n  ./ftp [s|c] ip port\n");
  return 127;
}
