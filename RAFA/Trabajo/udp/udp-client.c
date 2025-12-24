#include "contiki.h"
#include "net/routing/routing.h"
#include "random.h"
#include "net/netstack.h"
#include "net/ipv6/simple-udp.h"
#include <stdint.h>
#include <inttypes.h>


#include "sys/log.h"
#define LOG_MODULE "App"
#define LOG_LEVEL LOG_LEVEL_INFO

#define UDP_CLIENT_PORT    8765
#define UDP_SERVER_PORT    5678
#define SEND_INTERVAL      (10 * CLOCK_SECOND)

/* Temperature sensor */
//#include "dev/temperature-sensor.h"
#include "temperature-sensor.h"

//#include "dev/sensor.h"

/* Button */
#include "dev/button-hal.h"

static struct simple_udp_connection udp_conn;
static uint32_t rx_count = 0;

/* Mode: 0 = Celsius, 1 = Fahrenheit */
static uint8_t temperature_mode = 0;

/* Node ID */
#define NODE_ID 0xAA

/*---------------------------------------------------------------------------*/
PROCESS(udp_client_process, "UDP client");
AUTOSTART_PROCESSES(&udp_client_process);
/*---------------------------------------------------------------------------*/

static void
udp_rx_callback(struct simple_udp_connection *c,
                const uip_ipaddr_t *sender_addr,
                uint16_t sender_port,
                const uip_ipaddr_t *receiver_addr,
                uint16_t receiver_port,
                const uint8_t *data,
                uint16_t datalen)
{
  LOG_INFO("Received response '%.*s' from ", datalen, (char *) data);
  LOG_INFO_6ADDR(sender_addr);
  LOG_INFO_("\n");
  rx_count++;
}

/*---------------------------------------------------------------------------*/
PROCESS_THREAD(udp_client_process, ev, data)
{
  static struct etimer periodic_timer;
  static uint32_t tx_count = 0;
  static uint32_t missed_tx_count = 0;
  uip_ipaddr_t dest_ipaddr;

  PROCESS_BEGIN();

  /* Activate temperature sensor */
  SENSORS_ACTIVATE(temperature_sensor);

  /* Activate button HAL */
  button_hal_init();

  /* Register UDP */
  simple_udp_register(&udp_conn, UDP_CLIENT_PORT, NULL,
                      UDP_SERVER_PORT, udp_rx_callback);

  etimer_set(&periodic_timer, random_rand() % SEND_INTERVAL);

  while(1) {
    PROCESS_WAIT_EVENT();

    /* ---------------------- BUTTON HANDLING ----------------------- */
    if(ev == button_hal_press_event) {
      temperature_mode ^= 1;  // Toggle mode
      LOG_INFO("Mode changed: %s\n", (temperature_mode == 0) ? "Celsius" : "Fahrenheit");
    }

    /* ---------------------- TIMER EXPIRED ------------------------- */
    if(ev == PROCESS_EVENT_TIMER && etimer_expired(&periodic_timer)) {

      if(NETSTACK_ROUTING.node_is_reachable() &&
         NETSTACK_ROUTING.get_root_ipaddr(&dest_ipaddr)) {

        /* Read raw temperature from sensor (centi-degrees) */
        int raw_temp = temperature_sensor.value(0); // centi-degrees
        float temp_celsius = (float)raw_temp / 100.0f;

        /* Convert to Fahrenheit if mode = 1 */
        float temp_to_send = (temperature_mode == 0) ? temp_celsius : (temp_celsius * 1.8f + 32.0f);

        /* ------------------- Build F12,4 temperature ------------------ */
        // F12,4: 12 bits con signo para entero + 4 bits para decimal
        // Multiplicamos por 16 para el formato F12,4
        int16_t temp_f12_4 = (int16_t)(temp_to_send * 16.0f);

        /* ------------------- Build binary frame ---------------------- */
        uint8_t frame[8];

        frame[0] = 0x55;                     // Bandera inicio
        frame[1] = 0x01;                     // Identificador trama
        frame[2] = NODE_ID;                   // Identificador nodo
        frame[3] = (temperature_mode == 0) ? 0x01 : 0x02;  // Unidad

        // Temperatura 2 bytes, big endian
        frame[4] = (temp_f12_4 >> 8) & 0xFF; 
        frame[5] = temp_f12_4 & 0xFF;

        frame[6] = 0x01;                     // Tipo de alarma
        frame[7] = 0x00;                     // Valor alarma (0x00 no activa)

        /* Send frame */
        simple_udp_sendto(&udp_conn, frame, sizeof(frame), &dest_ipaddr);

        /* Logging */
        LOG_INFO("Sent temperature %.2f (%s) -> Frame: %02X %02X %02X %02X %02X %02X %02X %02X\n",
                 temp_to_send,
                 (temperature_mode == 0) ? "C" : "F",
                 frame[0], frame[1], frame[2], frame[3],
                 frame[4], frame[5], frame[6], frame[7]);

        tx_count++;

      } else {
        LOG_INFO("Not reachable yet\n");
        if(tx_count > 0) missed_tx_count++;
      }

      /* Reset timer */
      etimer_set(&periodic_timer, SEND_INTERVAL - CLOCK_SECOND + (random_rand() % (2 * CLOCK_SECOND)));
    }
  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
