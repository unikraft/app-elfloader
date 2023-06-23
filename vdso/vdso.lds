/* SPDX-License-Identifier: GPL-2.0 */
/* Modified from Linux kernel */

/*
 * Linker script for vDSO.  This is an ELF shared object prelinked to
 * its virtual address, and with only one read-only segment.
 * This script controls its layout.
 */

SECTIONS
{
	. = SIZEOF_HEADERS;

	.hash		: { *(.hash) }			:text
	.gnu.hash	: { *(.gnu.hash) }
	.dynsym		: { *(.dynsym) }
	.dynstr		: { *(.dynstr) }
	.gnu.version	: { *(.gnu.version) }
	.gnu.version_d	: { *(.gnu.version_d) }
	.gnu.version_r	: { *(.gnu.version_r) }

	.dynamic	: { *(.dynamic) }		:text	:dynamic

	.rodata		: {
		*(.rodata*)
		*(.data*)
		*(.sdata*)
		*(.got.plt) *(.got)
		*(.gnu.linkonce.d.*)
		*(.bss*)
		*(.dynbss*)
		*(.gnu.linkonce.b.*)
	}						:text

	/*
	 * Discard .note.gnu.property sections which are unused and have
	 * different alignment requirement from vDSO note sections.
	 */
	/DISCARD/ : {
		*(.note.gnu.property)
	}
	.note		: { *(.note.*) }		:text	:note

	.eh_frame_hdr	: { *(.eh_frame_hdr) }		:text	:eh_frame_hdr
	.eh_frame	: { KEEP (*(.eh_frame)) }	:text


	/*
	 * Text is well-separated from actual data: there's plenty of
	 * stuff that isn't used at runtime in between.
	 */

	.text		: {
		*(.text*)
	}		:text


	.altinstructions	: { *(.altinstructions) }	:text
	.altinstr_replacement	: { *(.altinstr_replacement) }	:text

	/DISCARD/ : {
		*(.discard)
		*(.discard.*)
	}
}


/*
 * We must supply the ELF program headers explicitly to get just one
 * PT_LOAD segment, and set the flags explicitly to make segments read-only.
 */
PHDRS
{
	text		PT_LOAD		FLAGS(5) FILEHDR PHDRS; /* PF_R|PF_X */
	dynamic		PT_DYNAMIC	FLAGS(4);		/* PF_R */
	note		PT_NOTE		FLAGS(4);		/* PF_R */
	eh_frame_hdr	PT_GNU_EH_FRAME;
}

VERSION {
	LINUX_2.6 {
	global:
		__kernel_vsyscall;
		__vdso_clock_gettime;
		__vdso_gettimeofday;
		__vdso_time;
		__vdso_clock_getres;
	local: *;
	};
}
