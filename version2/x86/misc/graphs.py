#!/usr/bin/python

# -*- coding: utf-8 -*-

#

import numpy as np
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker
import sys

opcodesDoc = []
opcodesRan = []
opcodesExcpt = []
signals = []
timingsRan = []
timingsExcpt = []
lines = []
skipped = 0

# CONFIGURATION
# ==================
#graphType = 'doc'		# plot only documented opcodes
graphType = 'undoc'		# plot only undocumented opcodes ('RAN' only, no exceptions)
x86TimingLogMode = True		# use different log format (x86 instead of RISC-V)
numBins = 10000 if not x86TimingLogMode else 50	  # number of histogram bins (more bins -> more fine-grained detail, but need FAR more data and we have less for x86)
graphThreshold = 5000		# skip opcodes w higher cycle count than this (in x86TimingLogMode)
printThreshold = 3700		# print line with any timing higher than this

# EXPECTED INPUT FORMAT if not x86TimingLogMode:
# 00000000000000001001011010010100 9694              illegal        EXCPT 11
# 00000000000000011000000100000001 18101              illegal       RAN

# EXPECTED INPUT FORMAT if x86TimingLogMode:
# c50c                              RAN 0 GENERAL_ERROR avg 20 min 18 test 3 max 312 test 5304
# c6742018                          EXCPT 4 GENERAL_ERROR avg 3324 min 3279 test 2166 max 382879 test 4105
# ==================


if len(sys.argv) < 2:
	print("Error: please include input filenames as arguments")
	sys.exit(1)

for filename in sys.argv[1:]:
	try:
		inputFile = open(filename)
		lines.extend(inputFile.read().splitlines())
		inputFile.close()
	except IOError:
		print("Error, couldn't open file: " + filename)
		sys.exit(1)

for line in lines:
	values = line.split()	# split on whitespace

	if(not x86TimingLogMode):
		if( len(values) < 3 or len(values) > 5 ):
			print("Malformed line, skipping. Line is:")
			print(line)
			skipped += 1
			continue
		elif(len(values) == 3):
			if(values[2] != "illegal"):
				opcodesDoc.append(int(values[1], 16))
			else:
				print("Malformed line, skipping. Line is:")
				print(line)
				skipped += 1
				continue
		else:
			if(values[3] == "EXCPT"):
				opcodesExcpt.append(int(values[1], 16))
				signals.append(int(values[4]))
			elif(values[3] == "RAN"):
				opcodesRan.append(int(values[1], 16))
			else:
				print("Malformed line, skipping. Line is:")
				print(line)
				skipped += 1
				continue
	else:
		if ( len(values) < 2 ):
			print("Malformed line, skipping. Line is:")
			print(line)
			skipped += 1
			continue	
		elif (values[1] == "RAN"):
			if(int(values[5]) > printThreshold):
				print(line)
			if(int(values[5]) < graphThreshold):
				opcodesRan.append(int(values[0], 16))
				timingsRan.append(int(values[5]))
		elif (values[1] == "EXCPT"):
			if(int(values[5]) > printThreshold):
				print(line)
			if(int(values[5]) < graphThreshold):
				opcodesExcpt.append(int(values[0], 16))
				timingsExcpt.append(int(values[5]))
				signals.append(int(values[2]))
		#there are no opcodesDoc in x86 timing attack log files

countDoc = len(opcodesDoc)
countRan = len(opcodesRan)
countExcpt = len(opcodesExcpt)
countSigill = signals.count(4)
countSigsegv = signals.count(11)
countSigbus = signals.count(7)
countSigother = countExcpt - countSigill - countSigsegv - countSigbus
countExcptNotIllegal = countExcpt - countSigill

print("Number of illegal opcodes (SIGILL): " + str(countSigill))
print("Number documented opcodes: " + str(countDoc))
print("Number undoc opcodes (ran): " + str(countRan))
print("Number possible undoc opcodes (exception but not SIGILL): " + str(countExcptNotIllegal))
print("Of which: " + str(countSigsegv) + " SIGSEGV, " + str(countSigbus) + " SIGBUS, " + str(countSigother) + " OTHER SIGNAL")
print("Did not check for duplicates.")
print("Use 'sort filename | uniq -d -s 33 -w 8 | wc -l' to count duplicated lines (jumps?) and 'sort filename | uniq -D -s 33 -w 8' to view them")
print("Skipped " + str(skipped) + " malformed lines")

import matplotlib.ticker as mticker

fig, ax = plt.subplots()
if x86TimingLogMode:
	listMax = max(opcodesExcpt)
	timeMax = max(timingsExcpt)
	temp = [sig==4 for sig in signals]
	colours = np.asarray(temp)
	ax.scatter(opcodesExcpt, timingsExcpt, c=np.where(colours,'b','r'))	
	ax.set_xlim([0, listMax+(0.1*listMax)])
	ax.set_ylim([0, timeMax+(0.1*timeMax)])
	#can't do ax.bar for all opcodes, run out of memory
elif graphType == 'doc':
	ax.hist(opcodesDoc, bins=numBins)
	ax.set_ylabel("Number of documented opcodes")
elif graphType == 'undoc':
	ax.hist(opcodesRan, bins=numBins)
	ax.set_ylabel("Number of undocumented opcodes")
ax.set_xlabel("Opcode (hex)")
axes = plt.gca()
axes.get_xaxis().set_major_formatter(ticker.FormatStrFormatter("%03x"))   #format x axis as hex
plt.show()
    

