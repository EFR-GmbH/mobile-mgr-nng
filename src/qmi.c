/**
 * @file
 * @brief Qmi manager
 * @ccmod{MBIM_X_MMG}
 */

#include <stdio.h>
#include <stdbool.h>
#include <glib.h>
#include <glib/gprintf.h>
#include <gio/gio.h>
#include <glib-unix.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "mbim.h"
#include "nng_server.h"

#include "libqmi-glib/libqmi-glib.h"

static GMainLoop *g_loop;
static gboolean g_release_cid = TRUE;
static GCancellable *g_cancellable;
static QmiDevice *g_device;
static QmiClient *g_client;
static QmiService g_service;
static Mbim_request *tmp_req = NULL;

/**
 * @brief Count the number of set bits in a 32-bit unsigned integer
 *
 * @param n The 32-bit unsigned integer to count bits in
 * @return int The number of set bits in the input
 */
static int count_set_bits(uint32_t n)
{
    int cnt = 0;
    while (n)
    {
        cnt += n & 1;
        n >>= 1;
    }

    return cnt;
}

/**
 * @brief Set an error message in the Mbim_request response
 *
 * @param request Pointer to the Mbim_request structure
 * @param error Error message string
 */
static void set_error(Mbim_request *request, const char *error)
{
    databuf_add_string(&request->resp, MB_ERROR, error);
    databuf_add_uint(&request->resp, MB_RESPONSE, MBIM_ERROR);
}

/**
 * @brief Handle the result of closing a QmiDevice asynchronously
 *
 * @param dev Pointer to the QmiDevice
 * @param res Pointer to the GAsyncResult
 */
static void close_ready(QmiDevice *dev, GAsyncResult *res)
{
    GError *error = NULL;

    if (!qmi_device_close_finish(dev, res, &error))
    {
        printf("error: couldn't close: %s\n", error->message);
        g_error_free(error);
    }

    g_main_loop_quit(g_loop);
}

/**
 * @brief Handle the result of releasing a QmiDevice client asynchronously
 *
 * @param dev Pointer to the QmiDevice
 * @param res Pointer to the GAsyncResult
 */
static void release_client_ready(QmiDevice *dev, GAsyncResult *res)
{
    GError *error = NULL;

    if (!qmi_device_release_client_finish(dev, res, &error))
    {
        printf("error: couldn't release client: %s\n", error->message);
        g_error_free(error);
    }

    qmi_device_close_async(dev, 10, NULL, (GAsyncReadyCallback) close_ready, NULL);
}

/**
 * @brief Perform cleanup operations when shutting down the operation
 */
static void operation_shutdown(void)
{
    QmiDeviceReleaseClientFlags flags = QMI_DEVICE_RELEASE_CLIENT_FLAGS_NONE;

    g_clear_object(&g_cancellable);

    if (!g_client)
    {
        g_main_loop_quit(g_loop);
        return;
    }

    if (g_release_cid)
        flags |= QMI_DEVICE_RELEASE_CLIENT_FLAGS_RELEASE_CID;

    qmi_device_release_client(g_device, g_client, flags, 10, NULL, (GAsyncReadyCallback) release_client_ready, NULL);
}

/**
 * @brief Handle the result of getting card status from QmiClientUim asynchronously
 *
 * @param client Pointer to the QmiClientUim
 * @param res Pointer to the GAsyncResult
 * @param request Pointer to the Mbim_request structure
 */
