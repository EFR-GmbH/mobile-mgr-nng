/**
 * @file
 * @ccmod{MBIM_X_MMG}
 */
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>

#include "nng_server.h"

#ifndef MBIM_NNG_SOCKET_FILE
#define MBIM_NNG_SOCKET_FILE "ipc:///tmp/mbim_nng.socket"
#endif

static bool is_running = true;

void signal_handler(int sig)
{
    if (sig == SIGINT)
        is_running = false;
}

/** Process incoming requests and handle them until stopped
 *
 * @param sock     Pointer to the socket
 * @param request  Pointer to the Mbim_request structure
 */
static void handle_request(nng_socket *sock, Mbim_request *request)
{
    bool ret = true;

    while (is_running && ret)
        ret = rep_server_perform_request(sock, request);

    if (ret)
        nng_close(*sock);
}

int main(int argc, char *argv[])
{
    nng_socket sock;
    Mbim_request request = {0};
    struct sigaction act;

    act.sa_handler = signal_handler;
    sigaction(SIGINT, &act, NULL);

    while (is_running)
    {
        if (!rep_server_open(&sock, MBIM_NNG_SOCKET_FILE))
        {
            printf("Server : Unable to start the server, exit");
            return 1;
        }

        handle_request(&sock, &request);
    }

    return 0;
}
