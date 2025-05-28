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
#include "mqtt_handler.h"
#include "lord.h"
#include "ntp_prot.h"

volatile time_t gtick;

void ac_accord_time(struct tm* tm)
{
    //周末常开
    if(tm->tm_wday == 0 || tm->tm_wday == 6) {
        if(!ac_is_open()) {
            ac_swi(1);
        }
    } else {
        if((tm->tm_hour*60 + tm->tm_min > 8*60+30) && (tm->tm_hour*60 + tm->tm_min < 21*60+10)) {
            if(ac_is_open()) {
                ac_swi(0);
            } 
        } else {
            if(!ac_is_open()) {
                ac_swi(1);
            } 
        }
    }
}

void easy_sw_timer()
{
    struct tm* tm;
    int times = 0;
    vTaskDelay(10*1000/portTICK_PERIOD_MS);
    gtick = time_stamp();
    while(1)
    {
        vTaskDelay(5000/portTICK_PERIOD_MS);
        if(times > 8*3600) {
            gtick = time_stamp();
            times = 0;
        } else {
            times += 5;
            gtick += 5;
        }
        tm = localtime(&gtick);
        ac_accord_time(tm);
        printf("day :%d hour: %d, min: %d\n", tm->tm_wday, tm->tm_hour, tm->tm_min);
    }
}