static void get_card_status_ready(QmiClientUim *client, GAsyncResult *res, Mbim_request *request)
{
    QmiMessageUimGetCardStatusOutput *output;
    GError *error = NULL;
    GArray *cards;
    QmiMessageUimGetCardStatusOutputCardStatusCardsElement *card;
    QmiMessageUimGetCardStatusOutputCardStatusCardsElementApplicationsElement *app;

    output = qmi_client_uim_get_card_status_finish(client, res, &error);
    if (!output)
    {
        printf("error: operation failed: %s\n", error->message);
        set_error(request, error->message);
        g_error_free(error);
        operation_shutdown();
        return;
    }

    if (!qmi_message_uim_get_card_status_output_get_result(output, &error))
    {
        printf("error: couldn't get card status: %s\n", error->message);
        set_error(request, error->message);
        g_error_free(error);
        qmi_message_uim_get_card_status_output_unref(output);
        operation_shutdown();
        return;
    }

    qmi_message_uim_get_card_status_output_get_card_status(output, NULL, NULL, NULL, NULL, &cards, NULL);

    if (cards->len < 1)
    {
        printf("error: no card found\n");
        set_error(request, "No card found");
        g_error_free(error);
        qmi_message_uim_get_card_status_output_unref(output);
        operation_shutdown();
        return;
    }

    card = &g_array_index(cards, QmiMessageUimGetCardStatusOutputCardStatusCardsElement, 0);
    if (card->applications->len < 1)
    {
        printf("error: no card app\n");
        set_error(request, "No card app");
        g_error_free(error);
        qmi_message_uim_get_card_status_output_unref(output);
        operation_shutdown();
        return;
    }

    app = &g_array_index(card->applications, QmiMessageUimGetCardStatusOutputCardStatusCardsElementApplicationsElement, 0);

    if (app->pin1_state == QMI_UIM_PIN_STATE_DISABLED || app->pin1_state == QMI_UIM_PIN_STATE_ENABLED_VERIFIED)
    {
        printf("PIN is UNLOCKED\n");

        databuf_add_uint(&request->resp, MB_PIN_STATUS, MBIM_PIN_UNLOCK);
        databuf_add_uint(&request->resp, MB_RESPONSE, MBIM_OK);
        qmi_message_uim_get_card_status_output_unref(output);
        operation_shutdown();
        return;
    }

    if (app->pin1_state != QMI_UIM_PIN_STATE_ENABLED_NOT_VERIFIED)
    {
        printf("PIN1 ");
        set_error(request, "Only PIN1 is supported");
        qmi_message_uim_get_card_status_output_unref(output);
        operation_shutdown();
        return;
    }

    printf("PIN is LOCKED\n");

    databuf_add_uint(&request->resp, MB_PIN_STATUS, MBIM_PIN_LOCK);
    databuf_add_uint(&request->resp, MB_RESPONSE, MBIM_OK);

    qmi_message_uim_get_card_status_output_unref(output);
    operation_shutdown();
}

/**
 * @brief Handle the result of verifying a PIN using QmiClientUim asynchronously
 *
 * @param client Pointer to the QmiClientUim
 * @param res Pointer to the GAsyncResult
 * @param request Pointer to the Mbim_request structure
 */
static void verify_pin_ready(QmiClientUim *client, GAsyncResult *res, Mbim_request *request)
{
    QmiMessageUimVerifyPinOutput *output;
    GError *error = NULL;

    output = qmi_client_uim_verify_pin_finish(client, res, &error);
    if (!output)
    {
        printf("error: operation failed: %s\n", error->message);
        set_error(request, error->message);
        g_error_free(error);
        operation_shutdown();
        return;
    }

    if (!qmi_message_uim_verify_pin_output_get_result(output, &error))
    {
        printf("error: couldn't verify PIN: %s\n", error->message);
        set_error(request, error->message);
        g_error_free(error);

        qmi_message_uim_verify_pin_output_unref(output);
        operation_shutdown();
        return;
    }

    printf("PIN verified successfully\n");
    databuf_add_uint(&request->resp, MB_PIN_STATUS, MBIM_PIN_UNLOCK);
    databuf_add_uint(&request->resp, MB_RESPONSE, MBIM_OK);

    qmi_message_uim_verify_pin_output_unref(output);
    operation_shutdown();
}

/**
 * @brief Handle the result of getting serving system information from QmiClientNas asynchronously
 *
 * @param client Pointer to the QmiClientNas
 * @param res Pointer to the GAsyncResult
 * @param request Pointer to the Mbim_request structure
 */
