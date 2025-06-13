#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/pbuf.h"
#include "lwip/altcp_tcp.h"
#include "lwip/tcp.h"
#include "hardware/uart.h"
#include "hardware/irq.h"
#include "hardware/rtc.h"
#include "pico/time.h"
#include "time.h"
#include "setupWifi.h"
#include "setupUart.h"

// Memory monitoring includes
extern char __StackLimit, __bss_end__;

#define TCP_BUF_SIZE 2048
#define TCP_SEND_INTERVAL_MS 20
#define TCP_CONNECTION_TIMEOUT_MS (2 * 60 * 1000) // 2 minutes
static struct altcp_pcb *current_connection = NULL;
static absolute_time_t last_tcp_send_time;
static absolute_time_t last_tcp_activity_time;

#define UART1_ID uart1
#define UART_RX_BUFFER_SIZE 1024
static uint8_t uart_rx_buffer[UART_RX_BUFFER_SIZE];
static volatile size_t uart_rx_buffer_head = 0;
static volatile size_t uart_rx_buffer_tail = 0;
static volatile bool uart_rx_buffer_overflow = false;
static volatile bool uart_data_ready = false;
void process_uart_data(void);

// Memory monitoring functions
size_t get_free_heap(void)
{
    extern char __bss_end__;
    extern char __StackLimit;

    char *heap_end = (char *)malloc(1);
    if (heap_end)
    {
        free(heap_end);
        return (size_t)(&__StackLimit - heap_end);
    }
    return 0;
}

size_t get_stack_usage(void)
{
    extern char __StackLimit;
    char stack_var;
    return (size_t)(&stack_var - &__StackLimit);
}

void print_memory_stats(void)
{
    static uint32_t last_report_time = 0;
    uint32_t now = to_ms_since_boot(get_absolute_time());

    // Report every second
    if (now - last_report_time > 1000)
    {
        printf("Memory Stats - Free Heap: %zu bytes, Stack Used: %zu bytes\n",
               get_free_heap(), get_stack_usage());
        last_report_time = now;
    }
}

void send200Ok(struct altcp_pcb *pcb)
{
    err_t err;
    char response[] = "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n";

    err = altcp_write(pcb, response, strlen(response), 0);
    err = altcp_output(pcb);
}

err_t recv(void *arg, struct altcp_pcb *pcb, struct pbuf *p, err_t err)
{
    char myBuff[TCP_BUF_SIZE];
    if (p != NULL)
    {
        last_tcp_activity_time = get_absolute_time();
        pbuf_copy_partial(p, myBuff, p->tot_len, 0);
        myBuff[p->tot_len] = 0;
        printf("TCP->UART: Sent %d bytes\n", p->tot_len);
        for (int i = 0; i < p->tot_len; ++i)
        {
            uart_putc_raw(UART1_ID, myBuff[i]);
        }
        altcp_recved(pcb, p->tot_len);
        pbuf_free(p);
    }
    else
    {
        ip_addr_t *remote_ip = altcp_get_ip(pcb, 0);
        u16_t remote_port = altcp_get_port(pcb, 0);

        if (remote_ip != NULL)
        {
            printf("Client %s:%d disconnected\n",
                   ip4addr_ntoa(ip_2_ip4(remote_ip)), remote_port);
        }
        else
        {
            printf("Client disconnected\n");
        }

        // NULL pbuf indicates connection closed by the client
        if (pcb == current_connection)
        {
            current_connection = NULL;
        }
        altcp_close(pcb);
    }
    return ERR_OK;
}

static err_t sent(void *arg, struct altcp_pcb *pcb, u16_t len)
{
    return ERR_OK;
}

static err_t accept(void *arg, struct altcp_pcb *pcb, err_t err)
{
    if (current_connection != NULL)
    {
        printf("Closing existing connection to accept new one\n");
        altcp_close(current_connection);
        current_connection = NULL;
    }

    ip_addr_t *remote_ip = altcp_get_ip(pcb, 0);
    u16_t remote_port = altcp_get_port(pcb, 0);

    if (remote_ip != NULL)
    {
        printf("Client %s:%d connected\n",
               ip4addr_ntoa(ip_2_ip4(remote_ip)), remote_port);
    }
    else
    {
        printf("Client connected\n");
    }

    altcp_recv(pcb, recv);
    altcp_sent(pcb, sent);
    current_connection = pcb;
    last_tcp_activity_time = get_absolute_time();

    return ERR_OK;
}

