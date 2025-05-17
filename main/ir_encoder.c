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
    .cwt = 560,   //untrig
    .st0 = 3100,  //trig
    .st1 = 1600,  //untrig
    .lg0 = 310,   //trig
    .lg1 = 1100,  //trig
    .data_buf = (unsigned int)ac_tcl_open,
    .data_len = sizeof(ac_tcl_open),
};

#define M_ST_IDLE 0
#define M_ST_BUSY 1
#define M_ST_END  2

#define M_STEP0 0
#define M_STEP1 1
#define M_STEP2 2
struct machine{
    int st;
    int stp;
    int bits;
};

static const char *TAG = "example";
static bool IRAM_ATTR tim_cb(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_data)
{
    static struct machine machine = {
        .st  = M_ST_IDLE,
        .stp = M_STEP0,
        .bits = 0,
    };

    struct ac_tcl_basic *ac_tcl = (struct ac_tcl_basic *)user_data;
    char *p8 =(char*) (ac_tcl->data_buf);
    
    gptimer_alarm_config_t alarm_config = {
        .reload_count = 0,
        .alarm_count = 1000000, // period = 1s
        .flags.auto_reload_on_alarm = true,
    };

    switch (machine.st)
    {
    case M_ST_IDLE: //idle
        switch (machine.stp)
        {
        case M_STEP0:
            alarm_config.alarm_count = ac_tcl->st0;
        break;
        case M_STEP1:
            alarm_config.alarm_count = ac_tcl->st1;
            st = M_ST_BUSY;
        break;
        default:
        }
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
        switch (machine.stp)
        {
        case M_STEP0:
            alarm_config.alarm_count = ac_tcl->cwt;
        break;
        case M_STEP1:
            alarm_config.alarm_count = ac_tcl->st1;
            if (ac_tcl->data_buf[machine.bits/8] & (0x80>>(machine.bits%8))) {
                alarm_config.alarm_count = ac_tcl->lg1;
            } else {
                alarm_config.alarm_count = ac_tcl->lg0;
            }
            machine.bits++;
            if (machine.bits == (ac_tcl->data_len)*8) {
                machine.st = M_ST_END;
                machine.bits = 0;
            }
        break;
        default:
            ;
        }
    break;
    case 2: //end
        case M_STEP0:
            alarm_config.alarm_count = ac_tcl->cwt;
        break;
        case M_STEP1:
            machine.st = M_ST_IDLE;
            g_isdone = 1;
        break;
        default:
            ;
    break;
    default:
    break;
    }

    

    if(next & 1){
        ir_cwave_on();
    } else {
        ir_cwave_off();
    }

    if (machine.act == M_STEP0) {
        machine.act = M_STEP1;
    } else if(machine.act == M_STEP1) {
        machine.act = M_STEP0;
    }

    if (g_isdone) {
        gptimer_stop(timer);
        gptimer_disable(timer);
        ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
    } else {
        ESP_ERROR_CHECK(gptimer_set_alarm_action(timer, &alarm_config));
    }
    
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