#include <srv_sm.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <helpers.h>

void srv_sm_init(srv_sm_env *env) {
  memset(env, 0, sizeof(srv_sm_env));
  for (int i = 0; i < FD_COUNT; ++i) {
    env->fds[i] = -1;
  }
}

int srv_max_fd(srv_sm_env *env) {
  int max_fd = 0;
  for (int i = 0; i < FD_COUNT; ++i) {
    if (env->fds[i] > max_fd) {
      max_fd = env->fds[i];
    }
  }
  return max_fd;
}

void srv_set_fds(srv_sm_env *env, fd_set *rd_fds) {
  FD_ZERO(rd_fds);
  for (int i = 0; i < FD_COUNT; ++i) {
    if (env->fds[i] >= 0) {
      FD_SET(env->fds[i], rd_fds);
    }
  }
}

int srv_sm_trans(int state_in, int *state_out, srv_sm_env *env_in,
                 int msg_source, const char *msg, unsigned int msg_len) {

  char command[5];
  if (msg_source == SM_MSG_CTRL_SOCK) {
    parse_command(command, msg, msg_len);
  }

  int ret = 0;
  char file_path[MAX_LINUX_PATH_LENGTH];

  switch (state_in) {
  case SRV_SM_STATE_INIT:
    if (msg_source == SM_MSG_CTRL_SOCK) {
      if (strcmp(command, FTP_CMD_USER) == 0) {
        send_response(env_in->fds[FD_CTRL_SOCK], FTP_CODE_PLEASE_SPECIFY_PASSWORD, "Please specify password");
        *state_out = SRV_SM_STATE_WAIT_FOR_PASS;
      }
      else {
        assert(0);
      }
    }
    break;
  case SRV_SM_STATE_WAIT_FOR_PASS:
    if (msg_source == SM_MSG_CTRL_SOCK) {
      if (strcmp(command, FTP_CMD_PASSWORD) == 0) {
        send_response(env_in->fds[FD_CTRL_SOCK], FTP_CODE_LOGIN_SUCCESSFUL, "Login successful");
        *state_out = SRV_SM_STATE_LOGGED_IN;
      }
      else {
        assert(0);
      }
    }
    break;
  case SRV_SM_STATE_LOGGED_IN:
    if (msg_source == SM_MSG_CTRL_SOCK) {
      if (strcmp(command, FTP_CMD_PASSIVE) == 0) {
        create_srv_sock(&env_in->fds[FD_DATA_SRV], env_in->ip, &env_in->passive_port);

        send_response(env_in->fds[FD_CTRL_SOCK],
                      FTP_CODE_ENTER_PASV,
                      "Passive mode entered. (0,0,0,0,%d,%d)",
                      (env_in->passive_port >> 8) & 0xFF,
                      env_in->passive_port & 0xFF);
        *state_out = SRV_SM_STATE_PASSIVE;
      }
      else {
        assert(0);
      }
    }
    break;
  case SRV_SM_STATE_PASSIVE:
    if (msg_source == SM_MSG_CTRL_SOCK) {
      if (strcmp(command, FTP_CMD_LIST) == 0) {
        send_response(env_in->fds[FD_CTRL_SOCK],
                      FTP_CODE_PASSIVE_INITIATED,
                      "Here comes the directory listing.");

        send_response(env_in->fds[FD_DATA_SOCK],
                      123,
                      "ls -al");

        close(env_in->fds[FD_DATA_SOCK]);
        close(env_in->fds[FD_DATA_SRV]);
        env_in->fds[FD_DATA_SOCK] = env_in->fds[FD_DATA_SRV] = -1;

        send_response(env_in->fds[FD_CTRL_SOCK],
                      FTP_CODE_TRANSFER_COMPLETE,
                      "Directory send OK.");
        *state_out = SRV_SM_STATE_LOGGED_IN;
      }
    }
    break;
  default:
    assert(0);
  }

  return ret;
}

