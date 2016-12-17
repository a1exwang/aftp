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
#include <sm.h>

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
      if (env.passive_last_command == FTP_LAST_CMD_STORE && env.local_fd > 0) {
        FD_SET(env.data_sock, &wr_fds);
      }
    }
    int max_fd = ctrl_sock > env.data_sock ? ctrl_sock : env.data_sock;
    int activity = select(max_fd + 1, &rd_fds, &wr_fds, NULL, NULL);

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

        double elapsed_time = sm_toc(&env);
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

    if (env.data_sock > 0 && env.local_fd > 0 && FD_ISSET(env.data_sock, &wr_fds)) {
      char send_buf[TCP_SEND_LENGTH];
      ssize_t rn = read(env.local_fd, send_buf, TCP_SEND_LENGTH);
      ssize_t wrn = write(env.data_sock, send_buf, (size_t) rn);
      assert(wrn == rn);
      if (rn < TCP_SEND_LENGTH) {
        // all data sent
        ftp_state = SM_STATE_LOGGED_IN;
        close(env.local_fd);
        env.local_fd = -1;
        env.passive_port = 0;
        close(env.data_sock);
        env.data_sock = -1;
        env.local_file_size = 0;
        double time = sm_toc(&env);
        printf("sending done time %f.\n", time);
      }
    }
  }
  return 0;
}