#pragma once

#include <stdbool.h>
#include <stddef.h>

#define WIFI_SSID_SIZE     33
#define WIFI_PASSWORD_SIZE 64 

typedef struct {
    char ssid[WIFI_SSID_SIZE];             // имя сети
    char password[WIFI_PASSWORD_SIZE];     // пароль WPA2
} wifi_credentials_t;

// Инициалищация Wi-Fi
void wifi_init(void);

// Подключение к роутеру
void wifi_connect(const wifi_credentials_t *credentials);

// Точка доступа - раздача сети 
void wifi_start_access_point(void);

// Плата в режиме настройки (раздаёт сеть)
bool wifi_is_setup_mode(void);

// Выключить Wi-Fi
void wifi_stop(void);

// Есть ли сейчас подключение к Wi-Fi
bool wifi_is_connected(void);

// Сохранить имя сети и пароль (NVS)
bool wifi_save_credentials(const wifi_credentials_t *credentials);

// Прочитать сохранённую сеть. false — если ничего не найдено
bool wifi_load_credentials(wifi_credentials_t *credentials);

// Стереть сохранённую сеть
void wifi_clear_credentials(void);

// Отметить, чтобы следующая загрузка была в режиме настройки
void wifi_request_setup_mode(void);

// Посмотреть отметку, требуется ли загрузка в режиме настройки
bool wifi_take_setup_request(void);

// Сеть, найденная при сканировании
typedef struct {
    char ssid[WIFI_SSID_SIZE];   // имя
    int rssi;        // сила сигнала в dBm: -40 отлично, -80 плохо
    bool secured;    // есть ли пароль
} wifi_network_t;

// Поиск сетей wifi
int wifi_scan(wifi_network_t *networks, int max_networks);
