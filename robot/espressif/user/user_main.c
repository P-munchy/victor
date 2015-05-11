#include "mem.h"
#include "c_types.h"
#include "ets_sys.h"
#include "osapi.h"
#include "gpio.h"
#include "os_type.h"
#include "user_interface.h"
#include "client.h"
#include "driver/uart.h"

/// USER_TASK_PRIO_0 is the lowest (idle) task priority
#define userTaskPrio USER_TASK_PRIO_0

/// The depth of the user idle task
#define    userTaskQueueLen    4
/// Queue for user idle task
os_event_t userTaskQueue[userTaskQueueLen];

/** User "idle" task
 * Called by OS with lowest priority.
 * Always requeues itself.
 */
LOCAL void ICACHE_FLASH_ATTR userTask(os_event_t *event)
{
  static const uint32 INTERVAL = 100000;
  static uint32 counter = 0;
  if (counter++ > INTERVAL)
  {
    uart_tx_one_char_no_wait(UART1, '.'); // Print a dot to show we're executing
    counter = 0;
  }
  system_os_post(userTaskPrio, 0, 0); // Repost ourselves
}

/** User initialization function
 * This function is responsible for setting all the wireless parameters and
 * Setting up any user application code to run on the espressif.
 * It is called automatically from the os main function.
 */
void ICACHE_FLASH_ATTR
user_init()
{
    uint32 i;
    int8 err;

    REG_SET_BIT(0x3ff00014, BIT(0));
    os_update_cpu_frequency(160);

    uart_init(BIT_RATE_5000000, BIT_RATE_74880);

    os_printf("Espressif booting up...\r\n");

    // Create config for Wifi AP
    struct softap_config ap_config;
    err = wifi_softap_get_config(&ap_config);
    if (err == false)
    {
      os_printf("Error getting wifi softap config\r\n");
    }

    // Get the mac address
    uint8 macaddr[6];
    err = wifi_get_macaddr(SOFTAP_IF, macaddr);
    if (err == false)
    {
      os_printf("Error getting mac address info\r\n");
    }

    os_sprintf(ap_config.ssid, "AnkiEspressif%02x%02x", macaddr[4], macaddr[5]);
    os_strcpy(ap_config.password, "2manysecrets");
    ap_config.ssid_len = 0;
    ap_config.channel = 2;
    ap_config.authmode = AUTH_WPA2_PSK;
    ap_config.max_connection = 4;
    ap_config.ssid_hidden = 0; // No hidden SSIDs, they create security problems
    ap_config.beacon_interval = 25; // Must be 50 or lower for iOS devices to connect

    // Setup ESP module to AP mode and apply settings
    wifi_set_opmode(SOFTAP_MODE);
    wifi_softap_set_config(&ap_config);
    wifi_set_phy_mode(PHY_MODE_11G);

    // Create ip config
    struct ip_info ipinfo;
    ipinfo.gw.addr = ipaddr_addr("0.0.0.0");
    ipinfo.ip.addr = ipaddr_addr("172.31.1.1");
    ipinfo.netmask.addr = ipaddr_addr("255.255.255.0");

    // Assign ip config
    wifi_set_ip_info(SOFTAP_IF, &ipinfo);

    // Start DHCP server
    wifi_softap_dhcps_start();

    // Setup Basestation client
    clientInit();

    // Enable UART0 RX interrupt
    // Only after clientInit
    uart_rx_intr_enable(UART0);

    // Setup user task
    system_os_task(userTask, userTaskPrio, userTaskQueue, userTaskQueueLen); // Initalize OS task
    //system_os_post(userTaskPrio, 0, 0); // Post user task

    os_printf("user initalization complete\r\n");

}
