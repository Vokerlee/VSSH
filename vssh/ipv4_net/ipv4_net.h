#ifndef IPV4_NET_H_
#define IPV4_NET_H_

#define _GNU_SOURCE

// General libs
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/time.h>
#include <unistd.h>
#include <errno.h>
#include <sysexits.h>
#include <err.h>
#include <syslog.h>

// Sockets libs
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <arpa/inet.h>

// OpenSSL lib
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/rsa.h>
#include <openssl/evp.h>
#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/dh.h>
#include <openssl/engine.h>

// Others libs
#define _UNIX03_THREADS
#include <pthread.h>
#include <arpa/inet.h>
#include <fcntl.h>

#include "ipv4_net_config.h"
#include "udt.h"

// IPv4 control message parameters
#define IPV4_SPARE_FIELDS 32
#define IPV4_SPARE_BUFFER_LENGTH 256
#define IPV4_SPARE_FLAGS 8

// IPv4 control message types
#define IPV4_MSG_HEADER_TYPE         1UL
#define IPV4_BUF_HEADER_TYPE         2UL
#define IPV4_FILE_HEADER_TYPE        3UL
#define IPV4_SHUTDOWN_TYPE           4UL
#define IPV4_BROADCAST_TYPE          5UL
#define IPV4_SHELL_REQUEST_TYPE      6UL
#define IPV4_USERS_LIST_REQUEST_TYPE 7UL
#define IPV4_ENCRYPTION_PG_NUM_TYPE  8UL
#define IPV4_ENCRYPTION_PUBKEY_TYPE  9UL

// IPv4 control message structure

typedef struct
{
    uint64_t message_type;
    uint64_t message_length;

    struct
    {
        uint32_t spare_fields[IPV4_SPARE_FIELDS];
        struct
        {
            char spare_buffer1[IPV4_SPARE_BUFFER_LENGTH];
            char spare_buffer2[IPV4_SPARE_BUFFER_LENGTH];
        };
    };
} ipv4_ctl_message;

// IPv4 library function declarations

int     ipv4_socket                  (int type, int optname);
       
int     ipv4_connect                 (int socket_fd, in_addr_t dest_ip, in_port_t dest_port, int connection_type);
int     ipv4_bind                    (int socket_fd, in_addr_t ip,      in_port_t port,      int connection_type, void *(*udt_server_handler)(void *));
int     ipv4_listen                  (int socket_fd);
int     ipv4_accept                  (int socket_fd, struct sockaddr *addr, socklen_t *length);
int     ipv4_close                   (int socket_fd, int connection_type);

// Standart API

ssize_t ipv4_send_message            (int socket_fd, const void *buffer, size_t n_bytes, int connection_type);
ssize_t ipv4_receive_message         (int socket_fd,       void *buffer, size_t n_bytes, int connection_type);
       
ssize_t ipv4_send_buffer             (int socket_fd, const void *buffer, size_t n_bytes, int msg_type,
                                      uint32_t *spare_fields, size_t spare_fields_size,  char *spare_buffer1, size_t spare_buffer_size1,
                                      char *spare_buffer2,    size_t spare_buffer_size2, int connection_type);
ssize_t ipv4_receive_buffer          (int socket_fd, void *buffer, size_t n_bytes,       int connection_type);
       
ssize_t ipv4_send_file               (int socket_fd, int file_fd, 
                                      uint32_t *spare_fields, size_t spare_fields_size,  char *spare_buffer1, size_t spare_buffer_size1, 
                                      char *spare_buffer2,    size_t spare_buffer_size2, int connection_type);
ssize_t ipv4_receive_file            (int socket_fd, int file_fd, size_t n_bytes,        int connection_type);
       
int     ipv4_send_ctl_message        (int socket_fd,          uint64_t msg_type,         uint64_t msg_length, 
                                      uint32_t *spare_fields, size_t spare_fields_size,  char *spare_buffer1, size_t spare_buffer_size1,
                                      char *spare_buffer2,    size_t spare_buffer_size2, int connection_type);

// Secured API

int     ipv4_send_ctl_message_secure (int socket_fd,         uint64_t msg_type,          uint64_t msg_length, 
                                     uint32_t *spare_fields, size_t spare_fields_size,   char *spare_buffer1, size_t spare_buffer_size1,
                                     char *spare_buffer2,    size_t spare_buffer_size2,  int connection_type, unsigned char *key);

ssize_t ipv4_send_message_secure    (int socket_fd, const void *buffer, size_t n_bytes,  int connection_type, unsigned char *key);
ssize_t ipv4_receive_message_secure (int socket_fd,       void *buffer, size_t n_bytes,  int connection_type, unsigned char *key);
int     ipv4_close_secure           (int socket_fd,                                      int connection_type, unsigned char *key);

ssize_t ipv4_receive_buffer_secure  (int socket_fd,       void *buffer, size_t n_bytes,  int connection_type, unsigned char *key);

ssize_t ipv4_send_buffer_secure     (int socket_fd, const void *buffer, size_t n_bytes,  int msg_type,
                                     uint32_t *spare_fields, size_t spare_fields_size,   char *spare_buffer1, size_t spare_buffer_size1,
                                     char *spare_buffer2,    size_t spare_buffer_size2,  int connection_type, unsigned char *key);

ssize_t ipv4_execute_DH_protocol    (int socket_fd, unsigned char *secret, int is_initiator, const char *rsa_key_path, int connection_type);

#endif // !IPV4_NET_H_
