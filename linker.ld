ENTRY(_start)

SECTIONS
{
    . = 0x10000;

    .text : {
        entry.o(.text)
        (.text)
    }

    .rodata : {
        (.rodata)
        (.rdata)
    }

    .data : {
        (.data)
    }

    .bss : {
        *(COMMON)
        (.bss)
    }
}