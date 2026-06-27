/* timer.h — PIT (8253/8254 Programmable Interval Timer) driver.
 *
 * The PIT is a legacy x86 device, so this driver lives in kernel/drivers/ and
 * reaches DOWN to the arch layer (io.h for port access, irq.h to register its
 * handler). It never reaches up. In Phase 2 the only job is counting ticks; the
 * tick is the future basis for preemptive scheduling, but that is a later phase.
 */
#ifndef SAPOS_DRIVERS_TIMER_H
#define SAPOS_DRIVERS_TIMER_H

#include <stdint.h>

/* Program PIT channel 0 to fire IRQ0 at `frequency` Hz and install the tick
 * handler. Does NOT unmask IRQ0 or enable interrupts — that final "go live" step
 * is arch_enable_irqs(), called only after every handler is installed. */
void timer_init(uint32_t frequency);

/* Monotonic tick count since timer_init(): one tick per IRQ0. */
uint64_t timer_ticks(void);

#endif /* SAPOS_DRIVERS_TIMER_H */
