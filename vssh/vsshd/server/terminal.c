#include "server.h"

#include <stdlib.h>
#include <termios.h>
#include <fcntl.h>
#include <unistd.h>

static int handle_terminal_commands(int socket_fd, int master_fd, int connection_type);

int handle_terminal_request(int socket_fd, int connection_type)
{
    int master_fd = posix_openpt(O_RDWR | O_NOCTTY);
	if (master_fd == -1)
    {
        ipv4_syslog(LOG_ERR, "[TERMINAL]: error while using posix_openpt(): %s", strerror(errno));
		return -1;
	}

    #define CLOSE_MASTER_AND_LOG(master_fd, corrupted_function)                                             \
    do {                                                                                                    \
        int saved_errno = errno;                                                                            \
        close(master_fd);                                                                                   \
        errno = saved_errno;                                                                                \
                                                                                                            \
        ipv4_syslog(LOG_ERR, "[TERMINAL]: error while using " #corrupted_function ": %s", strerror(errno)); \
                                                                                                            \
    } while(0)

    if (grantpt(master_fd) == -1)
    {
        CLOSE_MASTER_AND_LOG(master_fd, grantpt());
        return -1;
    }

    if (unlockpt(master_fd) == -1)
    {
        CLOSE_MASTER_AND_LOG(master_fd, unlockpt());
        return -1;
    }

    struct termios term;
	if (tcgetattr(master_fd, &term) == -1)
    {
		CLOSE_MASTER_AND_LOG(master_fd, tcgetattr());
		return -1;
	}

	cfmakeraw(&term);

	if (tcsetattr(master_fd, TCSANOW, &term) == -1)
    {
		CLOSE_MASTER_AND_LOG(master_fd, tcsetattr());
		return -1;
	}

    char *slave_pty_name = ptsname(master_fd);
    if (slave_pty_name == NULL)
    {
        CLOSE_MASTER_AND_LOG(master_fd, ptsname());
        return -1;
    }

    pid_t child_pid = fork();
    if (child_pid == -1)
    {
        CLOSE_MASTER_AND_LOG(master_fd, fork());
        return -1;
    }

    #undef CLOSE_MASTER_AND_LOG

    if (child_pid == 0)
    {
        if (setsid() == -1)
        {
            ipv4_syslog(LOG_ERR, "[TERMINAL]: error while using setsid(): %s", strerror(errno));
            exit(EXIT_FAILURE);
        }

        close(master_fd);

        int slave_fd = open(slave_pty_name, O_RDWR);
        if (slave_fd == -1)
        {
            ipv4_syslog(LOG_ERR, "[TERMINAL]: error while using open(): %s", strerror(errno));
            exit(EXIT_FAILURE);
        }

    #ifdef TIOCSCTTY // Acquire controlling tty on BSD
        if (ioctl(slaveFd, TIOCSCTTY, 0) == -1)
            errx(EX_OSERR, "ioctl() error: %s", strerror(errno));
    #endif

        if (dup2(slave_fd, STDIN_FILENO) != STDIN_FILENO)
        {
            ipv4_syslog(LOG_ERR, "[TERMINAL]: error while using dup2() for STDIN_FILENO: %s", strerror(errno));
            exit(EXIT_FAILURE);
        }
        if (dup2(slave_fd, STDOUT_FILENO) != STDOUT_FILENO)
        {
            ipv4_syslog(LOG_ERR, "[TERMINAL]: error while using dup2() for STDOUT_FILENO: %s", strerror(errno));
            exit(EXIT_FAILURE);
        }
        if (dup2(slave_fd, STDERR_FILENO) != STDERR_FILENO)
        {
            ipv4_syslog(LOG_ERR, "[TERMINAL]: error while using dup2() for STDERR_FILENO: %s", strerror(errno));
            exit(EXIT_FAILURE);
        }

        char *bash_argv[] = {"bash", NULL};
        if (execvp("bash", bash_argv) == -1)
        {
            ipv4_syslog(LOG_ERR, "[TERMINAL]: error while using execvp(): %s", strerror(errno));
            exit(EXIT_FAILURE);
        }
    }

    ipv4_syslog(LOG_INFO, "[TERMINAL]: successfully create terminal with bash");

    return handle_terminal_commands(socket_fd, master_fd, connection_type);
}

static int handle_terminal_commands(int socket_fd, int master_fd, int connection_type)
{
    int return_value = 0;

    ipv4_ctl_message ctl_message = {0};
    char bash_command[PACKET_DATA_SIZE + 1] = {0};
    char buffer[BUFSIZ + 1] = {0};

    while(1)
    {
        ssize_t recv_bytes_ctl = ipv4_receive_message(socket_fd, &ctl_message, sizeof(ipv4_ctl_message), connection_type);
        if (recv_bytes_ctl == -1)
        {
            ipv4_syslog(LOG_ERR, "[TERMINAL]: error during ipv4_receive_message(): %s", strerror(errno));
            return_value = -1;
            break;
        }

        ipv4_syslog(LOG_INFO, "[TERMINAL]: received bytes from client (ctl): %zu\n", recv_bytes_ctl);

        size_t bytes_to_read = ctl_message.message_length > PACKET_DATA_SIZE ? PACKET_DATA_SIZE: ctl_message.message_length;

        ssize_t recv_bytes = ipv4_receive_message(socket_fd, bash_command, bytes_to_read, connection_type);
        if (recv_bytes == -1)
        {
            ipv4_syslog(LOG_ERR, "[TERMINAL]: error during ipv4_receive_message(): %s", strerror(errno));
            return_value = -1;
            break;
        }

        ipv4_syslog(LOG_INFO, "[TERMINAL]: received bytes from client: %zu\n", recv_bytes);
        ipv4_syslog(LOG_INFO, "[TERMINAL]: get command: %s", bash_command);

        if (strcmp(bash_command, "exit\n") == 0)
            break;

        ssize_t write_master_bytes = write(master_fd, bash_command, bytes_to_read);
        if (write_master_bytes != bytes_to_read)
        {
            ipv4_syslog(LOG_ERR, "[TERMINAL]: error during write() in master_fd: %s", strerror(errno));
            return_value = -1;
            break;
        }

        sleep(1);

        ssize_t read_master_bytes = read(master_fd, buffer, sizeof(buffer));
        if (read_master_bytes == -1)
        {
            ipv4_syslog(LOG_ERR, "[TERMINAL]: error during read() from master_fd: %s", strerror(errno));
            return_value = -1;
            break;
        }

        ipv4_syslog(LOG_INFO, "[TERMINAL]: read bytes from master = %zu", read_master_bytes);

        ssize_t sent_bytes = ipv4_send_message(socket_fd, buffer, read_master_bytes, connection_type);
        if (sent_bytes == -1 || sent_bytes == 0)
        {
            ipv4_syslog(LOG_ERR, "[TERMINAL]: error during ipv4_send_message(): %s", strerror(errno));
            return_value = -1;
            break;
        }

        ipv4_syslog(LOG_INFO, "[TERMINAL]: sent bytes to client: %zu\n", sent_bytes);

        memset(bash_command, 0, bytes_to_read     + 1);
        memset(buffer,       0, read_master_bytes + 1);
    }

    ssize_t write_master_bytes = write(master_fd, "exit\n", sizeof("exit\n"));
    if (write_master_bytes != sizeof("exit\n"))
    {
        ipv4_syslog(LOG_ERR, "[TERMINAL]: error during write() in master_fd: %s", strerror(errno));
        return_value = -1;
    }

    if (return_value == 0)
        ipv4_syslog(LOG_INFO, "[TERMINAL]: successfully finish bash session");

    return return_value;
}