; isr.asm — exception entry stubs for vectors 0..31 (Phase 1).
;
; When the CPU takes an exception it jumps to the gate's offset, which is one of
; the isr_stub_N labels below. Each stub does the minimum to make every
; exception's stack frame look identical, then falls into the shared isr_common,
; which saves registers and calls the C handler.
;
; THE ERROR-CODE PROBLEM
; Some exceptions (8,10,11,12,13,14,17,21,29,30) make the CPU push a 4-byte
; error code; the rest don't. To give the C side ONE struct layout, the stubs
; for the no-error vectors push a dummy 0 in that slot. After the stubs, every
; frame is: [vector][error_code][rip][cs][rflags][rsp][ss].

bits 64

extern isr_handler          ; the common C handler, in isr.c

section .text

; ---- per-vector stub macros ------------------------------------------------

; Vector with NO CPU error code: push a fake one so the slot always exists.
%macro ISR_NOERR 1
isr_stub_%1:
    push qword 0            ; dummy error code
    push qword %1           ; vector number
    jmp isr_common
%endmacro

; Vector where the CPU ALREADY pushed an error code: just add the vector.
%macro ISR_ERR 1
isr_stub_%1:
    push qword %1           ; vector number (error code already beneath it)
    jmp isr_common
%endmacro

; ---- the 32 CPU exception vectors ------------------------------------------
; Error-code pushers are marked ISR_ERR; everything else ISR_NOERR.
ISR_NOERR 0     ; #DE Divide error
ISR_NOERR 1     ; #DB Debug
ISR_NOERR 2     ; NMI
ISR_NOERR 3     ; #BP Breakpoint   <-- the int3 self-test lands here
ISR_NOERR 4     ; #OF Overflow
ISR_NOERR 5     ; #BR BOUND range
ISR_NOERR 6     ; #UD Invalid opcode
ISR_NOERR 7     ; #NM Device not available
ISR_ERR   8     ; #DF Double fault          (error code)
ISR_NOERR 9     ; Coprocessor segment overrun (legacy)
ISR_ERR   10    ; #TS Invalid TSS           (error code)
ISR_ERR   11    ; #NP Segment not present   (error code)
ISR_ERR   12    ; #SS Stack-segment fault   (error code)
ISR_ERR   13    ; #GP General protection    (error code)
ISR_ERR   14    ; #PF Page fault            (error code)
ISR_NOERR 15    ; (reserved)
ISR_NOERR 16    ; #MF x87 FPU error
ISR_ERR   17    ; #AC Alignment check       (error code)
ISR_NOERR 18    ; #MC Machine check
ISR_NOERR 19    ; #XM SIMD FP exception
ISR_NOERR 20    ; #VE Virtualization
ISR_ERR   21    ; #CP Control protection    (error code)
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_ERR   29    ; #VC VMM communication     (error code)
ISR_ERR   30    ; #SX Security exception    (error code)
ISR_NOERR 31    ; (reserved)

; ---- the shared tail -------------------------------------------------------
isr_common:
    ; Save all 15 general-purpose registers. The PUSH ORDER is deliberate: the
    ; last value pushed sits at the lowest address, and that must line up with
    ; the FIRST field of `struct interrupt_frame`. So we push r15 first and rax
    ; last, giving an in-memory order of rax, rbx, ... r15 (low -> high).
    push r15
    push r14
    push r13
    push r12
    push r11
    push r10
    push r9
    push r8
    push rbp
    push rdi
    push rsi
    push rdx
    push rcx
    push rbx
    push rax

    cld                     ; SysV ABI requires DF=0 on entry to C code

    ; rsp now points at the fully-built interrupt_frame. We must pass that as
    ; arg0 (rdi) AND keep the stack 16-byte aligned for the SysV ABI. We can't
    ; assume the stack was aligned when the exception fired, so:
    mov rbp, rsp            ; rbp = frame pointer (and our arg). rbp's original
                            ; value is already saved on the stack, so clobbering
                            ; the register here is safe — it gets popped back.
    and rsp, -16            ; force 16-byte alignment (may waste up to 8 bytes)
    mov rdi, rbp            ; arg0 = &interrupt_frame
    call isr_handler
    mov rsp, rbp            ; drop any alignment padding

    ; --- epilogue ---
    ; In Phase 1 isr_handler never returns (every exception halts), so the code
    ; below is effectively dead today. It is written out in full anyway so the
    ; exception path is complete and ready for later phases that DO resume.
    pop rax
    pop rbx
    pop rcx
    pop rdx
    pop rsi
    pop rdi
    pop rbp
    pop r8
    pop r9
    pop r10
    pop r11
    pop r12
    pop r13
    pop r14
    pop r15

    add rsp, 16             ; discard the vector + error_code slots
    iretq                   ; pop rip,cs,rflags,rsp,ss and resume

; ---- table the C side uses to wire up the IDT ------------------------------
; A flat array of the 32 stub addresses, so idt.c can install them in a loop
; instead of naming each extern by hand.
section .rodata
global isr_stub_table
isr_stub_table:
%assign i 0
%rep 32
    dq isr_stub_ %+ i
%assign i i+1
%endrep
