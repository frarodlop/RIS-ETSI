#include "contiki.h"
#include "net/routing/routing.h"
#include "net/ipv6/simple-udp.h"
#include "dev/leds.h"
#include "dev/button-hal.h"
#include "temperature-sensor.h" 
#include <stdint.h>

/* --- CONFIGURACIÓN DE TIEMPOS --- */
#define SEND_INTERVAL      (5 * CLOCK_SECOND) // Enviar cada 5s 
#define MEASURE_INTERVAL   (1 * CLOCK_SECOND) // Medir cada 1s (respuesta rápida alarma)
#define BLINK_INTERVAL     (CLOCK_SECOND / 2) // Parpadeo 0.5s 

#define UDP_CLIENT_PORT    8765
#define UDP_SERVER_PORT    5678
#define NODE_ID            0xAA  // Tu ID asignado

/* --- VARIABLES COMPARTIDAS (Globales) --- */
/* Estas variables sirven de puente entre los hilos */
static volatile float current_temp_celsius = 0.0f;
static volatile uint8_t is_alarm_active = 0;   // 0 = OK, 1 = Alarma
static volatile uint8_t unit_mode = 0;         // 0 = Celsius (0x1), 1 = Fahrenheit (0x2)

/* --- DECLARACIÓN DE PROCESOS --- */
PROCESS(measurement_process, "Sensor Measurement Process");
PROCESS(alarm_led_process, "Alarm LED Process");
PROCESS(udp_send_process, "UDP Communication Process");

// Arrancamos los 3 procesos automáticamente
AUTOSTART_PROCESSES(&measurement_process, &alarm_led_process, &udp_send_process);

/* -------------------------------------------------------------------------- */
/* HILO 1: MEDICIÓN Y LÓGICA DE ALARMA (Productor)                            */
/* -------------------------------------------------------------------------- */
PROCESS_THREAD(measurement_process, ev, data)
{
  static struct etimer measure_timer;
  PROCESS_BEGIN();

  /* Inicializar sensor */
  SENSORS_ACTIVATE(temperature_sensor);
  
  etimer_set(&measure_timer, MEASURE_INTERVAL);

  while(1) {
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&measure_timer));

    /* 1. Leer sensor */
    int raw = temperature_sensor.value(0);

    float temp = (float)raw / 4.0f; // Convertir a °C según driver nRF52

    /* 2. Actualizar variable global (sección crítica simplificada) */
    current_temp_celsius = temp;

    /* 3. Comprobar umbral de alarma (> 35°C) [cite: 52] */
    if (current_temp_celsius > 35.0f) {
        is_alarm_active = 1;
    } else {
        is_alarm_active = 0;
    }

    etimer_reset(&measure_timer);
  }
  PROCESS_END();
}

/* -------------------------------------------------------------------------- */
/* HILO 2: INTERFAZ DE USUARIO - LED DE ALARMA (Actuador Visual)              */
/* -------------------------------------------------------------------------- */
PROCESS_THREAD(alarm_led_process, ev, data)
{
  static struct etimer blink_timer;
  PROCESS_BEGIN();

  etimer_set(&blink_timer, BLINK_INTERVAL);

  while(1) {
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&blink_timer));

    if (is_alarm_active) {
       /* Si hay alarma, conmutamos LED cada 0.5s  */
       leds_toggle(LEDS_RED);
    } else {
       /* Si no, aseguramos que esté apagado */
       leds_off(LEDS_RED);
    }

    etimer_reset(&blink_timer);
  }
  PROCESS_END();
}

/* -------------------------------------------------------------------------- */
/* HILO 3: COMUNICACIÓN UDP (Consumidor)                                      */
/* -------------------------------------------------------------------------- */
PROCESS_THREAD(udp_send_process, ev, data)
{
  static struct etimer send_timer;
  static struct simple_udp_connection udp_conn;
  uip_ipaddr_t dest_ipaddr;

  PROCESS_BEGIN();

  /* Inicializar botón y red */
  button_hal_init();
  simple_udp_register(&udp_conn, UDP_CLIENT_PORT, NULL,
                      UDP_SERVER_PORT, NULL);

  etimer_set(&send_timer, SEND_INTERVAL);

  while(1) {
    PROCESS_WAIT_EVENT();

    /* --- GESTIÓN DEL BOTÓN (Cambio de unidad) --- */
    if(ev == button_hal_press_event) {
        unit_mode ^= 1; // Alternar entre 0 y 1
    }

    /* --- GESTIÓN DEL ENVÍO (Cada 5s) --- */
    if(ev == PROCESS_EVENT_TIMER && etimer_expired(&send_timer)) {

        if(NETSTACK_ROUTING.node_is_reachable() && 
           NETSTACK_ROUTING.get_root_ipaddr(&dest_ipaddr)) {

            /* 1. Obtener temperatura a enviar según el modo */
            float temp_to_send;
            if (unit_mode == 0) {
                temp_to_send = current_temp_celsius; // Celsius
            } else {
                temp_to_send = (current_temp_celsius * 1.8f) + 32.0f; // Fahrenheit
            }

            /* 2. Conversión a Punto Fijo F12,4 [cite: 26] */
            // Multiplicamos por 16 (2^4) para desplazar la coma
            int16_t temp_f12_4 = (int16_t)(temp_to_send * 16.0f);

            /* 3. Construcción de la Trama (8 bytes) */
            uint8_t frame[8];
            frame[0] = 0x55;      // Bandera 
            frame[1] = 0x01;      // ID Trama
            frame[2] = NODE_ID;   // ID Nodo 
            
            // Unidad: 0x1 (C) o 0x2 (F) 
            frame[3] = (unit_mode == 0) ? 0x01 : 0x02; 

            // Valor Temperatura (Big Endian) 
            frame[4] = (temp_f12_4 >> 8) & 0xFF;
            frame[5] = temp_f12_4 & 0xFF;

            // Alarma (ID=1) y Estado 
            frame[6] = 0x01; 
            frame[7] = is_alarm_active; // 0x00 o 0x01

            /* 4. Enviar */
            simple_udp_sendto(&udp_conn, frame, sizeof(frame), &dest_ipaddr);
        }

        /* Reiniciar timer para el siguiente envío en 5s */
        etimer_reset(&send_timer);
    }
  }
  PROCESS_END();
}