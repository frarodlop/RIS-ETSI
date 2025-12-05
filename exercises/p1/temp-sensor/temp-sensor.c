/**
 * \file
 *         Práctica 1, ejercicio 2. Hello world modificado.
 * \author
 *         Rafael Muñoz Borrego
 *         Francisco José Rodríguez López  
 */

#include "contiki.h"
#include "temperature-sensor.h"
#include <stdio.h> /* For printf() */


/* --- PROCESSES --- */

PROCESS(timer_process, "Timer process");
PROCESS(temp_process , "Temp process" );
AUTOSTART_PROCESSES(&timer_process, 
                    &temp_process);


/* --- PROCESSES DEFINITIONS --- */

PROCESS_THREAD(timer_process, ev, data)
{
  static struct etimer timer;

  PROCESS_BEGIN();

  printf("[Timer process] Timer process starting...\r\n");

  /* Setup a periodic timer that expires after 3 seconds. */
  etimer_set(&timer, CLOCK_SECOND * 3);

  while(1){

    /* Send event to temperature process. */
    process_poll(&temp_process);

    /* Wait for the periodic timer to expire and then restart the timer. */
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&timer));
    etimer_reset(&timer);
  }

  PROCESS_END();
}


PROCESS_THREAD(temp_process, ev, data)
{
  float temp_celsius = 0;
  int   temp_integer = 0;
  int   temp_decimal = 0;


  PROCESS_BEGIN();

  printf("[Temp process] Temp process starting...\r\n");

  /* Activate sensor. */
  SENSORS_ACTIVATE(temperature_sensor);
  
  while(1){

    /* Wait event. */
    PROCESS_WAIT_EVENT_UNTIL(ev == PROCESS_EVENT_POLL);

    /* Read temperature. Resolution 0'25ºC. Get integer and decimal  *
     * part.                                                         */
    temp_celsius = temperature_sensor.value(0) * 0.25;
    temp_decimal = (temp_celsius - (int)temp_celsius) * 1e2;
    temp_integer = (int)temp_celsius;

    printf("[Temp process] Temperature measurement is: %d.%02dºC\r\n", 
      temp_integer, temp_decimal);
  }

  PROCESS_END();
}

