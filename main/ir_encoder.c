#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/gptimer.h"

#include "esp_log.h"

#include "ir_encoder.h"


SemaphoreHandle_t xSemaphore;
StaticSemaphore_t xMutexBuffer;
volatile static int g_isdone;

static const char ac_tcl_open[] = {
0xc4, 0xd3, 0x64, 0x80, 
0x00, 0x26, 0xc0, 0x20,
0x02, 0x00, 0x00, 0x00,
0x01, 0x02};
static const char ac_tcl_close[] = {
0xc4, 0xd3, 0x64, 0x80, 
0x00, 0x04, 0xc0, 0x20, 
0x02, 0x00, 0x00, 0x00,
0x01, 0x3f};

static struct ac_tcl_basic g_ac_tcl = {
    .cwt = 560,
    .st0 = 3100,
    .st1 = 1600,
    .lg0 = 310,
    .lg1 = 1100,
    .data_buf = (unsigned int)ac_tcl_open,
    .data_len = sizeof(ac_tcl_open),
};

static const char *TAG = "example";
static bool IRAM_ATTR tim_cb(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_data)
{
    static int st = 0;
    static int next = 0;
    struct ac_tcl_basic *ac_tcl = (struct ac_tcl_basic *)user_data;
    char *p8 =(char*) (ac_tcl->data_buf);
    

    gptimer_alarm_config_t alarm_config = {
        .reload_count = 0,
        .alarm_count = 1000000, // period = 1s
        .flags.auto_reload_on_alarm = true,
    };

    switch (st)
    {
    case 0: //idle
        if((next & 1) == 1) {
            alarm_config.alarm_count = ac_tcl->st1;
            st = 1;
        } else {
            alarm_config.alarm_count = ac_tcl->st0;
        }
        ESP_ERROR_CHECK(gptimer_set_alarm_action(timer, &alarm_config));
        next++;
        break;
    case 1: //busy
        if((next & 1) == 1) {
            if(p8[(next-2) /2 /8] & ((0x80>>(((next-2)/2)%8)))) {
                alarm_config.alarm_count = ac_tcl->lg1;
            } else {
                alarm_config.alarm_count = ac_tcl->lg0;
            }
            if((next-2) /2 == (ac_tcl->data_len)*8-1) {
                st = 2;
            }
        } else {
            alarm_config.alarm_count = ac_tcl->cwt;
        }
        ESP_ERROR_CHECK(gptimer_set_alarm_action(timer, &alarm_config));
        next++;
        break;
    case 2: //end
        if((next & 1) == 1) {
            st = 0;
            next = 0;
        } else {
            alarm_config.alarm_count = ac_tcl->cwt;
            ESP_ERROR_CHECK(gptimer_set_alarm_action(timer, &alarm_config));
            next++;
        }
        break;
    default:
        break;
    }
    // gptimer_enable(timer);
    if(next & 1){
        ir_cwave_on();
    } else {
        ir_cwave_off();
    }
    if(next == 0) {
        gptimer_stop(timer);
        gptimer_disable(timer);
        gptimer_del_timer(timer);
        ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
        g_isdone = 1;
        // ledc_timer_config(&ledc_timer);
        // gpio_set_level(0, 0);
    } 
        // printf( "st :%d\r\n", st );
    return pdTRUE;
}


void ac_swi(int sw)
{
    xSemaphoreTake(xSemaphore, ULONG_MAX);
    g_isdone = 0;
    ir_io_init(0);
    if(sw) {
        g_ac_tcl.data_buf = (unsigned int)ac_tcl_open;
    } else {
        g_ac_tcl.data_buf = (unsigned int)ac_tcl_close;
    }
    gptimer_handle_t gptimer = NULL;
    gptimer_config_t timer_config = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = 1000000, // 1MHz, 1 tick=1us
    };

    ESP_ERROR_CHECK(gptimer_new_timer(&timer_config, &gptimer));

    gptimer_event_callbacks_t cbs = {
        .on_alarm = tim_cb,
    };
    ESP_ERROR_CHECK(gptimer_register_event_callbacks(gptimer, &cbs, &g_ac_tcl));

    ESP_LOGI(TAG, "Enable timer");
    ESP_ERROR_CHECK(gptimer_enable(gptimer));

    ESP_LOGI(TAG, "Start timer, stop it at alarm event");
    gptimer_alarm_config_t alarm_config = {
        .reload_count = 0,
        .alarm_count = 1000000, // period = 1s
        .flags.auto_reload_on_alarm = true,
    };
    ESP_ERROR_CHECK(gptimer_set_alarm_action(gptimer, &alarm_config));
    ESP_ERROR_CHECK(gptimer_start(gptimer));
    while(!g_isdone);
    xSemaphoreGive(xSemaphore);
}

void ir_io_init(unsigned int pin)
{
    ledc_timer_rst(LEDC_LOW_SPEED_MODE, LEDC_TIMER_0);
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_LOW_SPEED_MODE,
        .timer_num        = LEDC_TIMER_0,
        .duty_resolution  = LEDC_TIMER_10_BIT,
        .freq_hz          = 38000,  // Set output frequency at 38 kHz
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    // Prepare and then apply the LEDC PWM channel configuration
    ledc_channel_config_t ledc_channel = {
        .speed_mode     = LEDC_LOW_SPEED_MODE,
        .channel        = LEDC_CHANNEL_0,
        .timer_sel      = LEDC_TIMER_0,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = pin,
        .duty           = 512, // Set duty to 0%
        .hpoint         = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));
    ledc_timer_pause(LEDC_LOW_SPEED_MODE, LEDC_TIMER_0);
}

void ir_cwave_off()
{
    ledc_timer_pause(LEDC_LOW_SPEED_MODE, LEDC_TIMER_0);
}

void ir_cwave_on()
{
    ledc_timer_resume(LEDC_LOW_SPEED_MODE, LEDC_TIMER_0);
}

void ir_encoder_init()
{
    xSemaphore = xSemaphoreCreateMutexStatic(&xMutexBuffer);
    ir_io_init(0);
}