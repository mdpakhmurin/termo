#include <stdio.h>
#include "mqtt_client.h"
#include "nvs.h"
#include "mqtt.h"

static esp_mqtt_client_handle_t client = NULL;
static volatile bool connected = false;

// Номер последнего сообщения, получение которого подтвердил брокер
static volatile int delivered_message_id = -1;

static void on_mqtt_event(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_id == MQTT_EVENT_PUBLISHED) {
        esp_mqtt_event_handle_t event = event_data;
        delivered_message_id = event->msg_id;
        printf("MQTT: брокер подтвердил сообщение %d\n", event->msg_id);
    }
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

void mqtt_start(const mqtt_settings_t *settings)
{
    esp_mqtt_client_config_t config = {
        .broker.address.uri = settings->uri,
        .credentials.username = settings->username,
        .credentials.authentication.password = settings->password,
    };

    client = esp_mqtt_client_init(&config);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, on_mqtt_event, NULL);
    esp_mqtt_client_start(client);
}

bool mqtt_save_settings(const mqtt_settings_t *settings)
{
    nvs_handle_t nvs;
    if (nvs_open("mqtt", NVS_READWRITE, &nvs) != ESP_OK) {
        return false;
    }

    esp_err_t err = nvs_set_str(nvs, "uri", settings->uri);
    if (err == ESP_OK) {
        err = nvs_set_str(nvs, "username", settings->username);
    }
    if (err == ESP_OK) {
        err = nvs_set_str(nvs, "password", settings->password);
    }
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);

    return err == ESP_OK;
}

bool mqtt_load_settings(mqtt_settings_t *settings)
{
    nvs_handle_t nvs;
    if (nvs_open("mqtt", NVS_READONLY, &nvs) != ESP_OK) {
        return false;
    }

    size_t uri_size = sizeof(settings->uri);
    size_t username_size = sizeof(settings->username);
    size_t password_size = sizeof(settings->password);
    esp_err_t err = nvs_get_str(nvs, "uri", settings->uri, &uri_size);
    if (err == ESP_OK) {
        err = nvs_get_str(nvs, "username", settings->username, &username_size);
    }
    if (err == ESP_OK) {
        err = nvs_get_str(nvs, "password", settings->password, &password_size);
    }
    nvs_close(nvs);

    return err == ESP_OK;
}

bool mqtt_is_connected(void)
{
    return connected;
}

int mqtt_publish(const char *topic, const char *value)
{
    if (!connected) {
        return -1;
    }

    return esp_mqtt_client_publish(client, topic, value, 0, 1, 1);
}

bool mqtt_is_delivered(int message_id)
{
    return message_id >= 0 && delivered_message_id == message_id;
}

void mqtt_stop(void)
{
    if (client != NULL) {
        esp_mqtt_client_stop(client);
    }
}
