/* Kernel layout: linked at physical 0x1000, where Penguin loads Linux/m68k
   kernels. One loadable segment; _end is page-aligned and is where Penguin
   places the bootinfo records. */
OUTPUT_FORMAT("elf32-m68k")
OUTPUT_ARCH(m68k)
ENTRY(_stext)

PHDRS
{
	image PT_LOAD;
}

SECTIONS
{
	.text 0x1000 : {
		*(.text.head)
		*(.text .text.*)
	} :image
	.rodata : { *(.rodata .rodata.*) } :image
	.data : { *(.data .data.*) } :image
	.bss (NOLOAD) : {
		__bss_start = .;
		*(.bss .bss.*)
		*(COMMON)
		__bss_end = .;
		. = ALIGN(4096);
		_end = .;
	} :image
	/DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
