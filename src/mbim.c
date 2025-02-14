/**
 * @file
 * @brief Mbim manager
 * @ccmod{MBIM_X_MMG}
 */

#include <stdio.h>
#include <stdbool.h>
#include <glib.h>
#include <glib/gprintf.h>
#include <gio/gio.h>
#include <glib-unix.h>
#include "libmbim-glib/libmbim-glib.h"

#include "mbim.h"
#include "nng_server.h"

static GMainLoop *g_loop;
static GCancellable *g_cancellable;
static MbimDevice *g_device;

/** Handle the SIGINT, SIGHUP, and SIGTERM signals gracefully
 *
 * @param psignum  Signal number
 *
 * @return FALSE to remove the signal handler
 */
static gboolean signals_handler(gpointer psignum)
{
    if (g_cancellable && !g_cancellable_is_cancelled(g_cancellable))
    {
        g_cancellable_cancel(g_cancellable);
        g_unix_signal_add(GPOINTER_TO_INT(psignum), (GSourceFunc)signals_handler, psignum);
        return FALSE;
    }

    if (g_loop && g_main_loop_is_running(g_loop))
        g_idle_add((GSourceFunc)g_main_loop_quit, g_loop);

    return FALSE;
}

/** Callback function when device close operation is ready
 *
 * @param dev      MbimDevice pointer
 * @param res      GAsyncResult pointer
 */
static void device_close_ready(MbimDevice *dev, GAsyncResult *res)
{
    GError *error = NULL;

    if (!mbim_device_close_finish(dev, res, &error))
    {
        printf("Couldn't close device: %s\n", error->message);
        g_error_free(error);
    }

    g_main_loop_quit(g_loop);
}

/** Close the MBIM connection and clean up resources
 *
 * @param request  Mbim_request pointer
 */
static void mbim_close(Mbim_request *request)
{
    g_clear_object(&g_cancellable);
    g_object_set(g_device, MBIM_DEVICE_IN_SESSION, request->tid ? TRUE : FALSE, NULL);

    mbim_device_close(g_device, 15, g_cancellable, (GAsyncReadyCallback)device_close_ready, NULL);
}

/** Set an error response for a Mbim_request
 *
 * @param request  Mbim_request pointer
 * @param error    Error message string
 */
static void set_error(Mbim_request *request, const char *error)
{
    databuf_add_string(&request->resp, MB_ERROR, error);
    databuf_add_uint(&request->resp, MB_RESPONSE, MBIM_ERROR);
}

/** Callback function when PIN operation is ready
 *
 * @param device   MbimDevice pointer
 * @param res      GAsyncResult pointer
 * @param request  Mbim_request pointer
 */
static void pin_ready(MbimDevice *device, GAsyncResult *res, Mbim_request *request)
{
    MbimMessage *response;
    GError *error = NULL;
    MbimPinType pin_type;
    MbimPinState pin_state;
    guint32 remaining_attempts;

    response = mbim_device_command_finish(device, res, &error);
    if (!response || !mbim_message_response_get_result(response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error))
    {
        printf("Mbim : Operation failed: %s\n", error->message);

        if (request->user_data)
        {
            printf("Mbim : Unlock SIM failed\n");
            set_error(request, "Unlock SIM failed");
        }
        else
            set_error(request, error->message);

        g_error_free(error);
        if (response)
            mbim_message_unref(response);

        mbim_close(request);
        return;
    }

    if (!mbim_message_pin_response_parse(response, &pin_type, &pin_state, &remaining_attempts, &error))
    {
        printf("Couldn't parse response message: %s\n", error->message);
        set_error(request, "Couldn't parse response message");
        g_error_free(error);
        mbim_message_unref(response);
        mbim_close(request);
        return;
    }

    if (request->user_data)
        printf("[%s] PIN operation successful\n", mbim_device_get_path_display(device));

    if (pin_state == MBIM_PIN_STATE_UNLOCKED || pin_type == MBIM_PIN_TYPE_PIN2)
    {
        printf("PIN is UNLOCKED\n");

        databuf_add_uint(&request->resp, MB_PIN_STATUS, MBIM_PIN_UNLOCK);
        databuf_add_uint(&request->resp, MB_RESPONSE, MBIM_OK);

        mbim_message_unref(response);
        mbim_close(request);
        return;
    }

    printf("PIN is LOCKED\n");

    if (pin_type != MBIM_PIN_TYPE_PIN1)
    {
        printf("Only PIN1 is supported\n");
        set_error(request, "Only PIN1 is supported");
        mbim_message_unref(response);
        mbim_close(request);
        return;
    }

    databuf_add_uint(&request->resp, MB_PIN_STATUS, MBIM_PIN_LOCK);
    databuf_add_uint(&request->resp, MB_RESPONSE, MBIM_OK);

    mbim_message_unref(response);
    mbim_close(request);
}

