ENTRY(_start)
PHDRS
{
  text PT_LOAD FLAGS(5);
  rodata PT_LOAD FLAGS(4);
  data PT_LOAD FLAGS(6);
}
SECTIONS
{
  __virtual_base = 0xffffffff80000000;
  __physical_base = 0x900200000;
  . = __virtual_base;
  .text : AT(__physical_base) ALIGN(0x1000) {
    *(.text .text.*)
  } :text
  . = ALIGN(0x1000);
  .rodata : AT(__physical_base + (ADDR(.rodata) - __virtual_base)) {
    *(.rodata .rodata.*)
  } :rodata
  . = ALIGN(0x1000);
  .data : AT(__physical_base + (ADDR(.data) - __virtual_base)) {
    KEEP(*(.byter_requests_start))
    KEEP(*(.byter_requests))
    KEEP(*(.byter_requests_end))
    *(.data .data.*)
    *(.got .got.*)
  } :data
  . = ALIGN(0x1000);
  .bss (NOLOAD) : AT(__physical_base + (ADDR(.bss) - __virtual_base)) {
    *(.bss .bss.*)
    *(COMMON)
  } :data
}
