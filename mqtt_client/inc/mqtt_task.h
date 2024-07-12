#if !defined(__MQTT_TASK_H__)
#define __MQTT_TASK_H__

#define MQTT_EVENT_SUB BIT1
#define MQTT_EVENT_UNSUB BIT2
#define MQTT_EVENT_PUB BIT3
#define MQTT_EVENT_CONNECTED_BIT BIT4
#define MQTT_EVENT_STOP_BIT BIT5

#define EVENT_SET true
#define EVENT_CLEAR false

#define EVENT_WAIT_ALL true

// max number of bytes in the whole buffer
#define MQTT_MSG_MAX_COUNT      (3)
#define MQTT_MSG_SINGLE_SIZE    (1024)
#define MQTT_MSG_BUFFER_TOTAL   (MQTT_MSG_SINGLE_SIZE * MQTT_MSG_MAX_COUNT)
#define MQTT_TASK_SIZE          (1024 + MQTT_MSG_BUFFER_TOTAL)

#define MQTT_MSG_URL_SIZE (64)
#define MQTT_MSG_TOPIC_SIZE (64)
#define MQTT_MSG_PAYLOAD_SIZE (1024)

esp_err_t mqtt_task_init(void);
esp_err_t mqtt_task_deinit(void);
void mqtt_connect(char *serverURL);
void mqtt_disconnect(void);
void mqtt_publish(char* t, char* p, int q, int r);
void mqtt_subscribe(char* t, int q);
void mqtt_unsubscribe(char* t);

#endif // __MQTT_TASK_H__