/** Callback function when subscriber ready status query operation is ready
 *
 * @param device   MbimDevice pointer
 * @param res      GAsyncResult pointer
 * @param request  Mbim_request pointer
 */
static void query_subscriber_ready_status_ready(MbimDevice *device, GAsyncResult *res, Mbim_request *request)
{
    MbimMessage *response;
    GError *error = NULL;
    MbimSubscriberReadyState ready_state;
    const gchar *ready_state_str;
    gchar *subscriber_id;
    gchar *sim_iccid;
    MbimReadyInfoFlag ready_info;
    gchar *ready_info_str;
    guint32 telephone_numbers_count;
    gchar **telephone_numbers;
    gchar *telephone_numbers_str;

    response = mbim_device_command_finish(device, res, &error);
    if (!response || !mbim_message_response_get_result(response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error))
    {
        printf("Operation failed: %s\n", error->message);
        set_error(request, error->message);

        g_error_free(error);
        if (response)
            mbim_message_unref(response);
        mbim_close(request);
        return;
    }

    if (!mbim_message_subscriber_ready_status_response_parse(response, &ready_state, &subscriber_id, &sim_iccid, &ready_info,
                                                             &telephone_numbers_count, &telephone_numbers, &error))
    {
        printf("Couldn't parse response message: %s\n", error->message);
        set_error(request, error->message);

        g_error_free(error);
        mbim_message_unref(response);
        mbim_close(request);
        return;
    }

    telephone_numbers_str = (telephone_numbers ? g_strjoinv(", ", telephone_numbers) : NULL);
    ready_state_str = mbim_subscriber_ready_state_get_string(ready_state);
    ready_info_str = mbim_ready_info_flag_build_string_from_mask(ready_info);

    printf("[%s] Subscriber ready status retrieved:\n"
             "\t      Ready state: '%s'\n"
             "\t    Subscriber ID: '%s'\n"
             "\t        SIM ICCID: '%s'\n"
             "\t       Ready info: '%s'\n"
             "\tTelephone numbers: (%u) '%s'\n",
             mbim_device_get_path_display(device), VALIDATE_UNKNOWN(ready_state_str), VALIDATE_UNKNOWN(subscriber_id),
             VALIDATE_UNKNOWN(sim_iccid), VALIDATE_UNKNOWN(ready_info_str), telephone_numbers_count,
             VALIDATE_UNKNOWN(telephone_numbers_str));

    databuf_add_string(&request->resp, MB_SUB_STATE, VALIDATE_UNKNOWN(ready_state_str));
    databuf_add_string(&request->resp, MB_SUB_ID, VALIDATE_UNKNOWN(subscriber_id));
    databuf_add_string(&request->resp, MB_SUB_SIM_ICCD, VALIDATE_UNKNOWN(sim_iccid));
    databuf_add_string(&request->resp, MB_SUB_READY_INFO, VALIDATE_UNKNOWN(ready_info_str));
    databuf_add_uint(&request->resp, MB_SUB_TEL_NB, telephone_numbers_count);
    databuf_add_string(&request->resp, MB_SUB_TEL_NUM, VALIDATE_UNKNOWN(telephone_numbers_str));

    databuf_add_uint(&request->resp, MB_RESPONSE, MBIM_OK);

    g_free(subscriber_id);
    g_free(sim_iccid);
    g_free(ready_info_str);
    g_strfreev(telephone_numbers);
    g_free(telephone_numbers_str);

    mbim_message_unref(response);
    mbim_close(request);
}

/** Callback function when register state query operation is ready
 *
 * @param device   MbimDevice pointer
 * @param res      GAsyncResult pointer
 * @param request  Mbim_request pointer
 */
