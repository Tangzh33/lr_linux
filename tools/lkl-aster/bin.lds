# SPDX-License-Identifier: GPL-2.0

OUTPUT_FORMAT(elf64-x86-64);

PHDRS
{
	header PT_LOAD FLAGS(4);  # R__
	text   PT_LOAD FLAGS(5);  # R_E
	rodata PT_LOAD FLAGS(4);  # R__
	data   PT_LOAD FLAGS(6);  # RW_
}

PROVIDE(BASE_ADDRESS = 0xffffffffc0001000);
PAGE_SIZE = 4096;

SECTIONS {
	#################################################
	# Header
	#################################################

	. = BASE_ADDRESS - PAGE_SIZE;

	.header : {
		*(.header)
	} : header

	#################################################
	# Text
	#################################################

	. = BASE_ADDRESS;
	_text = .;

	aster_call = BASE_ADDRESS - PAGE_SIZE;

	.text : {
		*(.setup)

		*(.text)
		*(.text.*)

		*(.ltext)
	} : text

	_etext = .;
	_stext = . - _text;

	#################################################
	# Rodata
	#################################################

	. = ALIGN(PAGE_SIZE);
	_rodata = .;

	.rodata : {
		*(.rodata)
		*(.rodata.*)

		*(.builtin_fw)
		*(.notes)
		*(.pci_fixup)

		*(__ex_table)
		*(__modver)
		*(__param)
	} : rodata

	_erodata = .;
	_srodata = . - _rodata;

	#################################################
	# Data
	#################################################

	. = ALIGN(PAGE_SIZE);
	_data = .;

	.data : {
		*(.data)
		*(.data.*)

		*(.ldata)

		*(.got.plt)
		*(.init.data)
	} : data

	_edata = .;
	_sdata = . - _data;

	#################################################
	# Bss
	#################################################

	. = ALIGN(PAGE_SIZE);
	_bss = .;

	.bss : {
		*(.bss)
		*(.bss.*)

		*(.lbss)
	} : data

	_ebss = .;
	_sbss = . - _bss;

	#################################################
	# None
	#################################################

	/DISCARD/ : { *(*) }
}
