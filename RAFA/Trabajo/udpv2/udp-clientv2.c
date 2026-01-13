#include "contiki.h"
#include "net/ipv6/simple-udp.h"
#include "net/routing/routing.h"
#include "lib/sensors.h"
#include "temperature-sensor.h"
#include "dev/button-hal.h"
#include "dev/leds.h"
#include "sys/etimer.h"
#include <stdio.h>
#include <stdint.h>

#define UDP_CLIENT_PORT 8765
#define UDP_SERVER_PORT 5678

#define START_FLAG 0x55

/* Tipos de tramas*/
#define FRAME_ID        0x01
#define FRAME_REG_REQ     0x10
#define FRAME_REG_ACK     0x11

/* Unidades de temperatura */
#define UNIT_CELSIUS 0x01
#define UNIT_FAHRENHEIT 0x02

/* Alarma */
#define TEMP_ALARM 0x01
#define ALARMA_OFF 0x00
#define ALARMA_ON  0x01

/* ID nodo*/
#ifndef NODE_ID
#define NODE_ID 0xAA
#endif

/* Periodos de las distintas acciones*/
#define SEND_INTERVAL   5
#define MEASURE_INTERVAL     2
#define LED_INTERVAL      500

/* Temperatura a la que se activa la alarma */
#define ALARM_LIMIT_RAW  (35 * 4)

static struct simple_udp_connection udp_conn;

static struct etimer info_timer;
static struct etimer measure_timer;
static struct etimer led_timer;

static uint8_t registered = 0;
static uint8_t unit = UNIT_CELSIUS;
static uint8_t valor_alarma = 0;

static void write_u16_be(uint8_t *p, uint16_t v)
{
  p[0] = (uint8_t)(v >> 8);
  p[1] = (uint8_t)(v & 0xFF);
}
static uint16_t raw_to_f12_4_celsius(int16_t temp_raw)
{
    // Paso 1: convertir raw -> °C
    int32_t celsius = (int32_t)temp_raw / 4;

    // Paso 2: convertir °C -> Q12.4 (x16)
    int32_t q12_4 = celsius * 16;

    // Saturación
    if (q12_4 < 0) q12_4 = 0;
    if (q12_4 > 0xFFFF) q12_4 = 0xFFFF;

    return (uint16_t)q12_4;
}

static uint16_t raw_to_f12_4_fahrenheit(int16_t temp_raw)
{
    // Paso 1: raw → °C enteros
    int32_t celsius = (int32_t)temp_raw / 4;

    // Paso 2: conversión C → F en Q12.4
    // F_Q12.4 = C * (144/5) + 512
    int32_t num = celsius * 144;

    // Ajuste para redondeo de la división entre 5
    if (num >= 0) num += 2;
    else          num -= 2;

    int32_t q = num / 5;

    int32_t fq = q + 512; // 32 * 16

    // Saturación
    if (fq < 0) fq = 0;
    if (fq > 0xFFFF) fq = 0xFFFF;

    return (uint16_t)fq;
}


static int get_root_ip(uip_ipaddr_t *dst)
{
  if(!NETSTACK_ROUTING.node_is_reachable()) {
    return 0;
  }
  return NETSTACK_ROUTING.get_root_ipaddr(dst);
}

static void send_register_req(void)
{
  uip_ipaddr_t dst;
  if(!get_root_ip(&dst)) {
    printf("CLIENT: Not reachable\r\n");
    return;
  }

  uint8_t msg[3] = { START_FLAG, FRAME_REG_REQ, (uint8_t)NODE_ID };
  printf("CLIENT: TX REG_REQ\r\n");
  simple_udp_sendto(&udp_conn, msg, sizeof(msg), &dst);
}

