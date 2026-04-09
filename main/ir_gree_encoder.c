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

extern struct ac_dev *acdevs[ACDEV_MAX];

gptimer_handle_t gptimer = NULL;


struct gree_irframe {
    uint8_t mode     : 3,
            swi      : 1,
            speed    : 2,
            swing_en : 1,
            sleep_en : 1;
            
    uint8_t temper   : 4,
            tmr1     : 4;

    uint8_t rev0;
    uint8_t rev1;
    uint8_t spec3;
    
    uint8_t swing_lr : 4,
            swing_ud : 4;
    uint8_t tview;
    uint8_t rev2;
    uint8_t rev3    : 4,
            check   : 4;
            
};
typedef struct gree_irframe gif_t;

#define GREE_FRAME0(_swi, _temp) \
    (gif_t){.mode = 1, .sleep_en = 1, .swing_en = 1, .swi = _swi, .temper = _temp, .rev0 = 0x20, \
        .rev1 = 0x50, .spec3 = 0x02, .swing_lr = 1, .tview = 0x40, \
        .swing_ud = 0x0}

#define GREE_FRAME1(_swi, _temp) \
    (gif_t){.mode = 1, .sleep_en = 1, .swing_en = 1, .swi = _swi, .temper = _temp, .rev0 = 0x20, \
        .rev1 = 0x70, .spec3 = 0x02, .tview = 0x00, \
        .swing_ud = 0x8, .rev2 = 0x80}


#define PERIOD_STM_START_S0 9000
#define PERIOD_STM_START_S1 4500
#define PERIOD_STM_BIT0_S0  680
#define PERIOD_STM_BIT0_S1  560
#define PERIOD_STM_BIT1_S0  680
#define PERIOD_STM_BIT1_S1  1600 
#define PERIOD_STM_JOINT_S0 600
#define PERIOD_STM_JOINT_S1 20000
#define PERIOD_STM_END_S0 600
#define PERIOD_STM_END_S1 40000

enum gree_st_machine {
    STM_RESET,
    STM_START0, 
    STM_START1, 
    STM_BITS0,
    STM_BITS1,
    STM_JOINT0,
    STM_JOINT1,
    STM_END0,
    STM_END1,
    STM_END2,
};

struct tx_descript {
    union {
        gif_t gif;
        uint8_t bits[9];
    } frame;
    int bidx;
    int st;
    SemaphoreHandle_t done;
};

struct gree_ac_dev {
    struct ac_dev ac;
    int pin;    
    struct tx_descript tx;
};

struct gree_ac_dev gac = {
    .pin = 5,
    .ac.cfg.open = 0,
    .ac.cfg.temp = 24,
    .ac.cfg.sleep = 1,
    .ac.cfg.temp_act = 24,
    .ac.cfg.temp_slp = 26,
    .tx.bidx = 0,
    .tx.st = STM_RESET,
};

void ir_cwave_off();
void ir_cwave_on();

