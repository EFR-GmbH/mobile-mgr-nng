#ifndef MBIM_NNG_MBIM_ENUM_H
#define MBIM_NNG_MBIM_ENUM_H

#include "databuf.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    MBIM_PIN_STATUS = 0,
    MBIM_PIN_ENTER,
    MBIM_SUBSCRIBER,
    MBIM_REGISTER,
    MBIM_ATTACH,
    MBIM_CONNECT,
    MBIM_IP,
    MBIM_STATUS,
    MBIM_DEVICE_CAPS,
    MBIM_PACKET_SERVICE,
    MBIM_SIGNAL,
    MBIM_UNKOWN
} Mbim_req_type;

typedef enum
{
    MB_PROT_MBIM = 0,
    MB_PROT_QMI,
    MB_PROT_UNKOWN
} Mbim_protocol;

typedef enum
{
    MBIM_OK = 0,
    MBIM_ERROR
} Mbim_resp_status;

typedef enum
{
    MBIM_PIN_UNLOCK = 0,
    MBIM_PIN_LOCK
} Mbim_pin_status;

typedef enum
{
    MBIM_REGISTER_UNKNOWN = 0,
    MBIM_REGISTER_DEREGISTERED,
    MBIM_REGISTER_SEARCHING,
    MBIM_REGISTER_HOME,
    MBIM_REGISTER_ROAMING,
    MBIM_REGISTER_PARTNER,
    MBIM_REGISTER_DENIED
} Mbim_register_state;

typedef enum
{
    QMI_REGISTER_DEREGISTERED = 0,
    QMI_REGISTER_REGISTERED,
    QMI_REGISTER_SEARCHING,
    QMI_REGISTER_DENIED,
    QMI_REGISTER_UNKNOWN
} Qmi_register_state;

typedef enum
{
    MBIM_AUTH_NONE = 0,
    MBIM_AUTH_PAP,
    MBIM_AUTH_CHAP,
    MBIM_AUTH_MSCHAPV2,
} Mbim_auth;

typedef enum
{
    MBIM_ACTIVATION_UNKNOWN = 0,
    MBIM_ACTIVATION_ACTIVATED,
    MBIM_ACTIVATION_ACTIVATING,
    MBIM_ACTIVATION_DEACTIVATED,
    MBIM_ACTIVATION_DEACTIVATING
} Mbim_activation_state;

