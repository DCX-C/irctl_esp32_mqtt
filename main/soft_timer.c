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
#include "soft_timer.h"

volatile time_t gtick;

#define GET_MINIUS(tm) (tm->tm_hour*60 + tm->tm_min)
#define TO_MINUIS(h, m) (h*60 + m)
#define V_INTER(v, l, h) (v > l && v < h)
#define V_OUTER(v, l, h) (v < l || v > h)

int time_in_sleep()
{
    int ret = 0;
    struct tm* tm;
    tm = localtime(&gtick);
    if (V_INTER(GET_MINIUS(tm), TO_MINUIS(23,0), TO_MINUIS(23,59))) {
        ret = 1;
    }
    if (V_INTER(GET_MINIUS(tm), TO_MINUIS(0,0), TO_MINUIS(9, 0))) {
        ret = 1;
    }
    return ret;
}

int ac_in_workday_adjust(struct tm* tm)
{
    int ctrl = 0;
    struct ac_cfg cfg;
    ac_read_cfg(AC_ID_USED, &cfg);
    if (V_INTER(GET_MINIUS(tm), TO_MINUIS(9, 0), TO_MINUIS(20,45))) {
        cfg.open = 0;
    } else {
        cfg.open = 1;
    } 

    if (time_in_sleep()) {
        cfg.temp = cfg.temp_slp;
        cfg.sleep = 1;
    } else {
        cfg.temp = cfg.temp_act;
        cfg.sleep = 0;
    }

    ac_write_cfg(AC_ID_USED, &cfg);
    return ctrl;
}

void ac_weeklayyer_adjust(struct tm* tm)
{
    int ctrl;

    if (V_INTER(tm->tm_wday, 0,6)) {
        ctrl = ac_in_workday_adjust(tm);
    } 
}

void soft_timer()
{
    struct tm* tm;
    int times = 0;
    vTaskDelay(10*1000/portTICK_PERIOD_MS);
    gtick = time_stamp();
    while(1)
    {
        vTaskDelay(5000/portTICK_PERIOD_MS);
        if(times > 12*3600) {
            gtick = time_stamp();
            times = 0;
        } else {
            times += 5;
            gtick += 5;
        }
        tm = localtime(&gtick);
        ac_weeklayyer_adjust(tm);
        printf("day :%d hour: %d, min: %d\n", tm->tm_wday, tm->tm_hour, tm->tm_min);
    }
}

TaskHandle_t soft_timer_hdl = NULL;
void soft_timer_init()
{
    xTaskCreate(soft_timer, "EASY_TIMER", 8*1024, NULL, 10, &soft_timer_hdl);
}

void soft_timer_suspend()
{
    vTaskSuspend(soft_timer_hdl);
}

void soft_timer_resume()
{
    vTaskResume(soft_timer_hdl);
}