static void register_state_ready(MbimDevice *device, GAsyncResult *res, Mbim_request *request)
{
    MbimMessage *response;
    GError *error = NULL;
    MbimNwError nw_error;
    MbimRegisterState register_state;
    MbimRegisterMode register_mode;
    MbimDataClass available_data_classes;
    gchar *available_data_classes_str;
    MbimCellularClass cellular_class;
    gchar *cellular_class_str;
    gchar *provider_id;
    gchar *provider_name;
    gchar *roaming_text;
    MbimRegistrationFlag registration_flag;
    gchar *registration_flag_str;

    response = mbim_device_command_finish(device, res, &error);
    if (!response || !mbim_message_response_get_result(response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error))
    {
        printf("Mbim : Operation failed: %s\n", error->message);
        set_error(request, error->message);

        g_error_free(error);
        if (response)
            mbim_message_unref(response);
        mbim_close(request);
        return;
    }

    if (!mbim_message_register_state_response_parse(response, &nw_error, &register_state, &register_mode, &available_data_classes,
                                                    &cellular_class, &provider_id, &provider_name, &roaming_text, &registration_flag,
                                                    &error))
    {
        printf("Couldn't parse response message: %s\n", error->message);
        set_error(request, error->message);

        g_error_free(error);
        mbim_message_unref(response);
        mbim_close(request);
        return;
    }

    available_data_classes_str = mbim_data_class_build_string_from_mask(available_data_classes);
    cellular_class_str = mbim_cellular_class_build_string_from_mask(cellular_class);
    registration_flag_str = mbim_registration_flag_build_string_from_mask(registration_flag);

    printf("[%s] Registration status:\n"
           "\t         Network error: '%s'\n"
           "\t        Register state: '%s'\n"
           "\t         Register mode: '%s'\n"
           "\tAvailable data classes: '%s'\n"
           "\tCurrent cellular class: '%s'\n"
           "\t           Provider ID: '%s'\n"
           "\t         Provider name: '%s'\n"
           "\t          Roaming text: '%s'\n"
           "\t    Registration flags: '%s'\n",
           mbim_device_get_path_display(device), VALIDATE_UNKNOWN(mbim_nw_error_get_string(nw_error)),
           VALIDATE_UNKNOWN(mbim_register_state_get_string(register_state)),
           VALIDATE_UNKNOWN(mbim_register_mode_get_string(register_mode)), VALIDATE_UNKNOWN(available_data_classes_str),
           VALIDATE_UNKNOWN(cellular_class_str), VALIDATE_UNKNOWN(provider_id), VALIDATE_UNKNOWN(provider_name),
           VALIDATE_UNKNOWN(roaming_text), VALIDATE_UNKNOWN(registration_flag_str));

    databuf_add_uint(&request->resp, MB_REGISTER_STATE, register_state);
    databuf_add_string(&request->resp, MB_REGISTER_NET_ERROR, VALIDATE_UNKNOWN(mbim_nw_error_get_string(nw_error)));
    databuf_add_string(&request->resp, MB_REGISTER_STATE_STR, VALIDATE_UNKNOWN(mbim_register_state_get_string(register_state)));
    databuf_add_string(&request->resp, MB_REGISTER_MODE, VALIDATE_UNKNOWN(mbim_register_mode_get_string(register_mode)));
    databuf_add_string(&request->resp, MB_REGISTER_DATA_CLASS, VALIDATE_UNKNOWN(available_data_classes_str));
    databuf_add_string(&request->resp, MB_REGISTER_CLASS, VALIDATE_UNKNOWN(cellular_class_str));
    databuf_add_string(&request->resp, MB_REGISTER_PROVIDER_ID, VALIDATE_UNKNOWN(provider_id));
    databuf_add_string(&request->resp, MB_REGISTER_PROVIDER_NAME, VALIDATE_UNKNOWN(provider_name));
    databuf_add_string(&request->resp, MB_REGISTER_ROAMING, VALIDATE_UNKNOWN(roaming_text));
    databuf_add_string(&request->resp, MB_REGISTER_FLAGS, VALIDATE_UNKNOWN(registration_flag_str));

    databuf_add_uint(&request->resp, MB_RESPONSE, MBIM_OK);

    g_free(available_data_classes_str);
    g_free(cellular_class_str);
    g_free(registration_flag_str);
    g_free(provider_name);
    g_free(provider_id);
    g_free(roaming_text);

    mbim_message_unref(response);
    mbim_close(request);
}

/** Callback function when packet service set operation is ready
 *
 * @param device   MbimDevice pointer
 * @param res      GAsyncResult pointer
 * @param request  Mbim_request pointer
 */
