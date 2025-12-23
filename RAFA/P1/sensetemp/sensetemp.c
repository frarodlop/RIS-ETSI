#include "contiki.h"
#include "temperature-sensor.h"
#include <stdio.h>

/*---------------------------------------------------------------------------*/
/* Declaración de procesos */
PROCESS(temp_process, "Proceso de lectura de temperatura");
PROCESS(timer_process, "Proceso de temporización");

/* Autoarranque de ambos procesos */
AUTOSTART_PROCESSES(&temp_process, &timer_process);
/*---------------------------------------------------------------------------*/

/* Proceso encargado de leer el sensor de temperatura */
PROCESS_THREAD(temp_process, ev, data)
{
  PROCESS_BEGIN();

  printf("Proceso de temperatura iniciado.\n");

  /* Activar el sensor interno de temperatura */
  SENSORS_ACTIVATE(temperature_sensor);

  while(1) {
    /* Espera hasta recibir evento del proceso de temporización */
    PROCESS_WAIT_EVENT_UNTIL(ev == PROCESS_EVENT_POLL);

    /* Leer temperatura en décimas de grado */
    int value = temperature_sensor.value(0);
    printf("Temperatura interna medida: %.2d ", value);


    /* Convertir a grados Celsius (según el driver de nRF52) */
    float temperature_c = value / 4.0;
    int entero = (int)temperature_c;
    int decimal = (int)((temperature_c - entero) * 100);
    printf("Temperatura: %d.%02d °C\n", entero, decimal);
  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/

/* Proceso encargado de generar el evento cada 3 segundos */
PROCESS_THREAD(timer_process, ev, data)
{
  static struct etimer timer;

  PROCESS_BEGIN();

  printf("Proceso de temporización iniciado. Enviando eventos cada 3 segundos...\n");

  etimer_set(&timer, 3 * CLOCK_SECOND);

  while(1) {
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&timer));

    /* Enviar evento al proceso de temperatura */
    process_poll(&temp_process);

    /* Reiniciar temporizador */
    etimer_reset(&timer);
  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
