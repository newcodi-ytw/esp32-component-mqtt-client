/*
	MQTT over TCP

	This example code is in the Public Domain (or CC0 licensed, at your option.)

	Unless required by applicable law or agreed to in writing, this
	software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
	CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdio.h>
#include <inttypes.h>
#include <stdint.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#if 0 /* CONFIG_USE_RINGBUFFER */
#include "freertos/ringbuf.h"
#else
#include "freertos/message_buffer.h"
#endif
#include "esp_system.h"
#include "esp_log.h"
#include "esp_event.h"

#include "esp_mac.h" // esp_base_mac_addr_get

#include "mqtt_client.h"

#include "mqtt_task.h"

static const char TAG[] = "MQTT-C";

static struct mqtt_task
{
	TaskHandle_t taskHandle;
	MessageBufferHandle_t xBufHandle;
	EventGroupHandle_t eventGroup;
	esp_mqtt_client_handle_t mqtt_client;
} this = {NULL};

static void mqtt_eventBitCtrl(const EventBits_t flags, bool setOrClear);
static void mqtt_eventWait(const EventBits_t flags, bool waitAllFlags);
static void mqtt_publisher(Mqtt_Msg_t *buffer);
static void mqtt_subscriber(Mqtt_Msg_t *buffer);

void mqtt_pubMsgTo(const char* topic, const char *context, int qos, int r)
{
	if(this.taskHandle == NULL) return;
	// ESP_LOGI(TAG, "pub@{%s}: %s", topic, context);
	
	Mqtt_Msg_t buffer = {
		.qos = qos,
		.retainFlag = r,
	};

	sprintf(buffer.topic, "%s", topic);
	sprintf(buffer.context, "%s", context);

	mqtt_eventBitCtrl(MQTT_EVENT_PUB, EVENT_SET);

	BaseType_t xHigherPriorityTaskWoken = pdFALSE;
	size_t sended = xMessageBufferSendFromISR(this.xBufHandle, &buffer, sizeof(Mqtt_Msg_t), &xHigherPriorityTaskWoken);

	// printf("mqtt_pubMsgTo sended=%d\n",sended);
	assert(sended == sizeof(Mqtt_Msg_t));
}

void mqtt_subTopicCfg(const char* topic, int qos, bool subOrUn)
{
	if(this.taskHandle == NULL) return;

	ESP_LOGI(TAG, "%s subscribe @ {%s}", subOrUn?"":"X", topic);

	Mqtt_Msg_t buffer = {
		.qos = qos,
	};
	sprintf(buffer.topic, "%s", topic);

	mqtt_eventBitCtrl(subOrUn ? MQTT_EVENT_SUB : MQTT_EVENT_UNSUB, EVENT_SET);

	BaseType_t xHigherPriorityTaskWoken = pdFALSE;
	size_t sended = xMessageBufferSendFromISR(this.xBufHandle, &buffer, sizeof(Mqtt_Msg_t), &xHigherPriorityTaskWoken);

	// printf("mqtt_pubMsgTo sended=%d\n",sended);
	assert(sended == sizeof(Mqtt_Msg_t));
}

static void mqtt_eventBitCtrl(const EventBits_t flags, bool setOrClear)
{
	if (this.eventGroup == NULL)
		return;

	if (setOrClear == EVENT_SET)
		xEventGroupSetBits(this.eventGroup, flags);
	else
		xEventGroupClearBits(this.eventGroup, flags);
}

static void mqtt_eventWait(const EventBits_t flags, bool waitAllFlags)
{
	if (this.eventGroup == NULL)
		return;

	xEventGroupWaitBits(this.eventGroup, flags, false, waitAllFlags, portMAX_DELAY);
}

static void mqtt_publisher(Mqtt_Msg_t *buffer)
{
	if(this.mqtt_client == NULL) return;

	size_t size = strlen(buffer->context);

	/* Remove trailing LF */
	if (buffer->context[size - 1] == 0x0a) size = size - 1;
	
	if (size)
	{
		esp_mqtt_client_publish(this.mqtt_client, buffer->topic, buffer->context, size, buffer->qos, buffer->retainFlag);

		// ESP_LOGI(TAG, "pub @ {%s}: %s", buffer->topic, buffer->context);
	}
	else
	{
		ESP_LOGI(TAG, "buffer size error");
	}
}

static void mqtt_subscriber(Mqtt_Msg_t *buffer)
{
	if(this.mqtt_client == NULL) return;

	esp_mqtt_client_subscribe(this.mqtt_client, buffer->topic, buffer->qos);

	ESP_LOGI(TAG, "sub @ {%s}", buffer->topic);
}

static void mqtt_unsubscriber(Mqtt_Msg_t *buffer)
{
	if(this.mqtt_client == NULL) return;

	esp_mqtt_client_unsubscribe(this.mqtt_client, buffer->topic);

	ESP_LOGI(TAG, "unsub @ {%s}", buffer->topic);
}

