/*
 * Section map for the RedHills Cafe link (ld.lld), following the Green Hills Cafe link of the retail
 * image (tools/rpx/link_ghs.py): code from 0x02000000 with .syscall first, the RPL import sections right
 * after .text so code reaches them with a 24-bit branch (tools/rpx/elf2rpx.py moves them to the loader
 * region and keeps those branches' relocations), and data from 0x10000000. Static constructor and
 * destructor lists go in .rodata, bounded by the symbols the crt's _main walks
 * (tools/redhills/runtime/cafe_start.c). build_redhills.py writes the import section list in place of
 * the IMPORT_SECTIONS line.
 */

OUTPUT_FORMAT("elf32-powerpc")
OUTPUT_ARCH(powerpc:common)
ENTRY(_start)

SECTIONS
{
	.syscall 0x02000000 : {
		KEEP(*(.syscall))
	}

	.text : ALIGN(32) {
		*(.text .text.* .gnu.linkonce.t.*)
	}

	/* IMPORT_SECTIONS */

	.rodata 0x10000000 : ALIGN(256) {
		*(.rodata .rodata.* .gnu.linkonce.r.*)
		*(.rodata1)
		*(.sdata2 .sdata2.* .gnu.linkonce.s2.*)
		*(.sbss2 .sbss2.* .gnu.linkonce.sb2.*)
		. = ALIGN(4);
		PROVIDE_HIDDEN(__preinit_array_start = .);
		KEEP(*(.preinit_array))
		PROVIDE_HIDDEN(__preinit_array_end = .);
		PROVIDE_HIDDEN(__init_array_start = .);
		KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*) SORT_BY_INIT_PRIORITY(.ctors.*)))
		KEEP(*(.init_array .ctors))
		PROVIDE_HIDDEN(__init_array_end = .);
		PROVIDE_HIDDEN(__fini_array_start = .);
		KEEP(*(SORT_BY_INIT_PRIORITY(.fini_array.*) SORT_BY_INIT_PRIORITY(.dtors.*)))
		KEEP(*(.fini_array .dtors))
		PROVIDE_HIDDEN(__fini_array_end = .);
	}

	.data : ALIGN(32) {
		*(.data .data.* .gnu.linkonce.d.*)
		*(.data1)
		*(.sdata .sdata.* .gnu.linkonce.s.*)
		*(.got1 .got2 .got .fixup)
	}

	.module_id : ALIGN(32) {
		KEEP(*(.module_id))
	}

	.bss (NOLOAD) : ALIGN(64) {
		*(.sbss .sbss.* .gnu.linkonce.sb.*)
		*(.scommon)
		*(.bss .bss.* .gnu.linkonce.b.*)
		*(COMMON)
	}

	/* Unwind tables and notes: the image is built without exceptions and carries no notes. */
	/DISCARD/ : {
		*(.eh_frame .eh_frame.* .eh_frame_hdr .gcc_except_table .gcc_except_table.*)
		*(.note.GNU-stack .note.gnu.* .comment .gnu.attributes .jcr .tm_clone_table)
	}
}
