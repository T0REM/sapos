/* sched.h — the cooperative round-robin scheduler (Phase 4, step 4a).
 *
 * Architecture-INDEPENDENT (ARCHITECTURE.md §4): it manages a run queue of tasks
 * and decides who runs next, but it never performs the register save/restore
 * itself — it asks the arch layer through context_switch() (arch/x86_64). The
 * scheduler does not know what CPU it is on.
 *
 * COOPERATIVE ONLY in 4a: switches happen exclusively at explicit sched_yield()
 * calls — never from the timer. That makes every switch a known, debuggable
 * point. Timer-driven preemption is step 4b.
 */
#ifndef SCRAPOS_CORE_SCHED_SCHED_H
#define SCRAPOS_CORE_SCHED_SCHED_H

#include "core/sched/task.h"

/* Turn the current execution context (kmain) into task 0 and make it the sole
 * member of the run queue, so there is always exactly one task running. Call
 * once, before task_create() or sched_yield(). kmain keeps running on its
 * existing boot stack; task 0 simply gives that flow a struct task and a slot in
 * the ring. Its rsp is recorded the first time it yields away. */
void sched_init(void);

/* Create a new READY task that will begin executing at entry on its first
 * switch-in, and append it to the run queue. Allocates the struct task and a
 * fresh KERNEL_STACK_SIZE kernel stack, and forges the stack so the first
 * context_switch into it lands cleanly at entry (see sched.c). Returns the task
 * (NULL on allocation failure). `entry` should not return in 4a; if it does, a
 * trampoline parks it harmlessly (clean exit/reaping is 4b+). */
struct task *task_create(void (*entry)(void));

/* Cooperatively give up the CPU: pick the next task round-robin and switch to
 * it. Returns (in this task) only once it is later scheduled again. If this is
 * the only task in the run queue, it is a no-op and returns immediately. */
void sched_yield(void);

#endif /* SCRAPOS_CORE_SCHED_SCHED_H */