static void get_serving_system_ready(QmiClientNas *client, GAsyncResult *res, Mbim_request *request)
{
    QmiMessageNasGetServingSystemOutput *output;
    GError *error = NULL;

    output = qmi_client_nas_get_serving_system_finish(client, res, &error);
    if (!output)
    {
        printf("error: operation failed: %s\n", error->message);
        set_error(request, error->message);
        g_error_free(error);
        operation_shutdown();
        return;
    }

    if (!qmi_message_nas_get_serving_system_output_get_result(output, &error))
    {
        printf("error: couldn't get serving system: %s\n", error->message);
        set_error(request, error->message);
        g_error_free(error);
        qmi_message_nas_get_serving_system_output_unref(output);
        operation_shutdown();
        return;
    }

    printf("[%s] Successfully got serving system:\n", qmi_device_get_path_display(g_device));

    {
        QmiNasRegistrationState registration_state;
        QmiNasAttachState cs_attach_state;
        QmiNasAttachState ps_attach_state;
        QmiNasNetworkType selected_network;

        qmi_message_nas_get_serving_system_output_get_serving_system(output, &registration_state, &cs_attach_state, &ps_attach_state,
                                                                     &selected_network, NULL, NULL);

        databuf_add_uint(&request->resp, MB_REGISTER_STATE, registration_state);
        databuf_add_string(&request->resp, MB_REGISTER_STATE_STR,
                           VALIDATE_UNKNOWN(qmi_nas_registration_state_get_string(registration_state)));
        databuf_add_string(&request->resp, MB_ATTACH_PCK_SERVICE_STATE,
                           ps_attach_state == QMI_NAS_ATTACH_STATE_ATTACHED ? "attached" : "detached");

        databuf_add_uint(&request->resp, MB_RESPONSE, MBIM_OK);
    }

    {
        char tmp[50] = {0};
        guint16 current_plmn_mcc;
        guint16 current_plmn_mnc;
        const gchar *current_plmn_description;

        if (qmi_message_nas_get_serving_system_output_get_current_plmn(output, &current_plmn_mcc, &current_plmn_mnc,
                                                                       &current_plmn_description, NULL))
        {
            snprintf(tmp, sizeof(tmp), "%hu%hu", current_plmn_mcc, current_plmn_mnc);
            databuf_add_string(&request->resp, MB_REGISTER_PROVIDER_NAME, VALIDATE_UNKNOWN(current_plmn_description));
            databuf_add_string(&request->resp, MB_REGISTER_PROVIDER_ID, tmp);
        }
    }

    qmi_message_nas_get_serving_system_output_unref(output);
    operation_shutdown();
}

/**
 * @brief Handle the result of starting network connection from QmiClientWds asynchronously
 *
 * @param client Pointer to the QmiClientWds
 * @param res Pointer to the GAsyncResult
 * @param request Pointer to the Mbim_request structure
 */
static void start_network_ready(QmiClientWds *client, GAsyncResult *res, Mbim_request *request)
{
    GError *error = NULL;
    QmiMessageWdsStartNetworkOutput *output;

    output = qmi_client_wds_start_network_finish(client, res, &error);
    if (!output)
    {
        printf("error: operation failed: %s\n", error->message);
        set_error(request, error->message);
        g_error_free(error);
        operation_shutdown();
        return;
    }

    if (!qmi_message_wds_start_network_output_get_result(output, &error))
    {
        printf("error: couldn't start network: %s\n", error->message);
        set_error(request, error->message);

        if (g_error_matches(error, QMI_PROTOCOL_ERROR, QMI_PROTOCOL_ERROR_CALL_FAILED))
        {
            QmiWdsCallEndReason cer;
            QmiWdsVerboseCallEndReasonType verbose_cer_type;
            gint16 verbose_cer_reason;

            if (qmi_message_wds_start_network_output_get_call_end_reason(output, &cer, NULL))
                printf("call end reason (%u): %s\n", cer, qmi_wds_call_end_reason_get_string(cer));

            if (qmi_message_wds_start_network_output_get_verbose_call_end_reason(output, &verbose_cer_type, &verbose_cer_reason, NULL))
                printf("verbose call end reason (%u,%d): [%s] %s\n", verbose_cer_type, verbose_cer_reason,
                       qmi_wds_verbose_call_end_reason_type_get_string(verbose_cer_type),
                       qmi_wds_verbose_call_end_reason_get_string(verbose_cer_type, verbose_cer_reason));
        }

        g_error_free(error);
        qmi_message_wds_start_network_output_unref(output);
        operation_shutdown();
        return;
    }

    databuf_add_uint(&request->resp, MB_RESPONSE, MBIM_OK);
    qmi_message_wds_start_network_output_unref(output);
    operation_shutdown();
}

