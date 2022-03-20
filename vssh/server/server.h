#ifndef SERVER_H_
#define SERVER_H_

#include "ipv4_net.h"
#include "ipv4_net_config.h"
#include "udt_api.h"

#define SSH_SERVER_PORT 16161

int launch_tcp_server(in_addr_t ip);
void *tcp_server_handler(void *connection_socket);

int launch_udp_server(in_addr_t ip);
void *udt_server_handler(void *connection_socket);

#endif // !SERVER_H_
