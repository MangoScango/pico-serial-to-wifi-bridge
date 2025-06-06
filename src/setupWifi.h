#ifndef SETUPWIFI_H
#define SETUPWIFI_H

#include <stdint.h>
#include "pico/cyw43_arch.h"
#include "pico/time.h"

int setup(uint32_t country, const char *ssid, const char *pass,
          uint32_t auth, const char *hostname, ip_addr_t *ip,
          ip_addr_t *mask, ip_addr_t *gw)
{
    const int RETRY_THRESHOLD = 10;          // Number of attempts before adding delay
    const int RETRY_DELAY_MS = 10000;         // Delay between retries after threshold in milliseconds
    const int CONNECTION_TIMEOUT_MS = 15000; // 15 seconds timeout for connection
    int retry_count = 0;
    int status = -1;

    while (status < 0)
    { // Unlimited retries
        if (retry_count > 0)
        {
            printf("Retrying WiFi connection (attempt %d)...\n", retry_count + 1);
            // Clean up previous attempt
            cyw43_arch_deinit();

            // Only add delay after RETRY_THRESHOLD attempts
            if (retry_count >= RETRY_THRESHOLD)
            {
                printf("Adding delay of %d ms between retries\n", RETRY_DELAY_MS);
                sleep_ms(RETRY_DELAY_MS);
            }
        }

        if (cyw43_arch_init_with_country(country))
        {
            printf("Failed to initialize WiFi.\n");
            retry_count++;
            continue;
        }

        cyw43_arch_enable_sta_mode();
        if (hostname != NULL)
        {
            netif_set_hostname(netif_default, hostname);
        }

        printf("Attempting to connect to WiFi network: %s\n", ssid);

        // Start blinking LED during connection attempt
        bool led_state = false;
        absolute_time_t next_blink_time = nil_time;

        // Use non-blocking approach with wifi_connect_timeout_ms
        uint32_t start_time = to_ms_since_boot(get_absolute_time());
        uint32_t current_time;

        // Start connection attempt
        if (cyw43_arch_wifi_connect_async(ssid, pass, auth))
        {
            printf("Failed to start WiFi connection process.\n");
            cyw43_arch_deinit();
            retry_count++;
            continue;
        }

        // Wait for connection with timeout while blinking LED
        int link_status = CYW43_LINK_UP + 1; // Initial invalid status

        do
        {
            // Blink LED every 100ms
            if (absolute_time_diff_us(get_absolute_time(), next_blink_time) <= 0)
            {
                led_state = !led_state;
                cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, led_state);
                next_blink_time = make_timeout_time_ms(100); // Next blink in 100ms
            }

            // Check connection status
            int new_status = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);
            if (new_status != link_status)
            {
                link_status = new_status;
                printf("connect status: %d\n", link_status);
            }

            // Check if connection succeeded or timed out
            current_time = to_ms_since_boot(get_absolute_time());
            if ((current_time - start_time) > CONNECTION_TIMEOUT_MS)
            {
                printf("Connection timed out after %dms\n", CONNECTION_TIMEOUT_MS);
                break;
            }

            // Small delay to prevent CPU hogging
            sleep_ms(5);

        } while (link_status != CYW43_LINK_UP);

        // Check if connection was successful
        if (link_status == CYW43_LINK_UP)
        {
            status = 0; // Success
        }
        else
        {
            status = -1; // Failed
        }

        if (status != 0)
        {
            printf("Connection failed\n");
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0); // LED off on failure
            retry_count++;
            continue;
        }

        // Connection successful - LED on solid
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
        if (ip != NULL)
        {
            netif_set_ipaddr(netif_default, ip);
        }
        if (mask != NULL)
        {
            netif_set_netmask(netif_default, mask);
        }
        if (gw != NULL)
        {
            netif_set_gw(netif_default, gw);
        }

        printf("WiFi connected successfully!\n");
        printf("IP: %s\n",
               ip4addr_ntoa(netif_ip_addr4(netif_default)));
        printf("Mask: %s\n",
               ip4addr_ntoa(netif_ip_netmask4(netif_default)));
        printf("Gateway: %s\n",
               ip4addr_ntoa(netif_ip_gw4(netif_default)));
        printf("Host Name: %s\n",
               netif_get_hostname(netif_default));
    }

    return status;
}

int connect(char *ssid, char *pass)
{
    uint32_t country = CYW43_COUNTRY_SWEDEN;
    uint32_t auth = CYW43_AUTH_WPA2_MIXED_PSK;
    return setup(country, ssid, pass, auth, "pico-serial-to-wifi-bridge", NULL, NULL, NULL);
}

#endif // SETUPWIFI_H