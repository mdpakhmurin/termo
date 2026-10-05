#include <stdio.h>
#include "mqtt_client.h"
#include "mqtt.h"

static esp_mqtt_client_handle_t client = NULL;
static volatile bool connected = false;

static void on_mqtt_event(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_id == MQTT_EVENT_CONNECTED) {
        connected = true;
        printf("MQTT: подключились к брокеру\n");
    }
    if (event_id == MQTT_EVENT_DISCONNECTED) {
        connected = false;
        printf("MQTT: отключились от брокера\n");
    }
    if (event_id == MQTT_EVENT_ERROR) {
        printf("MQTT: ошибка соединения\n");
    }
}

void mqtt_start(const char *uri, const char *username, const char *password)
{
    esp_mqtt_client_config_t config = {
        .broker.address.uri = uri,
        .credentials.username = username,
        .credentials.authentication.password = password,
    };

    client = esp_mqtt_client_init(&config);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, on_mqtt_event, NULL);
    esp_mqtt_client_start(client);
}

bool mqtt_publish(const char *topic, const char *value)
{
    if (!connected) {
        return false;
    }

    // QoS 1 — брокер подтвердит получение; retain 1 — брокер запомнит последнее значение
    int message_id = esp_mqtt_client_publish(client, topic, value, 0, 1, 1);
    return message_id >= 0;
}