/**
 * @brief Handle the result of getting current settings from QmiClientWds asynchronously
 *
 * @param client Pointer to the QmiClientWds
 * @param res Pointer to the GAsyncResult
 * @param request Pointer to the Mbim_request structure
 */
static void get_current_settings_ready(QmiClientWds *client, GAsyncResult *res, Mbim_request *request)
{
    GError *error = NULL;
    QmiMessageWdsGetCurrentSettingsOutput *output;
    GArray *array;
    guint32 addr = 0;
    guint32 netmask = 0;
    struct in_addr in_addr_val;
    struct in6_addr in6_addr_val;
    gchar buf4[INET_ADDRSTRLEN];
    gchar buf6[INET6_ADDRSTRLEN];
    guint8 prefix = 0;
    gchar *cidr;
    guint i;

    output = qmi_client_wds_get_current_settings_finish(client, res, &error);
    if (!output)
    {
        printf("error: operation failed: %s\n", error->message);
        set_error(request, error->message);
        g_error_free(error);
        operation_shutdown();
        return;
    }

    if (!qmi_message_wds_get_current_settings_output_get_result(output, &error))
    {
        printf("error: couldn't get current settings: %s\n", error->message);
        set_error(request, error->message);
        g_error_free(error);
        qmi_message_wds_get_current_settings_output_unref(output);
        operation_shutdown();
        return;
    }

    databuf_add_uint(&request->resp, MB_RESPONSE, MBIM_OK);

    if (qmi_message_wds_get_current_settings_output_get_ipv4_gateway_subnet_mask(output, &addr, NULL))
        netmask = count_set_bits(GUINT32_TO_BE(addr));

    if (qmi_message_wds_get_current_settings_output_get_ipv4_address(output, &addr, NULL) && netmask != 0)
    {
        in_addr_val.s_addr = GUINT32_TO_BE(addr);
        memset(buf4, 0, sizeof(buf4));
        inet_ntop(AF_INET, &in_addr_val, buf4, sizeof(buf4));
        cidr = g_strdup_printf("%s/%u", buf4, netmask);
        databuf_add_string(&request->resp, MB_IPV4_ADDR, cidr);
        databuf_add_uint(&request->resp, MB_IPV4_NB, 1);
        g_free(cidr);
    }

    if (qmi_message_wds_get_current_settings_output_get_ipv4_gateway_address(output, &addr, NULL))
    {
        in_addr_val.s_addr = GUINT32_TO_BE(addr);
        memset(buf4, 0, sizeof(buf4));
        inet_ntop(AF_INET, &in_addr_val, buf4, sizeof(buf4));
        databuf_add_string(&request->resp, MB_IPV4_GW, buf4);
    }

    if (qmi_message_wds_get_current_settings_output_get_ipv6_address(output, &array, &prefix, NULL))
    {
        for (i = 0; i < array->len; i++)
            in6_addr_val.s6_addr16[i] = GUINT16_TO_BE(g_array_index(array, guint16, i));

        memset(buf6, 0, sizeof(buf6));
        inet_ntop(AF_INET6, &in6_addr_val, buf6, sizeof(buf6));
        cidr = g_strdup_printf("%s/%u", buf6, prefix);
        databuf_add_string(&request->resp, MB_IPV6_ADDR, cidr);
        databuf_add_uint(&request->resp, MB_IPV6_NB, 1);
        g_free(cidr);
    }

    if (qmi_message_wds_get_current_settings_output_get_ipv6_gateway_address(output, &array, &prefix, NULL))
    {
        for (i = 0; i < array->len; i++)
            in6_addr_val.s6_addr16[i] = GUINT16_TO_BE(g_array_index(array, guint16, i));

        memset(buf6, 0, sizeof(buf6));
        inet_ntop(AF_INET6, &in6_addr_val, buf6, sizeof(buf6));
        printf("IPv6 GW : %s\n", buf6);
        databuf_add_string(&request->resp, MB_IPV6_GW, buf6);
    }

    qmi_message_wds_get_current_settings_output_unref(output);
    operation_shutdown();
}

