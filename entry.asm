
[BITS 32]
[SECTION .text]
global _start
extern _kernel_main     ; MSYS2 GCC จะเติม _ นำหน้าฟังก์ชัน C เสมอ

_start:
    call _kernel_main
    cli
.hang:
    hlt
    jmp .hang4