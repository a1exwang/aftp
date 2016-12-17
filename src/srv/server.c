
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

int server_main(const char *local_ip, unsigned short local_port) {
  struct sockaddr_in sa;
  memset(&sa, 0, sizeof(sa));
  sa.sin_family = AF_INET;
  sa.sin_port = htons(local_port);
  inet_pton(AF_INET, local_ip, &(sa.sin_addr));

  int ctrl_srv_sock = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
  if (ctrl_srv_sock <= 0) {
    fprintf(stderr, "socket: %s\n", strerror(errno));
    exit(1);
  }

  if (bind(ctrl_srv_sock, (struct sockaddr*) &sa, sizeof(sa)) < 0) {
    fprintf(stderr, "bind: failed\n");
    exit(1);
  }

  if (listen(ctrl_srv_sock, 1) < 0) {
    fprintf(stderr, "listen(): failed\n");
    exit(1);
  }

  int ftp_state = SRV_SM_STATE_INIT;
  srv_sm_env env;
  srv_sm_init(&env);
  env.fds[FD_CTRL_SRV] = ctrl_srv_sock;
  env.ip = local_ip;

  fd_set rd_fds;
  fd_set wr_fds;
  while (1) {
    srv_set_fds(&env, &rd_fds);
    int activity = select(srv_max_fd(&env) + 1, &rd_fds, NULL, NULL, NULL);

    if ((activity < 0) && (errno != EINTR)) {
      fprintf(stderr, "select(): error");
      break;
    }

    char buf[TCP_MTU];
    if (FD_ISSET(env.fds[FD_CTRL_SRV], &rd_fds)) {
      env.fds[FD_CTRL_SOCK] = accept(env.fds[FD_CTRL_SRV], NULL, NULL);
      assert(env.fds[FD_CTRL_SOCK] >= 0);
    }

    if (FD_ISSET(env.fds[FD_CTRL_SOCK], &rd_fds)) {
      ssize_t count = read(env.fds[FD_CTRL_SOCK], buf, sizeof(buf));
      buf[count] = 0;
      srv_sm_trans(ftp_state, &ftp_state, &env,
                   SM_MSG_CTRL_SOCK, buf, (unsigned int)count);
      printf("FTP: %s\n", buf);
    }

    if (env.fds[FD_DATA_SRV] > 0 && FD_ISSET(env.fds[FD_DATA_SRV], &rd_fds)) {
      env.fds[FD_DATA_SOCK] = accept(env.fds[FD_DATA_SRV], NULL, NULL);
      assert(env.fds[FD_DATA_SOCK] >= 0);
    }

    if (env.fds[FD_DATA_SOCK] > 0 && FD_ISSET(env.fds[FD_DATA_SOCK], &rd_fds)) {
//      ssize_t count = read(env.data_sock, buf, sizeof(buf));
//      if (count == 0) {
//        // Server closed the socket.
//        // Now we are back at LOGGED_IN state.
//        ftp_state = SM_STATE_LOGGED_IN;
//        if (env.local_fd >= 0) {
//          close(env.local_fd);
//          env.local_fd = -1;
//        }
//        struct timeval t2;
//        gettimeofday(&t2, NULL);
//        double elapsed_time = (t2.tv_sec - env.transfer_start_time.tv_sec) * 1000.0;      // sec to ms
//        elapsed_time += (t2.tv_usec - env.transfer_start_time.tv_usec) / 1000.0;   // us to ms
//        printf("Transfer session done. %d bytes, %f ms\n", env.local_file_size, elapsed_time);
//        env.local_file_size = 0;
//        env.passive_port = 0;
//        close(env.data_sock);
//        env.data_sock = -1;
//      }
//      else {
//        sm_trans(ftp_state, &ftp_state, &env,
//                 SM_MSG_DATA_SOCK, buf, (unsigned int)count,
//                 ctrl_srv_sock, env.data_sock);
//      }
    }
  }
  return 0;
}
