
#include <stdio.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/pbuf.h"
#include "lwip/altcp_tcp.h"

#include "hardware/rtc.h"
#include "time.h"

#include "setupWifi.h"

#define BUF_SIZE 2048

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
    while (true)
    {
        sleep_ms(500);
    }
}