static void send_info_frame(int16_t temp_raw_c)
{
  uip_ipaddr_t dst;
  if(!get_root_ip(&dst)) {
    printf("CLIENT: Not reachable\r\n");
    return;
  }

  if(temp_raw_c > ALARM_LIMIT_RAW) {
    valor_alarma = 1;
  } else {
    valor_alarma = 0;
  }

uint16_t temp_raw_f12_4;
if (unit == UNIT_CELSIUS) {
    temp_raw_f12_4 = raw_to_f12_4_celsius(temp_raw_c);
} else {
    temp_raw_f12_4 = raw_to_f12_4_fahrenheit(temp_raw_c);
}

  uint8_t frame[8];
  frame[0] = START_FLAG;
  frame[1] = FRAME_ID;
  frame[2] = (uint8_t)NODE_ID;
  frame[3] = unit;
  write_u16_be(&frame[4], temp_raw_f12_4);
  frame[6] = TEMP_ALARM;
  if(valor_alarma)
    frame[7] = 1;
  else
    frame[7] = 0;

  printf("CLIENT: TX INFO (unit=%u alarm=%u)\r\n",
         (unsigned)unit, (unsigned)valor_alarma);

  simple_udp_sendto(&udp_conn, frame, sizeof(frame), &dst);
}

static void udp_rx_callback(struct simple_udp_connection *c,
                            const uip_ipaddr_t *sender_addr,
                            uint16_t sender_port,
                            const uip_ipaddr_t *receiver_addr,
                            uint16_t receiver_port,
                            const uint8_t *data,
                            uint16_t datalen)
{
  (void)c; (void)sender_addr; (void)sender_port; (void)receiver_addr; (void)receiver_port;

  if(datalen < 4 || data[0] != START_FLAG) 
    return;

  if(data[1] == FRAME_REG_ACK) {
    uint8_t node_id = data[2];
    uint8_t status  = data[3];

    if(node_id == (uint8_t)NODE_ID && status == 0x01) {
      registered = 1;
      printf("CLIENT: REGISTERED OK\r\n");
    } else {
      registered = 0;
      printf("CLIENT: REGISTERED FAIL (status=%u)\r\n", (unsigned)status);
    }
  }
}

PROCESS(client_process, "UDP Client Process");
AUTOSTART_PROCESSES(&client_process);

PROCESS_THREAD(client_process, ev, data)
{
  static int16_t temp_raw;
  PROCESS_BEGIN();

  printf("CLIENT: starting...\r\n");

  SENSORS_ACTIVATE(temperature_sensor);

  simple_udp_register(&udp_conn,
                      UDP_CLIENT_PORT,
                      NULL,
                      UDP_SERVER_PORT,
                      udp_rx_callback);

  etimer_set(&measure_timer, CLOCK_SECOND * MEASURE_INTERVAL);
  etimer_set(&info_timer, CLOCK_SECOND * SEND_INTERVAL);
  etimer_set(&led_timer, (CLOCK_SECOND * LED_INTERVAL) / 1000);

  unit = UNIT_CELSIUS;
  registered = 0;
  valor_alarma = 0;
  leds_off(LEDS_RED);

  while(1) {
    PROCESS_YIELD();

    /* Botón: toggle unidad */
    if(ev == button_hal_press_event) {
      unit = (unit == UNIT_CELSIUS) ? UNIT_FAHRENHEIT : UNIT_CELSIUS;
      printf("CLIENT: Button -> unit=%u\r\n", (unsigned)unit);
    }

    /* Registro */
    if(etimer_expired(&measure_timer)) {
      etimer_reset(&measure_timer);
      printf("CLIENT: reachable=%d registered=%d\r\n",
             NETSTACK_ROUTING.node_is_reachable(), registered);
      if(!registered) {
        send_register_req();
      }
    }

    /* INFO */
    if(etimer_expired(&info_timer)) {
      etimer_reset(&info_timer);
      if(registered) {
        temp_raw = (int16_t)temperature_sensor.value(0);
        send_info_frame(temp_raw);
      }
    }

    /* LED rojo */
    if(etimer_expired(&led_timer)) {
      etimer_reset(&led_timer);
      if(valor_alarma) 
        leds_toggle(LEDS_RED);
      else 
        leds_off(LEDS_RED);
    }
  }

  PROCESS_END();
}
