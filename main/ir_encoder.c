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
gptimer_handle_t gptimer = NULL;
volatile static int g_isdone;

static unsigned char ac_tcl_open[] = {
0xc4, 0xd3, 0x64, 0x80, 
0x00, 0x26, 0xc0, 0xa0,
0x1e, 0x00, 0x00, 0x00,
0x01, 0x9e};
static unsigned char ac_tcl_close[] = {
0xc4, 0xd3, 0x64, 0x80, 
0x00, 0x04, 0xc0, 0x20, 
0x02, 0x00, 0x00, 0x00,
0x01, 0x3f};

struct ac_tcl_basic g_ac_tcl = {
    .cwt = 560,   //untrig
    .st0 = 3100,  //trig
    .st1 = 1600,  //untrig
    .lg0 = 310,   //trig
    .lg1 = 1100,  //trig
    .data_buf = ac_tcl_open,
    .data_len = sizeof(ac_tcl_open),
    .is_open  = 0,
    .is_fixed = 0,
    .temp     = 26,
    .pin      = 5,
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

static const char *TAG = "ir_encoder";
static bool IRAM_ATTR tim_cb(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_data)
{
    static struct machine machine = {
        .st  = M_ST_IDLE,
        .stp = M_STEP0,
        .bits = 0,
    };

    struct ac_tcl_basic *ac_tcl = (struct ac_tcl_basic *)user_data;
    
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
            machine.st = M_ST_BUSY;
            break;
        default:
            break;
        }
        break;

    case 1: //busy
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
            break;
        }
        break;
    case 2: //end
        switch (machine.stp)
        {
        case M_STEP0:
            alarm_config.alarm_count = ac_tcl->cwt;
            break;
        case M_STEP1:
            machine.st = M_ST_IDLE;
            g_isdone = 1;
            break;
        default:
            break;
        }
        break;

    default:
        break;
    }

    if (machine.stp == M_STEP0) {
        ir_cwave_on();
        machine.stp = M_STEP1;
    } else if(machine.stp == M_STEP1) {
        ir_cwave_off();
        machine.stp = M_STEP0;
    }

    if (g_isdone) {
        gptimer_stop(timer);
        ledc_timer_pause(LEDC_LOW_SPEED_MODE, LEDC_TIMER_0);
        // ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
    } else {
        ESP_ERROR_CHECK(gptimer_set_alarm_action(timer, &alarm_config));
    }
    
    return pdTRUE;
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
    ir_io_init(g_ac_tcl.pin);

    //gptiemr init
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
}


unsigned char bits_reverse(unsigned char in)
{
    in = ((in&0xf0) >> 4) | ((in&0x0f) << 4);
    in = ((in&0xcc) >> 2) | ((in&0x33) << 2);
    in = ((in&0xaa) >> 1) | ((in&0x55) << 1);
    return in;
}

void ac_swi(int sw)
{
    xSemaphoreTake(xSemaphore, ULONG_MAX);
    g_isdone = 0;
    
    if(sw) {
        g_ac_tcl.data_buf = (void *)ac_tcl_open;
    } else {
        g_ac_tcl.data_buf = (void *)ac_tcl_close;
    }

    // Prepare and then apply the LEDC PWM channel configuration
    ledc_channel_config_t ledc_channel = {
        .speed_mode     = LEDC_LOW_SPEED_MODE,
        .channel        = LEDC_CHANNEL_0,
        .timer_sel      = LEDC_TIMER_0,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = g_ac_tcl.pin,
        .duty           = 512, // Set duty to 0%
        .hpoint         = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));    

    gptimer_alarm_config_t alarm_config = {
        .reload_count = 0,
        .alarm_count = 1000000, // period = 1s
        .flags.auto_reload_on_alarm = true,
    };

    ESP_ERROR_CHECK(gptimer_set_alarm_action(gptimer, &alarm_config));
    ESP_ERROR_CHECK(gptimer_start(gptimer));
    while(!g_isdone);
    xSemaphoreGive(xSemaphore);
    g_ac_tcl.is_open = sw;
    gpio_set_direction(g_ac_tcl.pin, GPIO_MODE_DISABLE);
}

int ac_fixed_tgl()
{
    g_ac_tcl.is_fixed = !g_ac_tcl.is_fixed;
    return g_ac_tcl.is_fixed;
}

void ac_pre_t()
{
    ac_tcl_open[7]  = bits_reverse(31-g_ac_tcl.temp);
    ac_tcl_open[13] = 0;
    for(int i = 0;i<13;i++)
    {
        ac_tcl_open[13] += bits_reverse(ac_tcl_open[i]);
    }
    ac_tcl_open[13] = bits_reverse(ac_tcl_open[13]);
}

void ac_tup()
{
    if (g_ac_tcl.temp < 30) {
        g_ac_tcl.temp++;
    }
    ac_pre_t();
}

void ac_tdown()
{
    if (g_ac_tcl.temp > 16) {
        g_ac_tcl.temp--;
    }
    ac_pre_t();
}

void ac_set_temperature(int t)
{
    g_ac_tcl.temp = t;
    ac_pre_t();
}

int ac_get_temperature()
{
    return g_ac_tcl.temp;
}

int ac_is_open() {
    return g_ac_tcl.is_open;
}

int ac_is_fixed()
{
    return g_ac_tcl.is_fixed;
}