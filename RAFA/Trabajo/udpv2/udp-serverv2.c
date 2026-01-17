#include "contiki.h"
#include "net/ipv6/simple-udp.h"
#include "net/routing/routing.h"
#include "net/ipv6/uip-ds6.h"
#include "net/ipv6/uip-debug.h"
#include <stdio.h>
#include <stdint.h>

#define UDP_SERVER_PORT 5678
#define UDP_CLIENT_PORT 8765

#define START_FLAG 0x55

/* Tipos de trama */
#define FRAME_INFO        0x01
#define FRAME_REG_REQ     0x10
#define FRAME_REG_ACK     0x11

static struct simple_udp_connection udp_conn;

static uint16_t read_temp_12_4(const uint8_t *p)
{
  return ((uint16_t)p[0] << 8) | (uint16_t)p[1];
}

static void temp_f12_4_to_dec(uint16_t raw)
{
    /* Convertir F12.4 → centésimas (XX.YY)
       Fórmula: centi = raw * 100 / 16
       +8 es para redondear apropiadamente cuando dividimos entre 16
    */
    uint32_t centi = (raw * 100u + 8u) / 16u;

    uint32_t entero = centi / 100u;  // parte entera
    uint32_t dec    = centi % 100u;  // parte decimal (00–99)

    printf("%lu.%02lu", (unsigned long)entero, (unsigned long)dec);
}


static void send_reg_ack(const uip_ipaddr_t *dst, uint8_t node_id, uint8_t status)
{
  uint8_t resp[4];
  resp[0] = START_FLAG;
  resp[1] = FRAME_REG_ACK;
  resp[2] = node_id;
  resp[3] = status;
  simple_udp_sendto(&udp_conn, resp, sizeof(resp), dst);
}

static void udp_rx_callback(struct simple_udp_connection *c,
                            const uip_ipaddr_t *sender_addr,
                            uint16_t sender_port,
                            const uip_ipaddr_t *receiver_addr,
                            uint16_t receiver_port,
                            const uint8_t *data,
                            uint16_t datalen)
{
  (void)c; (void)sender_port; (void)receiver_addr; (void)receiver_port;

  if(datalen < 2 || data[0] != START_FLAG) {
    return;
  }

  const uint8_t type = data[1];

  /* Negociación: REGISTER_REQ */
  if(type == FRAME_REG_REQ) {
    if(datalen < 3) return;

    uint8_t node_id = data[2];
    uint8_t ok = (node_id != 0x00 && node_id != 0xFF) ? 0x01 : 0x00;

    printf("RX REG_REQ from node_id=%u -> %s\r\n",
           (unsigned)node_id, ok ? "OK" : "NO OK");

    send_reg_ack(sender_addr, node_id, ok);
    return;
  }

  /* Trama INFO */
  if(type == FRAME_INFO) {
    if(datalen < 8) return;

    uint8_t id_nodo = data[2];
    uint8_t unidad = data[3];
    uint16_t raw_temp = read_temp_12_4(&data[4]);
    uint8_t id_alarma = data[6];
    uint8_t estado_alarma = data[7];

    /* ID;Unidad;Temp;ID_alarma;Estado */
    printf("%u;%u;", (unsigned)id_nodo, (unsigned)unidad);
    temp_f12_4_to_dec(raw_temp);
    printf(";%u;%u\r\n", (unsigned)id_alarma, (unsigned)estado_alarma);
    return;
  }
}

PROCESS(server_process, "RPL Root + UDP Server");
AUTOSTART_PROCESSES(&server_process);

PROCESS_THREAD(server_process, ev, data)
{
  static struct etimer ip_timer;
  PROCESS_BEGIN();

  printf("SERVER: starting RPL root...\r\n");
  NETSTACK_ROUTING.root_start();

  /* UDP */
  simple_udp_register(&udp_conn,
                      UDP_SERVER_PORT,
                      NULL,
                      UDP_CLIENT_PORT,
                      udp_rx_callback);

  /* Mostrar IP global del root cada 5s */
  etimer_set(&ip_timer, CLOCK_SECOND * 5);

  while(1) {
    PROCESS_WAIT_EVENT();

    if(etimer_expired(&ip_timer)) {
      etimer_reset(&ip_timer);
      const uip_ds6_addr_t *a = uip_ds6_get_global(ADDR_PREFERRED);
      if(a != NULL) {
        printf("SERVER Global IPv6: ");
        uip_debug_ipaddr_print(&a->ipaddr);
        printf("\r\n");
      } else {
        printf("SERVER has no global IPv6 yet\r\n");
      }
    }
  }

  PROCESS_END();
}
