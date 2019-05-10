#!/usr/bin/python

# -*- coding: utf-8 -*-

#

import numpy as np
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker
import sys
import re

opcodes = []
lengths = []

# EXPECTED INPUT FORMAT:
# r:  0004010000000000... add byte ptr [rcx+rax*1], al                                               ( 3)  .r: ( 3) sigtrap   2  ffffffff  000401
# ==================

if len(sys.argv) != 2:
	print("Error: please include an input filename as an argument")
	sys.exit(1)

with open(sys.argv[1], "r") as logfile:
	for line in logfile:
		lens = re.search('\(\s*[0-9]+\)  .r: \(\s*([0-9]+)\)', line)
		if(lens != None):
			opcode = re.search('r:\s+([0-9a-z]+)', line)
			if opcode == None:
				print(line) #debug
			else:
				opcodes.append(int(opcode.group(1), 16))
				lengths.append(int(lens.group(1)))

fig, ax = plt.subplots()
fig.set_size_inches(20, 11.25) # fullscreen size
ax.step(opcodes,lengths)
ax.set_xlabel("Instruction (hex)")
ax.set_ylabel("Instruction length (bytes)")
plt.yticks(np.arange(0, max(lengths)+1, 1.0))
axes = plt.gca()
axes.get_xaxis().set_major_formatter(ticker.FormatStrFormatter("%03x"))   #format x axis as hex
plt.savefig('decodedLengths.png', dpi=100)

