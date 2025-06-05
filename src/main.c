
#include <stdio.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/pbuf.h"
#include "lwip/altcp_tcp.h"

#include "hardware/uart.h"
#include "hardware/irq.h"
#include "hardware/rtc.h"
#include "time.h"

#include "setupWifi.h"

#define BUF_SIZE 2048

#define UART1_ID uart1
#define BAUD_RATE 115200
#define DATA_BITS 8
#define STOP_BITS 1
#define PARITY UART_PARITY_NONE

#define UART1_TX_PIN 4
#define UART1_RX_PIN 5

void getDateNow(struct tm *t)
{
    datetime_t rtc;
    rtc_get_datetime(&rtc);

    t->tm_sec = rtc.sec;
    t->tm_min = rtc.min;
    t->tm_hour = rtc.hour;
    t->tm_mday = rtc.day;
    t->tm_mon = rtc.month - 1;
    t->tm_year = rtc.year - 1900;
    t->tm_wday = rtc.dotw;
    t->tm_yday = 0;
    t->tm_isdst = -1;
}

void sendData(struct altcp_pcb *pcb, char *myBuff)
{
    err_t err;
    char *html = myBuff;
    char headers[1024] = {0};
    char Status[] = "HTTP/1.1 200 OK\r\nContent-Type: text/html;charset=UTF-8\r\nServer:Picow\r\n";

    struct tm t;
    getDateNow(&t);
    char Date[100];
    strftime(Date, sizeof(Date), "Date: %a, %d %b %Y %k:%M:%S %Z\r\n", &t);

    char ContLen[100] = {0};
    snprintf(ContLen, sizeof ContLen, "Content-Length:%d \r\n", strlen(html));
    snprintf(headers, sizeof headers, "%s%s%s\r\n", Status, Date, ContLen);

    char data[2048] = {0};
    snprintf(data, sizeof data, "%s%s", headers, html);

    err = altcp_write(pcb, data, strlen(data), 0);
    err = altcp_output(pcb);
}

err_t recv(void *arg, struct altcp_pcb *pcb, struct pbuf *p, err_t err)
{
    char myBuff[BUF_SIZE];
    if (p != NULL)
    {
        // printf("recv total %d  this buffer %d next %d err %d\n", p->tot_len, p->len, p->next, err);
        pbuf_copy_partial(p, myBuff, p->tot_len, 0);
        myBuff[p->tot_len] = 0;
        // printf("Buffer= %s\n", myBuff);
        printf("%s\n", myBuff);
        altcp_recved(pcb, p->tot_len);
        pbuf_free(p);
        sendData(pcb, myBuff);
    }
    return ERR_OK;
}

static err_t sent(void *arg, struct altcp_pcb *pcb, u16_t len)
{
    // altcp_close(pcb);
}

static err_t accept(void *arg, struct altcp_pcb *pcb, err_t err)
{
    altcp_recv(pcb, recv);
    altcp_sent(pcb, sent);
    printf("connect!\n");

    return ERR_OK;
}

void setRTC()
{
    datetime_t t = {
        .year = 2023,
        .month = 02,
        .day = 03,
        .dotw = 5,
        .hour = 11,
        .min = 10,
        .sec = 00};
    rtc_init();
    rtc_set_datetime(&t);
}

// RX interrupt handler
void on_uart_rx()
{
    while (uart_is_readable(UART1_ID))
    {
        uint8_t ch = uart_getc(UART1_ID);
        // Can we send it back?
        if (uart_is_writable(UART1_ID))
        {
            uart_putc(UART1_ID, ch);
        }
    }
}

int main()
{
    stdio_init_all();
    setRTC();
    connect(WIFI_SSID, WIFI_PASSWORD);
    struct altcp_pcb *pcb = altcp_new(NULL);
    altcp_accept(pcb, accept);

    altcp_bind(pcb, IP_ADDR_ANY, 80);
    cyw43_arch_lwip_begin();
    pcb = altcp_listen_with_backlog(pcb, 3);
    cyw43_arch_lwip_end();

    uart_init(UART1_ID, BAUD_RATE);

    // Set the TX and RX pins by using the function select on the GPIO
    // Set datasheet for more information on function select
    gpio_set_function(UART1_TX_PIN, UART_FUNCSEL_NUM(UART1_ID, UART1_TX_PIN));
    gpio_set_function(UART1_RX_PIN, UART_FUNCSEL_NUM(UART1_ID, UART1_RX_PIN));

    // Set UART flow control CTS/RTS, we don't want these, so turn them off
    uart_set_hw_flow(UART1_ID, false, false);

    // Set our data format
    uart_set_format(UART1_ID, DATA_BITS, STOP_BITS, PARITY);

    // Turn off FIFO's - we want to do this character by character
    uart_set_fifo_enabled(UART1_ID, false);

    // Set up a RX interrupt
    // We need to set up the handler first
    // Select correct interrupt for the UART we are using
    int UART_IRQ = UART1_ID == uart1 ? UART1_IRQ : UART0_IRQ;

    // And set up and enable the interrupt handlers
    irq_set_exclusive_handler(UART_IRQ, on_uart_rx);
    irq_set_enabled(UART_IRQ, true);

    // Now enable the UART to send interrupts - RX only
    uart_set_irq_enables(UART1_ID, true, false);

    // OK, all set up.
    // Lets send a basic string out, and then run a loop and wait for RX interrupts
    // The handler will count them, but also reflect the incoming data back with a slight change!
    uart_puts(UART1_ID, "\nHello, uart interrupts\n");

    while (true)
    {
        sleep_ms(500);
        // tight_loop_contents();
    }
}
