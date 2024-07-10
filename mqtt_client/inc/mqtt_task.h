#if !defined(__MQTT_TASK_H__)
#define __MQTT_TASK_H__

#define MQTT_EVENT_SUB            BIT1
#define MQTT_EVENT_UNSUB          BIT2
#define MQTT_EVENT_PUB            BIT3
#define MQTT_EVENT_CONNECTED_BIT  BIT4

#define EVENT_SET       true
#define EVENT_CLEAR     false

#define EVENT_WAIT_ALL  true

typedef struct mqtt_msg
{
    char topic[512];
    char context[512];
    int qos;
    int retainFlag;
}Mqtt_Msg_t;

void mqtt_task_init(const char* url);
void mqtt_pubMsgTo(const char* topic, const char* context, int qos, int r);
void mqtt_subTopicCfg(const char* topic, int qos, bool subOrUn);

#endif // __MQTT_TASK_H__
