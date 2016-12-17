#ifndef FTP_CLIENT_SM_H
#define FTP_CLIENT_SM_H

#include <defs.h>
#include <time.h>
#include <sys/time.h>


enum {
  SM_STATE_INIT,
  SM_STATE_CONNECTED,
  SM_STATE_WELCOMED,
  SM_STATE_WAIT_FOR_PASSWORD,
  SM_STATE_LOGGED_IN,
  SM_STATE_PASSIVE,
};

enum {
  FTP_LAST_CMD_NONE,
  FTP_LAST_CMD_RETRIEVE,
  FTP_LAST_CMD_STORE,
  FTP_LAST_CMD_LIST
};

typedef struct {
  char local_cwd[MAX_LINUX_PATH_LENGTH];
  char remote_cwd[MAX_LINUX_PATH_LENGTH];
  char file_name[MAX_LINUX_PATH_LENGTH];
  unsigned short passive_port;
  int data_sock;
  const char *ip;
  int local_fd;
  int local_file_size;
  int passive_last_command;
  struct timeval transfer_start_time;
} sm_env;

void sm_init(sm_env *env);

int sm_trans(int state_in, int *state_out, sm_env *env_in,
             int msg_source, const char *msg, unsigned int msg_len,
             int ctrl_sock, int data_sock);

#endif //FTP_CLIENT_SM_H