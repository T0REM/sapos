/* task.h — the task structure and run-queue node (Phase 4, step 4a).
 *
 * A "task" here is a kernel thread: an independent flow of control with its own
 * kernel stack, scheduled cooperatively (it runs until it calls sched_yield()).
 * There are no separate address spaces yet — every task shares the one kernel
 * page table built in Phase 3. Processes / userspace / per-task address spaces
 * come later (Phase 5).
 *
 * Per ARCHITECTURE.md §4 this is the architecture-INDEPENDENT half of
 * multitasking: a struct task is pure bookkeeping (a saved stack pointer, a
 * state, a run-queue link). It contains no x86 and no assembly. The actual
 * register save/restore lives in the arch layer (arch/x86_64/context.asm); the
 * scheduler only ever asks for it through the named context_switch() seam.
 */
#ifndef SCRAPOS_CORE_SCHED_TASK_H
#define SCRAPOS_CORE_SCHED_TASK_H

#include <stdint.h>

/* Per-task kernel stack size: 16 KiB. Chosen so kmalloc routes it through the
 * buddy large-allocation path (anything > 2048 B does), which yields whole,
 * page-aligned, contiguous pages — exactly what a stack wants — while still
 * handing back a ready-to-use kernel VIRTUAL pointer. That keeps the scheduler
 * free of any physical->virtual / HHDM math (see task_create in sched.c). */
#define KERNEL_STACK_SIZE 16384u

/* A task is either waiting its turn on the run queue, or actively executing.
 * 4a has no blocking/sleeping/exiting states yet — with only these two, every
 * task except the running one is always runnable, which is what makes "advance
 * to the next link" a correct round-robin choice (see sched_yield). */
enum task_state {
    TASK_READY,    /* on the run queue, eligible to be switched in */
    TASK_RUNNING,  /* currently executing on the CPU              */
};

struct task {
    /* Saved kernel stack pointer. Meaningful only while the task is NOT
     * running: it points at the task's forged/saved register frame, and is the
     * value context_switch() reloads to resume the task. The running task's
     * real rsp lives in the CPU; this field is stale until it yields. */
    uint64_t rsp;

    /* Base of the kmalloc'd kernel stack (its LOW address). Kept so the stack
     * can be freed when clean task exit/reaping arrives (4b+); unused in 4a. */
    void *stack_base;

    uint32_t        id;     /* small monotonic id, handy for debug prints */
    enum task_state state;

    /* Intrusive run-queue link. The run queue is a CIRCULAR singly-linked list,
     * so `next` is never NULL — the last task points back at the first. That is
     * what makes round-robin a one-line "current = current->next". */
    struct task *next;
};

#endif /* SCRAPOS_CORE_SCHED_TASK_H */
