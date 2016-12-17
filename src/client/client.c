#include "sm.h"
#include <sys/select.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <errno.h>
#include <unistd.h>
#include <assert.h>
#include <time.h>
#include <sys/time.h>

int client_main(const char *server_ip, unsigned short server_port) {
  struct sockaddr_in sa;

  memset(&sa, 0, sizeof(sa));
  sa.sin_family = AF_INET;
  sa.sin_port = htons(server_port);
  inet_pton(AF_INET, server_ip, &(sa.sin_addr));

  int ctrl_sock = socket(AF_INET, SOCK_STREAM, 0);
  if (ctrl_sock <= 0) {
    fprintf(stderr, "socket: failed\n");
  }

  if (connect(ctrl_sock, (struct sockaddr*) &sa, sizeof(sa)) < 0) {
    fprintf(stderr, "connect: failed\n");
    exit(1);
  }

  int ftp_state = SM_STATE_CONNECTED;
  sm_env env;
  sm_init(&env);
  env.ip = server_ip;

  fd_set rd_fds;
  fd_set wr_fds;
  while (1) {
    FD_ZERO(&rd_fds);
    FD_SET(ctrl_sock, &rd_fds);
    FD_SET(STDIN_FD, &rd_fds);
    if (env.data_sock >= 0) {
      FD_SET(env.data_sock, &rd_fds);
    }
    int max_fd = ctrl_sock > env.data_sock ? ctrl_sock : env.data_sock;
    int activity = select(max_fd + 1, &rd_fds, NULL, NULL, NULL);

    if ((activity < 0) && (errno != EINTR)) {
      fprintf(stderr, "select error");
      break;
    }

    char buf[TCP_MTU];
    if (FD_ISSET(STDIN_FD, &rd_fds)) {
      ssize_t count = read(STDIN_FD, buf, sizeof(buf));
      buf[count] = 0;
      sm_trans(ftp_state, &ftp_state, &env,
               SM_MSG_STDIN, buf, (unsigned int)count,
               ctrl_sock, env.data_sock);
      ssize_t write_len = write(ctrl_sock, buf, (size_t) count);
      assert(write_len > 0);
    }

    if (env.data_sock > 0 && FD_ISSET(env.data_sock, &rd_fds)) {
      ssize_t count = read(env.data_sock, buf, sizeof(buf));
      if (count == 0) {
        // Server closed the socket.
        // Now we are back at LOGGED_IN state.
        ftp_state = SM_STATE_LOGGED_IN;
        if (env.local_fd >= 0) {
          close(env.local_fd);
          env.local_fd = -1;
        }
        struct timeval t2;
        gettimeofday(&t2, NULL);
        double elapsed_time = (t2.tv_sec - env.transfer_start_time.tv_sec) * 1000.0;      // sec to ms
        elapsed_time += (t2.tv_usec - env.transfer_start_time.tv_usec) / 1000.0;   // us to ms
        printf("Transfer session done. %d bytes, %f ms\n", env.local_file_size, elapsed_time);
        env.local_file_size = 0;
        env.passive_port = 0;
        close(env.data_sock);
        env.data_sock = -1;
      }
      else {
        sm_trans(ftp_state, &ftp_state, &env,
                 SM_MSG_DATA_SOCK, buf, (unsigned int)count,
                 ctrl_sock, env.data_sock);
      }
    }

    if (FD_ISSET(ctrl_sock, &rd_fds)) {
      ssize_t count = read(ctrl_sock, buf, sizeof(buf));
      buf[count] = 0;
      sm_trans(ftp_state, &ftp_state, &env,
               SM_MSG_CTRL_SOCK, buf, (unsigned int)count,
               ctrl_sock, env.data_sock);
      printf("FTP: %s\n", buf);
    }
  }
  return 0;
}