static void packet_service_ready(MbimDevice *device, GAsyncResult *res, Mbim_request *request)
{
    MbimMessage *response;
    GError *error = NULL;
    guint32 nw_error;
    MbimPacketServiceState packet_service_state;
    MbimDataClass highest_available_data_class;
    gchar *highest_available_data_class_str;
    gchar *uplink_speed_str;
    gchar *downlink_speed_str;
    guint64 uplink_speed;
    guint64 downlink_speed;

    response = mbim_device_command_finish(device, res, &error);
    if (!response || !mbim_message_response_get_result(response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error))
    {
        printf("Operation failed: %s\n", error->message);
        set_error(request, error->message);

        g_error_free(error);
        if (response)
            mbim_message_unref(response);
        mbim_close(request);
        return;
    }

    if (!mbim_message_packet_service_response_parse(response, &nw_error, &packet_service_state, &highest_available_data_class,
                                                    &uplink_speed, &downlink_speed, &error))
    {
        printf("Couldn't parse response message: %s\n", error->message);
        set_error(request, error->message);

        g_error_free(error);
        mbim_message_unref(response);
        mbim_close(request);
        return;
    }

    highest_available_data_class_str = mbim_data_class_build_string_from_mask(highest_available_data_class);
    uplink_speed_str = g_strdup_printf("%" G_GUINT64_FORMAT " bps", uplink_speed);
    downlink_speed_str = g_strdup_printf("%" G_GUINT64_FORMAT " bps", downlink_speed);

    printf("[%s] Packet service status:\n"
             "\t         Network error: '%s'\n"
             "\t  Packet service state: '%s'\n"
             "\tAvailable data classes: '%s'\n"
             "\t          Uplink speed: '%" G_GUINT64_FORMAT " bps'\n"
             "\t        Downlink speed: '%" G_GUINT64_FORMAT " bps'\n",
             mbim_device_get_path_display(device), VALIDATE_UNKNOWN(mbim_nw_error_get_string(nw_error)),
             VALIDATE_UNKNOWN(mbim_packet_service_state_get_string(packet_service_state)),
             VALIDATE_UNKNOWN(highest_available_data_class_str), uplink_speed, downlink_speed);

    databuf_add_string(&request->resp, MB_ATTACH_NET_ERROR, VALIDATE_UNKNOWN(mbim_nw_error_get_string(nw_error)));
    databuf_add_string(&request->resp, MB_ATTACH_PCK_SERVICE_STATE, VALIDATE_UNKNOWN(mbim_packet_service_state_get_string(packet_service_state)));
    databuf_add_string(&request->resp, MB_ATTACH_DATA_CLASS, VALIDATE_UNKNOWN(highest_available_data_class_str));
    databuf_add_string(&request->resp, MB_ATTACH_UP_SPEED_STR, VALIDATE_UNKNOWN(uplink_speed_str));
    databuf_add_string(&request->resp, MB_ATTACH_DOWN_SPEED_STR, VALIDATE_UNKNOWN(downlink_speed_str));
    databuf_add_uint(&request->resp, MB_ATTACH_UP_SPEED, (unsigned int) uplink_speed);
    databuf_add_uint(&request->resp, MB_ATTACH_DOWN_SPEED, (unsigned int) downlink_speed);

    databuf_add_uint(&request->resp, MB_RESPONSE, MBIM_OK);

    g_free(highest_available_data_class_str);
    g_free(uplink_speed_str);
    g_free(downlink_speed_str);

    mbim_message_unref(response);
    mbim_close(request);
}

/** Callback function when connect set operation is ready
 *
 * @param device   MbimDevice pointer
 * @param res      GAsyncResult pointer
 * @param request  Mbim_request pointer
 */
static void connect_ready(MbimDevice *device, GAsyncResult *res, Mbim_request *request)
{
    MbimMessage *response;
    GError *error = NULL;
    guint32 session_id;
    MbimActivationState activation_state;
    MbimVoiceCallState voice_call_state;
    MbimContextIpType ip_type;
    const MbimUuid *context_type;
    guint32 nw_error;

    response = mbim_device_command_finish(device, res, &error);
    if (!response || !mbim_message_response_get_result(response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error))
    {
        printf("Operation failed: %s\n", error->message);
        set_error(request, error->message);

        g_error_free(error);
        if (response)
            mbim_message_unref(response);
        mbim_close(request);
        return;
    }

    if (!mbim_message_connect_response_parse(response, &session_id, &activation_state, &voice_call_state, &ip_type, &context_type,
                                             &nw_error, &error))
    {
        printf("Couldn't parse response message: %s\n", error->message);
        set_error(request, error->message);

        g_error_free(error);
        mbim_message_unref(response);
        mbim_close(request);
        return;
    }
    mbim_message_unref(response);

    databuf_add_uint(&request->resp, MB_RESPONSE, MBIM_OK);

    if (!request->user_data)
    {
        printf("[%s] Successfully connected\n", mbim_device_get_path_display(device));
        mbim_close(request);
        return;
    }

    printf("[%s] Connection status:\n"
           "\t      Session ID: '%u'\n"
           "\tActivation state: '%s'\n"
           "\tVoice call state: '%s'\n"
           "\t         IP type: '%s'\n"
           "\t    Context type: '%s'\n"
           "\t   Network error: '%s'\n",
           mbim_device_get_path_display(device), session_id, VALIDATE_UNKNOWN(mbim_activation_state_get_string(activation_state)),
           VALIDATE_UNKNOWN(mbim_voice_call_state_get_string(voice_call_state)),
           VALIDATE_UNKNOWN(mbim_context_ip_type_get_string(ip_type)),
           VALIDATE_UNKNOWN(mbim_context_type_get_string(mbim_uuid_to_context_type(context_type))),
           VALIDATE_UNKNOWN(mbim_nw_error_get_string(nw_error)));

    databuf_add_string(&request->resp, MB_STATE_ACTIVATION_STR, VALIDATE_UNKNOWN(mbim_activation_state_get_string(activation_state)));
    databuf_add_string(&request->resp, MB_STATE_VOICE_CALL_STATE, VALIDATE_UNKNOWN(mbim_voice_call_state_get_string(voice_call_state)));
    databuf_add_string(&request->resp, MB_STATE_IP_TYPE, VALIDATE_UNKNOWN(mbim_context_ip_type_get_string(ip_type)));
    databuf_add_string(&request->resp, MB_STATE_CONTEXT_TYPE, VALIDATE_UNKNOWN(mbim_context_type_get_string(mbim_uuid_to_context_type(context_type))));
    databuf_add_string(&request->resp, MB_STATE_NETWORK_ERROR, VALIDATE_UNKNOWN(mbim_nw_error_get_string(nw_error)));

    databuf_add_uint(&request->resp, MB_STATE_ACTIVATION, activation_state);
    databuf_add_uint(&request->resp, MB_STATE_SESSION_ID, session_id);

    mbim_close(request);
}