/**
 * @brief Handle the result of getting packet service status from QmiClientWds asynchronously
 *
 * @param client Pointer to the QmiClientWds
 * @param res Pointer to the GAsyncResult
 * @param request Pointer to the Mbim_request structure
 */
static void get_packet_service_status_ready(QmiClientWds *client, GAsyncResult *res, Mbim_request *request)
{
    GError *error = NULL;
    QmiMessageWdsGetPacketServiceStatusOutput *output;
    QmiWdsConnectionStatus status;

    output = qmi_client_wds_get_packet_service_status_finish(client, res, &error);
    if (!output)
    {
        printf("error: operation failed: %s\n", error->message);
        set_error(request, error->message);
        g_error_free(error);
        return;
    }

    if (!qmi_message_wds_get_packet_service_status_output_get_result(output, &error))
    {
        printf("error: couldn't get packet service status: %s\n", error->message);
        set_error(request, error->message);
        g_error_free(error);
        qmi_message_wds_get_packet_service_status_output_unref(output);
        return;
    }

    qmi_message_wds_get_packet_service_status_output_get_connection_status(output, &status, NULL);
    databuf_add_uint(&request->resp, MB_RESPONSE, MBIM_OK);
    databuf_add_uint(&request->resp, MB_STATE_ACTIVATION, status);

    operation_shutdown();
}

/**
 * @brief Handle the result of getting signal information from QmiClientNas asynchronously
 *
 * @param client Pointer to the QmiClientNas
 * @param res Pointer to the GAsyncResult
 * @param request Pointer to the Mbim_request structure
 */
static void get_signal_info_ready(QmiClientNas *client, GAsyncResult *res, Mbim_request *request)
{
    QmiMessageNasGetSignalInfoOutput *output;
    GError *error = NULL;
    gint8 rssi;
    gint8 rsrq;
    gint16 rsrp;
    gint16 snr;

    output = qmi_client_nas_get_signal_info_finish(client, res, &error);
    if (!output)
    {
        printf("error: operation failed: %s\n", error->message);
        set_error(request, error->message);
        g_error_free(error);
        operation_shutdown();
        return;
    }

    if (!qmi_message_nas_get_signal_info_output_get_result(output, &error))
    {
        printf("error: couldn't get signal info: %s\n", error->message);
        set_error(request, error->message);
        g_error_free(error);
        qmi_message_nas_get_signal_info_output_unref(output);
        operation_shutdown();
        return;
    }

    if (qmi_message_nas_get_signal_info_output_get_gsm_signal_strength(output, &rssi, NULL))
    {
        databuf_add_uint(&request->resp, MB_SIGNAL_RSSI, rssi);
    }

    if (qmi_message_nas_get_signal_info_output_get_lte_signal_strength(output, &rssi, &rsrq, &rsrp, &snr, NULL))
    {
        printf("LTE:\n\tRSSI: '%d dBm'\n\tRSRQ: '%d dB'\n\tRSRP: '%d dBm'\n\tSNR: '%.1lf dB'\n", rssi, rsrq, rsrp,
               (0.1) * ((gdouble) snr));
        databuf_add_uint(&request->resp, MB_SIGNAL_RSSI, rssi);
        databuf_add_uint(&request->resp, MB_SIGNAL_RSRQ, rsrq);
        databuf_add_uint(&request->resp, MB_SIGNAL_RSRP, rsrp);
        databuf_add_uint(&request->resp, MB_SIGNAL_RSSNR, snr);
    }

    databuf_add_uint(&request->resp, MB_RESPONSE, MBIM_OK);

    qmi_message_nas_get_signal_info_output_unref(output);
    operation_shutdown();
}

