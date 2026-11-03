bits 32
section .text
    align 4
    ; ของ Qemu
    dd 0x1BADB002              ; Magic Number
    dd 0x00                    ; Flags
    dd -(0x1BADB002 + 0x00)    ; Checksum

global _start
extern kernel_main

_start:
    cli
    call kernel_main
.hang:
    hlt
    jmp .hang
