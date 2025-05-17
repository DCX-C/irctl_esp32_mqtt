/* MQTT (over TCP) Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

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

#define DEVICE_ID "664786226bc31504f06ac4b3_A111"


/*
ir ctrl
*/

/*extern*/
struct tm* time_print();
time_t time_stamp();


/* global */
static const char *TAG = "mqtt_example";
static int isopen = 0;
volatile time_t gtstamp;

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
    "\"open_ac\"",
    "\"close_ac\"",
    "\"get_time\"",
    "\"get_status\"",
    "\"tgl_status\"",

};

int mqtt_data_event_handler(void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;
    static int st1 = 3;
    char topic[196];
    char request_id[64];
    char chartime[60];
    char status_str[32];
    int msg_id = 0;
    int cmd_code = 99;
    char *cmd_str;
    cJSON *tree;
    cJSON *cmd;
    cJSON *paras;
    cJSON *item;
    double number = 0; 
    tree = cJSON_Parse(event->data);
    cmd = cJSON_GetObjectItem(tree, "cmd");
    if(cmd) {
        cmd_str = cJSON_Print(cmd);
        printf("cmd recv: %s\n", cmd_str);

        //确认命令
        for(int i = 0;i<5;i++)
        {
            printf("cur i:%d\n", i);
            if(memcmp(cmdtbl[i], cmd_str, strlen(cmd_str))) {
                cmd_code = 9999;
                continue;
            } else {
                cmd_code = i;
                break;
            }
        }
    } else {
        cmd_code = 9999;
    }
    //执行命令
    switch (cmd_code)
    {
        case 0:
            ac_swi(1);
            st1 = 1;
            esp_mqtt_client_publish(client, "M2M/AC_PUB", "{\"OPEN ACK\"}", 0, 0, 0);
            break;
        case 1:
            ac_swi(0);
            st1 = 0;
            esp_mqtt_client_publish(client, "M2M/AC_PUB", "{\"CLOSE ACK\"}", 0, 0, 0);
            break;
        case 2:
            sprintf(chartime, "{\"%s\"}", ctime(&gtstamp));
            esp_mqtt_client_publish(client, "M2M/AC_PUB", chartime, 0, 0, 0);
            break;
        case 3:
            sprintf(status_str,  "{\"st0\": %d, \"st1:\": %d}", isopen, st1);
            esp_mqtt_client_publish(client, "M2M/AC_PUB", status_str, 0, 0, 0);
            break;
        case 4:
            if(isopen) {
                ac_swi(0);
                isopen = 0;
            } else {
                ac_swi(1);
                isopen = 1;
            }
            sprintf(status_str,  "{\"st0\": %d, \"st1:\": %d}", isopen, st1);
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
    if(tree) {
        cJSON_Delete(tree);
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
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    int msg_id = 0;
    char topic[128];
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

static void mqtt_app_start(void)
{
    esp_mqtt_client_config_t mqtt_cfg = {
        // .broker.address.uri = CONFIG_BROKER_URL,
        .broker.address.uri = "mqtt://1f6cb80fe5.st1.iotda-device.cn-north-4.myhuaweicloud.com",
        .broker.address.hostname = "1f6cb80fe5.st1.iotda-device.cn-north-4.myhuaweicloud.com",
        .broker.address.port = 1883,
        .credentials.username = "664786226bc31504f06ac4b3_A111",
        .credentials.client_id = "664786226bc31504f06ac4b3_A111_0_0_2024051808",
        .credentials.authentication.password = "912ed5fb7a28c2508cb445d34c2724ce48f18a292cdaacd71aef7a0f2ef0290c",
    };
#if CONFIG_BROKER_URL_FROM_STDIN
    char line[128];

    if (strcmp(mqtt_cfg.broker.address.uri, "FROM_STDIN") == 0) {
        int count = 0;
        printf("Please enter url of mqtt broker\n");
        while (count < 128) {
            int c = fgetc(stdin);
            if (c == '\n') {
                line[count] = '\0';
                break;
            } else if (c > 0 && c < 127) {
                line[count] = c;
                ++count;
            }
            vTaskDelay(10 / portTICK_PERIOD_MS);
        }
        mqtt_cfg.broker.address.uri = line;
        printf("Broker url: %s\n", line);
    } else {
        ESP_LOGE(TAG, "Configuration mismatch: wrong broker url");
        abort();
    }
#endif /* CONFIG_BROKER_URL_FROM_STDIN */

    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    /* The last argument may be used to pass data to the event handler, in this example mqtt_event_handler */
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);
}

void easy_timer()
{
    static struct tm* tmp;
    static int opat = 0;
    static int times = 0;
    static time_t tstamp;
    vTaskDelay(5*1000/portTICK_PERIOD_MS);
    tstamp = time_stamp();
    gtstamp = tstamp;
    while(1)
    {
        vTaskDelay(15*1000/portTICK_PERIOD_MS);
        if(times > 4*60*4) {
            tstamp = time_stamp();
            times = 0;
        } else {
            tstamp += 15;
            times++;
        }
        gtstamp = tstamp;
        tmp = localtime(&tstamp);
        printf("day :%d hour: %d, min: %d\n", tmp->tm_wday, tmp->tm_hour, tmp->tm_min);
        //周末
        if(tmp->tm_wday == 0 || tmp->tm_wday == 6) {
            if(!isopen) {
                ac_swi(1);
                isopen = 1;
            }
        } else {
            if((tmp->tm_hour*60 + tmp->tm_min > 8*60+30) && tmp->tm_hour < 21) {
                if(isopen) {
                    ac_swi(0);
                    isopen = 0;
                } 
            } else {
                if(!isopen) {
                    ac_swi(1);
                    isopen = 1;
                } 
            }
        }
    }
}


void app_main(void)
{
    ESP_LOGI(TAG, "[APP] Startup..");
    ESP_LOGI(TAG, "[APP] Free memory: %" PRIu32 " bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());

    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("mqtt_client", ESP_LOG_VERBOSE);
    esp_log_level_set("mqtt_example", ESP_LOG_VERBOSE);
    esp_log_level_set("transport_base", ESP_LOG_VERBOSE);
    esp_log_level_set("esp-tls", ESP_LOG_VERBOSE);
    esp_log_level_set("transport", ESP_LOG_VERBOSE);
    esp_log_level_set("outbox", ESP_LOG_VERBOSE);

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
     * Read "Establishing Wi-Fi or Ethernet Connection" section in
     * examples/protocols/README.md for more information about this function.
     */
    ESP_ERROR_CHECK(example_connect());
    ir_encoder_init();
    mqtt_app_start();
    xTaskCreate(easy_timer, "EASY_TIMER", 16*1024, NULL, 10, NULL);
}