static inline bool uart_rx_buffer_push(uint8_t c)
{
    size_t next_head = (uart_rx_buffer_head + 1) % UART_RX_BUFFER_SIZE;

    if (next_head != uart_rx_buffer_tail)
    {
        uart_rx_buffer[uart_rx_buffer_head] = c;
        uart_rx_buffer_head = next_head;
        return true;
    }
    else
    {
        uart_rx_buffer_overflow = true;
        return false; // Buffer full
    }
}

void on_uart_rx()
{
    while (uart_is_readable(UART1_ID))
    {
        uint8_t ch = uart_getc(UART1_ID);
        if (!uart_rx_buffer_push(ch))
        {
            // Buffer is full
            break;
        }
        uart_data_ready = true;
    }
}

void process_uart_data(void)
{
    if (!uart_data_ready || current_connection == NULL)
    {
        return;
    }

    // Only send if we're past the minimum interval or if we have a lot of data
    if (absolute_time_diff_us(last_tcp_send_time, get_absolute_time()) < TCP_SEND_INTERVAL_MS * 1000 &&
        ((uart_rx_buffer_head - uart_rx_buffer_tail + UART_RX_BUFFER_SIZE) % UART_RX_BUFFER_SIZE) < UART_RX_BUFFER_SIZE / 2)
    {
        return;
    }

    // Calculate how many bytes we have to send
    size_t bytes_available = (uart_rx_buffer_head - uart_rx_buffer_tail + UART_RX_BUFFER_SIZE) % UART_RX_BUFFER_SIZE;
    if (bytes_available == 0)
    {
        uart_data_ready = false;
        return;
    }

    // Prepare data to send
    uint8_t send_buffer[UART_RX_BUFFER_SIZE];
    size_t send_size = 0;

    // Copy data from circular buffer to linear buffer for sending
    while (uart_rx_buffer_tail != uart_rx_buffer_head && send_size < UART_RX_BUFFER_SIZE)
    {
        send_buffer[send_size++] = uart_rx_buffer[uart_rx_buffer_tail];
        uart_rx_buffer_tail = (uart_rx_buffer_tail + 1) % UART_RX_BUFFER_SIZE;
    }

    // Send the data over TCP
    if (send_size > 0)
    {
        err_t err = altcp_write(current_connection, send_buffer, send_size, TCP_WRITE_FLAG_COPY);
        if (err == ERR_OK)
        {
            altcp_output(current_connection); // Flush the data
            printf("UART->TCP: Sent %d bytes\n", send_size);
        }
        else
        {
            printf("Failed to send data to TCP connection: %d\n", err);
        }

        // Reset overflow flag if we managed to send data
        uart_rx_buffer_overflow = false;
    }

    last_tcp_send_time = get_absolute_time();

    // Reset data ready flag if buffer is empty
    if (uart_rx_buffer_head == uart_rx_buffer_tail)
    {
        uart_data_ready = false;
    }
}

void check_connection_health(void)
{
    if (current_connection == NULL)
        return;

    if (absolute_time_diff_us(last_tcp_activity_time, get_absolute_time()) > TCP_CONNECTION_TIMEOUT_MS * 1000)
    {
        ip_addr_t *remote_ip = altcp_get_ip(current_connection, 0);
        u16_t remote_port = altcp_get_port(current_connection, 0);
        if (remote_ip)
        {
            printf("Client %s:%d connection closed due to inactivity\n",
                   ip4addr_ntoa(ip_2_ip4(remote_ip)), remote_port);
        }
        else
        {
            printf("Closing stale connection due to inactivity\n");
        }
        altcp_close(current_connection);
        current_connection = NULL;
    }
}

int main()
{
    stdio_init_all();
    connect(WIFI_SSID, WIFI_PASSWORD);
    struct altcp_pcb *pcb = altcp_new(NULL);
    altcp_accept(pcb, accept);

    altcp_bind(pcb, IP_ADDR_ANY, 8080);
    cyw43_arch_lwip_begin();
    pcb = altcp_listen_with_backlog(pcb, 3);
    cyw43_arch_lwip_end();
    last_tcp_send_time = get_absolute_time();

    setupUart(UART1_ID, on_uart_rx);
    printf("UART RX Buffer Size: %d bytes\n", UART_RX_BUFFER_SIZE);

    print_memory_stats();
    printf("Ready to accept connections on port 8080...\n");

    while (true)
    {
        process_uart_data();
        check_connection_health();

        if (uart_rx_buffer_overflow)
        {
            printf("WARNING: UART RX buffer overflow detected\n");
        }

        sleep_ms(5);
    }
}