/**
 * @brief Handle the result of allocating a client for a QmiService asynchronously
 *
 * @param dev Pointer to the QmiDevice
 * @param res Pointer to the GAsyncResult
 * @param request Pointer to the Mbim_request structure
 */
static void allocate_client_ready(QmiDevice *dev, GAsyncResult *res, Mbim_request *request)
{
    GError *error = NULL;
    g_release_cid = TRUE;

    g_client = qmi_device_allocate_client_finish(dev, res, &error);
    if (!g_client)
    {
        printf("error: couldn't create client for the '%s' service: %s\n", qmi_service_get_string(g_service), error->message);
        set_error(request, error->message);
        g_error_free(error);
        g_main_loop_quit(g_loop);
        return;
    }

    databuf_add_string(&request->resp, MB_DEVICE, qmi_device_get_path_display(dev));

    switch (request->type)
    {
    case MBIM_PIN_STATUS:
        qmi_client_uim_get_card_status(QMI_CLIENT_UIM(g_client), NULL, 10, g_cancellable, (GAsyncReadyCallback) get_card_status_ready,
                                       request);
        return;

    case MBIM_PIN_ENTER: {
        char *pin_code = databuf_get_string(&request->req, MB_PIN_CODE);
        if (!pin_code)
        {
            set_error(request, "You must provide a pin code (MB_PIN_CODE)");
            break;
        }

        GError *error = NULL;
        GArray *dummy_aid = g_array_new(FALSE, FALSE, sizeof(guint8));

        QmiMessageUimVerifyPinInput *input = qmi_message_uim_verify_pin_input_new();
        if (!qmi_message_uim_verify_pin_input_set_info(input, QMI_UIM_PIN_ID_PIN1, pin_code, &error) ||
            !qmi_message_uim_verify_pin_input_set_session(input, QMI_UIM_SESSION_TYPE_CARD_SLOT_1, dummy_aid, &error))
        {
            set_error(request, error->message);
            g_error_free(error);
            qmi_message_uim_verify_pin_input_unref(input);
            g_array_unref(dummy_aid);
            operation_shutdown();
            return;
        }
        g_array_unref(dummy_aid);

        qmi_client_uim_verify_pin(QMI_CLIENT_UIM(g_client), input, 10, g_cancellable, (GAsyncReadyCallback) verify_pin_ready, request);
        qmi_message_uim_verify_pin_input_unref(input);
    }
        return;

    case MBIM_REGISTER:
    case MBIM_PACKET_SERVICE:
        printf("Asynchronously getting serving system...");
        qmi_client_nas_get_serving_system(QMI_CLIENT_NAS(g_client), NULL, 10, g_cancellable,
                                          (GAsyncReadyCallback) get_serving_system_ready, request);
        return;

    case MBIM_CONNECT: {
        char *apn;
        char *username;
        char *password;
        int auth = -1;

        g_release_cid = FALSE;
        apn = databuf_get_string(&request->req, MB_APN);
        if (!apn)
        {
            set_error(request, "You must provide an APN (MB_APN)");
            return;
        }

        databuf_get_uint(&request->req, MB_AUTH, &auth);
        if (auth == -1)
        {
            set_error(request, "You must provide a auth protocol (MB_AUTH)");
            return;
        }

        QmiMessageWdsStartNetworkInput *input = qmi_message_wds_start_network_input_new();
        qmi_message_wds_start_network_input_set_apn(input, apn, NULL);

        switch (auth)
        {
        case MBIM_AUTH_PAP:
            qmi_message_wds_start_network_input_set_authentication_preference(input, QMI_WDS_AUTHENTICATION_PAP, NULL);
            break;
        case MBIM_AUTH_CHAP:
        case MBIM_AUTH_MSCHAPV2:
            qmi_message_wds_start_network_input_set_authentication_preference(input, QMI_WDS_AUTHENTICATION_CHAP, NULL);
            break;

        case MBIM_AUTH_NONE:
        default:
        }

        username = databuf_get_string(&request->req, MB_USERNAME);
        password = databuf_get_string(&request->req, MB_PASSWORD);

        if (username && username[0])
            qmi_message_wds_start_network_input_set_username(input, username, NULL);

        if (password && password[0])
            qmi_message_wds_start_network_input_set_username(input, password, NULL);


        qmi_client_wds_start_network(QMI_CLIENT_WDS(g_client), input, 180, g_cancellable, (GAsyncReadyCallback) start_network_ready,
                                     request);
        if (input)
            qmi_message_wds_start_network_input_unref(input);
    }
        return;

    case MBIM_IP: {
        QmiMessageWdsGetCurrentSettingsInput *input;

        input = qmi_message_wds_get_current_settings_input_new();
        qmi_message_wds_get_current_settings_input_set_requested_settings(
            input,
            (QMI_WDS_GET_CURRENT_SETTINGS_REQUESTED_SETTINGS_DNS_ADDRESS | QMI_WDS_GET_CURRENT_SETTINGS_REQUESTED_SETTINGS_GRANTED_QOS |
             QMI_WDS_GET_CURRENT_SETTINGS_REQUESTED_SETTINGS_IP_ADDRESS | QMI_WDS_GET_CURRENT_SETTINGS_REQUESTED_SETTINGS_GATEWAY_INFO |
             QMI_WDS_GET_CURRENT_SETTINGS_REQUESTED_SETTINGS_MTU | QMI_WDS_GET_CURRENT_SETTINGS_REQUESTED_SETTINGS_DOMAIN_NAME_LIST |
             QMI_WDS_GET_CURRENT_SETTINGS_REQUESTED_SETTINGS_IP_FAMILY),
            NULL);

        qmi_client_wds_get_current_settings(QMI_CLIENT_WDS(g_client), input, 10, g_cancellable,
                                            (GAsyncReadyCallback) get_current_settings_ready, request);
        qmi_message_wds_get_current_settings_input_unref(input);
    }
        return;

    case MBIM_STATUS:
        qmi_client_wds_get_packet_service_status(QMI_CLIENT_WDS(g_client), NULL, 10, g_cancellable,
                                                 (GAsyncReadyCallback) get_packet_service_status_ready, request);
        return;

    case MBIM_SIGNAL:
        qmi_client_nas_get_signal_info(QMI_CLIENT_NAS(g_client), NULL, 10, g_cancellable, (GAsyncReadyCallback) get_signal_info_ready,
                                       NULL);
        return;

    default: break;
    }
}

