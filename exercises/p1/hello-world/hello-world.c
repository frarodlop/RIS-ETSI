/**
 * \file
 *         Práctica 1, ejercicio 2. Hello world modificado.
 * \author
 *         Rafael Muñoz Borrego
 *         Francisco José Rodríguez López  
 */

#include "contiki.h"
#include <stdio.h> /* For printf() */


/* --- PROCESSES --- */

PROCESS(periodic_process   , "Periodic process");
PROCESS(hello_world_process, "Hello world process");
AUTOSTART_PROCESSES(&periodic_process, 
                    &hello_world_process);


/* --- PROCESSES DEFINITIONS --- */

PROCESS_THREAD(periodic_process, ev, data)
{
  static struct etimer timer;

  PROCESS_BEGIN();

  printf("[Periodic    process] Starting...\n");

  /* Setup a periodic timer that expires after 5 seconds. */
  etimer_set(&timer, CLOCK_SECOND * 5);

  while(1) {
    /* Wait for the periodic timer to expire and then restart the timer. */
    //printf("[Periodic process] Waiting 5 seconds...\n");
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&timer));

    /* Send event to wake up processes. */
    //printf("[Periodic process] Sending event to other processes...\n");
    process_poll(&hello_world_process);

    /* Reset timer. */
    etimer_reset(&timer);
  }

  PROCESS_END();
}


PROCESS_THREAD(hello_world_process, ev, data)
{
  static unsigned counter = 0;

  PROCESS_BEGIN();

  printf("[Hello world process] Starting...\n");

  while(1) {
    /* Wait event from timer process. */
    //printf("[Hello world process] Waiting for event.\n");
    PROCESS_WAIT_EVENT_UNTIL(ev == PROCESS_EVENT_POLL);
    printf("[Hello world process] Hello, world! (number %d)\n", counter);

    /* Increment and reset counter */
    if (counter < 20u){
      counter++;
    } else {
      counter = 0u;
    }
  }

  PROCESS_END();
}
