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
#include "freertos/message_buffer.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_event.h"

#include "esp_mac.h" // esp_base_mac_addr_get
#include "mqtt_client.h"
#include "mqtt_task.h"

typedef enum mqtt_task_action
{
	MQTT_ACTION_CONNECT,
	MQTT_ACTION_DISCONNECT,
	MQTT_ACTION_PUB,
	MQTT_ACTION_SUB,
	MQTT_ACTION_UNSUB,
	MQTT_ACTION_MAX
} mqtt_task_action_e;

typedef struct mqtt_msg
{
	mqtt_task_action_e action;
	char serverURL[MQTT_MSG_URL_SIZE];
	char topic[MQTT_MSG_TOPIC_SIZE];
	char payload[MQTT_MSG_PAYLOAD_SIZE];
	int qos;
	int retained;
} Mqtt_Msg_t;

static const char TAG[] = "MQTT Client";
static struct _mqtt_task_hdl
{
	bool connected;
	TaskHandle_t taskHandle;
	MessageBufferHandle_t xBufHandle;
	esp_mqtt_client_handle_t mqtt_client;
} handle, *this = &handle;

inline static esp_err_t _xMsgBufReceive(Mqtt_Msg_t *msg)
{
	return xMessageBufferReceive(this->xBufHandle, msg, sizeof(Mqtt_Msg_t), portMAX_DELAY);
}

static esp_err_t _xMsgBufSend(Mqtt_Msg_t *msg)
{
	if (xMessageBufferIsFull(this->xBufHandle))
	{
		ESP_LOGI(TAG, "xMessageBuffer Is Full");
		return ESP_FAIL;
	}

	BaseType_t xHiPriTaskToken = pdFALSE;
	size_t msgSize = sizeof(Mqtt_Msg_t);
	size_t sended = xMessageBufferSendFromISR(this->xBufHandle, msg, msgSize, &xHiPriTaskToken);

	if (sended != msgSize)
	{
		return ESP_FAIL;
	}

	return ESP_OK;
}

static esp_err_t _mqtt_event_handler(esp_mqtt_event_handle_t event)
{
	switch (event->event_id)
	{
	case MQTT_EVENT_CONNECTED:
		ESP_LOGI(TAG, "connected");
		this->connected = true;
		break;
	case MQTT_EVENT_DISCONNECTED:
		ESP_LOGI(TAG, "disconnected");
		this->connected = false;
		break;
	case MQTT_EVENT_PUBLISHED:
		ESP_LOGI(TAG, "published msg-id: %d", event->msg_id);
		break;
	case MQTT_EVENT_SUBSCRIBED:
		ESP_LOGI(TAG, "subscribed msg-id: %d", event->msg_id);
		break;
	case MQTT_EVENT_UNSUBSCRIBED:
		ESP_LOGI(TAG, "unsubscribed msg-id: %d", event->msg_id);
		break;
	case MQTT_EVENT_DATA:
	{
		char* topic = malloc(sizeof(char) * event->topic_len);
		char* data = malloc(sizeof(char) * event->data_len);
		if(topic == NULL || data == NULL) break;

		strncpy(topic, event->topic, event->topic_len);
		strncpy(data, event->data, event->data_len);

		ESP_LOGI(TAG, "\nTopic:%s"
						"\nqos:%d retian:%d"
						"\n%s\n",
						topic,
						event->qos, event->retain,
						data);
		free(topic);
		free(data);
		break;
	}
	case MQTT_EVENT_BEFORE_CONNECT:
	case MQTT_EVENT_ERROR:
		break;
	default:
		ESP_LOGI(TAG, "event id: %d", event->event_id);
		break;
	}

	return ESP_OK;
}