/**
 * @brief Callback function to set expected data format on a QmiDevice after it has been opened and allocated a client
 *
 * @param dev Pointer to the QmiDevice
 * @return FALSE Always return FALSE to remove this source from the event loop
 */
static gboolean device_set_expected_data_format_cb(QmiDevice *dev)
{
    GError *error = NULL;

    if (!qmi_device_set_expected_data_format(dev, QMI_DEVICE_EXPECTED_DATA_FORMAT_RAW_IP, &error))
    {
        printf("error: cannot set expected data format: %s\n", error->message);
        set_error(tmp_req, error->message);
        g_error_free(error);
    }
    else
        databuf_add_uint(&tmp_req->resp, MB_RESPONSE, MBIM_OK);

    g_clear_object(&g_cancellable);
    g_main_loop_quit(g_loop);

    g_object_unref(dev);
    return FALSE;
}

/**
 * @brief Callback function to handle device open operations
 *
 * @param dev Pointer to the QmiDevice
 * @param res Pointer to the GAsyncResult
 * @param request Pointer to the Mbim_request structure
 */
static void device_open_ready(QmiDevice *dev, GAsyncResult *res, Mbim_request *request)
{
    GError *error = NULL;
    guint8 cid = QMI_CID_NONE;

    if (!qmi_device_open_finish(dev, res, &error))
    {
        printf("error: couldn't open the QmiDevice: %s\n", error->message);
        return;
    }

    if (request->type == MBIM_ATTACH)
    {
        tmp_req = request;
        databuf_add_string(&request->resp, MB_DEVICE, MBIM_NNG_DEVICE);
        g_idle_add((GSourceFunc) device_set_expected_data_format_cb, g_object_ref(dev));
        return;
    }

    qmi_device_allocate_client(dev, g_service, cid, 10, g_cancellable, (GAsyncReadyCallback) allocate_client_ready, request);
}

