/* BSD Socket API Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>
#include <sys/param.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_check.h"
#include "driver/gpio.h"
#include "driver/uart.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>
#include <stdbool.h>

#define KEEPALIVE_IDLE              CONFIG_EXAMPLE_KEEPALIVE_IDLE
#define KEEPALIVE_INTERVAL          CONFIG_EXAMPLE_KEEPALIVE_INTERVAL
#define KEEPALIVE_COUNT             CONFIG_EXAMPLE_KEEPALIVE_COUNT

static const char *TAG = "bridge_eth";

struct server_port;
typedef void (*sock_handler_t)(int, struct server_port*);

#define BUFF_SZ 4096

struct server_port {
    uint16_t       port;
    sock_handler_t handler;
    uart_port_t    uart;
    char           buff[BUFF_SZ];
};

static void do_echo(int sock, struct server_port* srv)
{
    for (;;)
    {
        int len = recv(sock, srv->buff, BUFF_SZ, 0);
        if (len < 0) {
            ESP_LOGE(TAG, "Error occurred during receiving: errno %d", errno);
            return;
        } else if (len == 0) {
            ESP_LOGW(TAG, "Connection closed");
            return;
        } else {
            // send() can return less bytes than supplied length.
            // Walk-around for robust implementation.
            char* ptr = srv->buff;
            while (len) {
                int const written = send(sock, ptr, len, 0);
                if (written < 0) {
                    ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
                    // Failed to retransmit, giving up
                    return;
                }
                len -= written;
                ptr += written;
            }
        }
    }
}

static void do_bridge(int sock, struct server_port* srv)
{
    ESP_ERROR_CHECK(fcntl(sock, F_SETFL, fcntl(sock, F_GETFL, 0) | O_NONBLOCK));
    gpio_set_level(CONFIG_BRIDGE_LED_GPIO, 1);
    for (;;) {
        bool idle = true;
        // Read UART
        for (;;) {
            int size = uart_read_bytes(srv->uart, srv->buff, BUFF_SZ, idle ? 0 : 1);
            if (size < 0) {
                ESP_LOGE(TAG, "Uart read failed");
                return;
            }
            if (!size)
                break;

            ESP_LOGI(TAG, "UART -> Eth  %d bytes", size);
            char* ptr = srv->buff;
            while (size > 0) {
                int const written = send(sock, ptr, size, 0);
                if (written < 0) {
                    ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
                    break;
                }
                size -= written;
                ptr  += written;
            }
            idle = false;
        }
        // Read Eth
        int const rx_len = recv(sock, srv->buff, BUFF_SZ, 0);
        if (rx_len < 0) {
            if (errno != EWOULDBLOCK) {
                ESP_LOGE(TAG, "Error occurred during receiving: errno %d", errno);
                break;
            }
        } else if (rx_len == 0) {
            ESP_LOGW(TAG, "Connection closed");
            break;
        } else {
            ESP_LOGI(TAG, "Eth  -> UART %d bytes", rx_len);
            uart_write_bytes(srv->uart, srv->buff, rx_len);
            idle = false;
        }
        if (idle)
            vTaskDelay(1);
    }
    for (;;) {
        int const left = uart_read_bytes(srv->uart, srv->buff, BUFF_SZ, 8);
        if (left <= 0)
            break;
    }
    gpio_set_level(CONFIG_BRIDGE_LED_GPIO, 0);
}

static void tcp_server_task(void *pvParameters)
{
    char addr_str[128];
    struct server_port* srv = pvParameters;
    int ip_protocol = 0;
    int keepAlive = 1;
    int keepIdle = KEEPALIVE_IDLE;
    int keepInterval = KEEPALIVE_INTERVAL;
    int keepCount = KEEPALIVE_COUNT;
    struct sockaddr_storage dest_addr;

    struct sockaddr_in *dest_addr_ip4 = (struct sockaddr_in *)&dest_addr;
    dest_addr_ip4->sin_addr.s_addr = htonl(INADDR_ANY);
    dest_addr_ip4->sin_family = AF_INET;
    dest_addr_ip4->sin_port = htons(srv->port);
    ip_protocol = IPPROTO_IP;

    int listen_sock = socket(AF_INET, SOCK_STREAM, ip_protocol);
    if (listen_sock < 0) {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        vTaskDelete(NULL);
        return;
    }
    int opt = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    ESP_LOGI(TAG, "Socket created");

    int err = bind(listen_sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err != 0) {
        ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
        goto CLEAN_UP;
    }
    ESP_LOGI(TAG, "Socket bound, port %d", srv->port);

    err = listen(listen_sock, 1);
    if (err != 0) {
        ESP_LOGE(TAG, "Error occurred during listen: errno %d", errno);
        goto CLEAN_UP;
    }

    while (1) {

        ESP_LOGI(TAG, "Socket listening");

        struct sockaddr_storage source_addr; // Large enough for both IPv4 or IPv6
        socklen_t addr_len = sizeof(source_addr);
        int sock = accept(listen_sock, (struct sockaddr *)&source_addr, &addr_len);
        if (sock < 0) {
            ESP_LOGE(TAG, "Unable to accept connection: errno %d", errno);
            break;
        }

        // Set tcp keepalive option
        setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &keepAlive, sizeof(int));
        setsockopt(sock, IPPROTO_TCP, TCP_KEEPIDLE, &keepIdle, sizeof(int));
        setsockopt(sock, IPPROTO_TCP, TCP_KEEPINTVL, &keepInterval, sizeof(int));
        setsockopt(sock, IPPROTO_TCP, TCP_KEEPCNT, &keepCount, sizeof(int));
        // Convert ip address to string
        if (source_addr.ss_family == PF_INET) {
            inet_ntoa_r(((struct sockaddr_in *)&source_addr)->sin_addr, addr_str, sizeof(addr_str) - 1);
        }
        ESP_LOGI(TAG, "Socket accepted ip address: %s", addr_str);

        (srv->handler)(sock, srv);

        shutdown(sock, 0);
        close(sock);
    }

CLEAN_UP:
    close(listen_sock);
    vTaskDelete(NULL);
}

#ifdef CONFIG_UART_CTS_EN
#define UART_FLOWCTRL UART_HW_FLOWCTRL_CTS_RTS
#define UART_CTS_GPIO CONFIG_UART_CTS_GPIO
#else
#define UART_FLOWCTRL UART_HW_FLOWCTRL_RTS
#define UART_CTS_GPIO UART_PIN_NO_CHANGE
#endif

#define UART_TX_GPIO  CONFIG_UART_TX_GPIO
#define UART_RX_GPIO  CONFIG_UART_RX_GPIO
#define UART_RTS_GPIO CONFIG_UART_RTS_GPIO
#define UART_BITRATE  CONFIG_UART_BITRATE

#define UART_RX_BUF_SZ (1024 * CONFIG_UART_RX_BUFF_SIZE)
#define UART_TX_BUF_SZ (1024 * CONFIG_UART_TX_BUFF_SIZE)

static esp_err_t bridge_uart_init(void)
{
    /* Configure UART */
    uart_config_t uart_config = {
        .baud_rate = CONFIG_UART_BITRATE,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_FLOWCTRL,
        .rx_flow_ctrl_thresh = UART_HW_FIFO_LEN(UART_NUM_1) - 16
    };

    ESP_RETURN_ON_ERROR(uart_param_config(UART_NUM_1, &uart_config), TAG, "uart_param_config failed");
    ESP_RETURN_ON_ERROR(uart_set_pin(UART_NUM_1, UART_TX_GPIO, UART_RX_GPIO, UART_RTS_GPIO, UART_CTS_GPIO), TAG, "uart_set_pin failed");
    ESP_RETURN_ON_ERROR(uart_driver_install(UART_NUM_1, UART_RX_BUF_SZ, UART_TX_BUF_SZ, 0, NULL, 0), TAG, "uart_driver_install failed");

    gpio_set_level(CONFIG_BRIDGE_LED_GPIO, 0);
    gpio_set_direction(CONFIG_BRIDGE_LED_GPIO, GPIO_MODE_OUTPUT);

    return ESP_OK;
}

static struct server_port echo_server   = {.port = CONFIG_ECHO_PORT,   .handler = do_echo};
static struct server_port bridge_server = {.port = CONFIG_BRIDGE_PORT, .handler = do_bridge, .uart = UART_NUM_1};

void tcp_server_create(void)
{
    ESP_ERROR_CHECK(bridge_uart_init());
    xTaskCreate(tcp_server_task, "echo_server",   4096, (void*)&echo_server,   5, NULL);
    xTaskCreate(tcp_server_task, "bridge_server", 4096, (void*)&bridge_server, 5, NULL);
}
