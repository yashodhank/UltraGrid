#ifdef HAVE_CONFIG_H
#include "config.h"
#include "config_unix.h"
#include "config_win32.h"
#endif // HAVE_CONFIG_H


#ifdef HAVE_NCURSES

#include <curses.h>

#include <stdlib.h>
#include <stdio.h>
#include "debug.h"

void usage(const char *progname);
int main(int argc, char *argv[]);
void sig_handler(int signal);

void usage(const char *progname) {
        printf("Usage: %s <hostname> <port>\n", progname);
}

int fd = -1;// = socket(AF_INET6, SOCK_STREAM, 0);

void sig_handler(int signal) {
        UNUSED(signal);
        close(fd);
        endwin();
        exit(EXIT_SUCCESS);
}

static void *reading_thread(void *arg) {
        UNUSED(arg);
        ssize_t bytes;
        char buf[1024];
        while ((bytes = recv(fd, buf, sizeof(buf), 0)) > 0) {
                write(1, buf, bytes);
        }
        return NULL;
}

int main(int argc, char *argv[])
{
        pthread_t reading_thread_id;

        if(argc != 3) {
                usage(argv[0]);
                return EXIT_FAILURE;
        }

        signal(SIGINT, sig_handler);
        signal(SIGPIPE, sig_handler);

        const char *hostname = argv[1];
        uint16_t port = atoi(argv[2]);

        struct addrinfo hints, *res, *res0;
        int err;

        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        const char *port_str = argv[2];
        err = getaddrinfo(hostname, port_str, &hints, &res0);

        if(err) {
                fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(err));
                return EXIT_FAILURE;
        }

        char what[1024];

        for (res = res0; res; res = res->ai_next) {
                fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
                if  (fd < 0) {
                        snprintf(what, 1024, "socket failed: %s", strerror(errno));
                        continue;
                }

                if(connect(fd, res->ai_addr, res->ai_addrlen) == -1) {
                        fd = -1;

                        snprintf(what, 1024, "connect failed: %s:%d :%s",  hostname, port, strerror(errno));
                        continue;
                }

                break; /* okay we got one */
        }

        freeaddrinfo(res0);

        if(fd < 0 ) {
                fprintf(stderr, "%s\n", what);
                return EXIT_FAILURE;
        }

        initscr();
        keypad(stdscr, TRUE);
        scrollok(stdscr, TRUE);

        pthread_create(&reading_thread_id, NULL, reading_thread, NULL);

        while (1) {
                char message[1024] = { '\0' };
                int key = getch();
                switch(key) {
                        case '0':
                        case '1':
                        case '2':
                        case '3':
                        case '4':
                        case '5':
                        case '6':
                        case '7':
                        case '8':
                        case '9':
                                snprintf(message, 1024, "capture.data %d", 
                                                key - '0');
                                break;
                        case 'q':
                                goto finish;
                        default:
                                printw("Unknown key: %d\n", key);
                }

                if(strlen(message) > 0) {
                        printw("Sent message: \"%s\"\n", message);
                        ssize_t total_written = 0;
                        do {
                                ssize_t ret = write(fd, message, strlen(message)+1 - total_written);
                                if(ret <= 0) {
                                        perror("Error sending command");
                                        goto finish;
                                }
                                total_written += ret;
                        } while(total_written < (int) strlen(message) + 1);
                }
        }

finish:
        pthread_cancel(reading_thread_id);
        pthread_join(reading_thread_id, NULL);

        close(fd);
        endwin();
        
        return EXIT_SUCCESS;
}

#else // ! HAVE_NCURSES
#include <stdio.h>
int main () { fprintf(stderr, "Recompile with ncurses support!\n"); return 1; }

#endif // HAVE_NCURSES

