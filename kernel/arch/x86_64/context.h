/* context.h — the one named seam the scheduler uses to switch CPU context.
 *
 * Per ARCHITECTURE.md §4, the scheduler (core layer) must not contain assembly
 * and must not know it is on x86. It expresses "stop running task A, start
 * running task B" as a single call to this function; the x86_64 register
 * save/restore that actually implements it lives in context.asm and is the only
 * thing that knows about rbx/rbp/r12-r15 and rsp.
 */
#ifndef SCRAPOS_ARCH_X86_64_CONTEXT_H
#define SCRAPOS_ARCH_X86_64_CONTEXT_H

#include <stdint.h>

/* Switch from the current task to another.
 *   old_rsp — where to STORE the outgoing task's stack pointer (i.e. &task->rsp
 *             of the task we are leaving). After context_switch returns into the
 *             outgoing task in some future switch, this is what was reloaded.
 *   new_rsp — the incoming task's previously-saved (or freshly-forged) rsp.
 *
 * Implemented in context.asm: it pushes the SysV callee-saved registers onto the
 * current stack, saves rsp into *old_rsp, loads rsp from new_rsp, pops the
 * callee-saved registers back, and `ret`s — which returns into the NEW task's
 * saved context (for a brand-new task, into its forged entry frame). */
void context_switch(uint64_t *old_rsp, uint64_t new_rsp);

#endif /* SCRAPOS_ARCH_X86_64_CONTEXT_H */
