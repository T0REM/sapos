/* sched.c — cooperative round-robin scheduler (Phase 4, step 4a).
 *
 * Architecture-independent (ARCHITECTURE.md §4). It owns the run queue and the
 * round-robin policy; the only machine-specific thing it does is call the arch
 * layer's context_switch() through its header — the one named seam. No assembly
 * lives here. See sched.h for the contract and task.h for the structure.
 */
#include <stddef.h>
#include <stdint.h>

#include "core/sched/sched.h"
#include "core/sched/task.h"
#include "core/mm/slab.h"            /* kmalloc — struct task + kernel stacks   */
#include "arch/x86_64/context.h"     /* context_switch — the arch seam (§4)     */

/* The run queue is a circular singly-linked list of every task. `current` is the
 * task executing right now (always non-NULL after sched_init). Round-robin is
 * then just "advance to current->next". With only READY/RUNNING states in 4a,
 * every task but `current` is runnable, so the immediate successor is always a
 * valid next choice — no scanning needed. */
static struct task *current = NULL;

/* Monotonic id source. Task 0 is kmain (assigned in sched_init). */
static uint32_t next_id = 0;

void sched_init(void) {
    /* Adopt the currently-running flow (kmain) as task 0. We do NOT allocate a
     * stack for it: it already owns the boot stack and is running on it right
     * now. We only need a struct task so it has a node in the ring and a place
     * to save its rsp when it first yields. */
    struct task *t = kmalloc(sizeof *t);

    t->rsp        = 0;            /* filled in by the first switch AWAY from it */
    t->stack_base = NULL;         /* boot stack, not ours to free               */
    t->id         = next_id++;    /* -> 0                                       */
    t->state      = TASK_RUNNING; /* it is, in fact, what is running            */
    t->next       = t;            /* ring of one: points at itself              */

    current = t;
}

/* Where a task's entry function lands if it ever RETURNS. In 4a we have no clean
 * task exit/reaping yet, so instead of letting entry_fn `ret` into nowhere we
 * forge its return address to point here and simply yield forever, parking the
 * finished task harmlessly. Proper teardown (free the stack, unlink from the
 * ring, never schedule it again) arrives in 4b+. */
static void task_trampoline(void) {
    for (;;) {
        sched_yield();
    }
}

struct task *task_create(void (*entry)(void)) {
    struct task *t = kmalloc(sizeof *t);
    if (t == NULL) {
        return NULL;
    }

    /* A whole, page-aligned kernel stack. kmalloc(16 KiB) routes through the
     * buddy large path and returns a usable kernel VIRTUAL pointer — so no HHDM
     * math leaks into this core-layer file (see task.h). */
    void *stack = kmalloc(KERNEL_STACK_SIZE);
    if (stack == NULL) {
        kfree(t);
        return NULL;
    }

    /* --- Forge the initial stack -------------------------------------------
     *
     * Build a fake saved-context frame that context_switch's epilogue will pop,
     * so the FIRST switch into this task `ret`s straight into entry(). Laid out
     * from the top of the stack downward (high -> low address):
     *
     *   [top]            16-aligned top of the stack
     *   [top-8]   : task_trampoline   <- entry()'s return address (safety net)
     *   [top-16]  : entry             <- where context_switch's `ret` jumps
     *   [top-24]  : rbp = 0  ]
     *   [top-32]  : rbx = 0  ]
     *   [top-40]  : r12 = 0  ]  the six callee-saved slots the epilogue pops,
     *   [top-48]  : r13 = 0  ]  in the SAME order context.asm pops them
     *   [top-56]  : r14 = 0  ]
     *   [top-64]  : r15 = 0  ]  <- task->rsp starts here
     *
     * Alignment: the SysV ABI puts rsp at 0 (mod 16) right before a `call`, i.e.
     * 8 (mod 16) at the callee's first instruction. After context_switch pops the
     * six regs and `ret`s entry off [top-16], rsp = top-8; with top 16-aligned,
     * top-8 == 8 (mod 16) — exactly what entry() would see from a real call. */
    uint64_t top = (uint64_t)stack + KERNEL_STACK_SIZE;
    top &= ~(uint64_t)0xF;                   /* align the top down to 16 */
    uint64_t *sp = (uint64_t *)top;

    *--sp = (uint64_t)task_trampoline;       /* [top-8]  entry()'s return addr  */
    *--sp = (uint64_t)entry;                 /* [top-16] context_switch ret tgt */
    *--sp = 0;                               /* [top-24] rbp                    */
    *--sp = 0;                               /* [top-32] rbx                    */
    *--sp = 0;                               /* [top-40] r12                    */
    *--sp = 0;                               /* [top-48] r13                    */
    *--sp = 0;                               /* [top-56] r14                    */
    *--sp = 0;                               /* [top-64] r15                    */

    t->rsp        = (uint64_t)sp;
    t->stack_base = stack;
    t->id         = next_id++;
    t->state      = TASK_READY;

    /* Append to the ring so tasks run in creation order. Walk to the tail (the
     * node that links back to `current`) and splice in just before `current`.
     * The list is tiny, so the O(n) walk is irrelevant. */
    struct task *tail = current;
    while (tail->next != current) {
        tail = tail->next;
    }
    tail->next = t;
    t->next    = current;

    return t;
}

void sched_yield(void) {
    struct task *prev = current;
    struct task *next = prev->next;   /* round-robin: the immediate successor */

    if (next == prev) {
        return;                       /* only one task — nothing to switch to */
    }

    prev->state = TASK_READY;
    next->state = TASK_RUNNING;
    current     = next;

    /* Save prev's rsp into prev->rsp, load next->rsp, and resume next. When prev
     * is later scheduled again, control returns right here, on prev's stack. */
    context_switch(&prev->rsp, next->rsp);
}
