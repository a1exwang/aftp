
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

srv_sm_env env;
void int_handler(int sig) {
  srv_sm_destroy(&env);
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

  if (listen(ctrl_srv_sock, 1) < 0) {
    fprintf(stderr, "listen(): failed\n");
    close(ctrl_srv_sock);
    exit(1);
  }

  int ftp_state = SRV_SM_STATE_INIT;
  srv_sm_init(&env);
  env.fds[FD_CTRL_SRV] = ctrl_srv_sock;
  env.ip = local_ip;


  signal(SIGINT, int_handler);

  fd_set rd_fds;
  fd_set wr_fds;
  while (1) {
    srv_set_fds(&env, ftp_state, &rd_fds, &wr_fds);
    int activity = select(srv_max_fd(&env) + 1, &rd_fds, &wr_fds, NULL, NULL);

    if ((activity < 0) && (errno != EINTR)) {
      fprintf(stderr, "select(): error");
      srv_sm_destroy(&env);
      break;
    }

    char buf[TCP_MTU];
    if (FD_ISSET(env.fds[FD_CTRL_SRV], &rd_fds)) {
      env.fds[FD_CTRL_SOCK] = accept(env.fds[FD_CTRL_SRV], NULL, NULL);
      assert(env.fds[FD_CTRL_SOCK] >= 0);
      send_response(env.fds[FD_CTRL_SOCK], FTP_CODE_HELLO, "aftp by Alex Wang v0.1");
    }

    if (FD_ISSET(env.fds[FD_CTRL_SOCK], &rd_fds)) {
      ssize_t count = read(env.fds[FD_CTRL_SOCK], buf, sizeof(buf));
      if (count > 0) {
        buf[count] = 0;
        printf("Ctrl sock read %d bytes: %s\n", (int) count, buf);
        srv_sm_trans(ftp_state, &ftp_state, &env,
                     SM_MSG_CTRL_SOCK, buf, (unsigned int)count);
      }
    }

    if (env.fds[FD_DATA_SRV] > 0 && FD_ISSET(env.fds[FD_DATA_SRV], &rd_fds)) {
      env.fds[FD_DATA_SOCK] = accept(env.fds[FD_DATA_SRV], NULL, NULL);
      assert(env.fds[FD_DATA_SOCK] >= 0);
    }

    if (env.fds[FD_DATA_SOCK] > 0 && FD_ISSET(env.fds[FD_DATA_SOCK], &rd_fds)) {
      ssize_t count = read(env.fds[FD_DATA_SOCK], buf, sizeof(buf));
      if (count == 0) {
        // client closed the socket.
        // Now we are back at LOGGED_IN state.
        send_response(env.fds[FD_CTRL_SOCK], FTP_CODE_TRANSFER_COMPLETE, "File transfer complete.");
        srv_data_channel_destroy(&env);
        ftp_state = SRV_SM_STATE_LOGGED_IN;
      }
      else {
        ssize_t c = write(env.file_fd, buf, (size_t) count);
        assert(c == count);
      }
    }
    if (env.fds[FD_DATA_SOCK] > 0 && FD_ISSET(env.fds[FD_DATA_SOCK], &wr_fds)) {
      srv_sm_trans(ftp_state, &ftp_state, &env,
                   SM_MSG_DATA_SOCK_WR, buf, 0);
    }
  }
  return 0;
}