static const char *TAG = "ir_encoder";
static bool IRAM_ATTR tim_cb(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_data)
{
    struct gree_ac_dev *gac = (struct gree_ac_dev *)user_data;
    
    gptimer_alarm_config_t alarm_config = {
        .reload_count = 0,
        .alarm_count = 1000000,         // same to 1000000us
        .flags.auto_reload_on_alarm = true,
    };

    int bit = (gac->tx.frame.bits[gac->tx.bidx/8] & (1<<(gac->tx.bidx%8)));


    switch (gac->tx.st)
    {
    case STM_RESET: 
        gptimer_stop(timer);
        ledc_timer_pause(LEDC_LOW_SPEED_MODE, LEDC_TIMER_0);
        xSemaphoreGive(gac->tx.done);
        gac->tx.bidx = 0;
        break;
    case STM_START0:
        alarm_config.alarm_count = PERIOD_STM_START_S0;
        gac->tx.st = STM_START1;
        break;
    case STM_START1:
        alarm_config.alarm_count = PERIOD_STM_START_S1;
        gac->tx.st = STM_BITS0;
        break;
    case STM_BITS0:
        alarm_config.alarm_count = (bit) ? PERIOD_STM_BIT1_S0 : PERIOD_STM_BIT0_S0;
        gac->tx.st = STM_BITS1;
        break;
    case STM_BITS1:
        alarm_config.alarm_count = (bit) ? PERIOD_STM_BIT1_S1 : PERIOD_STM_BIT0_S1;
        gac->tx.bidx++;
        if (gac->tx.bidx == 35) {
            gac->tx.st = STM_JOINT0;
        } else if (gac->tx.bidx == 72) {
            gac->tx.st = STM_END0;
        } else {
            gac->tx.st = STM_BITS0;
        }
        break;     
    case STM_JOINT0:
        alarm_config.alarm_count = PERIOD_STM_JOINT_S0;
        gac->tx.st = STM_JOINT1;
        break;
    case STM_JOINT1:
        alarm_config.alarm_count = PERIOD_STM_JOINT_S1;
        gac->tx.st = STM_BITS0;
        gac->tx.bidx = 40;
        break;
    case STM_END0:
        alarm_config.alarm_count = PERIOD_STM_END_S0;
        gac->tx.st = STM_END1;
        break;
    case STM_END1: 
        alarm_config.alarm_count = PERIOD_STM_END_S1;
        gac->tx.st = STM_END2;
        break;
    case STM_END2:
        gac->tx.bidx = 0;
        gac->tx.st = STM_RESET;
        break;
    default :
        gac->tx.bidx = 0;
        gac->tx.st = STM_RESET;
    }

    if (gac->tx.st != STM_RESET) {
        if (gac->tx.st & 1) {
            ir_cwave_off();
        } else {
            ir_cwave_on();
        }
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
    gac.tx.done = xSemaphoreCreateBinary();
    ir_io_init(gac.pin);

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
    ESP_ERROR_CHECK(gptimer_register_event_callbacks(gptimer, &cbs, &gac));

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

static void gree_ac_open(struct ac_dev *ac);
static void gree_ac_close(struct ac_dev *ac);
static void gree_ac_read_cfg(struct ac_dev *ac, struct ac_cfg *cfg);
static void gree_ac_write_cfg(struct ac_dev *ac, struct ac_cfg *cfg);

struct ac_ops gree_ac_ops = {
    .open = gree_ac_open,
    .close = gree_ac_close,
    .read_cfg = gree_ac_read_cfg,
    .write_cfg = gree_ac_write_cfg,
};

void gree_ac_ir_tranx(struct ac_dev *ac)
{
    struct gree_ac_dev *gac = (struct gree_ac_dev *)ac;

    // printf("send: [0x%x, 0x%x, 0x%x, 0x%x, 0x%x], [0x%x, 0x%x, 0x%x, 0x%x]\r\n", 
    //     gac->tx.frame.bits[0], gac->tx.frame.bits[1], gac->tx.frame.bits[2], gac->tx.frame.bits[3], 
    //     gac->tx.frame.bits[4], 
    //     gac->tx.frame.bits[5], gac->tx.frame.bits[6], gac->tx.frame.bits[7], gac->tx.frame.bits[8]);

    if (gac->tx.st != STM_RESET) {
        return;
    }

    gptimer_alarm_config_t alarm_config = {
        .reload_count = 0,
        .alarm_count = 1000, // period = 1ms
        .flags.auto_reload_on_alarm = true,
    };

    ESP_ERROR_CHECK(gptimer_set_alarm_action(gptimer, &alarm_config));
    ir_io_init(gac->pin);
    gac->tx.frame.gif = GREE_FRAME0(gac->ac.cfg.open, gac->ac.cfg.temp);
    gac->tx.frame.gif.sleep_en = gac->ac.cfg.sleep;
    gac->tx.frame.gif.swi = gac->ac.cfg.open;
    if (gac->ac.cfg.open) {
        gac->tx.frame.gif.check = gac->ac.cfg.temp+7;
    } else {
        gac->tx.frame.gif.check = gac->ac.cfg.temp-1;
    }

    gac->tx.st = STM_START0;
    ESP_ERROR_CHECK(gptimer_start(gptimer));
    xSemaphoreTake(gac->tx.done, portMAX_DELAY);

    gac->tx.frame.gif = GREE_FRAME1(gac->ac.cfg.open, gac->ac.cfg.temp);
    gac->tx.frame.gif.sleep_en = gac->ac.cfg.sleep;
    gac->tx.frame.gif.swi = gac->ac.cfg.open;
    if (gac->ac.cfg.open) {
        gac->tx.frame.gif.check = gac->ac.cfg.temp+3;
    } else {
        gac->tx.frame.gif.swing_ud = 0;
        gac->tx.frame.gif.check = gac->ac.cfg.temp+3;
    }

    gac->tx.st = STM_START0;
    ESP_ERROR_CHECK(gptimer_start(gptimer));
    xSemaphoreTake(gac->tx.done, portMAX_DELAY);
    gpio_set_direction(gac->pin, GPIO_MODE_DISABLE);
}

static void gree_ac_open(struct ac_dev *ac)
{
    struct gree_ac_dev *gac = (struct gree_ac_dev *)ac;
    gac->ac.cfg.open = 1;
    gree_ac_ir_tranx(ac);
}

static void gree_ac_close(struct ac_dev *ac)
{
    struct gree_ac_dev *gac = (struct gree_ac_dev *)ac;
    gac->ac.cfg.open = 0;
    gree_ac_ir_tranx(ac);
}

static void gree_ac_read_cfg(struct ac_dev *ac, struct ac_cfg *cfg)
{
    struct gree_ac_dev *gac = (struct gree_ac_dev *)ac;
    *cfg = gac->ac.cfg;
}

static void gree_ac_write_cfg(struct ac_dev *ac, struct ac_cfg *cfg)
{
    struct gree_ac_dev *gac = (struct gree_ac_dev *)ac;
    if (memcmp(&gac->ac.cfg, cfg, sizeof(*cfg))) {
        gac->ac.cfg = *cfg;
        gree_ac_ir_tranx(ac);
    }
}


void gree_ac_register(int pid)
{
    ir_encoder_init();
    gac.ac.ops = &gree_ac_ops;
    acdevs[pid] = &(gac.ac);
    acdevs[pid]->mutex = xSemaphoreCreateMutex();
}