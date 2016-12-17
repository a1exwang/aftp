#pragma once

#include <defs.h>
#include <unistd.h>
#include <sys/select.h>

enum {
  SRV_SM_STATE_INIT,
  SRV_SM_STATE_WAIT_FOR_PASS,
  SRV_SM_STATE_LOGGED_IN,
  SRV_SM_STATE_PASSIVE
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

  int fds[FD_COUNT];
} srv_sm_env;

int srv_max_fd(srv_sm_env *env);
void srv_set_fds(srv_sm_env *env, fd_set *rd_fds);
void srv_sm_init(srv_sm_env *env);
int srv_sm_trans(int state_in, int *state_out, srv_sm_env *env_in,
                 int msg_source, const char *msg, unsigned int msg_len);
