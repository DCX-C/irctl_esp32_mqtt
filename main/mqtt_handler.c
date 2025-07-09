#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <time.h>
#include "esp_wifi.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "protocol_examples_common.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"

#include "esp_log.h"
#include "mqtt_client.h"
#include "cJSON.h"

#include "ir_encoder.h"

static const char *TAG = "mqtt_handler";
#define DEVICE_ID "664786226bc31504f06ac4b3_A111"

static void log_error_if_nonzero(const char *message, int error_code)
{
    if (error_code != 0) {
        ESP_LOGE(TAG, "Last error %s: 0x%x", message, error_code);
    }
}

void get_request_id(char* data, int dlen, char* request_id)
{
    char* pos;
    printf("data:%x, dlen:%d\r\n", (int)data, dlen);
    if(dlen <= 1 || data == NULL) {
        return;
    } 

    pos = strstr(data, "request_id=");
    if(pos == NULL) {
        request_id[0] = 0;
        return;
    }
    pos += 12;
    while((*pos) != '\r' && (*pos) != '\n' && (*pos) != 0 && ((int)(pos-data))<dlen )
    {
        *request_id = *pos;
        request_id++;
        pos++;
    }
    (*request_id) = '\0';
}   

const char cmdtbl[][16] = {
    "\"open_ac\"",      //0
    "\"close_ac\"",     //1
    "\"get_time\"",     //2
    "\"get_status\"",   //3
    "\"tgl_status\"",   //4
    "\"tup\"",           //5
    "\"tdown\"",         //6
    "\"fixed\"",        //7
};

int mqtt_data_event_handler(void *event_data)
{
    extern time_t gtick;
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;
    char topic[196];
    char request_id[64];
    char chartime[60];
    char status_str[32];
    int msg_id = 0;
    int cmd_code = -1;
    char *cmd_str = NULL;
    double number = 0; 
    cJSON *tree;
    cJSON *cmd;
    cJSON *cjval;
    cJSON *paras;
    cJSON *item;
    
    tree = cJSON_Parse(event->data);
    cmd = cJSON_GetObjectItem(tree, "cmd");
    cjval = cJSON_GetObjectItem(tree, "val");
    
    //非JSON包直接退出
    if (!cmd) {
        goto hdl_free;
    }

    cmd_str = cJSON_Print(cmd);
    printf("cmd recv: %s\n", cmd_str);

    //确认命令
    for(int i = 0;i<sizeof(cmdtbl)/sizeof(cmdtbl[0]);i++)
    {
        if(strncmp(cmdtbl[i], cmd_str, strlen(cmd_str))) {
            cmd_code = -1;
            continue;
        } else {
            cmd_code = i;
            break;
        }
    }
    printf("cmd ack: %d\n", cmd_code);

    //执行命令
    switch (cmd_code)
    {
        case 0:
            ac_swi(1);
            esp_mqtt_client_publish(client, "M2M/AC_PUB", "{\"OPEN ACK\"}", 0, 0, 0);
            break;
        case 1:
            ac_swi(0);
            esp_mqtt_client_publish(client, "M2M/AC_PUB", "{\"CLOSE ACK\"}", 0, 0, 0);
            break;
        case 2:
            sprintf(chartime, "{\"%s\"}", ctime(&gtick));
            esp_mqtt_client_publish(client, "M2M/AC_PUB", chartime, 0, 0, 0);
            break;
        case 3:
            sprintf(status_str,  "{\"isopen\": %d}", ac_is_open());
            esp_mqtt_client_publish(client, "M2M/AC_PUB", status_str, 0, 0, 0);
            break;
        case 4:
            ac_swi(!ac_is_open());
            sprintf(status_str,  "{\"isopen\": %d}", ac_is_open());
            esp_mqtt_client_publish(client, "M2M/AC_PUB", status_str, 0, 0, 0);
            break;
        case 5:
            ac_tup();
            ac_swi(1);
            sprintf(status_str,  "{\"T = %d\"}", ac_get_temperature());
            esp_mqtt_client_publish(client, "M2M/AC_PUB", status_str, 0, 0, 0);
            break;
        case 6:
            ac_tdown();
            ac_swi(1);
            sprintf(status_str,  "{\"T = %d\"}", ac_get_temperature());
            esp_mqtt_client_publish(client, "M2M/AC_PUB", status_str, 0, 0, 0);
            break;
        case 7:
            sprintf(status_str,  "{\"is_locked\": %d}", ac_fixed_tgl());
            esp_mqtt_client_publish(client, "M2M/AC_PUB", status_str, 0, 0, 0);
            break;
        default:
            esp_mqtt_client_publish(client, "$oc/devices/DEVICE_ID/user/app", "{\"UNDEFINE\"}", 0, 0, 0);
            break;
    }
    
    printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
    printf("DATA=%.*s\r\n", event->data_len, event->data);
    
    get_request_id(event->topic, event->topic_len, request_id);
    if(request_id[0] != 0) {
        sprintf(topic, "$oc/devices/"DEVICE_ID"/sys/commands/response/request_id=%s", request_id);
        msg_id = esp_mqtt_client_publish(client, topic, "{\"result_code\": 0}", 0, 0, 0);
    }

hdl_free:
    if(tree) {
        cJSON_Delete(tree);
    }
    if(cmd_str) {
        free(cmd_str);
    }

    return msg_id;
}

/*
 * @brief Event handler registered to receive MQTT events
 *
 *  This function is called by the MQTT client event loop.
 *
 * @param handler_args user data registered to the event.
 * @param base Event base for the handler(always MQTT Base in this example).
 * @param event_id The id for the received event.
 * @param event_data The data for the event, esp_mqtt_event_handle_t.
 */
void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    int msg_id = 0;
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%" PRIi32 "", base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        msg_id = esp_mqtt_client_publish(client, "$oc/devices/"DEVICE_ID"/sys/messages/up", "MQTT_EVENT_CONNECTED", 0, 1, 0);
        ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);

        msg_id = esp_mqtt_client_subscribe(client, "M2M/ACSWI", 0);
        ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);

        // msg_id = esp_mqtt_client_subscribe(client, "$oc/devices/664786226bc31504f06ac4b3_A111/user/app", 0);
        // ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);
        // msg_id = esp_mqtt_client_subscribe(client, "/topic/qos1", 1);
        // ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);

        // msg_id = esp_mqtt_client_unsubscribe(client, "/topic/qos1");
        // ESP_LOGI(TAG, "sent unsubscribe successful, msg_id=%d", msg_id);
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        break;

    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d ", event->msg_id);
        ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT_EVENT_DATA");
        msg_id = mqtt_data_event_handler(event_data);
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            log_error_if_nonzero("reported from esp-tls", event->error_handle->esp_tls_last_esp_err);
            log_error_if_nonzero("reported from tls stack", event->error_handle->esp_tls_stack_err);
            log_error_if_nonzero("captured as transport's socket errno",  event->error_handle->esp_transport_sock_errno);
            ESP_LOGI(TAG, "Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));
        }
        break;
    default:
        ESP_LOGI(TAG, "Other event id:%d", event->event_id);
        break;
    }
}