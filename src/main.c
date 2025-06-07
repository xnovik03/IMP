#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "driver/gpio.h"
#include "my_data.h"
#include "esp_http_server.h"
#include "../components/ssd1306.h"
#include "../components/font8x8_basic.h"
#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"
#include "esp_log.h"
#include "mqtt_client.h"

static const char *TAG = "MQTT_TCP";
// Definice GPIO pro SPI
#define CONFIG_SPI_INTERFACE 1
#define CONFIG_MOSI_GPIO 23
#define CONFIG_SCLK_GPIO 18
#define CONFIG_CS_GPIO 5
#define CONFIG_DC_GPIO 27
#define CONFIG_RESET_GPIO 17

#define BUTTON_SELECT_GPIO 34 
#define BUTTON_OK_GPIO 35     

#define ROOM_COUNT 4
#define TEMP_MIN 15
#define TEMP_MAX 30
static esp_mqtt_client_handle_t client; // Globální proměnná pro MQTT klienta

SSD1306_t dev;


typedef struct {
    bool light_on;    
    int temperature;   
} Room;

Room rooms[ROOM_COUNT]; 
int current_room = 0;   
int current_option = 0; 
int menu_level = 0;     // Úroveň menu (0: výběr pokoje, 1: výběr možnosti, 2: nastavení)

typedef enum {
    STATE_MAIN_MENU,
    STATE_VIEW_STATUS,
    STATE_CHANGE_MENU,
    STATE_ROOM_SETTINGS,
    STATE_ADJUST_LIGHT,
    STATE_ADJUST_TEMPERATURE
} AppState;

AppState current_state = STATE_MAIN_MENU;

// Publikování stavu pokoje na MQTT server
void send_room_state_to_server(int room_number) {
    char topic[50];
    char message[100];

    // Formátování tématu a zprávy
    snprintf(topic, sizeof(topic), "home/room/%d", room_number + 1);
    snprintf(message, sizeof(message), "{\"light\":%d,\"temperature\":%d}",
             rooms[room_number].light_on, rooms[room_number].temperature);

    // Publikování zprávy jako retained
    esp_mqtt_client_publish(client, topic, message, 0, 1, 1); // Poslední parametr '1' nastaví zprávu jako retained
    ESP_LOGI(TAG, "Sent state to server: %s -> %s", topic, message);
}


// Přihlášení k odběru stavu pokoje na MQTT serveru
void fetch_room_state_from_server(int room_number) {
    char topic[50];

    // Formátování tématu
    snprintf(topic, sizeof(topic), "home/room/%d", room_number + 1);

    // Přihlášení k odběru tématu
    esp_mqtt_client_subscribe(client, topic, 0);
    ESP_LOGI(TAG, "Subscribed to topic: %s", topic);
}

// Aktualizace zobrazení na OLED displeji
void update_display() {
    ssd1306_clear_screen(&dev, false);
    char buffer[32];

    switch (current_state) {
        case STATE_MAIN_MENU:  // Hlavní menu
            ssd1306_display_text(&dev, 0, "1. Uvidet stav", 14, current_option == 0);
            ssd1306_display_text(&dev, 1, "2. Provest zmeny", 16, current_option == 1);
            break;

        case STATE_VIEW_STATUS:  // Zobrazení stavu všech pokojů
            for (int i = 0; i < ROOM_COUNT; i++) {
                snprintf(buffer, sizeof(buffer), "Room %d: %s, %dC",
                         i + 1,
                         rooms[i].light_on ? "ON" : "OFF",
                         rooms[i].temperature);
                ssd1306_display_text(&dev, i, buffer, strlen(buffer), false);
            }
            ssd1306_display_text(&dev, ROOM_COUNT, "Zpet", 4, true);
            break;

        case STATE_CHANGE_MENU:   // Výběr pokoje pro změny
            for (int i = 0; i < ROOM_COUNT; i++) {
                snprintf(buffer, sizeof(buffer), "%s Room %d", (i == current_room ? "->" : "  "), i + 1);
                ssd1306_display_text(&dev, i, buffer, strlen(buffer), (i == current_room));
            }
            ssd1306_display_text(&dev, ROOM_COUNT, "Zpet", 4, current_room == ROOM_COUNT);
            break;

        case STATE_ROOM_SETTINGS:  // Nastavení pokoje
            snprintf(buffer, sizeof(buffer), "Room %d", current_room + 1);
            ssd1306_display_text(&dev, 0, buffer, strlen(buffer), false);
            ssd1306_display_text(&dev, 1, "1. Svetlo", 10, current_option == 0);
            ssd1306_display_text(&dev, 2, "2. Teplota", 11, current_option == 1);
            ssd1306_display_text(&dev, 3, "3. Zpet", 7, current_option == 2);
            break;

        case STATE_ADJUST_LIGHT:  // Nastavení světla
            snprintf(buffer, sizeof(buffer), "Svetlo: %s", rooms[current_room].light_on ? "ON" : "OFF");
            ssd1306_display_text(&dev, 0, buffer, strlen(buffer), false);
            ssd1306_display_text(&dev, 2, "Right button:OK", 20, false);
            break;

        case STATE_ADJUST_TEMPERATURE:  // Nastavení teploty
            snprintf(buffer, sizeof(buffer), "Teplota: %dC", rooms[current_room].temperature);
            ssd1306_display_text(&dev, 0, buffer, strlen(buffer), false);
            ssd1306_display_text(&dev, 2, "Left button: +", 15, false);
            ssd1306_display_text(&dev, 3, "Right button: OK", 16, false);
            break;
    }
       // Odeslání zobrazeného obsahu na displej
    ssd1306_show_buffer(&dev);
}

