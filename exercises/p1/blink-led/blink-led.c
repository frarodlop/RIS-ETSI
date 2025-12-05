/**
 * \file
 *         Práctica 1, ejercicio 3. Parpadeo de LEDS.
 * \author
 *         Rafael Muñoz Borrego
 *         Francisco José Rodríguez López  
 */

#include "contiki.h"
#include "dev/leds.h"
#include <stdio.h> /* For printf() */


/* --- PROCESSES --- */

PROCESS(parpadeo_1_process, "Parpadeo 1 process");
PROCESS(parpadeo_2_process, "Parpadeo 2 process");
PROCESS(timer_process     , "Timer process"     );
AUTOSTART_PROCESSES(&parpadeo_1_process, 
                    &parpadeo_2_process, 
                    &timer_process);


/* --- PROCESSES DEFINITIONS --- */

PROCESS_THREAD(timer_process, ev, data)
{
  static struct etimer timer;

  PROCESS_BEGIN();

  printf("[Timer      process] Starting...\n");

  /* Setup a periodic timer that expires after 5 seconds. */
  etimer_set(&timer, CLOCK_SECOND * 5);

  /* Wait for the periodic timer to expire and then restart the timer. */
  printf("[Timer      process] Waiting 5 seconds.\n");
  PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&timer));

  /* Send event to wake up processes. */
  printf("[Timer      process] Sending event to other processes.\n");
  process_poll(&parpadeo_1_process);  
  process_poll(&parpadeo_2_process);  

  PROCESS_END();
}


PROCESS_THREAD(parpadeo_1_process, ev, data)
{
  static struct etimer timer;

  PROCESS_BEGIN();

  printf("[Parpadeo 1 process] Starting...\n");

  /* Wait event from timer process. */
  printf("[Parpadeo 1 process] Waiting for event.\n");
  PROCESS_WAIT_EVENT_UNTIL(ev == PROCESS_EVENT_POLL);
  printf("[Parpadeo 1 process] Event received!\n");

  /* Setup a periodic timer that expires after 2 seconds. */
  etimer_set(&timer, CLOCK_SECOND * 2);

  while(1) {

    /* LED 1. */
    printf("[Parpadeo 1 process] Commuting LED 1.\n");
    leds_toggle(LEDS_GREEN);

    /* Wait for the periodic timer to expire and then restart the timer. */
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&timer));
    etimer_reset(&timer);
  }

  PROCESS_END();
}


PROCESS_THREAD(parpadeo_2_process, ev, data)
{
  static struct etimer timer;

  PROCESS_BEGIN();

  printf("[Parpadeo 2 process] Starting...\n");

  /* Wait event from timer process. */
  printf("[Parpadeo 2 process] Waiting for event.\n");
  PROCESS_WAIT_EVENT_UNTIL(ev == PROCESS_EVENT_POLL);
  printf("[Parpadeo 2 process] Event received!\n");

  /* Setup a periodic timer that expires after 3 seconds. */
  etimer_set(&timer, CLOCK_SECOND * 3);

  while(1) {

    /* LED 2. */
    printf("[Parpadeo 2 process] Commuting LED 2.\n");
    leds_toggle(LEDS_RED);

    /* Wait for the periodic timer to expire and then restart the timer. */
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&timer));
    etimer_reset(&timer);
  }

  PROCESS_END();
}


