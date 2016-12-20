#include "sm.h"
#include <helpers.h>

#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sm.h>

int create_passive_data_socket(const char *ip, unsigned short port) {
  struct sockaddr_in sa;
  memset(&sa, 0, sizeof(sa));
  sa.sin_family = AF_INET;
  sa.sin_port = htons(port);
  inet_pton(AF_INET, ip, &(sa.sin_addr));

  int data_sock = socket(AF_INET, SOCK_STREAM, 0);
  if (data_sock <= 0) {
    fprintf(stderr, "socket: failed\n");
  }

  if (connect(data_sock, (struct sockaddr*) &sa, sizeof(sa)) < 0) {
    fprintf(stderr, "connect: failed\n");
    exit(1);
  }
  return data_sock;
}

void sm_get_full_path(char *file_path, sm_env *env) {
  strcpy(file_path, env->local_cwd);
  strcat(file_path, env->remote_cwd); // starts with "/"
  if (file_path[strlen(file_path) - 1] != '/')
    strcat(file_path, "/");
  strcat(file_path, env->file_name);
}

void sm_init(sm_env *env) {
  memset(env, 0, sizeof(sm_env));
  strcpy(env->local_cwd, "/home/alexwang/pl/ftp-client");
  strcpy(env->remote_cwd, "/");
  env->local_fd = -1;
}

int parse_response_code(const char *str, unsigned int len) {
  assert(len > 4);
  char tmp[4];
  memcpy(tmp, str, 4);
  return atoi(tmp);
}


void parse_param(char *param, const char *str, unsigned int len) {
  memcpy(param, str + 5, len - 5);
  trim(param);
}