/**
 * @brief Callback function to handle new QmiDevice creation
 *
 * @param unused Unused parameter
 * @param res Pointer to the GAsyncResult
 * @param request Pointer to the Mbim_request structure
 */
static void device_new_ready(GObject *unused, GAsyncResult *res, Mbim_request *request)
{
    GError *error = NULL;
    QmiDeviceOpenFlags open_flags = QMI_DEVICE_OPEN_FLAGS_PROXY | QMI_DEVICE_OPEN_FLAGS_AUTO;

    g_device = qmi_device_new_finish(res, &error);
    if (!g_device)
    {
        printf("error: couldn't create QmiDevice: %s\n", error->message);
        set_error(request, error->message);
        g_error_free(error);
        g_main_loop_quit(g_loop);
        return;
    }

    qmi_device_open(g_device, open_flags, 15, g_cancellable, (GAsyncReadyCallback) device_open_ready, request);
}

/**
 * @brief Signal handler for SIGINT, SIGHUP, and SIGTERM signals
 *
 * @return gboolean True if signal handling should continue, otherwise False
 */
static gboolean signals_handler(void)
{
    if (g_cancellable)
    {
        if (!g_cancellable_is_cancelled(g_cancellable))
        {
            printf("cancelling the operation...\n");
            g_cancellable_cancel(g_cancellable);
            return G_SOURCE_CONTINUE;
        }
    }

    if (g_loop && g_main_loop_is_running(g_loop))
    {
        printf("cancelling the main loop...\n");
        g_idle_add((GSourceFunc) g_main_loop_quit, g_loop);
    }

    return G_SOURCE_REMOVE;
}

/**
 * @brief Perform a QMI request based on the provided Mbim_request structure
 *
 * @param request Pointer to the Mbim_request structure containing the request details
 */
void qmi_perform_request(Mbim_request *request)
{
    GFile *file;
    guint intid;
    guint hupid;
    guint termid;
    const char *qmi_device = MBIM_NNG_DEVICE;

    if (access(qmi_device, R_OK) != 0)
    {
        printf("No %s file\n", qmi_device);
        set_error(request, "No qmi device file");
        return;
    }

    switch (request->type)
    {
    case MBIM_PIN_STATUS:
    case MBIM_PIN_ENTER: g_service = QMI_SERVICE_UIM; break;

    case MBIM_REGISTER:
    case MBIM_PACKET_SERVICE:
    case MBIM_SIGNAL: g_service = QMI_SERVICE_NAS; break;

    case MBIM_CONNECT:
    case MBIM_IP:
    case MBIM_STATUS: g_service = QMI_SERVICE_WDS; break;
    case MBIM_ATTACH: break;
    default: set_error(request, "Unsupported request"); return;
    }

    file = g_file_new_for_commandline_arg(qmi_device);

    g_cancellable = g_cancellable_new();
    g_loop = g_main_loop_new(NULL, FALSE);

    intid = g_unix_signal_add(SIGINT, (GSourceFunc) signals_handler, GUINT_TO_POINTER(SIGINT));
    hupid = g_unix_signal_add(SIGHUP, (GSourceFunc) signals_handler, GUINT_TO_POINTER(SIGHUP));
    termid = g_unix_signal_add(SIGTERM, (GSourceFunc) signals_handler, GUINT_TO_POINTER(SIGTERM));


    qmi_device_new(file, g_cancellable, (GAsyncReadyCallback) device_new_ready, request);
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
