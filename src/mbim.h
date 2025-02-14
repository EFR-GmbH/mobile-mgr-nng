#ifndef MBIM_NNG_MBIM_H
#define MBIM_NNG_MBIM_H

#include "databuf.h"
#include "mbim_enum.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef MBIM_NNG_DEVICE
#define MBIM_NNG_DEVICE "/dev/cdc-wdm0"
#endif
#define VALIDATE_UNKNOWN(str) ((str) ? (str) : "unknown")

typedef struct mbim_request
{
    Mbim_req_type type;
    Mbim_protocol proto;
    unsigned int tid;
    unsigned int user_data;
    Databuf req;
    Databuf resp;
} Mbim_request;

void mbim_perform_request(Mbim_request *request);
void qmi_perform_request(Mbim_request *request);

#ifdef __cplusplus
}
#endif

#endif // MBIM_NNG_MBIM_H
