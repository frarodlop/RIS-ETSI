#include "contiki.h"
#include "dev/leds.h"
#include <stdio.h>

/*---------------------------------------------------------------------------*/
/* Declaración de procesos */
PROCESS(parpadeo_1_process, "Parpadeo LED1");
PROCESS(parpadeo_2_process, "Parpadeo LED2");
PROCESS(timer_process, "Timer inicial");

/* Autoarranque */
AUTOSTART_PROCESSES(&parpadeo_1_process, &parpadeo_2_process, &timer_process);
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(parpadeo_1_process, ev, data)
{
  static struct etimer timer2s;

  PROCESS_BEGIN();

  printf("Proceso Parpadeo 1 iniciado, esperando evento del timer_process...\n");

  /* Esperar evento del proceso timer_process antes de iniciar el parpadeo */
  PROCESS_WAIT_EVENT_UNTIL(ev == PROCESS_EVENT_POLL);

  printf("Evento recibido. Iniciando parpadeo LED1 cada 2s.\n");
  etimer_set(&timer2s, 2 * CLOCK_SECOND);

  while(1) {
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&timer2s));

    leds_toggle(LEDS_GREEN);  /* LED1 (puede variar según la placa) */
    printf("LED1 conmutado\n");

    etimer_reset(&timer2s);
  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(parpadeo_2_process, ev, data)
{
  static struct etimer timer3s;

  PROCESS_BEGIN();

  printf("Proceso Parpadeo 2 iniciado, esperando evento del timer_process...\n");

  /* Esperar evento del proceso timer_process antes de iniciar el parpadeo */
  PROCESS_WAIT_EVENT_UNTIL(ev == PROCESS_EVENT_POLL);

  printf("Evento recibido. Iniciando parpadeo LED2 cada 3s.\n");
  etimer_set(&timer3s, 3 * CLOCK_SECOND);

  while(1) {
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&timer3s));

    leds_toggle(LEDS_RED);  /* LED2 (puede variar según la placa) */
    printf("LED2 conmutado\n");

    etimer_reset(&timer3s);
  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(timer_process, ev, data)
{
  static struct etimer timer5s;

  PROCESS_BEGIN();

  printf("Proceso Timer iniciado. Esperando 5 segundos antes de enviar eventos...\n");

  etimer_set(&timer5s, 5 * CLOCK_SECOND);
  PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&timer5s));

  printf("5 segundos transcurridos. Enviando eventos de inicio...\n");

  /* process_poll() marca los procesos como listos para ejecutar inmediatamente */
  process_poll(&parpadeo_1_process);
  process_poll(&parpadeo_2_process);

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
