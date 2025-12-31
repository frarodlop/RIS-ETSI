#include "contiki.h"
#include "net/routing/routing.h"
#include "net/ipv6/simple-udp.h"
#include "net/netstack.h"
#include <stdio.h>
#include <stdint.h>

#define UDP_SERVER_PORT    5678

static struct simple_udp_connection udp_conn;

PROCESS(udp_server_process, "UDP Server Process");
AUTOSTART_PROCESSES(&udp_server_process);

/* -------------------------------------------------------------------------- */
/* FUNCIÓN DE CALLBACK (Se ejecuta al recibir un paquete)                     */
/* -------------------------------------------------------------------------- */
static void
udp_rx_callback(struct simple_udp_connection *c,
         const uip_ipaddr_t *sender_addr,
         uint16_t sender_port,
         const uip_ipaddr_t *receiver_addr,
         uint16_t receiver_port,
         const uint8_t *data,
         uint16_t datalen)
{
  /* * Validación básica: 
   * Esperamos 8 bytes y que empiece por la bandera 0x55 
   */
  if(datalen == 8 && data[0] == 0x55) {

      /* 1. Extraer Identificador de Nodo (Byte 2) */
      uint8_t node_id = data[2];

      /* 2. Extraer Unidad (Byte 3) */
      uint8_t unit_val = data[3]; 
      // Nota: 1 = Celsius, 2 = Fahrenheit

      /* 3. Extraer Temperatura (Bytes 4 y 5) */
      // Reconstruimos el int16_t desde Big Endian
      int16_t temp_raw = (int16_t)((data[4] << 8) | data[5]);
      
      // Convertimos de Punto Fijo (x16) a parte Entera y Decimal
      // Hacemos esto manual para evitar problemas con printf de floats en algunos MCUs
      int16_t temp_int = temp_raw / 16;
      int16_t temp_frac = ((temp_raw < 0 ? -temp_raw : temp_raw) % 16) * 625; 
      // * 625 porque 1/16 = 0.0625. Al multiplicar por 625 obtenemos 4 decimales teóricos,
      // pero para simplificar la visualización mostraremos 2 decimales aproximados.

      /* 4. Extraer ID Alarma (Byte 6) */
      uint8_t alarm_id = data[6];

      /* 5. Extraer Estado Alarma (Byte 7) */
      uint8_t alarm_state = data[7];

      /* * IMPRIMIR POR PUERTO SERIE CON EL FORMATO SOLICITADO:
       * <ID_nodo>;<Unidad>;<Temperatura>;<ID alarma>;<Estado de la alarma>
       * * Ejemplo de salida: AA;1;25.50;1;0
       */
      
      // Truco para imprimir decimales sin %f:
      // temp_frac / 100 nos da los dos primeros decimales aprox (0.0625 * 10000 -> 625)
      printf("%02X;%u;%d.%02u;%u;%u\n", 
             node_id, 
             unit_val, 
             temp_int, (unsigned int)(temp_frac / 100), // Parte decimal simplificada
             alarm_id, 
             alarm_state);
  } else {
      printf("Trama recibida incorrecta o desconocida.\n");
  }
}

/* -------------------------------------------------------------------------- */
/* HILO PRINCIPAL DEL SERVIDOR                                                */
/* -------------------------------------------------------------------------- */
PROCESS_THREAD(udp_server_process, ev, data)
{
  PROCESS_BEGIN();

  /* 1. Iniciar como Root del DAG (necesario para que los clientes lleguen aquí) */
  NETSTACK_ROUTING.root_start();

  /* 2. Registrar conexión UDP */
  /* NULL en remote_addr significa que aceptamos paquetes de cualquiera */
  simple_udp_register(&udp_conn, UDP_SERVER_PORT, NULL,
                      UDP_SERVER_PORT, udp_rx_callback);

  printf("Servidor UDP iniciado. Esperando datos en puerto %d...\n", UDP_SERVER_PORT);

  PROCESS_END();
}