static esp_err_t mqtt_event_handler(esp_mqtt_event_handle_t event)
{
	switch (event->event_id)
	{
	case MQTT_EVENT_CONNECTED:
		ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
		mqtt_eventBitCtrl(MQTT_EVENT_CONNECTED_BIT, EVENT_SET);
		break;
	case MQTT_EVENT_DISCONNECTED:
		ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
		mqtt_eventBitCtrl(MQTT_EVENT_CONNECTED_BIT, EVENT_CLEAR);
		break;
	case MQTT_EVENT_SUBSCRIBED:
		ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
		break;
	case MQTT_EVENT_UNSUBSCRIBED:
		ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
		break;
	case MQTT_EVENT_PUBLISHED:
		ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
		break;
	case MQTT_EVENT_DATA:
		ESP_LOGI(TAG, "MQTT_EVENT_DATA");
		ESP_LOGI(TAG, "%s", event->data);
		ESP_LOG_BUFFER_HEXDUMP(TAG, event->data, event->data_len, ESP_LOG_INFO);
		break;
	case MQTT_EVENT_ERROR:
		ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
		break;
	default:
		ESP_LOGI(TAG, "Other event id:%d", event->event_id);
		break;
	}
	return ESP_OK;
}

void mqtt_task(void *pvParameters)
{
	// Initialize
	if (this.mqtt_client != NULL)
		return;

	char *serverURL = pvParameters;
	ESP_LOGI(TAG, "Start:param.url=[%s]\n", serverURL);
	// Create Buffer stream
	this.xBufHandle = xMessageBufferCreate(sizeof(Mqtt_Msg_t) + sizeof(size_t));
	configASSERT(this.xBufHandle);

	// Create Event Group
	this.eventGroup = xEventGroupCreate();
	configASSERT(this.eventGroup);

	// Set client id from mac
	uint8_t mac[8];
	ESP_ERROR_CHECK(esp_base_mac_addr_get(mac));

	char client_id[64];
	sprintf(client_id, "pub-%02x%02x%02x%02x%02x%02x", mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
	ESP_LOGI(TAG, "client_id=[%s]\n", client_id);

	esp_mqtt_client_config_t mqtt_cfg = {
		.uri = serverURL,
		.event_handle = mqtt_event_handler,
		.client_id = client_id};

	this.mqtt_client = esp_mqtt_client_init(&mqtt_cfg);

	esp_mqtt_client_start(this.mqtt_client);

	mqtt_eventBitCtrl(MQTT_EVENT_CONNECTED_BIT, EVENT_CLEAR);

	// Wait for connection to MQTT Broker
	// mqtt_eventWait(MQTT_EVENT_CONNECTED_BIT, EVENT_WAIT_ALL);

	ESP_LOGI(TAG, "MQTT Connected\n");

	while (1)
	{
		Mqtt_Msg_t buffer = {0};
		size_t received = xMessageBufferReceive(this.xBufHandle, &buffer, sizeof(Mqtt_Msg_t), portMAX_DELAY);

		if (received > 0)
		{
			EventBits_t EventBits = xEventGroupGetBits(this.eventGroup);
			if (EventBits & MQTT_EVENT_CONNECTED_BIT)
			{
				if (EventBits & MQTT_EVENT_PUB)
				{
					mqtt_publisher(&buffer);
					mqtt_eventBitCtrl(MQTT_EVENT_PUB, EVENT_CLEAR);
				}
				else if (EventBits & MQTT_EVENT_SUB)
				{
					mqtt_subscriber(&buffer);
					mqtt_eventBitCtrl(MQTT_EVENT_SUB, EVENT_CLEAR);
				}
				else if (EventBits & MQTT_EVENT_UNSUB)
				{
					mqtt_unsubscriber(&buffer);
					mqtt_eventBitCtrl(MQTT_EVENT_UNSUB, EVENT_CLEAR);
				}
			}
			else
			{
				ESP_LOGI(TAG, "Connection to MQTT broker is broken. Skip to send\n");
			}
		}
		else
		{
			ESP_LOGI(TAG, "xMessageBufferReceive fail\n");
			break;
		}
	} // end while

	// Stop connection
	esp_mqtt_client_stop(this.mqtt_client);
	vTaskDelete(NULL);
}

void mqtt_task_init(const char *url)
{
	if (this.taskHandle != NULL)
		return;

	ESP_LOGI(TAG, "start mqtt client: url=[%s]\n", url);

	// Start MQTT task
	char serverURL[64];
	strncpy(serverURL, url, sizeof(serverURL) / sizeof(serverURL[0]));

	this.taskHandle = xTaskGetCurrentTaskHandle();
	xTaskCreate(mqtt_task, TAG, 1024 * 6, (void *)serverURL, 9, NULL);
}