/** Callback function when IP configuration query operation is ready
 *
 * @param device   MbimDevice pointer
 * @param res      GAsyncResult pointer
 * @param request  Mbim_request pointer
 */
static void ip_configuration_query_ready(MbimDevice *device, GAsyncResult *res, Mbim_request *request)
{
    GError *error = NULL;
    MbimMessage *response;
    MbimIPConfigurationAvailableFlag ipv4configurationavailable;
    MbimIPConfigurationAvailableFlag ipv6configurationavailable;
    guint32 ipv4addresscount;
    MbimIPv4Element **ipv4address;
    guint32 ipv6addresscount;
    MbimIPv6Element **ipv6address;
    const MbimIPv4 *ipv4gateway;
    const MbimIPv6 *ipv6gateway;
    guint32 ipv4dnsservercount;
    MbimIPv4 *ipv4dnsserver;
    guint32 ipv6dnsservercount;
    MbimIPv6 *ipv6dnsserver;
    guint32 ipv4mtu;
    guint32 ipv6mtu;
    gchar *str;
    gchar *cidr;
    GInetAddress *addr;

    response = mbim_device_command_finish(device, res, &error);
    if (!response || !mbim_message_response_get_result(response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error))
    {
        printf("Couldn't get IP configuration response message: %s\n", error->message);
        set_error(request, error->message);

        g_clear_error(&error);
        if (response)
            mbim_message_unref(response);

        mbim_close(request);
        return;
    }

    if (!mbim_message_ip_configuration_response_parse(
            response,
            NULL, /* sessionid */
            &ipv4configurationavailable,
            &ipv6configurationavailable,
            &ipv4addresscount,
            &ipv4address,
            &ipv6addresscount,
            &ipv6address,
            &ipv4gateway,
            &ipv6gateway,
            &ipv4dnsservercount,
            &ipv4dnsserver,
            &ipv6dnsservercount,
            &ipv6dnsserver,
            &ipv4mtu,
            &ipv6mtu,
            &error))
    {
        printf("Couldn't parse IP configuration response message: %s\n", error->message);
        set_error(request, error->message);

        g_clear_error(&error);
        if (response)
            mbim_message_unref(response);

        mbim_close(request);
        return;
    }

    databuf_add_uint(&request->resp, MB_RESPONSE, MBIM_OK);

    if (!(ipv4configurationavailable & MBIM_IP_CONFIGURATION_AVAILABLE_FLAG_GATEWAY))
        ipv4gateway = NULL;

    if (!(ipv6configurationavailable & MBIM_IP_CONFIGURATION_AVAILABLE_FLAG_GATEWAY))
        ipv6gateway = NULL;

    databuf_add_uint(&request->resp, MB_IPV4_NB, ipv4addresscount);
    databuf_add_uint(&request->resp, MB_IPV6_NB, ipv6addresscount);

    if (ipv4gateway)
    {
        addr = g_inet_address_new_from_bytes((const guint8 *) ipv4gateway, G_SOCKET_FAMILY_IPV4);
        str = g_inet_address_to_string (addr);
        databuf_add_string(&request->resp, MB_IPV4_GW, str);
        g_object_unref(addr);
    }

    if (ipv6gateway)
    {
        addr = g_inet_address_new_from_bytes((const guint8 *) ipv6gateway, G_SOCKET_FAMILY_IPV6);
        str = g_inet_address_to_string(addr);
        databuf_add_string(&request->resp, MB_IPV6_GW, str);
        g_object_unref(addr);
    }

    for (int i = 0; i < ipv4addresscount; i++)
    {
        addr = g_inet_address_new_from_bytes((guint8 *) &ipv4address[i]->ipv4_address, G_SOCKET_FAMILY_IPV4);
        str = g_inet_address_to_string (addr);
        cidr = g_strdup_printf("%s/%u", str, ipv4address[i]->on_link_prefix_length);

        databuf_add_string(&request->resp, MB_IPV4_ADDR, cidr);

        g_free(str);
        g_free(cidr);
        g_object_unref(addr);
    }

    for (int i = 0; i < ipv6addresscount; i++)
    {
        addr = g_inet_address_new_from_bytes((guint8 *) &ipv6address[i]->ipv6_address, G_SOCKET_FAMILY_IPV6);
        str = g_inet_address_to_string(addr);
        cidr = g_strdup_printf("%s/%u", str, ipv6address[i]->on_link_prefix_length);

        databuf_add_string(&request->resp, MB_IPV6_ADDR, cidr);

        g_free(str);
        g_free(cidr);
        g_object_unref(addr);
    }

    mbim_ipv4_element_array_free(ipv4address);
    mbim_ipv6_element_array_free(ipv6address);
    g_free(ipv4dnsserver);
    g_free(ipv6dnsserver);

    if (response)
        mbim_message_unref(response);
    mbim_close(request);
}

