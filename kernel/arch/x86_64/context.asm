; context.asm — the x86_64 context switch (Phase 4, step 4a).
;
; This is the dirty machine half of multitasking that ARCHITECTURE.md §4 keeps
; out of the scheduler. It saves the outgoing task's CPU state on its own stack,
; flips rsp to the incoming task's stack, restores that task's state, and returns
; into it. One function, called as a normal C function from sched_yield().
;
; WHY ONLY THE CALLEE-SAVED REGISTERS
; context_switch is reached through an ordinary `call`, so the SysV AMD64 ABI is
; in force. The C caller has ALREADY preserved every caller-saved (volatile)
; register it still needed — rax, rcx, rdx, rsi, rdi, r8-r11 — because to the
; compiler a function call may clobber them. So the only registers whose live
; values would otherwise be lost across the switch are the callee-saved
; (non-volatile) ones: rbx, rbp, r12, r13, r14, r15. rsp is saved/loaded
; explicitly below; rip is carried by the call/ret pair (each task's "where was
; I" is the return address on its own stack). That is the entire resumable state
; of a task suspended INSIDE a function call — which is why a cooperative switch
; is cheap. (A preemptive switch from an interrupt can't assume this and must
; save all 15 GPRs; that is what the ISR stubs do.)

bits 64

section .text
global context_switch

; void context_switch(uint64_t *old_rsp, uint64_t new_rsp)
;   rdi = old_rsp  (address to store the outgoing rsp into)
;   rsi = new_rsp  (the incoming task's saved rsp)
context_switch:
    ; Save the outgoing task's callee-saved registers on ITS OWN stack. Order
    ; matters: it must mirror the pop order below (and the order a new task's
    ; stack is forged in sched.c) so a saved task is restored byte-for-byte.
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15

    mov [rdi], rsp      ; *old_rsp = rsp — remember exactly where we stopped.
    mov rsp, rsi        ; THE SWITCH: rsp now points into the incoming task's
                        ; stack. Everything below runs on that stack.

    ; Restore the incoming task's callee-saved registers (mirror of the pushes:
    ; r15 was at the lowest address, so it pops first).
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp

    ret                 ; Pop the incoming stack's return address into rip and
                        ; jump there. For a resumed task that is its old
                        ; sched_yield() call site; for a brand-new task it is the
                        ; forged entry_fn address, so it begins executing cleanly.
