#!/usr/bin/python
# -*- coding: utf-8 -*-

#

# Note: run with sudo python testOpcodeFile.py
# Make sure you have run with the standard run.sh at least once first so drivers are loaded
# Expected input format is a file with the following per line: opcode numBytes, e.g. 0f0d97 3
# To work with large numbers of opcodes, make a custom config file with just one programmable counter and then grep / otherwise parse values as needed

import ctypes
import sys

nanobench = ctypes.CDLL('./nanobench.so')
nanobench.profileInstr.argtypes = (ctypes.c_char_p, ctypes.c_char_p, ctypes.c_int, ctypes.c_bool)
config = ctypes.c_char_p("configs/your-custom-cfg.txt")

lines = []

if len(sys.argv) < 2:
	print("Error: please include filename as argument")
	sys.exit(1)

with open(sys.argv[1], "r") as logfile:
	lines = logfile.read().splitlines()

for line in lines:
	values = line.split()	# split on whitespace
	instr = bytearray.fromhex(values[0])
	instr_bytes = (ctypes.c_char * len(instr)).from_buffer(instr)
	instrLen = int(values[1])	
	print(values[0]),
	print(" "),
	nanobench.profileInstr(instr_bytes, config, ctypes.c_int(instrLen), ctypes.c_bool(True))