/** Callback function when query device caps operation is ready
 *
 * @param device   MbimDevice pointer
 * @param res      GAsyncResult pointer
 * @param request  Mbim_request pointer
 */
static void query_device_caps_ready(MbimDevice *device, GAsyncResult *res, Mbim_request *request)
{
    MbimMessage *response;
    GError *error = NULL;
    MbimDeviceType device_type;
    const gchar *device_type_str;
    MbimCellularClass cellular_class;
    gchar *cellular_class_str;
    MbimVoiceClass voice_class;
    const gchar *voice_class_str;
    MbimSimClass sim_class;
    gchar *sim_class_str;
    MbimDataClass data_class;
    gchar *data_class_str;
    MbimSmsCaps sms_caps;
    gchar *sms_caps_str;
    MbimCtrlCaps ctrl_caps;
    gchar *ctrl_caps_str;
    guint32 max_sessions;
    gchar *custom_data_class;
    gchar *device_id;
    gchar *firmware_info;
    gchar *hardware_info;

    response = mbim_device_command_finish(device, res, &error);
    if (!response || !mbim_message_response_get_result(response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error))
    {
        printf("Operation failed: %s\n", error->message);
        set_error(request, error->message);
        g_error_free(error);
        if (response)
            mbim_message_unref(response);
        mbim_close(request);
        return;
    }

    if (!mbim_message_device_caps_response_parse(
            response,
            &device_type,
            &cellular_class,
            &voice_class,
            &sim_class,
            &data_class,
            &sms_caps,
            &ctrl_caps,
            &max_sessions,
            &custom_data_class,
            &device_id,
            &firmware_info,
            &hardware_info,
            &error))
    {
        printf("Couldn't parse response message: %s\n", error->message);
        set_error(request, error->message);
        g_error_free(error);
        mbim_message_unref(response);
        mbim_close(request);
        return;
    }

    databuf_add_uint(&request->resp, MB_RESPONSE, MBIM_OK);

    device_type_str = mbim_device_type_get_string (device_type);
    cellular_class_str = mbim_cellular_class_build_string_from_mask (cellular_class);
    voice_class_str = mbim_voice_class_get_string (voice_class);
    sim_class_str = mbim_sim_class_build_string_from_mask (sim_class);
    data_class_str = mbim_data_class_build_string_from_mask (data_class);
    sms_caps_str = mbim_sms_caps_build_string_from_mask (sms_caps);
    ctrl_caps_str = mbim_ctrl_caps_build_string_from_mask (ctrl_caps);

    databuf_add_uint(&request->resp, MB_DEV_MAX_SESSION, max_sessions);
    databuf_add_string(&request->resp, MB_DEV_TYPE, VALIDATE_UNKNOWN(device_type_str));
    databuf_add_string(&request->resp, MB_DEV_CELL_CLASS, VALIDATE_UNKNOWN(cellular_class_str));
    databuf_add_string(&request->resp, MB_DEV_VOICE_CLASS, VALIDATE_UNKNOWN(voice_class_str));
    databuf_add_string(&request->resp, MB_DEV_SIM_CLASS, VALIDATE_UNKNOWN(sim_class_str));
    databuf_add_string(&request->resp, MB_DEV_DATA_CLASS, VALIDATE_UNKNOWN(data_class_str));
    databuf_add_string(&request->resp, MB_DEV_SMS_CAPS, VALIDATE_UNKNOWN(sms_caps_str));
    databuf_add_string(&request->resp, MB_DEV_CTRL_CAPS, VALIDATE_UNKNOWN(ctrl_caps_str));
    databuf_add_string(&request->resp, MB_DEV_CUST_DATA_CLASS, VALIDATE_UNKNOWN(custom_data_class));
    databuf_add_string(&request->resp, MB_DEV_ID, VALIDATE_UNKNOWN(device_id));
    databuf_add_string(&request->resp, MB_DEV_FMW_INFO, VALIDATE_UNKNOWN(firmware_info));
    databuf_add_string(&request->resp, MB_DEV_HW_INFO, VALIDATE_UNKNOWN(hardware_info));

    g_free(cellular_class_str);
    g_free(sim_class_str);
    g_free(data_class_str);
    g_free(sms_caps_str);
    g_free(ctrl_caps_str);
    g_free(custom_data_class);
    g_free(device_id);
    g_free(firmware_info);
    g_free(hardware_info);

    mbim_message_unref(response);
    mbim_close(request);
}

/** Callback function when query signal operation is ready
 *
 * @param device   MbimDevice pointer
 * @param res      GAsyncResult pointer
 * @param request  Mbim_request pointer
 */
