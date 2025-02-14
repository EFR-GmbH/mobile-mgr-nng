/**
 * @file
 * @brief Mbim NNG Server
 * @ccmod{MBIM_X_SRV}
 */
#include <stdio.h>
#include <unistd.h>
#include <time.h>

#include "nng_server.h"
#include "mbim.h"
#include "mbim_enum.h"
#include "nng/protocol/reqrep0/rep.h"

#define NODE_BIND_RETRIES 3
#define NODE_RETRY_SLEEP 200 * 1000

/**
 * Sleep for a specified number of microseconds.
 *
 * @param time_us Time in microseconds to sleep
 */
inline static void sleep_us(uint64_t time_us)
{
    const struct timespec ts = {
        .tv_sec = time_us / (1000000),
        .tv_nsec = (time_us % 1000000) * 1000,
    };

    nanosleep(&ts, NULL);
}

/**
 * Open and bind an NNG REP socket.
 *
 * @param sock Pointer to the NNG socket
 * @param url  URL to bind the socket to
 *
 * @return True on success, otherwise false
 */
static bool server_open(nng_socket *sock, const char *url)
{
    int ret;

    ret = nng_rep0_open(sock);
    if (ret)
    {
        printf("Server : Open REP socket failed [%d] : %s\n", ret, nng_strerror(ret));
        return false;
    }

    ret = nng_listen(*sock, url, NULL, 0);
    if (ret)
    {
        printf("Server : Bind REP socket failed [%d] : %s\n", ret, nng_strerror(ret));
        nng_close(*sock);
        return false;
    }

    return true;
}

/**
 * Handle incoming requests for the MBIM server.
 *
 * @param request Pointer to the Mbim_request structure
 */
static void handle_request(Mbim_request *request)
{
    request->type = MBIM_UNKOWN;
    request->proto = MB_PROT_UNKOWN;
    if (!databuf_is_valid(&request->req))
    {
        databuf_add_string(&request->resp, MB_ERROR, "Server : Invalid request");
        databuf_add_uint(&request->resp, MB_RESPONSE, MBIM_ERROR);
        return;
    }

    databuf_get_uint(&request->req, MB_REQUEST, &request->type);
    if (request->type == MBIM_UNKOWN)
    {
        databuf_add_string(&request->resp, MB_ERROR, "Server : Unknown request");
        databuf_add_uint(&request->resp, MB_RESPONSE, MBIM_ERROR);
        return;
    }

    databuf_get_uint(&request->req, MB_PROTOCOL, &request->proto);
    if (request->proto == MB_PROT_UNKOWN)
    {
        databuf_add_string(&request->resp, MB_ERROR, "Server : Unknown protocol");
        databuf_add_uint(&request->resp, MB_RESPONSE, MBIM_ERROR);
        return;
    }

    request->tid = 0;
    databuf_get_uint(&request->req, MB_SESSION_TID, &request->tid);

    if (request->proto == MB_PROT_MBIM)
        mbim_perform_request(request);
    else
        qmi_perform_request(request);
}

/**
 * Open an NNG REP socket and retry binding if it fails.
 *
 * @param sock Pointer to the NNG socket
 * @param url  URL to bind the socket to
 *
 * @return True on success, otherwise false
 */
bool rep_server_open(nng_socket *sock, const char *url)
{
    int retry = 0;
    while (retry < NODE_BIND_RETRIES)
    {
        if (server_open(sock, url))
            return true;

        retry++;
        sleep_us(NODE_RETRY_SLEEP * retry);
    }

    return false;
}

/**
 * Perform a request handling loop for the MBIM server.
 *
 * @param sock     Pointer to the NNG socket
 * @param request  Pointer to the Mbim_request structure
 *
 * @return True on success, otherwise false
 */
bool rep_server_perform_request(nng_socket *sock, Mbim_request *request)
{
    unsigned char *buf = NULL;
    size_t size = 0;
    int ret;

    ret = nng_recv(*sock, &buf, &size, NNG_FLAG_NONBLOCK | NNG_FLAG_ALLOC);
    if (ret == NNG_EAGAIN)
    {
        sleep_us(100 * 1000);
        return true;
    }

    if (ret != 0)
    {
        printf("Server : Receive failed [%d] : %s\n", ret, nng_strerror(ret));
        nng_close(*sock);
        return false;
    }

    databuf_set_buf(&request->req, buf, size);
    databuf_init(&request->resp);

    handle_request(request);

    if ((ret = nng_send(*sock, request->resp.buf, request->resp.len, 0)) != 0)
        printf("Failed to reply: %s\n", nng_strerror(ret));

    databuf_free(&request->req);
    databuf_free(&request->resp);

    return true;
}
