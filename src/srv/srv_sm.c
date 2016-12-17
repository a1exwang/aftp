#include <srv_sm.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <helpers.h>
#include <fcntl.h>
#include <errno.h>

void srv_sm_init(srv_sm_env *env) {
  memset(env, 0, sizeof(srv_sm_env));
  for (int i = 0; i < FD_COUNT; ++i) {
    env->fds[i] = -1;
  }
  strcpy(env->ftp_root, "/home/alexwang/pl/ftp");
  strcpy(env->ftp_cwd, "/");
}

void srv_sm_destroy(srv_sm_env *env) {
  for (int i = 0; i < FD_COUNT; ++i) {
    if (env->fds > 0) {
      close(env->fds[i]);
    }
  }
  if (env->file_fd > 0) {
    close(env->file_fd);
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

void srv_set_fds(srv_sm_env *env, int state, fd_set *rd_fds, fd_set *wr_fds) {
  FD_ZERO(rd_fds);
  for (int i = 0; i < FD_COUNT; ++i) {
    if (env->fds[i] >= 0) {
      FD_SET(env->fds[i], rd_fds);
    }
  }

  FD_ZERO(wr_fds);
  if (state == SRV_SM_STATE_SENDING) {
    FD_SET(env->fds[FD_DATA_SOCK], wr_fds);
  }
}

void srv_data_channel_destroy(srv_sm_env *env) {
  close(env->file_fd);
  env->file_fd = -1;
  close(env->fds[FD_DATA_SOCK]);
  env->fds[FD_DATA_SOCK] = -1;
  close(env->fds[FD_DATA_SRV]);
  env->fds[FD_DATA_SRV] = -1;
  env->offset = 0;
  env->passive_port = 0;
}

int srv_sm_trans(int state_in, int *state_out, srv_sm_env *env,
                 int msg_source, const char *msg, unsigned int msg_len) {

  char command[5];
  if (msg_source == SM_MSG_CTRL_SOCK) {
    parse_command(command, msg, msg_len);
  }

  int ret = 0;
  char file_path[MAX_LINUX_PATH_LENGTH];

  if (msg_source == SM_MSG_CTRL_SOCK) {
    if (strcmp(command, "help") == 0) {
      const char *help = "214-The following commands are recognized.\n USER PASS PASV CWD PWD LIST\n";
      write(env->fds[FD_CTRL_SOCK], help, strlen(help));
      send_response(env->fds[FD_CTRL_SOCK],
                    FTP_CODE_HELP,
                    "Help OK");
      return 0;
    }
    else if (strcmp(command, "syst") == 0) {
      send_response(env->fds[FD_CTRL_SOCK],
                    FTP_CODE_SYS_TYPE,
                    "UNIX Type: L8");
      return 0;
    }
    else if (strcmp(command, "feat") == 0) {
      const char *features = "211-Features:\nPASV\n";
      write(env->fds[FD_CTRL_SOCK], features, strlen(features));
      send_response(env->fds[FD_CTRL_SOCK],
                    FTP_CODE_FEATURES,
                    "End");
      return 0;
    }
    else if (strcmp(command, "type") == 0) {
      send_response(env->fds[FD_CTRL_SOCK],
                    FTP_CODE_SWITCH_TO_BIN_MODE,
                    "Switching to binary mode.");
      return 0;
    }
    else if (strcmp(command, "mdtm") == 0) {
      send_response(env->fds[FD_CTRL_SOCK],
                    213,
                    "20161204191607");
      return 0;
    }
    else if (strcmp(command, "cwd") == 0) {
      char param1[MAX_LINUX_PATH_LENGTH];
      parse_param1(param1, msg, msg_len);
      trim(param1);
      if (param1[0] == '/') {
        strcpy(env->ftp_cwd, param1);
      }
      else {
        strcat(env->ftp_cwd, param1);
        strcat(env->ftp_cwd, "/");
      }

      send_response(env->fds[FD_CTRL_SOCK],
                    213,
                    "Pathname created. \"%s\" is current directory", env->ftp_cwd);
      return 0;
    }
    else if (strcmp(command, "pwd") == 0) {
      send_response(env->fds[FD_CTRL_SOCK],
                    213,
                    "Pathname created. \"%s\" is current directory", env->ftp_cwd);
      return 0;
    }
  }

  switch (state_in) {
  case SRV_SM_STATE_INIT:
    if (msg_source == SM_MSG_CTRL_SOCK) {
      if (strcmp(command, FTP_CMD_USER) == 0) {
        send_response(env->fds[FD_CTRL_SOCK], FTP_CODE_PLEASE_SPECIFY_PASSWORD, "Please specify password");
        *state_out = SRV_SM_STATE_WAIT_FOR_PASS;
      }
      else {
        send_response(env->fds[FD_CTRL_SOCK],
                      FTP_CODE_PLEASE_LOGIN,
                      "Please login with USER and PASS");
      }
    }
    break;
  case SRV_SM_STATE_WAIT_FOR_PASS:
    if (msg_source == SM_MSG_CTRL_SOCK) {
      if (strcmp(command, FTP_CMD_PASSWORD) == 0) {
        send_response(env->fds[FD_CTRL_SOCK], FTP_CODE_LOGIN_SUCCESSFUL, "Login successful");
        *state_out = SRV_SM_STATE_LOGGED_IN;
      }
      else {
        srv_sm_destroy(env);
        assert(0);
      }
    }
    break;
  case SRV_SM_STATE_LOGGED_IN:
    if (msg_source == SM_MSG_CTRL_SOCK) {
      if (strcmp(command, FTP_CMD_PASSIVE) == 0) {
        env->passive_port = 0;
        create_srv_sock(&env->fds[FD_DATA_SRV], env->ip, &env->passive_port);

        send_response(env->fds[FD_CTRL_SOCK],
                      FTP_CODE_ENTER_PASV,
                      "Entering passive mode (127,0,0,1,%d,%d)",
                      (env->passive_port >> 8) & 0xFF,
                      env->passive_port & 0xFF);
        *state_out = SRV_SM_STATE_PASSIVE;
      }
      else {
        fprintf(stderr, "command: \"%s\"\n", command);
        assert(0);
      }
    }
    break;
  case SRV_SM_STATE_PASSIVE:
    if (msg_source == SM_MSG_CTRL_SOCK) {
      if (strcmp(command, FTP_CMD_LIST) == 0) {
        send_response(env->fds[FD_CTRL_SOCK],
                      FTP_CODE_PASSIVE_INITIATED,
                      "Here comes the directory listing.");

        // redirect `ls -l` to remote client.
        char cmd_buf[MAX_LINUX_PATH_LENGTH];
        sprintf(cmd_buf, "sh -c \"ls -l %s%s | tail -n +2\"", env->ftp_root, env->ftp_cwd);
        FILE *fp = popen(cmd_buf, "r");
        assert(fp != NULL);
        char buf[MAX_LINUX_PATH_LENGTH];
        while (fgets(buf, sizeof(buf) - 1, fp) != NULL) {
          write(env->fds[FD_DATA_SOCK], buf, strlen(buf));
        }
        /* close */
        pclose(fp);

        close(env->fds[FD_DATA_SOCK]);
        close(env->fds[FD_DATA_SRV]);
        env->fds[FD_DATA_SOCK] = env->fds[FD_DATA_SRV] = -1;

        send_response(env->fds[FD_CTRL_SOCK],
                      FTP_CODE_TRANSFER_COMPLETE,
                      "Directory send OK.");
        *state_out = SRV_SM_STATE_LOGGED_IN;
      }
      else if (strcmp(command, FTP_CMD_RETRIEVE) == 0) {
        send_response(env->fds[FD_CTRL_SOCK],
                      FTP_CODE_PASSIVE_INITIATED,
                      "File starts sending.");
        // parse file path and open file
        char tmp_file_path[MAX_LINUX_PATH_LENGTH];
        strcpy(tmp_file_path, msg + 5);
        trim(tmp_file_path);
        sprintf(file_path, "%s%s/%s", env->ftp_root, env->ftp_cwd, tmp_file_path);
        env->file_fd = open(file_path, O_RDONLY);
        env->offset = 0;
        if (env->file_fd < 0) {
          fprintf(stderr, "open(): failed, %s\n", strerror(errno));
          srv_sm_destroy(env);
          assert(0);
        }

        *state_out = SRV_SM_STATE_SENDING;
      }
      else if (strcmp(command, FTP_CMD_STORE) == 0) {
        send_response(env->fds[FD_CTRL_SOCK],
                      FTP_CODE_PASSIVE_INITIATED,
                      "File starts receiving.");

        // parse file path and open file
        char tmp_file_path[MAX_LINUX_PATH_LENGTH];
        strcpy(tmp_file_path, msg + 5);
        trim(tmp_file_path);
        sprintf(file_path, "%s%s%s", env->ftp_root, env->ftp_cwd, tmp_file_path);
        env->file_fd = open(file_path, O_WRONLY | O_CREAT, 0644);
        env->offset = 0;
        if (env->file_fd < 0) {
          fprintf(stderr, "open(): failed, %s\n", strerror(errno));
          srv_sm_destroy(env);
          assert(0);
        }

        *state_out = SRV_SM_STATE_RECEIVING;
      }
    }
  case SRV_SM_STATE_SENDING: {
    if (msg_source == SM_MSG_DATA_SOCK_WR) {
      char buf[TCP_SEND_LENGTH];
      ssize_t len = read(env->file_fd, buf, TCP_SEND_LENGTH);
      ssize_t a = write(env->fds[FD_DATA_SOCK], buf, (size_t) len);
      assert(a == len);
      env->offset += len;
      if (len != TCP_SEND_LENGTH) {
        // all data sent
        srv_data_channel_destroy(env);

        send_response(env->fds[FD_CTRL_SOCK],
                      FTP_CODE_TRANSFER_COMPLETE,
                      "File transfer complete.");

        *state_out = SRV_SM_STATE_LOGGED_IN;
      }
    }
    break;
  }
  case SRV_SM_STATE_RECEIVING: {
    break;
  }
  default:
    srv_sm_destroy(env);
    assert(0);
  }

  return ret;
}
