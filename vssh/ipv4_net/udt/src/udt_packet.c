#include "ipv4_net.h"
#include "udt_packet.h"
#include "udt_buffer.h"
#include "udt_core.h"
#include "udt_utils.h"

extern udt_conn_t connection;

extern pthread_mutex_t handshake_mutex;
extern pthread_cond_t  handshake_cond;

void udt_packet_deserialize(udt_packet_t *packet)
{
    if (packet == NULL)
        return;

    uint32_t *block = &(packet->header._head0);
    for (int i = 0; i < PACKET_HEADER_SIZE; ++i)
    {
        *block = ntohl(*block);
        block++;
    }
}

void udt_packet_serialize(udt_packet_t *packet)
{
    if (packet == NULL)
        return;

    uint32_t *block = &(packet->header._head0);
    for (int i = 0; i < PACKET_HEADER_SIZE; ++i)
    {
        *block = htonl(*block);
        block++;
    }
}

ssize_t udt_packet_new(udt_packet_t *packet, const void *buffer, size_t len)
{
    if (packet == NULL)
        return -1;

    if (len > sizeof(packet->data))
        return -1;

    memset(packet->data, 0, sizeof(packet->data));
    memcpy(packet->data, buffer, len);
    udt_packet_serialize(packet);

    return len;
}

ssize_t udt_packet_new_handshake(udt_packet_t *packet)
{
    if (packet == NULL)
        return -1;

    packet_clear_header (*packet);
    packet_set_ctrl     (*packet);
    packet_set_type     (*packet, PACKET_TYPE_HANDSHAKE);
    packet_set_timestamp(*packet, 0);
    packet_set_id       (*packet, 0);

    uint32_t buffer[8] = {0};

    uint32_t flight_flag_size = 10;
    uint32_t id = 10;
    uint32_t request_type = 0;
    uint32_t cookie = 10;

    buffer[0] = UDT_VERSION;
    buffer[1] = connection.type;
    buffer[2] = 0x123123; // random number
    buffer[3] = PACKET_DATA_SIZE;
    buffer[4] = flight_flag_size;
    buffer[5] = request_type;
    buffer[6] = id;
    buffer[7] = cookie;

    for (int i = 0; i < 8; ++i)
        buffer[i] = htonl(buffer[i]);

    return udt_packet_new(packet, buffer, sizeof(buffer));
}

int udt_handle_request_packet(udt_packet_t *packet)
{
    if (((ipv4_ctl_message *) packet)->message_type == IPV4_BROADCAST_TYPE)
    {
        udt_syslog(LOG_INFO, "packet: broadcast request");
        const char respond_message[PACKET_DATA_SIZE] = {0};

        ssize_t sent_bytes = sendto(connection.socket_fd, respond_message, PACKET_DATA_SIZE, 0, (struct sockaddr *) &connection.addr, connection.addrlen);
        if (sent_bytes == -1 || sent_bytes != sizeof(respond_message))
            udt_syslog(LOG_ERR, "cannot respond to broadcast request");

        return IPV4_BROADCAST_TYPE;
    }

    return 0;
}

