#ifndef MBIM_NNG_SERVER_H
#define MBIM_NNG_SERVER_H

#include <stdint.h>
#include "nng/nng.h"

#include "mbim.h"

#include "databuf.h"

#ifdef __cplusplus
extern "C" {
#endif

bool rep_server_open(nng_socket *sock, const char *url);
bool rep_server_perform_request(nng_socket *sock, Mbim_request *request);

#ifdef __cplusplus
}
#endif

#endif // MBIM_NNG_SERVER_H