static void query_signal_ready(MbimDevice *device, GAsyncResult *res, Mbim_request *request)
{
    MbimMessage *response;
    GError *error = NULL;
    guint32 rssi = 0, error_rate = 0, rscp = 0, ecno = 0, rsrq = 0, rsrp = 0, rssnr = 0;

    response = mbim_device_command_finish (device, res, &error);
    if (!response || !mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error))
    {
        printf("error: operation failed: %s\n", error->message);
        set_error(request, error->message);
        g_error_free (error);
        if (response)
            mbim_message_unref (response);
        mbim_close(request);
        return;
    }

    if (!mbim_message_atds_signal_response_parse (
            response,
            &rssi,
            &error_rate,
            &rscp,
            &ecno,
            &rsrq,
            &rsrp,
            &rssnr,
            &error))
    {
        printf("error: couldn't parse response message: %s\n", error->message);
        set_error(request, error->message);
        g_error_free(error);
        mbim_message_unref(response);
        mbim_close(request);
        return;
    }

    databuf_add_uint(&request->resp, MB_RESPONSE, MBIM_OK);

    databuf_add_uint(&request->resp, MB_SIGNAL_RSSI, rssi);
    databuf_add_uint(&request->resp, MB_SIGNAL_ERROR_RATE, error_rate);
    databuf_add_uint(&request->resp, MB_SIGNAL_RSCP, rscp);
    databuf_add_uint(&request->resp, MB_SIGNAL_ECNO, ecno);
    databuf_add_uint(&request->resp, MB_SIGNAL_RSRQ, rsrq);
    databuf_add_uint(&request->resp, MB_SIGNAL_RSRP, rsrp);
    databuf_add_uint(&request->resp, MB_SIGNAL_RSSNR, rssnr);

    mbim_message_unref(response);
    mbim_close(request);
}

/** Callback function when device open operation is ready
 *
 * @param dev      MbimDevice pointer
 * @param res      GAsyncResult pointer
 * @param request  Mbim_request pointer
 */
static void device_open_ready(MbimDevice *dev, GAsyncResult *res, Mbim_request *request)
{
    int timeout = 40;
    char *pin_code;
    char *apn;
    char *username;
    char *password;
    int auth = -1;
    guint32 session_id = 0;
    GError *error = NULL;
    MbimMessage *mb_request = NULL;
    GAsyncReadyCallback callback = NULL;

    if (!mbim_device_open_finish(dev, res, &error))
    {
        printf("Couldn't open the MbimDevice: %s\n", error->message);
        set_error(request, error->message);
        g_error_free(error);
        g_main_loop_quit(g_loop);
        return;
    }

    databuf_add_string(&request->resp, MB_DEVICE, mbim_device_get_path_display(dev));

    request->user_data = 0;
    switch (request->type)
    {
        case MBIM_PIN_STATUS:
            mb_request = mbim_message_pin_query_new(NULL);
            callback = (GAsyncReadyCallback) pin_ready;
            break;

        case MBIM_PIN_ENTER:
            pin_code = databuf_get_string(&request->req, MB_PIN_CODE);
            if (!pin_code)
            {
                set_error(request, "You must provide a pin code (MB_PIN_CODE)");
                break;
            }

            mb_request = mbim_message_pin_set_new(MBIM_PIN_TYPE_PIN1, MBIM_PIN_OPERATION_ENTER, pin_code, NULL, &error);
            if (!mb_request)
                break;

            callback = (GAsyncReadyCallback) pin_ready;
            request->user_data = 1;
            break;

        case MBIM_SUBSCRIBER:
            mb_request = mbim_message_subscriber_ready_status_query_new(NULL);
            callback = (GAsyncReadyCallback) query_subscriber_ready_status_ready;
            break;

        case MBIM_REGISTER:
            mb_request = mbim_message_register_state_query_new(NULL);
            callback = (GAsyncReadyCallback) register_state_ready;
            break;

        case MBIM_ATTACH:
            mb_request = mbim_message_packet_service_set_new(MBIM_PACKET_SERVICE_ACTION_ATTACH, &error);
            if (!mb_request)
                break;

            callback = (GAsyncReadyCallback) packet_service_ready;
            timeout = 120;
            break;

        case MBIM_CONNECT:
            apn = databuf_get_string(&request->req, MB_APN);
            if (!apn)
            {
                set_error(request, "You must provide an APN (MB_APN)");
                break;
            }

            databuf_get_uint(&request->req, MB_AUTH, &auth);
            if (auth == -1)
            {
                set_error(request, "You must provide a auth protocol (MB_AUTH)");
                break;
            }

            username = databuf_get_string(&request->req, MB_USERNAME);
            password = databuf_get_string(&request->req, MB_PASSWORD);

            mb_request = mbim_message_connect_set_new(session_id, MBIM_ACTIVATION_COMMAND_ACTIVATE, apn, username,
                                                      password, MBIM_COMPRESSION_NONE, auth, MBIM_CONTEXT_IP_TYPE_DEFAULT,
                                                      mbim_uuid_from_context_type(MBIM_CONTEXT_TYPE_INTERNET), &error);
            if (!mb_request)
                break;

            callback = (GAsyncReadyCallback) connect_ready;
            timeout = 120;
            break;

        case MBIM_IP:
            mb_request = mbim_message_ip_configuration_query_new(session_id, MBIM_IP_CONFIGURATION_AVAILABLE_FLAG_NONE,
                                                                MBIM_IP_CONFIGURATION_AVAILABLE_FLAG_NONE, 0, NULL,
                                                                0, NULL, NULL, NULL, 0, NULL, 0, NULL, 0, 0, &error);
            if (!mb_request)
                break;

            callback = (GAsyncReadyCallback) ip_configuration_query_ready;
            timeout = 60;
            break;

        case MBIM_STATUS:
            mb_request = mbim_message_connect_query_new(session_id, MBIM_ACTIVATION_STATE_UNKNOWN, MBIM_VOICE_CALL_STATE_NONE,
                                                        MBIM_CONTEXT_IP_TYPE_DEFAULT, mbim_uuid_from_context_type(MBIM_CONTEXT_TYPE_INTERNET),
                                                        0, &error);
            if (!mb_request)
                break;

            callback = (GAsyncReadyCallback) connect_ready;
            request->user_data = 1;
            break;

        case MBIM_DEVICE_CAPS:
            mb_request = mbim_message_device_caps_query_new(NULL);
            callback = (GAsyncReadyCallback) query_device_caps_ready;
            break;

        case MBIM_PACKET_SERVICE:
            mb_request = mbim_message_packet_service_query_new(NULL);
            callback = (GAsyncReadyCallback) packet_service_ready;
            break;

        case MBIM_SIGNAL:
            mb_request = mbim_message_atds_signal_query_new(NULL);
            callback = (GAsyncReadyCallback) query_signal_ready;
            break;

        default:
            break;
    }

    if (callback)
        mbim_device_command(g_device, mb_request, timeout, g_cancellable, callback, request);

    if (mb_request)
        mbim_message_unref(mb_request);

    if (error)
    {
        set_error(request, error->message);
        g_error_free(error);
        mbim_close(request);
    }
}

