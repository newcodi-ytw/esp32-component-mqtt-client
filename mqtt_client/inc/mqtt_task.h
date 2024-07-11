#if !defined(__MQTT_TASK_H__)
#define __MQTT_TASK_H__

#define MQTT_EVENT_SUB            BIT1
#define MQTT_EVENT_UNSUB          BIT2
#define MQTT_EVENT_PUB            BIT3
#define MQTT_EVENT_CONNECTED_BIT  BIT4
#define MQTT_EVENT_STOP_BIT       BIT5

#define EVENT_SET       true
#define EVENT_CLEAR     false

#define EVENT_WAIT_ALL  true

#define MQTT_TOPIC_LEN          (512)
#define MQTT_CONTEXT_LEN        (1024)

//number of message of 1 buffer
#define MQTT_MSG_BUFFER_SZIE    (10) 

typedef struct mqtt_msg
{
    char topic[MQTT_TOPIC_LEN];
    char context[MQTT_CONTEXT_LEN];
    int qos;
    int retainFlag;
}Mqtt_Msg_t;

esp_err_t mqtt_task_start(const char* url);
esp_err_t mqtt_task_stop(void);
esp_err_t mqtt_pubMsgTo(const char* topic, const char* context, int qos, int r);
esp_err_t mqtt_subTopicCfg(const char* topic, int qos, bool subOrUn);

#endif // __MQTT_TASK_H__
