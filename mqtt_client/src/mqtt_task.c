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
#include "esp_check.h"
#include "esp_event.h"

#include "esp_mac.h" // esp_base_mac_addr_get

#include "mqtt_client.h"

#include "mqtt_task.h"

static const char TAG[] = "MQTT-C";

static struct mqtt_task
{
	char serverURL[50];
	TaskHandle_t taskHandle;
	MessageBufferHandle_t xBufHandle;
	EventGroupHandle_t eventGroup;
	esp_mqtt_client_handle_t mqtt_client;
} this;

static esp_err_t mqtt_eventBitCtrl(const EventBits_t flags, bool setOrClear);
static esp_err_t mqtt_eventWait(const EventBits_t flags, bool waitAllFlags);
static esp_err_t mqtt_publisher(Mqtt_Msg_t *buffer);
static esp_err_t mqtt_subscriber(Mqtt_Msg_t *buffer);
static void mqtt_task(void *pvParameters);

esp_err_t mqtt_pubMsgTo(const char* topic, const char *context, int qos, int r)
{
	ESP_RETURN_ON_FALSE(this.taskHandle != NULL, ESP_FAIL, TAG, "task not init");
	
	Mqtt_Msg_t buffer = {
		.qos = qos,
		.retainFlag = r,
	};
	sprintf(buffer.topic, "%s", topic);
	sprintf(buffer.context, "%s", context);
	size_t bufferSize = sizeof(Mqtt_Msg_t);

	mqtt_eventBitCtrl(MQTT_EVENT_PUB, EVENT_SET);

	BaseType_t xHigherPriorityTaskWoken = pdFALSE;
	size_t sended = xMessageBufferSendFromISR(this.xBufHandle, &buffer, bufferSize, &xHigherPriorityTaskWoken);

	ESP_RETURN_ON_FALSE((sended == bufferSize), ESP_FAIL, TAG, "size diff %d != %d", sended, bufferSize);

	return ESP_OK;
}

esp_err_t mqtt_subTopicCfg(const char* topic, int qos, bool subOrUn)
{
	ESP_RETURN_ON_FALSE(this.taskHandle != NULL, ESP_FAIL, TAG, "task not init");

	ESP_LOGI(TAG, "%s subscribe @ {%s}", subOrUn?"":"X", topic);

	Mqtt_Msg_t buffer = {
		.qos = qos,
	};
	sprintf(buffer.topic, "%s", topic);
	size_t bufferSize = sizeof(Mqtt_Msg_t);

	mqtt_eventBitCtrl(subOrUn ? MQTT_EVENT_SUB : MQTT_EVENT_UNSUB, EVENT_SET);

	BaseType_t xHigherPriorityTaskWoken = pdFALSE;
	size_t sended = xMessageBufferSendFromISR(this.xBufHandle, &buffer, sizeof(Mqtt_Msg_t), &xHigherPriorityTaskWoken);
	
	ESP_RETURN_ON_FALSE((sended == bufferSize), ESP_FAIL, TAG, "size diff %d != %d", sended, bufferSize);
	return ESP_OK;
}

static esp_err_t mqtt_eventBitCtrl(const EventBits_t flags, bool setOrClear)
{
	ESP_RETURN_ON_FALSE(this.eventGroup != NULL, ESP_FAIL, TAG, "eventGroup not init");

	if (setOrClear == EVENT_SET)
		xEventGroupSetBits(this.eventGroup, flags);
	else
		xEventGroupClearBits(this.eventGroup, flags);
	
	return ESP_OK;
}

static esp_err_t mqtt_eventWait(const EventBits_t flags, bool waitAllFlags)
{
	ESP_RETURN_ON_FALSE(this.eventGroup != NULL, ESP_FAIL, TAG, "eventGroup not init");

	xEventGroupWaitBits(this.eventGroup, flags, false, waitAllFlags, portMAX_DELAY);

	return ESP_OK;
}

static esp_err_t mqtt_publisher(Mqtt_Msg_t *buffer)
{
	ESP_RETURN_ON_FALSE(this.mqtt_client != NULL, ESP_FAIL, TAG, "mqtt_client not init");

	size_t size = strlen(buffer->context);
	/* Remove trailing LF */
	if (buffer->context[size - 1] == 0x0a) size = size - 1;
	if (size)
	{
		esp_mqtt_client_publish(this.mqtt_client, buffer->topic, buffer->context, size, buffer->qos, buffer->retainFlag);
	}
	else
	{
		ESP_LOGI(TAG, "buffer size error");
	}

	return ESP_OK;
}

static esp_err_t mqtt_subscriber(Mqtt_Msg_t *buffer)
{
	ESP_RETURN_ON_FALSE(this.mqtt_client != NULL, ESP_FAIL, TAG, "mqtt_client not init");

	esp_mqtt_client_subscribe(this.mqtt_client, buffer->topic, buffer->qos);

	ESP_LOGI(TAG, "sub @ {%s}", buffer->topic);

	return ESP_OK;
}

static esp_err_t mqtt_unsubscriber(Mqtt_Msg_t *buffer)
{
	ESP_RETURN_ON_FALSE(this.mqtt_client != NULL, ESP_FAIL, TAG, "mqtt_client not init");

	esp_mqtt_client_unsubscribe(this.mqtt_client, buffer->topic);

	ESP_LOGI(TAG, "unsub @ {%s}", buffer->topic);

	return ESP_OK;
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

static void mqtt_task(void *pvParameters)
{
	// Initialize
	ESP_RETURN_ON_FALSE(this.mqtt_client == NULL, (void)0, TAG, "mqtt_client already init");

	this.eventGroup = NULL;
	this.mqtt_client = NULL;
	this.taskHandle = NULL;
	this.xBufHandle = NULL;

	char *serverURL = (char *)pvParameters;
	ESP_LOGI(TAG, "Start:param.url=[%s]\n", serverURL);
	// Create Buffer stream
	this.xBufHandle = xMessageBufferCreate(sizeof(Mqtt_Msg_t) + sizeof(size_t));
	ESP_RETURN_ON_FALSE(this.xBufHandle != NULL, (void)0, TAG, "xBufHandle failed");

	// Create Event Group
	this.eventGroup = xEventGroupCreate();
	ESP_RETURN_ON_FALSE(this.eventGroup != NULL, (void)0, TAG, "eventGroup failed");

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

	ESP_RETURN_ON_FALSE(this.mqtt_client != NULL, (void)0, TAG, "mqtt_client failed");

	esp_mqtt_client_start(this.mqtt_client);

	mqtt_eventBitCtrl(MQTT_EVENT_CONNECTED_BIT, EVENT_CLEAR);

	// Wait for connection to MQTT Broker
	mqtt_eventWait(MQTT_EVENT_CONNECTED_BIT, EVENT_WAIT_ALL);

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

esp_err_t mqtt_task_init(const char *url)
{
	ESP_RETURN_ON_FALSE(this.taskHandle == NULL, ESP_FAIL, TAG, "already init");

	// Start MQTT task
	sprintf(this.serverURL, "%s", url);
	ESP_LOGI(TAG, "start mqtt client: url=[%s]\n", this.serverURL);

	this.taskHandle = xTaskGetCurrentTaskHandle();
	ESP_RETURN_ON_FALSE(this.taskHandle != NULL, ESP_FAIL, TAG, "task init fail");

	xTaskCreate(mqtt_task, TAG, 1024 * 6, (void *)this.serverURL, 9, NULL);

	return ESP_OK;
}
