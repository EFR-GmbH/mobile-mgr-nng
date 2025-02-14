#include <signal.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <nng/nng.h>
#include <nng/protocol/reqrep0/req.h>

#include "mbim_enum.h"

#define NNG_IPC_PREFIX "ipc://"
#define NNG_SOCKET "/tmp/mbim_nng.socket"

#define GET_MOB_INFO_RETRY_TIMEOUT_MS 10000 //10 sec

static bool check_resp(Databuf *response)
{
    int status = -1;
    if (!databuf_is_valid(response))
    {
        printf("Error : Response is not valid\n");
        databuf_free(response);
        return false;
    }

    if (!databuf_get_uint(response, MB_RESPONSE, &status))
    {
        printf("Error : response does not contains the status\n");
        databuf_free(response);
        return false;
    }

    if (status != MBIM_OK)
    {
        printf("Error : Resp status is error : %s\n", databuf_get_string(response, MB_ERROR));
        databuf_free(response);
        return false;
    }

    return true;
}

static bool get_resp(nng_socket sock, Databuf *request, Databuf *response)
{
    unsigned char *buf = NULL;
    size_t size = 0;

    int ret = nng_send(sock, request->buf, request->len, 0);
    if (ret != 0)
    {
        printf("Failed to send request: %s\n", nng_strerror(ret));
        nng_close(sock);
        return false;
    }

    ret = nng_recv(sock, &buf, &size, NNG_FLAG_ALLOC);

    if (ret != 0)
    {
        printf("Failed to receive data: %s\n", nng_strerror(ret));
        nng_free(buf, size);
        return false;
    }
    databuf_set_buf(response, buf, size);

    return check_resp(response);
}

void pin_status(Databuf *response)
{
    int pin_status = -1;
    if (!databuf_get_uint(response, MB_PIN_STATUS, &pin_status))
        printf("Error : no pin status (MB_PIN_STATUS)\n");
    else
        printf("Ok : Pin status (%d) : %s\n", pin_status, pin_status == MBIM_PIN_UNLOCK ? "MBIM_PIN_UNLOCK" : "MBIM_PIN_LOCK");
}

void subscriber(Databuf *response)
{
    int telephone_numbers_count = -1;
    printf("MB_SUB_STATE : %s\n" , databuf_get_string(response, MB_SUB_STATE));
    printf("MB_SUB_ID : %s\n" , databuf_get_string(response, MB_SUB_ID));
    printf("MB_SUB_SIM_ICCD : %s\n" , databuf_get_string(response, MB_SUB_SIM_ICCD));
    printf("MB_SUB_READY_INFO : %s\n" , databuf_get_string(response, MB_SUB_READY_INFO));
    printf("MB_SUB_TEL_NUM : %s\n" , databuf_get_string(response, MB_SUB_TEL_NUM));

    if (!databuf_get_uint(response, MB_SUB_TEL_NB, &telephone_numbers_count))
        printf("Error : no telephone_numbers_count (MB_SUB_TEL_NUM)\n");
    else
        printf("Ok :telephone_numbers_count : %d\n", telephone_numbers_count);
}

void mregister(Databuf *response)
{
    int register_state = -1;
    printf("MB_REGISTER_NET_ERROR : %s\n" , databuf_get_string(response, MB_REGISTER_NET_ERROR));
    printf("MB_REGISTER_STATE_STR : %s\n" , databuf_get_string(response, MB_REGISTER_STATE_STR));
    printf("MB_REGISTER_MODE : %s\n" , databuf_get_string(response, MB_REGISTER_MODE));
    printf("MB_REGISTER_DATA_CLASS : %s\n" , databuf_get_string(response, MB_REGISTER_DATA_CLASS));
    printf("MB_REGISTER_CLASS : %s\n" , databuf_get_string(response, MB_REGISTER_CLASS));
    printf("MB_REGISTER_PROVIDER_ID : %s\n" , databuf_get_string(response, MB_REGISTER_PROVIDER_ID));
    printf("MB_REGISTER_PROVIDER_NAME : %s\n" , databuf_get_string(response, MB_REGISTER_PROVIDER_NAME));
    printf("MB_REGISTER_ROAMING : %s\n" , databuf_get_string(response, MB_REGISTER_ROAMING));
    printf("MB_REGISTER_FLAGS : %s\n" , databuf_get_string(response, MB_REGISTER_FLAGS));

    if (!databuf_get_uint(response, MB_REGISTER_STATE, &register_state))
        printf("Error : no register_state (MB_REGISTER_STATE)\n");
    else
        printf("Ok : register_state : %d\n", register_state);
}

