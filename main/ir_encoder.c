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

struct ac_dev *acdevs[ACDEV_MAX] = {NULL};


void ac_open(int devid)
{
    if (xSemaphoreTake(acdevs[devid]->mutex, portMAX_DELAY) == pdTRUE) {
        acdevs[devid]->ops->open(acdevs[devid]);
        xSemaphoreGive(acdevs[devid]->mutex); // 释放锁
    }
}

void ac_close(int devid)
{
    if (xSemaphoreTake(acdevs[devid]->mutex, portMAX_DELAY) == pdTRUE) {
        acdevs[devid]->ops->close(acdevs[devid]);
        xSemaphoreGive(acdevs[devid]->mutex); // 释放锁
    }
}

void ac_read_cfg(int devid, struct ac_cfg *cfg)
{
    if (xSemaphoreTake(acdevs[devid]->mutex, portMAX_DELAY) == pdTRUE) {
        acdevs[devid]->ops->read_cfg(acdevs[devid], cfg);
        xSemaphoreGive(acdevs[devid]->mutex); // 释放锁
    }
}

void ac_write_cfg(int devid, struct ac_cfg *cfg)
{
    if (xSemaphoreTake(acdevs[devid]->mutex, portMAX_DELAY) == pdTRUE) {
        acdevs[devid]->ops->write_cfg(acdevs[devid], cfg);
        xSemaphoreGive(acdevs[devid]->mutex); // 释放锁
    }
}