/** Callback function when new device is available
 *
 * @param unused   Unused parameter
 * @param res      GAsyncResult pointer
 * @param request  Mbim_request pointer
 */
static void device_new_ready(GObject *unused, GAsyncResult *res, Mbim_request *request)
{
    GError *error = NULL;
    MbimDeviceOpenFlags open_flags = MBIM_DEVICE_OPEN_FLAGS_PROXY;

    (void) unused;

    g_device = mbim_device_new_finish(res, &error);
    if (!g_device)
    {
        printf("Couldn't create MbimDevice: %s\n", error->message);
        set_error(request, error->message);
        g_error_free(error);
        g_main_loop_quit(g_loop);
        return;
    }

    if (request->tid)
        g_object_set(g_device, MBIM_DEVICE_IN_SESSION, TRUE, MBIM_DEVICE_TRANSACTION_ID, request->tid, NULL);

    mbim_device_open_full(g_device, open_flags, 5, g_cancellable, (GAsyncReadyCallback) device_open_ready, request);
}

/** Perform the MBIM request and handle the response
 *
 * @param request  Mbim_request pointer
 */
void mbim_perform_request(Mbim_request *request)
{
    GFile *file;
    guint intid;
    guint hupid;
    guint termid;
    const char *mbim_device = MBIM_NNG_DEVICE;

    if (access(mbim_device, R_OK) != 0)
    {
        printf("No %s file\n", mbim_device);
        set_error(request, "No mbim device file");
        return;
    }

    file = g_file_new_for_commandline_arg(mbim_device);

    g_cancellable = g_cancellable_new();
    g_loop = g_main_loop_new(NULL, FALSE);

    intid = g_unix_signal_add(SIGINT, (GSourceFunc) signals_handler, GUINT_TO_POINTER(SIGINT));
    hupid = g_unix_signal_add(SIGHUP, (GSourceFunc) signals_handler, GUINT_TO_POINTER(SIGHUP));
    termid = g_unix_signal_add(SIGTERM, (GSourceFunc) signals_handler, GUINT_TO_POINTER(SIGTERM));

    mbim_device_new(file, g_cancellable, (GAsyncReadyCallback) device_new_ready, request);
    g_main_loop_run(g_loop);

    g_source_remove(intid);
    g_source_remove(hupid);
    g_source_remove(termid);

    if (g_cancellable)
        g_object_unref(g_cancellable);
    if (g_device)
        g_object_unref(g_device);

    g_main_loop_unref(g_loop);
    g_object_unref(file);

    g_device = NULL;
    g_loop = NULL;
    g_cancellable = NULL;
}
