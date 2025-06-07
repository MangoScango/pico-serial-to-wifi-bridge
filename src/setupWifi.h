#ifndef SETUPWIFI_H
#define SETUPWIFI_H

#include <stdint.h>
#include "pico/cyw43_arch.h"

int setup(uint32_t country, const char *ssid, const char *pass,
		  uint32_t auth, const char *hostname, ip_addr_t *ip,
		  ip_addr_t *mask, ip_addr_t *gw)
{
	// Retry indefinitely until successful connection
	int retry_count = 0;
	int status = -1;
	int flashrate = 100;

	while (status < 0) {
		if (retry_count > 0) {
			printf("Connection attempt %d, retrying...\n", retry_count + 1);
			
			// Blink LED during deinit and reinit to indicate we're still trying
			for (int i = 0; i < 3; i++) {
				cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
				sleep_ms(flashrate);
				cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
				sleep_ms(flashrate);
			}
			
			// Clean up previous attempt
			cyw43_arch_deinit();
			// sleep_ms(1000); // Wait before retrying
		}
		
		if (cyw43_arch_init_with_country(country))
		{
			return 1;
		}

		cyw43_arch_enable_sta_mode();
		if (hostname != NULL)
		{
			netif_set_hostname(netif_default, hostname);
		}
		if (cyw43_arch_wifi_connect_async(ssid, pass, auth))
		{
			return 2;
			}
		
		flashrate = 100;
		status = CYW43_LINK_UP + 1;
		
		// Wait for connection to establish
		while (status >= 0 && status != CYW43_LINK_UP)
		{
			int new_status = cyw43_tcpip_link_status(&cyw43_state,
													CYW43_ITF_STA);
			if (new_status != status)
			{
				status = new_status;
				if (status > 0) {
					flashrate = flashrate / (status + 1);
				}
				printf("connect status: %d %d\n", status, flashrate);
			}
			
			// Always blink the LED regardless of status
			cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
			sleep_ms(flashrate);
			cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
			sleep_ms(flashrate);
		}
		
		if (status < 0)
		{
			// Ensure LED is off when connection failed before retry
			cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
			printf("Connection failed with status %d. Retrying...\n", status);
			retry_count++;
			
			// Continue to next retry iteration
		}
		else
		{
			// Only set LED permanently on when successfully connected
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

			printf("IP: %s\n",
				ip4addr_ntoa(netif_ip_addr4(netif_default)));
			printf("Mask: %s\n",
				ip4addr_ntoa(netif_ip_netmask4(netif_default)));
			printf("Gateway: %s\n",
				ip4addr_ntoa(netif_ip_gw4(netif_default)));
			printf("Host Name: %s\n",
				netif_get_hostname(netif_default));
			
			printf("Connection successful after %d attempt(s).\n", retry_count + 1);
			break;
		}
	}
	
	return status;
}

int connect(char *ssid, char *pass)
{
	uint32_t country = CYW43_COUNTRY_SWEDEN;
	uint32_t auth = CYW43_AUTH_WPA2_MIXED_PSK;
	return setup(country, ssid, pass, auth, "pico-scope", NULL, NULL, NULL);
}

#endif // SETUPWIFI_H