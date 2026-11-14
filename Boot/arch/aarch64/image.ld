ENTRY(_head)
SECTIONS
{
	. = 0;
	.text : { __text_start = .; KEEP(*(.text.boot)) *(.text .text.*) __text_end = .; }
	.rodata : { __rodata_start = .; *(.rodata .rodata.*) __rodata_end = .; }
	.data : { __data_start = .; *(.data .data.*) __data_end = .; }
	.bss (NOLOAD) : { __bss_start = .; *(.bss .bss.*) *(COMMON) __bss_end = .; }
}