enum mbim_vartype // 2 bytes (var name), 2 bytes data type
{
    // Request/response
    MB_ERROR = ((1 << 8) | DT_STRING),
    MB_REQUEST = ((2 << 8) | DT_UINT), // Mbim_req_type
    MB_RESPONSE = ((3 << 8) | DT_UINT), // Mbim_resp_status
    MB_SESSION_TID  = ((4 << 8) | DT_UINT),
    MB_APN = ((5 << 8) | DT_STRING),
    MB_USERNAME = ((6 << 8) | DT_STRING),
    MB_PASSWORD = ((7 << 8) | DT_STRING),
    MB_AUTH = ((8 << 8) | DT_UINT), // Mbim_auth
    MB_DEVICE = ((9 << 8) | DT_STRING),
    // Pin
    MB_PIN_STATUS = ((10 << 8) | DT_UINT), // Mbim_pin_status
    MB_PIN_CODE = ((11 << 8) | DT_STRING),
    MB_PROTOCOL = ((12 << 8) | DT_UINT), // Mbim_protocol
    // Subscriber
    MB_SUB_STATE = ((20 << 8) | DT_STRING),
    MB_SUB_ID = ((21 << 8) | DT_STRING),
    MB_SUB_SIM_ICCD = ((22 << 8) | DT_STRING),
    MB_SUB_READY_INFO = ((23 << 8) | DT_STRING),
    MB_SUB_TEL_NB = ((24 << 8) | DT_UINT),
    MB_SUB_TEL_NUM = ((25 << 8) | DT_STRING),
    // Register
    MB_REGISTER_STATE = ((30 << 8) | DT_UINT), // Mbim_register_state
    MB_REGISTER_NET_ERROR = ((31 << 8) | DT_STRING),
    MB_REGISTER_STATE_STR = ((32 << 8) | DT_STRING),
    MB_REGISTER_MODE = ((33 << 8) | DT_STRING),
    MB_REGISTER_DATA_CLASS = ((34 << 8) | DT_STRING),
    MB_REGISTER_CLASS = ((35 << 8) | DT_STRING),
    MB_REGISTER_PROVIDER_ID = ((36 << 8) | DT_STRING),
    MB_REGISTER_PROVIDER_NAME = ((37 << 8) | DT_STRING),
    MB_REGISTER_ROAMING = ((38 << 8) | DT_STRING),
    MB_REGISTER_FLAGS = ((39 << 8) | DT_STRING),
    // Attach
    MB_ATTACH_NET_ERROR = ((50 << 8) | DT_STRING),
    MB_ATTACH_PCK_SERVICE_STATE = ((51 << 8) | DT_STRING),
    MB_ATTACH_DATA_CLASS = ((52 << 8) | DT_STRING),
    MB_ATTACH_UP_SPEED = ((53 << 8) | DT_UINT),
    MB_ATTACH_DOWN_SPEED = ((54 << 8) | DT_UINT),
    MB_ATTACH_UP_SPEED_STR = ((55 << 8) | DT_STRING),
    MB_ATTACH_DOWN_SPEED_STR = ((56 << 8) | DT_STRING),
    // Status
    MB_STATE_ACTIVATION = ((60 << 8) | DT_UINT), // Mbim_activation_state
    MB_STATE_ACTIVATION_STR = ((61 << 8) | DT_STRING),
    MB_STATE_SESSION_ID = ((62 << 8) | DT_UINT),
    MB_STATE_VOICE_CALL_STATE = ((63 << 8) | DT_STRING),
    MB_STATE_IP_TYPE = ((64 << 8) | DT_STRING),
    MB_STATE_CONTEXT_TYPE = ((65 << 8) | DT_STRING),
    MB_STATE_NETWORK_ERROR = ((66 << 8) | DT_STRING),
    // IP
    MB_IPV4_NB = ((70 << 8) | DT_UINT),
    MB_IPV6_NB = ((71 << 8) | DT_UINT),
    MB_IPV4_GW = ((72 << 8) | DT_STRING),
    MB_IPV6_GW = ((73 << 8) | DT_STRING),
    MB_IPV4_ADDR = ((74 << 8) | DT_STRING),
    MB_IPV6_ADDR = ((75 << 8) | DT_STRING),
    // Device caps
    MB_DEV_TYPE = ((80 << 8) | DT_STRING),
    MB_DEV_CELL_CLASS = ((81 << 8) | DT_STRING),
    MB_DEV_VOICE_CLASS = ((82 << 8) | DT_STRING),
    MB_DEV_SIM_CLASS = ((83 << 8) | DT_STRING),
    MB_DEV_DATA_CLASS = ((84 << 8) | DT_STRING),
    MB_DEV_SMS_CAPS = ((85 << 8) | DT_STRING),
    MB_DEV_CTRL_CAPS = ((86 << 8) | DT_STRING),
    MB_DEV_MAX_SESSION = ((87 << 8) | DT_UINT),
    MB_DEV_CUST_DATA_CLASS = ((88 << 8) | DT_STRING),
    MB_DEV_ID = ((89 << 8) | DT_STRING),
    MB_DEV_FMW_INFO = ((90 << 8) | DT_STRING),
    MB_DEV_HW_INFO = ((91 << 8) | DT_STRING),
    // Signal
    MB_SIGNAL_RSSI = ((100 << 8) | DT_UINT),
    MB_SIGNAL_ERROR_RATE = ((101 << 8) | DT_UINT),
    MB_SIGNAL_RSCP = ((102 << 8) | DT_UINT),
    MB_SIGNAL_ECNO = ((103 << 8) | DT_UINT),
    MB_SIGNAL_RSRQ = ((104 << 8) | DT_UINT),
    MB_SIGNAL_RSRP = ((105 << 8) | DT_UINT),
    MB_SIGNAL_RSSNR = ((106 << 8) | DT_UINT),

};

#ifdef __cplusplus
}
#endif

#endif // MBIM_NNG_MBIM_ENUM_H

