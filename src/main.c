#include <stdio.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/pbuf.h"
#include "lwip/altcp_tcp.h"
#include "lwip/apps/sntp.h"

#include "hardware/uart.h"
#include "hardware/irq.h"
#include "hardware/rtc.h"
#include "time.h"
#include "pico/util/datetime.h"

#include "setupWifi.h"

#define BUF_SIZE 2048

#define UART1_ID uart1
#define BAUD_RATE 115200
#define DATA_BITS 8
#define STOP_BITS 1
#define PARITY UART_PARITY_NONE

#define UART1_TX_PIN 4
#define UART1_RX_PIN 5

bool getDateNow(struct tm *t)
{
    datetime_t rtc;
    bool state = rtc_get_datetime(&rtc);
    if (state)
    {
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
    return state;
}

void send200Ok(struct altcp_pcb *pcb, char *myBuff)
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
        pbuf_copy_partial(p, myBuff, p->tot_len, 0);
        myBuff[p->tot_len] = 0;
        printf("%s\n", myBuff);
        for (int i = 0; i < p->tot_len; ++i)
        {
            uart_putc_raw(UART1_ID, myBuff[i]);
        }
        altcp_recved(pcb, p->tot_len);
        pbuf_free(p);
        send200Ok(pcb, myBuff);
    }
    return ERR_OK;
}

static err_t sent(void *arg, struct altcp_pcb *pcb, u16_t len)
{
    altcp_close(pcb);
}

static err_t accept(void *arg, struct altcp_pcb *pcb, err_t err)
{
    altcp_recv(pcb, recv);
    altcp_sent(pcb, sent);
    printf("connect!\n");

    return ERR_OK;
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

// Custom implementation of pico_localtime_r for Swedish time (CET/CEST)
// This overrides the weak implementation in the Pico SDK
struct tm *pico_localtime_r(const time_t *time, struct tm *tm)
{
    // First get UTC time
    gmtime_r(time, tm);

    // Check if DST is active (rough approximation)
    // DST in Europe: Last Sunday in March to Last Sunday in October
    int isDST = 0;
    if ((tm->tm_mon > 2 && tm->tm_mon < 9) ||
        (tm->tm_mon == 2 && tm->tm_mday - tm->tm_wday > 24) ||
        (tm->tm_mon == 9 && tm->tm_mday - tm->tm_wday <= 24))
    {
        isDST = 1; // Summer time (CEST, UTC+2)
    }

    // Apply Swedish time offset: +1 hour (CET) or +2 hours (CEST during summer)
    time_t adjusted_time = *time + 3600 + (isDST ? 3600 : 0);
    return gmtime_r(&adjusted_time, tm);
}

// Modified SNTPSetRTC function to use our Swedish time implementation
void SNTPSetRTC(u32_t t, u32_t us)
{
    printf("Updating RTC\n");
    time_t seconds_since_1970 = t - 2208988800; // Convert NTP epoch to Unix epoch

    // Create a datetime_t structure using our custom localtime implementation
    struct tm datetime;
    pico_localtime_r(&seconds_since_1970, &datetime);

    // Convert tm structure to datetime_t for RTC
    datetime_t dt;
    dt.year = datetime.tm_year + 1900;
    dt.month = datetime.tm_mon + 1;
    dt.day = datetime.tm_mday;
    dt.dotw = datetime.tm_wday;
    dt.hour = datetime.tm_hour;
    dt.min = datetime.tm_min;
    dt.sec = datetime.tm_sec;

    // Initialize RTC and set the datetime
    rtc_init();
    rtc_set_datetime(&dt);

    // Determine if we're in DST for the log message
    int isDST = 0;
    if ((datetime.tm_mon > 2 && datetime.tm_mon < 9) ||
        (datetime.tm_mon == 2 && datetime.tm_mday - datetime.tm_wday > 24) ||
        (datetime.tm_mon == 9 && datetime.tm_mday - datetime.tm_wday <= 24))
    {
        isDST = 1;
    }

    printf("Time set to Swedish time (UTC+%d)\n", 1 + isDST);
    printf("Current time: %04d-%02d-%02d %02d:%02d:%02d\n",
           dt.year, dt.month, dt.day, dt.hour, dt.min, dt.sec);
}

int main()
{
    stdio_init_all();
    connect(WIFI_SSID, WIFI_PASSWORD);
    struct altcp_pcb *pcb = altcp_new(NULL);
    altcp_accept(pcb, accept);

    altcp_bind(pcb, IP_ADDR_ANY, 80);
    cyw43_arch_lwip_begin();
    pcb = altcp_listen_with_backlog(pcb, 3);
    cyw43_arch_lwip_end();

    // This causes PANIC
    // sntp_setoperatingmode(SNTP_OPMODE_POLL);
    // sntp_setservername(0, "pool.ntp.org");
    // sntp_init();

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
    // Print all UART settings
    printf("\n");
    printf("UART Settings:\n");
    printf("UART Id: %s\n", UART1_ID == uart1 ? "uart1" : "uart0");
    printf("Baud Rate: %d\n", BAUD_RATE);
    printf("Data Bits: %d\n", DATA_BITS);
    printf("Stop Bits: %d\n", STOP_BITS);
    printf("Parity: %s\n", PARITY == UART_PARITY_NONE ? "None" : (PARITY == UART_PARITY_ODD ? "Odd" : "Even"));
    printf("TX Pin: %d\n", UART1_TX_PIN);
    printf("RX Pin: %d\n", UART1_RX_PIN);

    while (true)
    {
        sleep_ms(500);
        // tight_loop_contents();
    }
}