void packet_service(Databuf *response)
{
    int uplink_speed = -1;
    int downlink_speed = -1;

    printf("MB_ATTACH_NET_ERROR : %s\n" , databuf_get_string(response, MB_ATTACH_NET_ERROR));
    printf("MB_ATTACH_PCK_SERVICE_STATE : %s\n" , databuf_get_string(response, MB_ATTACH_PCK_SERVICE_STATE));
    printf("MB_ATTACH_DATA_CLASS : %s\n" , databuf_get_string(response, MB_ATTACH_DATA_CLASS));
    printf("MB_ATTACH_UP_SPEED_STR : %s\n" , databuf_get_string(response, MB_ATTACH_UP_SPEED_STR));
    printf("MB_ATTACH_DOWN_SPEED_STR : %s\n" , databuf_get_string(response, MB_ATTACH_DOWN_SPEED_STR));

    if (!databuf_get_uint(response, MB_ATTACH_UP_SPEED, &uplink_speed))
        printf("Error : no uplink_speed (MB_ATTACH_UP_SPEED)\n");
    else
        printf("Ok : uplink_speed : %d\n", uplink_speed);

    if (!databuf_get_uint(response, MB_ATTACH_DOWN_SPEED, &downlink_speed))
        printf("Error : no downlink_speed (MB_ATTACH_DOWN_SPEED)\n");
    else
        printf("Ok : downlink_speed : %d\n", downlink_speed);
}

void connected(Databuf *response)
{
    printf("Connected !!!\n");
}

void status(Databuf *response)
{
    int activation_state = -1;
    int session_id = -1;

    printf("MB_STATE_ACTIVATION_STR : %s\n" , databuf_get_string(response, MB_STATE_ACTIVATION_STR));
    printf("MB_STATE_VOICE_CALL_STATE : %s\n" , databuf_get_string(response, MB_STATE_VOICE_CALL_STATE));
    printf("MB_STATE_IP_TYPE : %s\n" , databuf_get_string(response, MB_STATE_IP_TYPE));
    printf("MB_STATE_CONTEXT_TYPE : %s\n" , databuf_get_string(response, MB_STATE_CONTEXT_TYPE));
    printf("MB_STATE_NETWORK_ERROR : %s\n" , databuf_get_string(response, MB_STATE_NETWORK_ERROR));

    if (!databuf_get_uint(response, MB_STATE_ACTIVATION, &activation_state))
        printf("Error : no activation_state (MB_STATE_ACTIVATION)\n");
    else
        printf("Ok : activation_state : %d\n", activation_state);

    if (!databuf_get_uint(response, MB_STATE_SESSION_ID, &session_id))
        printf("Error : no session_id (MB_STATE_SESSION_ID)\n");
    else
        printf("Ok : session_id : %d\n", session_id);
}

void ip_state(Databuf *response)
{
    int ipv4_nb = -1;
    int ipv6_nb = -1;
    unsigned char *ipv4 = NULL;
    unsigned char *ipv6 = NULL;

    if (!databuf_get_uint(response, MB_IPV4_NB, &ipv4_nb))
        printf("Error : no ipv4_nb (MB_IPV4_NB)\n");
    else
        printf("Ok : ipv4_nb : %d\n", ipv4_nb);

    if (!databuf_get_uint(response, MB_IPV6_NB, &ipv6_nb))
        printf("Error : no ipv6_nb (MB_IPV6_NB)\n");
    else
        printf("Ok : ipv6_nb : %d\n", ipv6_nb);

    printf("MB_IPV4_GW : %s\n" , databuf_get_string(response, MB_IPV4_GW));
    printf("MB_IPV6_GW : %s\n" , databuf_get_string(response, MB_IPV6_GW));

    for (int i = 0; i < ipv4_nb; i++)
    {
        ipv4 = databuf_get_next_string(response, MB_IPV4_ADDR, ipv4);
        printf("MB_IPV4_ADDR : %s\n" , ipv4);
    }

    for (int i = 0; i < ipv6_nb; i++)
    {
        ipv6 = databuf_get_next_string(response, MB_IPV6_ADDR, ipv6);
        printf("MB_IPV6_ADDR : %s\n" , ipv6);
    }
}

void device_caps(Databuf *response)
{
    int max_sessions = -1;
    if (!databuf_get_uint(response, MB_DEV_MAX_SESSION, &max_sessions))
        printf("Error : no max_sessions (MB_DEV_MAX_SESSION)\n");
    else
        printf("Ok : max_sessions : %d\n", max_sessions);

    printf("MB_DEV_TYPE : %s\n" , databuf_get_string(response, MB_DEV_TYPE));
    printf("MB_DEV_CELL_CLASS : %s\n" , databuf_get_string(response, MB_DEV_CELL_CLASS));
    printf("MB_DEV_VOICE_CLASS : %s\n" , databuf_get_string(response, MB_DEV_VOICE_CLASS));
    printf("MB_DEV_SIM_CLASS : %s\n" , databuf_get_string(response, MB_DEV_SIM_CLASS));
    printf("MB_DEV_DATA_CLASS : %s\n" , databuf_get_string(response, MB_DEV_DATA_CLASS));
    printf("MB_DEV_SMS_CAPS : %s\n" , databuf_get_string(response, MB_DEV_SMS_CAPS));
    printf("MB_DEV_CTRL_CAPS : %s\n" , databuf_get_string(response, MB_DEV_CTRL_CAPS));
    printf("MB_DEV_CUST_DATA_CLASS : %s\n" , databuf_get_string(response, MB_DEV_CUST_DATA_CLASS));
    printf("MB_DEV_ID : %s\n" , databuf_get_string(response, MB_DEV_ID));
    printf("MB_DEV_FMW_INFO : %s\n" , databuf_get_string(response, MB_DEV_FMW_INFO));
    printf("MB_DEV_HW_INFO : %s\n" , databuf_get_string(response, MB_DEV_HW_INFO));
}

