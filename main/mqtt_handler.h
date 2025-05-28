#ifndef _MQTT_HANDLER_H
#define _MQTT_HANDLER_H

void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);
int mqtt_data_event_handler(void *event_data);


#endif