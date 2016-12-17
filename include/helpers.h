#pragma once

void trim(char *str);
unsigned short parse_pasv_port(const char *str, unsigned int len);
void parse_command(char *command, const char *str, unsigned int len);
void parse_param1(char *param1, const char *str, unsigned int len);
void send_response(int fd, int code, char *format_string, ...);
void create_srv_sock(int *fd, const char *ip, unsigned short *port);