// Obsluha tlačítek pro navigaci v menu
void handle_buttons() {
    static int last_select_state = 1;
    static int last_ok_state = 1;

    int select_state = gpio_get_level(BUTTON_SELECT_GPIO);
    int ok_state = gpio_get_level(BUTTON_OK_GPIO);

    if (select_state == 0 && last_select_state == 1) {
        switch (current_state) {
            case STATE_MAIN_MENU:
                current_option = (current_option + 1) % 2; // Přepínání mezi "Uvidet stav" a "Provest zmeny"
                break;

            case STATE_VIEW_STATUS:
                // Přepínání mezi pokoji a možností "Zpět"
                current_room = (current_room + 1) % (ROOM_COUNT + 1);
                break;

            case STATE_CHANGE_MENU:
                current_room = (current_room + 1) % (ROOM_COUNT + 1);
                break;

            case STATE_ROOM_SETTINGS:
                current_option = (current_option + 1) % 3; // Přepínání mezi "Svetlo", "Teplota" a "Zpet"
                break;

            case STATE_ADJUST_LIGHT:
                break;

            case STATE_ADJUST_TEMPERATURE:
                // Změna teploty "+" při krátkém stisku
                rooms[current_room].temperature++;
                if (rooms[current_room].temperature > TEMP_MAX) {
                    rooms[current_room].temperature = TEMP_MIN;
                }
                send_room_state_to_server(current_room);
                break;
        }
        update_display();
        vTaskDelay(200 / portTICK_PERIOD_MS);
    }
    last_select_state = select_state;

    if (ok_state == 0 && last_ok_state == 1) {
        switch (current_state) {
            case STATE_MAIN_MENU:
                if (current_option == 0) {
                    current_state = STATE_VIEW_STATUS;
                } else {
                    current_state = STATE_CHANGE_MENU;
                }
                break;

            case STATE_VIEW_STATUS:
                if (current_room == ROOM_COUNT) {
                    current_state = STATE_MAIN_MENU; // Návrat na hlavní obrazovku
                }
                break;

            case STATE_CHANGE_MENU:
                if (current_room == ROOM_COUNT) {
                    current_state = STATE_MAIN_MENU; // Návrat na hlavní obrazovku
                } else {
                    current_state = STATE_ROOM_SETTINGS;
                }
                break;

            case STATE_ROOM_SETTINGS:
                if (current_option == 2) {
                    current_state = STATE_CHANGE_MENU; // Návrat na seznam pokojů
                } else {
                    current_state = (current_option == 0) ? STATE_ADJUST_LIGHT : STATE_ADJUST_TEMPERATURE;
                }
                break;

            case STATE_ADJUST_LIGHT:
                rooms[current_room].light_on = !rooms[current_room].light_on;
                send_room_state_to_server(current_room);
                current_state = STATE_ROOM_SETTINGS;
                break;

            case STATE_ADJUST_TEMPERATURE:
                current_state = STATE_ROOM_SETTINGS;
                break;
        }
        update_display();
        vTaskDelay(200 / portTICK_PERIOD_MS);
    }
    last_ok_state = ok_state;
}

// Inicializace všech pokojů s výchozími hodnotami
void init_rooms() {
    for (int i = 0; i < ROOM_COUNT; i++) {
        rooms[i].light_on = false; // Výchozí hodnota
        rooms[i].temperature = TEMP_MIN; // Výchozí hodnota
    }
    ESP_LOGI(TAG, "Rooms initialized with default values.");
}


