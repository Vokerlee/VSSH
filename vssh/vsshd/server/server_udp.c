#include "server.h"

extern const char *VSSH_RSA_PUBLIC_KEY_PATH;

void *udt_server_handler(void *connection_socket)
{
    int socket_fd = (int) connection_socket;
    
    ipv4_ctl_message ctl_message;
    char message[PACKET_DATA_SIZE + 1] = {0};

    unsigned char secret[IPV4_SPARE_BUFFER_LENGTH] = {0};
    int secret_size = ipv4_execute_DH_protocol(socket_fd, secret, 1, VSSH_RSA_PUBLIC_KEY_PATH, SOCK_STREAM_UDT);
    if (secret_size <= 0)
    {
        ipv4_udt_syslog(LOG_ERR, "Diffie-Hellman protocol failed");

        void *retval = NULL;
        pthread_exit(retval);
    }

    ipv4_udt_syslog(LOG_INFO, "Diffie-Hellman protocol succeed");
    ipv4_udt_syslog(LOG_INFO, "is ready to work");

    while(1)
    {
        ssize_t recv_bytes = ipv4_receive_message_secure(socket_fd, &ctl_message, sizeof(ipv4_ctl_message), SOCK_STREAM_UDT, secret);
        if (recv_bytes != -1 && recv_bytes != 0)
        {
            switch (ctl_message.message_type)
            {
                case IPV4_MSG_HEADER_TYPE:
                {
                    recv_bytes = ipv4_receive_message_secure(socket_fd, message, ctl_message.message_length, SOCK_STREAM_UDT, secret);
                    if (recv_bytes == -1 || recv_bytes == 0)
                        ipv4_udt_syslog(LOG_ERR, "couldn't receive message after getting msg header");

                    ipv4_udt_syslog(LOG_INFO, "message length: %zu", recv_bytes);
                    ipv4_udt_syslog(LOG_INFO, "get message: %s", message);

                    break;
                }

                case IPV4_SHELL_REQUEST_TYPE:
                {
                    ipv4_udt_syslog(LOG_INFO, "get shell request");
                    handle_terminal_request(socket_fd, SOCK_STREAM_UDT, ctl_message.spare_buffer1, secret);

                    break;
                }

                case IPV4_FILE_HEADER_TYPE:
                {
                    ipv4_udt_syslog(LOG_INFO, "get file \"%s\" to user \"%s\"", ctl_message.spare_buffer2, ctl_message.spare_buffer1);
                    handle_file(socket_fd, SOCK_STREAM_UDT, ctl_message.message_length, ctl_message.spare_buffer1, ctl_message.spare_buffer2, secret);

                    break;
                }

                case IPV4_USERS_LIST_REQUEST_TYPE:
                {
                    ipv4_udt_syslog(LOG_INFO, "get users list request");
                    handle_users_list_request(socket_fd, SOCK_STREAM_UDT, secret);
                    
                    break;
                }
                    
                default:
                    break;
            }

            memset(message, 0, sizeof(message));
        }
    }

    void *retval = 0;
    pthread_exit(retval);
}

int launch_vssh_udp_server(in_addr_t ip)
{
    int socket_fd = ipv4_socket(SOCK_DGRAM, SO_REUSEADDR);
    if (socket_fd == -1)
    {
        ipv4_udt_syslog(LOG_ERR, "error while getting socket: %s", strerror(errno));
        ipv4_udt_syslog(LOG_ERR, "exit because of error");
        exit(EXIT_FAILURE);
    }

    int bind_state = ipv4_bind(socket_fd, ip, htons(SSH_SERVER_PORT), SOCK_STREAM_UDT, udt_server_handler);
    if (bind_state == -1)
    {
        ipv4_udt_syslog(LOG_ERR, "error while binding to socket");
        close(socket_fd);
        ipv4_udt_syslog(LOG_ERR, "exit because of error");
        exit(EXIT_FAILURE);
    }

    return -1; // unreachable
}