int sm_trans(int state_in, int *state_out, sm_env *env,
             int msg_source, const char *msg, unsigned int msg_len,
             int ctrl_sock, int data_sock) {

  char command[5];
  if (msg_source == SM_MSG_STDIN) {
    parse_command(command, msg, msg_len);
  }
  int code = 0;
  if (msg_source == SM_MSG_CTRL_SOCK) {
    code = parse_response_code(msg, msg_len);
  }

  if (msg_source == SM_MSG_STDIN) {
    if (strcmp(command, FTP_CLIENT_CMD_PWD) == 0) {
      printf("%s\n", env->local_cwd);
      return SM_RET_NONE;
    }
    else if (strcmp(command, FTP_CLIENT_CMD_CD) == 0) {
      char rel_path[MAX_LINUX_PATH_LENGTH];
      char full_path[MAX_LINUX_PATH_LENGTH];
      strcpy(full_path, env->local_cwd);
      parse_param1(rel_path, msg, msg_len);
      strcat(full_path, "/");
      strcat(full_path, rel_path);

      if (realpath(full_path, env->local_cwd)) {
        printf("%s\n", env->local_cwd);
      }
      else {
        fprintf(stderr, "Wrong path.\n");
      }
      return SM_RET_NONE;
    }
  }

  int ret = SM_RET_SEND_TO_SERVER;
  char file_path[MAX_LINUX_PATH_LENGTH];

  switch (state_in) {
  case SM_STATE_CONNECTED:
    if (msg_source == SM_MSG_CTRL_SOCK) {
      printf("Server welcome: %s\n", msg);
      *state_out = SM_STATE_WELCOMED;
    }
    else {
      assert(0);
    }
    break;
  case SM_STATE_WELCOMED:
    if (msg_source == SM_MSG_CTRL_SOCK) {
      if (code == FTP_CODE_PLEASE_SPECIFY_PASSWORD)
        *state_out = SM_STATE_WAIT_FOR_PASSWORD;
      else
        assert(0);
    }
    else if (msg_source == SM_MSG_STDIN) {
      // OK
    }
    else {
      assert(0);
    }
    break;
  case SM_STATE_WAIT_FOR_PASSWORD:
    if (msg_source == SM_MSG_CTRL_SOCK) {
      if (code == FTP_CODE_LOGIN_SUCCESSFUL)
        *state_out = SM_STATE_LOGGED_IN;
      else
        assert(0);
    }
    else if (msg_source == SM_MSG_STDIN) {
      // OK
    }
    else {
      assert(0);
    }
    break;
  case SM_STATE_LOGGED_IN:
    if (msg_source == SM_MSG_CTRL_SOCK) {
      if (code == FTP_CODE_ENTER_PASV) {
        unsigned short port = parse_pasv_port(msg, msg_len);
        env->passive_port = port;
        env->data_sock = create_passive_data_socket(env->ip, env->passive_port);
        *state_out = SM_STATE_PASSIVE;
        printf("Passive data socket connected.\n");
      }
    }
    else if (msg_source == SM_MSG_STDIN) {
      // OK
    }
    else {
      assert(0);
    }
    break;
  case SM_STATE_PASSIVE:
    if (msg_source == SM_MSG_STDIN) {
      if (strcmp(command, FTP_CMD_RETRIEVE) == 0) {
        parse_param(env->file_name, msg, msg_len);
        sm_get_full_path(file_path, env);
        int fd = open(file_path, O_RDWR | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) {
          printf("open: \"%s\" failed! %s\n", file_path, strerror(errno));
          exit(1);
        }
        env->local_fd = fd;
        env->passive_last_command = FTP_LAST_CMD_RETRIEVE;
      }
      else if (strcmp(command, FTP_CMD_STORE) == 0) {
        parse_param(env->file_name, msg, msg_len);

        sm_get_full_path(file_path, env);
        int fd = open(file_path, O_RDONLY);
        if (fd < 0) {
          printf("open: \"%s\" failed! %s\n", file_path, strerror(errno));
          exit(1);
        }
        env->local_fd = fd;
        env->passive_last_command = FTP_LAST_CMD_STORE;
      }
      else if (strcmp(command, FTP_CMD_LIST) == 0) {
        env->passive_last_command = FTP_LAST_CMD_LIST;
      }
      else {
        assert(0);
      }
    }
    else if (msg_source == SM_MSG_CTRL_SOCK) {
      if (code == FTP_CODE_PASSIVE_INITIATED) {
        sm_tick(env);

        switch (env->passive_last_command) {
        case FTP_LAST_CMD_LIST:
          break;
        case FTP_LAST_CMD_STORE: {
          sm_get_full_path(file_path, env);

          int fd = open(file_path, O_RDONLY);
          if (fd < 0) {
            fprintf(stderr, "open: failed\n");
            exit(1);
          }
          env->local_fd = fd;
//          char buf1[65536];
//          while (1) {
//            ssize_t total = read(fd, buf1, sizeof(buf1));
//            if (total <= 0)
//              break;
//            write(env->data_sock, buf1, (size_t) total);
//            env->local_file_size += total;
//          }
//          close(fd);
//          close(env->data_sock);
//          env->data_sock = -1;
//          env->passive_port = 0;
//          *state_out = SM_STATE_LOGGED_IN;
          break;
        }
        case FTP_LAST_CMD_RETRIEVE:
          break;
        default:
          assert(0);
        }
      }

    }
    else if (msg_source == SM_MSG_DATA_SOCK) {
      switch (env->passive_last_command) {
      case FTP_LAST_CMD_LIST:
        printf("%s", msg);
        env->local_file_size += msg_len;
        break;
      case FTP_LAST_CMD_RETRIEVE:
        assert(env->local_fd >= 0);
        assert(write(env->local_fd, msg, msg_len) == msg_len);
        env->local_file_size += msg_len;
        break;
      default:
        assert(0);
      }
    }
    else {
      assert(0);
    }
    break;
  default:
    assert(0);
  }
  return ret;
}

void sm_tick(sm_env *env) {
  gettimeofday(&env->transfer_start_time, NULL);
}
double sm_toc(sm_env *env) {
  struct timeval t2;
  gettimeofday(&t2, NULL);
  double elapsed_time = (t2.tv_sec - env->transfer_start_time.tv_sec) * 1000.0;      // sec to ms
  elapsed_time += (t2.tv_usec - env->transfer_start_time.tv_usec) / 1000.0;   // us to ms
  return elapsed_time;
}