// Iniciace tlačítek
void init_buttons() {
    gpio_config_t button_conf = {
        .pin_bit_mask = (1ULL << BUTTON_SELECT_GPIO) | (1ULL << BUTTON_OK_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,  // Povolit pull-up rezistory
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&button_conf);
}


// Zpracování příchozích MQTT dat pro pokoj
void handle_mqtt_data(const char *topic, const char *data) {
    int room_number = -1;
    int light_on = 0;
    int temperature = 0;

    if (sscanf(topic, "home/room/%d", &room_number) == 1) {
        if (room_number >= 1 && room_number <= ROOM_COUNT) {
            room_number--; // Převod na index pole

            // Parsování dat
            if (sscanf(data, "{\"light\":%d,\"temperature\":%d}", &light_on, &temperature) == 2) {
                rooms[room_number].light_on = light_on;
                rooms[room_number].temperature = temperature;
                ESP_LOGI(TAG, "Room %d updated: Light=%d, Temperature=%d", room_number + 1, light_on, temperature);

                // Aktualizace displeje
                if (current_room == room_number) {
                    update_display();
                }
            } else {
                ESP_LOGW(TAG, "Invalid data format: %s", data);
            }
        } else {
            ESP_LOGW(TAG, "Invalid room number in topic: %d", room_number);
        }
    } else {
        ESP_LOGW(TAG, "Invalid topic format: %s", topic);
    }
}

// Zpracování událostí Wi-Fi
static void wifi_event_handler(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    switch (event_id)
    {
    case WIFI_EVENT_STA_START:
        printf("WiFi connecting WIFI_EVENT_STA_START ... \n");
        break;
    case WIFI_EVENT_STA_CONNECTED:
        printf("WiFi connected WIFI_EVENT_STA_CONNECTED ... \n");
        break;
    case WIFI_EVENT_STA_DISCONNECTED:
        printf("WiFi lost connection WIFI_EVENT_STA_DISCONNECTED ... \n");
        break;
    case IP_EVENT_STA_GOT_IP:
        printf("WiFi got IP ... \n\n");
        break;
    default:
        break;
    }
}

// Inicializace a připojení Wi-Fi
void wifi_connection()
{
    // 1 - Wi-Fi/LwIP Init Phase
    esp_netif_init();                    // TCP/IP initiation           s1.1
    esp_event_loop_create_default();     // event loop                       s1.2
    esp_netif_create_default_wifi_sta(); // WiFi station                       s1.3
    wifi_init_config_t wifi_initiation = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&wifi_initiation); //                               s1.4
    // 2 - Wi-Fi Configuration Phase
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL);
    wifi_config_t wifi_configuration = {
        .sta = {
            .ssid = SSID,
            .password = PASS}};
    esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_configuration);
    // 3 - Wi-Fi Start Phase
    esp_wifi_start();
    // 4- Wi-Fi Connect Phase
    esp_wifi_connect();
}

// Zpracování MQTT událostí
static esp_err_t mqtt_event_handler_cb(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = event_data;

    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
            for (int i = 0; i < ROOM_COUNT; i++) {
                fetch_room_state_from_server(i); // Přihlášení k odběru všech pokojů
            }
            break;

        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "MQTT_EVENT_DATA");
            char topic[50], data[100];
            snprintf(topic, event->topic_len + 1, "%.*s", event->topic_len, event->topic);
            snprintf(data, event->data_len + 1, "%.*s", event->data_len, event->data);
            handle_mqtt_data(topic, data); // Zpracování přijatých dat
            break;

        default:
            break;
    }
    return ESP_OK;
}

// Hlavní handler MQTT událostí
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%ld", base, event_id);
    mqtt_event_handler_cb(handler_args, base, event_id, event_data);
}
static void mqtt_app_start(void) {
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = "mqtt://mqtt.eclipseprojects.io"
    };
    client = esp_mqtt_client_init(&mqtt_cfg); // Přiřadit globální proměnnou
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, client);
    esp_mqtt_client_start(client);

      // Přihlášení k odběru témat po připojení k MQTT
    for (int i = 0; i < ROOM_COUNT; i++) {
        fetch_room_state_from_server(i);
    }
}

void app_main(void) {
    nvs_flash_init();
     // Inicializace displeje
    spi_master_init(&dev, CONFIG_MOSI_GPIO, CONFIG_SCLK_GPIO, CONFIG_CS_GPIO, CONFIG_DC_GPIO, CONFIG_RESET_GPIO);
    ssd1306_init(&dev, 128, 64);
    ssd1306_clear_screen(&dev, false);

    wifi_connection();
    vTaskDelay(2000 / portTICK_PERIOD_MS);

    init_rooms();
    init_buttons();
    update_display();
    mqtt_app_start();
    current_state = STATE_MAIN_MENU;

    // Načtení stavu všech pokojů ze serveru
    for (int i = 0; i < ROOM_COUNT; i++) {
        fetch_room_state_from_server(i);
    }

    while (1) {
        handle_buttons();
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}

