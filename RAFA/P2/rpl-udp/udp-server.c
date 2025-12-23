#include <stdio.h>
#include <string.h>

#include "contiki.h"
#include "net/routing/routing.h"
#include "net/netstack.h"
#include "net/ipv6/simple-udp.h"

#include "sys/log.h"
#define LOG_MODULE "App"
#define LOG_LEVEL LOG_LEVEL_INFO

#define UDP_CLIENT_PORT    8765
#define UDP_SERVER_PORT    5678

static struct simple_udp_connection udp_conn;

PROCESS(udp_server_process, "UDP server");
AUTOSTART_PROCESSES(&udp_server_process);

/* ----------------------------- */
/* Manual float parser            */
/* ----------------------------- */
static float parse_float(const char *s)
{
  float result = 0.0f;
  float sign = 1.0f;
  float divisor = 1.0f;
  int point_seen = 0;

  if(*s == '-') {
    sign = -1.0f;
    s++;
  }

  while(*s) {
    if(*s == '.') {
      point_seen = 1;
    } else if(*s >= '0' && *s <= '9') {
      result = result * 10.0f + (*s - '0');
      if(point_seen) {
        divisor *= 10.0f;
      }
    } else {
      break;
    }
    s++;
  }

  return sign * (result / divisor);
}

/* Celsius → Fahrenheit */
static float c_to_f(float c)
{
  return (c * 9.0f / 5.0f) + 32.0f;
}

/* ----------------------------- */
/* UDP receive callback          */
/* ----------------------------- */
static void
udp_rx_callback(struct simple_udp_connection *c,
                const uip_ipaddr_t *sender_addr,
                uint16_t sender_port,
                const uip_ipaddr_t *receiver_addr,
                uint16_t receiver_port,
                const uint8_t *data,
                uint16_t datalen)
{
  char buffer[32];
  char reply[32];

  LOG_INFO("Received %d bytes: '%.*s' from ", datalen, datalen, (char *)data);
  LOG_INFO_6ADDR(sender_addr);
  LOG_INFO_("\n");

  /* Safely copy and null-terminate */
  if(datalen >= sizeof(buffer))
    datalen = sizeof(buffer) - 1;
  memcpy(buffer, data, datalen);
  buffer[datalen] = '\0';

  /* Parse float manually */
  float celsius = parse_float(buffer);
  float fahrenheit = c_to_f(celsius);

  int entero = (int)fahrenheit;
  int decimal = (int)((fahrenheit - entero) * 100);

  snprintf(reply, sizeof(reply), "%d.%02d", entero, decimal);

  LOG_INFO("Sending back %s°F\n", reply);

  simple_udp_sendto(&udp_conn, reply, strlen(reply), sender_addr);
}

/* ----------------------------- */
/* Main process                  */
/* ----------------------------- */
PROCESS_THREAD(udp_server_process, ev, data)
{
  PROCESS_BEGIN();

  LOG_INFO("Starting UDP server\n");

  NETSTACK_ROUTING.root_start();

  simple_udp_register(&udp_conn,
                      UDP_SERVER_PORT,
                      NULL,
                      UDP_CLIENT_PORT,
                      udp_rx_callback);

  LOG_INFO("UDP server listening on %d\n", UDP_SERVER_PORT);

  PROCESS_END();
}
