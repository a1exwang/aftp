#pragma once

#include <defs.h>
#include <unistd.h>
#include <sys/select.h>

enum {
  SRV_SM_STATE_INIT,
  SRV_SM_STATE_WAIT_FOR_PASS,
  SRV_SM_STATE_LOGGED_IN,
  SRV_SM_STATE_PASSIVE,
  SRV_SM_STATE_SENDING,
  SRV_SM_STATE_RECEIVING
};

enum {
  FD_DATA_SRV,
  FD_DATA_SOCK,
  FD_CTRL_SRV,
  FD_CTRL_SOCK,

  FD_COUNT
};

typedef struct {
  const char *ip;
  unsigned short passive_port;

  int offset;
  int file_fd;


  int fds[FD_COUNT];

  char ftp_root[MAX_LINUX_PATH_LENGTH];
  char ftp_cwd[MAX_LINUX_PATH_LENGTH];
} srv_sm_env;

int srv_max_fd(srv_sm_env *env);
void srv_set_fds(srv_sm_env *env, int state, fd_set *rd_fds, fd_set *wr_fds);
void srv_sm_init(srv_sm_env *env);
void srv_sm_destroy(srv_sm_env *env);
void srv_data_channel_destroy(srv_sm_env *env);
int srv_sm_trans(int state_in, int *state_out, srv_sm_env *env_in,
                 int msg_source, const char *msg, unsigned int msg_len);
