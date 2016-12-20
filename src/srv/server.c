
#include <defs.h>
#include <srv_sm.h>

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
#include <fcntl.h>
#include <signal.h>
#include <helpers.h>

#define MAX_CONCURRENT_CLIENTS 10

srv_sm_env env[MAX_CONCURRENT_CLIENTS];
void int_handler(int sig) {
  for (int i = 0; i < MAX_CONCURRENT_CLIENTS; ++i) {
    srv_sm_destroy(&env[i]);
  }
}

int server_main(const char *local_ip, unsigned short local_port) {
  struct sockaddr_in sa;
  memset(&sa, 0, sizeof(sa));
  sa.sin_family = AF_INET;
  sa.sin_port = htons(local_port);
  inet_pton(AF_INET, local_ip, &(sa.sin_addr));

  int ctrl_srv_sock = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
  if (ctrl_srv_sock <= 0) {
    fprintf(stderr, "socket: %s\n", strerror(errno));
    close(ctrl_srv_sock);
    exit(1);
  }

  if (bind(ctrl_srv_sock, (struct sockaddr*) &sa, sizeof(sa)) < 0) {
    fprintf(stderr, "bind: failed, %s\n", strerror(errno));
    close(ctrl_srv_sock);
    exit(1);
  }

  if (listen(ctrl_srv_sock, MAX_CONCURRENT_CLIENTS) < 0) {
    fprintf(stderr, "listen(): failed\n");
    close(ctrl_srv_sock);
    exit(1);
  }

//  int ftp_state = SRV_SM_STATE_INIT;
  int ftp_states[MAX_CONCURRENT_CLIENTS];
  for (int i = 0; i < MAX_CONCURRENT_CLIENTS; ++i) {
    ftp_states[i] = SRV_SM_STATE_INIT;
    srv_sm_init(&env[i]);
    env[i].fds[FD_CTRL_SRV] = ctrl_srv_sock;
    env[i].ip = local_ip;
  }

  signal(SIGINT, int_handler);

  fd_set rd_fds;
  fd_set wr_fds;
  while (1) {
    FD_ZERO(&rd_fds);
    FD_ZERO(&wr_fds);

    FD_SET(ctrl_srv_sock, &rd_fds);
    for (int i = 0; i < MAX_CONCURRENT_CLIENTS; ++i) {
      if (env[i].connected)
        srv_set_fds(&env[i], ftp_states[i], &rd_fds, &wr_fds);
    }
    int activity = select(srv_max_fd(env, MAX_CONCURRENT_CLIENTS) + 1, &rd_fds, &wr_fds, NULL, NULL);

    if ((activity < 0) && (errno != EINTR)) {
      fprintf(stderr, "select(): error");
      for (int i = 0; i < MAX_CONCURRENT_CLIENTS; ++i) {
        srv_sm_destroy(&env[i]);
      }
      break;
    }

    char buf[TCP_MTU];
    if (FD_ISSET(ctrl_srv_sock, &rd_fds)) {
      for (int i = 0; i < MAX_CONCURRENT_CLIENTS; ++i) {
        if (!env[i].connected) {
          env[i].connected = 1;
          env[i].fds[FD_CTRL_SOCK] = accept(ctrl_srv_sock, NULL, NULL);
          assert(env[i].fds[FD_CTRL_SOCK] >= 0);
          send_response(env[i].fds[FD_CTRL_SOCK], FTP_CODE_HELLO, "aftp by Alex Wang v0.1");
          break;
        }
      }
    }

    for (int i = 0; i < MAX_CONCURRENT_CLIENTS; ++i) {
      if (FD_ISSET(env[i].fds[FD_CTRL_SOCK], &rd_fds)) {
        ssize_t count = read(env[i].fds[FD_CTRL_SOCK], buf, sizeof(buf));
        if (count > 0) {
          buf[count] = 0;
          printf("Ctrl sock read %d bytes: %s\n", (int) count, buf);
          srv_sm_trans(ftp_states[i], &ftp_states[i], &env[i],
                       SM_MSG_CTRL_SOCK, buf, (unsigned int) count);
        }
      }

      if (env[i].fds[FD_DATA_SRV] > 0 && FD_ISSET(env[i].fds[FD_DATA_SRV], &rd_fds)) {
        env[i].fds[FD_DATA_SOCK] = accept(env[i].fds[FD_DATA_SRV], NULL, NULL);
        assert(env[i].fds[FD_DATA_SOCK] >= 0);
      }

      if (env[i].fds[FD_DATA_SOCK] > 0 && FD_ISSET(env[i].fds[FD_DATA_SOCK], &rd_fds)) {
        ssize_t count = read(env[i].fds[FD_DATA_SOCK], buf, sizeof(buf));
        if (count == 0) {
          // client closed the socket.
          // Now we are back at LOGGED_IN state.
          send_response(env[i].fds[FD_CTRL_SOCK], FTP_CODE_TRANSFER_COMPLETE, "File transfer complete.");
          srv_data_channel_destroy(&env[i]);
          ftp_states[i] = SRV_SM_STATE_LOGGED_IN;
        } else {
          ssize_t c = write(env[i].file_fd, buf, (size_t) count);
          assert(c == count);
        }
      }
      if (env[i].fds[FD_DATA_SOCK] > 0 && FD_ISSET(env[i].fds[FD_DATA_SOCK], &wr_fds)) {
        srv_sm_trans(ftp_states[i], &ftp_states[i], &env[i],
                     SM_MSG_DATA_SOCK_WR, buf, 0);
      }
    }
  }
  return 0;
}