int udt_packet_parse(udt_packet_t packet)
{
    udt_packet_deserialize(&packet);

    if (packet_is_control(packet)) // control packet
    {
        switch (packet_get_type(packet))
        {
            case PACKET_TYPE_HANDSHAKE: // handshake
                udt_syslog(LOG_INFO, "packet: handshake");

                if (connection.is_client == 1) // client
                {
                    pthread_cond_signal(&handshake_cond);
                    udt_handshake_terminate();

                    return 0;
                }
                else if (connection.is_connected == 0) // server
                {
                    udt_syslog(LOG_INFO, "create new process for client...");

                    int fork_value = fork();
                    if (fork_value == -1) // error
                    {
                        udt_syslog(LOG_ERR, "error in fork() while creating process for client...");

                        return PACKET_SYSTEM_ERROR;
                    }
                    else if (fork_value == 0) // child
                    {
                        int new_socket_fd = ipv4_socket(SOCK_DGRAM, SO_REUSEADDR);
                        if (new_socket_fd == -1)
                        {
                            udt_syslog(LOG_ERR, "couldn't create socket...");
                            udt_syslog(LOG_NOTICE, "exit because of error");
                            exit(EXIT_FAILURE);
                        }
                            
                        struct timeval tv = {.tv_sec = UDT_SECONDS_TIMEOUT_SERVER, .tv_usec = UDT_USECONDS_TIMEOUT_SERVER};
                        setsockopt(new_socket_fd, SOL_SOCKET, SO_RCVTIMEO, (struct timeval *) &tv, sizeof(struct timeval));

                        connection.socket_fd = new_socket_fd;

                        udt_packet_new_handshake(&packet);
                        udt_send_packet_buffer_write(&packet);
                        udt_handshake_terminate();

                        return 0;
                    }
                }

                return 0;

            case PACKET_TYPE_KEEPALIVE:             // keep-alive
                udt_syslog(LOG_INFO, "packet: keep-alive");
                return 0;

            case PACKET_TYPE_ACK:                   // ack
                udt_syslog(LOG_INFO, "packet: ack");
                if (packet_get_msgnum(packet) == connection.last_packet_number)
                    connection.is_in_wait = 0;

                return 0;

            case PACKET_TYPE_NAK:                   // nak
                udt_syslog(LOG_INFO, "packet: nak");
                return 0;

            case PACKET_TYPE_CONGDELAY:             // congestion-delay warn
                udt_syslog(LOG_INFO, "packet: congestion-delay");
                return 0;

            case PACKET_TYPE_SHUTDOWN:              // shutdown
                udt_syslog(LOG_INFO, "packet: shutdown");

                if (connection.is_connected == 0)
                {
                    udt_syslog(LOG_NOTICE, "unknown client tries to shutdown me");
                    return PACKET_UNKNOWN_CLIENT_ERROR;
                }

                connection.is_connected = 0;
                if (connection.is_client == 0) // server
                {
                    udt_syslog(LOG_NOTICE, "successfully finish job and exit");
                    exit(EXIT_SUCCESS);
                }
                    
                return 0;

            case PACKET_TYPE_ACK2:                  // ack of ack
                udt_syslog(LOG_INFO, "packet: ack of ack");
                return 0;

            case PACKET_TYPE_DROPREQ:               // message drop request
                udt_syslog(LOG_INFO, "packet: drop request");
                return 0;

            case PACKET_TYPE_ERRSIG:                // error signal
                udt_syslog(LOG_INFO, "packet: error signal");
                return 0;

            default:                                // unsupported packet type
                udt_syslog(LOG_INFO, "packet: unknown");
                return PACKET_UNKNOWN_TYPE_ERROR;
        }
    }
    else // data packet
    {
        udt_syslog(LOG_INFO, "packet: data");

        if (connection.is_connected == 1)
        {
            if (packet.header._head1 & 0x80000000 &&
                packet.header._head1 & 0x40000000)      // solo packet
                udt_recv_buffer_write(packet.data, PACKET_DATA_SIZE);

            else if (packet.header._head1 & 0x40000000) // last packet
            {
                setsockopt(connection.socket_fd, SOL_SOCKET, SO_RCVTIMEO, (struct timeval *) &(connection.saved_tv), sizeof(struct timeval));

                if (packet_get_msgnum(packet) == (connection.last_packet_number + 1))
                {
                    udt_recv_buffer_write(packet.data, PACKET_DATA_SIZE);
                    connection.last_packet_number = 0;
                }
                else
                {
                    udt_syslog(LOG_INFO, "received packet doesn't correspond expectable message number");
                    return PACKET_INVALID_MSGNUM_ERROR;
                }
            }
                
            else if (packet.header._head1 & 0x80000000) // first packet
            {
                socklen_t optlen;
                getsockopt(connection.socket_fd, SOL_SOCKET, SO_RCVTIMEO, &(connection.saved_tv), &optlen);

                struct timeval new_tv = {.tv_sec = UDT_SECONDS_TIMEOUT_READ, .tv_usec = UDT_USECONDS_TIMEOUT_READ};
                setsockopt(connection.socket_fd, SOL_SOCKET, SO_RCVTIMEO, (struct timeval *) &new_tv, sizeof(struct timeval));

                udt_recv_buffer_write(packet.data, -1);
                connection.last_packet_number = 1;
            }
                
            else // middle packet
            {
                if (packet_get_msgnum(packet) == (connection.last_packet_number + 1))
                {
                    udt_recv_buffer_write(packet.data, -1);
                    connection.last_packet_number++;
                }
                else
                {
                    udt_syslog(LOG_INFO, "received packet doesn't correspond expectable message number");
                    return PACKET_INVALID_MSGNUM_ERROR;
                }
            } 

            udt_packet_t packet_ack;
            size_t message_number = packet_get_msgnum(packet);

            packet_clear_header (packet_ack);
            packet_set_ctrl     (packet_ack);
            packet_set_type     (packet_ack, PACKET_TYPE_ACK);
            packet_set_timestamp(packet_ack, 0x0000051c);
            packet_set_id       (packet_ack, 0x08c42c74);
            packet_set_msgnum   (packet_ack, message_number);

            udt_packet_new(&packet_ack, NULL, 0);
            udt_send_packet_buffer_write(&packet_ack);
        }
        else
        {
            udt_syslog(LOG_NOTICE, "packet: data was from unknown client");
            return PACKET_UNKNOWN_CLIENT_ERROR;
        }
    }

    return 0;
}