static esp_err_t _mqtt_make_connection(char *url)
{
	if (this->connected)
	{
		ESP_LOGI(TAG, "already connected");
		return ESP_FAIL;
	}
	
	// Set client id from mac
	uint8_t mac[8];
	ESP_ERROR_CHECK(esp_base_mac_addr_get(mac));

	char client_id[64];
	sprintf(client_id, "ct-dev-%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
	ESP_LOGI(TAG, "client_id=[%s]\n", client_id);

	esp_mqtt_client_config_t mqtt_cfg = {
		.uri = url,
		.event_handle = _mqtt_event_handler,
		.client_id = client_id};

	this->mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
	esp_mqtt_client_start(this->mqtt_client);

	ESP_LOGI(TAG, "making connection to %s", url);
	return ESP_OK;
}

static esp_err_t _mqtt_make_disconnection(void)
{
	esp_mqtt_client_destroy(this->mqtt_client);
	this->connected = false;

	ESP_LOGI(TAG, "close connection");
	return ESP_OK;
}

static esp_err_t _mqtt_make_publish(Mqtt_Msg_t *msg)
{
	if (!this->connected)
	{
		ESP_LOGI(TAG, "No connection");
		return ESP_FAIL;
	}

	size_t size = strlen(msg->payload);
	/* Remove trailing LF */
	if (msg->payload[size - 1] == 0x0a)
		size = size - 1;
	if (size)
	{
		esp_mqtt_client_publish(this->mqtt_client, msg->topic, msg->payload, size, msg->qos, msg->retained);
	}
	else
	{
		ESP_LOGI(TAG, "buffer size error");
	}

	return ESP_OK;
}

static esp_err_t _mqtt_make_subscribe(Mqtt_Msg_t *msg)
{
	if (!this->connected)
	{
		ESP_LOGI(TAG, "No connection");
		return ESP_FAIL;
	}

	esp_mqtt_client_subscribe(this->mqtt_client, msg->topic, msg->qos);

	ESP_LOGI(TAG, "subscribing to %s", msg->topic);

	return ESP_OK;
}

static esp_err_t _mqtt_make_unsubscribe(Mqtt_Msg_t *msg)
{
	if (!this->connected)
	{
		ESP_LOGI(TAG, "No connection");
		return ESP_FAIL;
	}

	esp_mqtt_client_unsubscribe(this->mqtt_client, msg->topic);

	ESP_LOGI(TAG, "unsubscribing to %s", msg->topic);

	return ESP_OK;
}

void mqtt_connect(char *serverURL)
{
	Mqtt_Msg_t _msg;
	_msg.action = MQTT_ACTION_CONNECT;
	sprintf(_msg.serverURL, "%s", serverURL);

	_xMsgBufSend(&_msg);
}

void mqtt_disconnect(void)
{
	Mqtt_Msg_t _msg;
	_msg.action = MQTT_ACTION_DISCONNECT;

	_xMsgBufSend(&_msg);
}

void mqtt_publish(char *t, char *p, int q, int r)
{
	Mqtt_Msg_t _msg;
	_msg.action = MQTT_ACTION_PUB;
	sprintf(_msg.topic, "%s", t);
	sprintf(_msg.payload, "%s", p);
	_msg.qos = q;
	_msg.retained = r;

	_xMsgBufSend(&_msg);
}

void mqtt_subscribe(char *t, int q)
{
	Mqtt_Msg_t _msg;
	_msg.action = MQTT_ACTION_SUB;
	sprintf(_msg.topic, "%s", t);
	// sprintf(_msg.payload, "%s", p);
	_msg.qos = q;
	// _msg.retained = r;

	_xMsgBufSend(&_msg);
}

void mqtt_unsubscribe(char *t)
{
	Mqtt_Msg_t _msg;
	_msg.action = MQTT_ACTION_UNSUB;
	sprintf(_msg.topic, "%s", t);
	// sprintf(_msg.payload, "%s", p);
	// _msg.qos = q;
	// _msg.retained = r;

	_xMsgBufSend(&_msg);
}

static void _mqtt_task(void *arg)
{
	while (1)
	{
		if(xMessageBufferIsEmpty(this->xBufHandle))
		{
			//depends on the network speed
			vTaskDelay(pdMS_TO_TICKS(20));
			continue;
		}

		Mqtt_Msg_t* msg = malloc(sizeof(Mqtt_Msg_t));
		if (_xMsgBufReceive(msg))
		{
			if (msg->action == MQTT_ACTION_CONNECT)
			{
				_mqtt_make_connection(msg->serverURL);
			}
			else if (msg->action == MQTT_ACTION_DISCONNECT)
			{
				_mqtt_make_disconnection();
			}
			else if (msg->action == MQTT_ACTION_PUB)
			{
				_mqtt_make_publish(msg);
			}
			else if (msg->action == MQTT_ACTION_SUB)
			{
				_mqtt_make_subscribe(msg);
			}
			else if (msg->action == MQTT_ACTION_UNSUB)
			{
				_mqtt_make_unsubscribe(msg);
			}
		}
		if(msg)
		{
			// ESP_LOGI(TAG, "msg id:%d free", msg->action);
			free(msg);
		}
	}
	this->taskHandle = NULL;
	vTaskDelete(NULL);
}

esp_err_t mqtt_task_init(void)
{
	if (this->xBufHandle != NULL)
	{
		ESP_LOGI(TAG, "xMsgBuf already init");
		return ESP_FAIL;
	}
	this->xBufHandle = xMessageBufferCreate(MQTT_MSG_BUFFER_TOTAL);
	if (this->xBufHandle == NULL)
	{
		ESP_LOGI(TAG, "xMsgBuf init failed");
		return ESP_FAIL;
	}
	/*  --------------------------------------------------------------------- */
	if (this->taskHandle != NULL)
	{
		ESP_LOGI(TAG, "Task already init");
		return ESP_FAIL;
	}
	xTaskCreate(_mqtt_task, TAG, MQTT_TASK_SIZE, NULL, 9, &(this->taskHandle));
	if (this->taskHandle == NULL)
	{
		ESP_LOGI(TAG, "Task init failed");
		return ESP_FAIL;
	}
	/*  --------------------------------------------------------------------- */

	ESP_LOGI(TAG, "Task started");
	return ESP_OK;
}

esp_err_t mqtt_task_deinit(void)
{
	if (this->taskHandle == NULL)
	{
		ESP_LOGI(TAG, "Task is NOT init");
		return ESP_FAIL;
	}
	vTaskDelete(this->taskHandle);
	this->taskHandle = NULL;
	/*  --------------------------------------------------------------------- */
	if (this->xBufHandle == NULL)
	{
		ESP_LOGI(TAG, "xMsgBuf is NOT init");
		return ESP_FAIL;
	}
	vMessageBufferDelete(this->xBufHandle);
	this->xBufHandle = NULL;
	/*  --------------------------------------------------------------------- */

	ESP_LOGI(TAG, "Task removed");
	return ESP_OK;
}