void signal_state(Databuf *response)
{
    int rssi = -1;
    int error_rate = -1;
    int rscp = -1;
    int ecno = -1;
    int rsrq = -1;
    int rsrp = -1;
    int rssnr = -1;

    if (!databuf_get_uint(response, MB_SIGNAL_RSSI, &rssi))
        printf("Error : no rssi (MB_SIGNAL_RSSI)\n");
    else
        printf("Ok : rssi : %d\n", rssi);

    if (!databuf_get_uint(response, MB_SIGNAL_ERROR_RATE, &error_rate))
        printf("Error : no error_rate (MB_SIGNAL_ERROR_RATE)\n");
    else
        printf("Ok : error_rate : %d\n", error_rate);

    if (!databuf_get_uint(response, MB_SIGNAL_RSCP, &rscp))
        printf("Error : no rscp (MB_SIGNAL_RSCP)\n");
    else
        printf("Ok : rscp : %d\n", rscp);

    if (!databuf_get_uint(response, MB_SIGNAL_ECNO, &ecno))
        printf("Error : no ecno (MB_SIGNAL_ECNO)\n");
    else
        printf("Ok : ecno : %d\n", ecno);

    if (!databuf_get_uint(response, MB_SIGNAL_RSRQ, &rsrq))
        printf("Error : no rsrq (MB_SIGNAL_RSRQ)\n");
    else
        printf("Ok : rsrq : %d\n", rsrq);

    if (!databuf_get_uint(response, MB_SIGNAL_RSRP, &rsrp))
        printf("Error : no rsrp (MB_SIGNAL_RSRP)\n");
    else
        printf("Ok : rsrp : %d\n", rsrp);

    if (!databuf_get_uint(response, MB_SIGNAL_RSSNR, &rssnr))
        printf("Error : no rssnr (MB_SIGNAL_RSSNR)\n");
    else
        printf("Ok : rssnr : %d\n", rssnr);
}

typedef void (*callback)(Databuf *response);

bool perform_request(nng_socket sock, Mbim_req_type mbim_req)
{
    Databuf request = {0};
    Databuf response = {0};
    callback cb = NULL;

    databuf_init(&request);

    databuf_add_uint(&request, MB_REQUEST, mbim_req);
    switch (mbim_req)
    {
        case MBIM_PIN_STATUS:
            cb = pin_status;
            break;

        case MBIM_SUBSCRIBER:
            cb = subscriber;
            break;

        case MBIM_REGISTER:
            cb = mregister;
            break;

        case MBIM_IP:
            cb = ip_state;
            break;

        case MBIM_STATUS:
            cb = status;
            break;

        case MBIM_DEVICE_CAPS:
            cb = device_caps;
            break;

        case MBIM_PACKET_SERVICE:
            cb = packet_service;
            break;

        case MBIM_SIGNAL:
            cb = signal_state;
            break;

        default:
            databuf_free(&request);
            return true;
    }

    if (!get_resp(sock, &request, &response))
    {
        databuf_free(&request);
        return false;
    }

    cb(&response);

    databuf_free(&request);
    databuf_free(&response);

    return true;
}

int main(int argc, char *argv[])
{
    nng_socket sock;
    int ret = nng_req0_open(&sock);
    if (ret != 0)
    {
        printf("Failed to open socket: %s\n", nng_strerror(ret));
        return false;
    }

    size_t url_len = strlen(NNG_IPC_PREFIX) + strlen(NNG_SOCKET) + 1;
    char *url = (char *) calloc(url_len, 1);

    snprintf(url, url_len, NNG_IPC_PREFIX"%s", NNG_SOCKET);
    ret = nng_dial(sock, url, NULL, 0);
    free(url);

    if (ret != 0)
    {
        printf("Failed to dial to socket: %s\n", nng_strerror(ret));
        nng_close(sock);
        return false;
    }

    ret = nng_socket_set_ms(sock, NNG_OPT_RECVTIMEO, GET_MOB_INFO_RETRY_TIMEOUT_MS);
    if (ret != 0)
        printf("Failed to set receive timeout: %s\n", nng_strerror(ret));

    for (int i = MBIM_PIN_STATUS; i < MBIM_UNKOWN; i++)
    {
        perform_request(sock, i);
        sleep(1);
    }
    nng_close(sock);

    return 0;
}
