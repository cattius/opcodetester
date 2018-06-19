#!/usr/bin/python

# -*- coding: utf-8 -*-

#

import numpy as np
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker

#choose which graphs are generated
firstGraph = False  #full opcodes
secondGraph = False  #group by 1st byte
thirdGraph = True  #group by num of opcode bytes (1, 2, or 3)

opcodeFile = open('output.txt')   #feed in output file (pipe test.c output to file from test.c)
lines = opcodeFile.read().splitlines()
opcodes = np.zeros(len(lines), dtype=np.int64)
firstBytes = np.zeros(len(lines), dtype=np.int64)
i = 0
oneByteOpcodeCount = 0
twoByteOpcodeCount = 0
threeByteOpcodeCount = 0
for opcode in lines:
    if(len(opcode) == 2):
        oneByteOpcodeCount = oneByteOpcodeCount + 1
    elif(len(opcode) == 4):
        twoByteOpcodeCount = twoByteOpcodeCount + 1
    elif(len(opcode) == 6):
        threeByteOpcodeCount = threeByteOpcodeCount + 1
    else:
        print("Error: opcode length not 2, 4, or 6")

    intOpcode = int(opcode, 16)     #convert from hex to int
    intFirstByte = int(opcode[0:2], 16)
    opcodes[i] = intOpcode
    firstBytes[i] = intFirstByte
    i = i + 1

#graph 1
if(firstGraph):
    fig, ax = plt.subplots()
    ax.hist(opcodes, bins='auto')
    ax.set_xlabel("Opcode (hex)")
    ax.set_ylabel("Number of undocumented opcodes")
    maxOpcodeVal = 0xffffff
    xinterval = np.arange(0,maxOpcodeVal,maxOpcodeVal / 15)
    ax.set_xticks(xinterval)
    ax.margins(x=0)
    axes = plt.gca()
    axes.get_xaxis().set_major_formatter(ticker.FormatStrFormatter("%x"))   #format x axis as hex
    fig.tight_layout()  #prevent y-axis label getting clipped
    plt.show()

#graph 2
if(secondGraph):
    fig, ax = plt.subplots()
    ax.hist(firstBytes, bins='auto')
    ax.set_xlabel("Opcode first byte (hex)")
    ax.set_ylabel("Number of undocumented opcodes")
    maxVal = np.max(firstBytes)
    minVal = np.min(firstBytes)
    rangeVal = maxVal - minVal
    xinterval = np.arange(0,maxVal,rangeVal / 15)
    ax.set_xticks(xinterval)
    ax.margins(x=0)
    axes = plt.gca()
    axes.get_xaxis().set_major_formatter(ticker.FormatStrFormatter("%x"))   #format x axis as hex
    fig.tight_layout()  #prevent y-axis label getting clipped
    plt.show()

#graph 3
if(thirdGraph):
    #current counts for reference: (161, 20453, 2007923)
    print(oneByteOpcodeCount, twoByteOpcodeCount, threeByteOpcodeCount) #DEBUG
    fig, ax = plt.subplots()
    byteCounts = np.zeros(3)
    byteCounts[0] = oneByteOpcodeCount
    byteCounts[1] = twoByteOpcodeCount
    byteCounts[2] = threeByteOpcodeCount
    plt.bar([1, 2, 3], byteCounts)
    ax.set_xlabel("Number of opcode bytes")
    ax.set_ylabel("Number of undocumented opcodes")
    #maxVal = np.max(firstBytes)
    #minVal = np.min(firstBytes)
    #rangeVal = maxVal - minVal
    #xinterval = np.arange(0,maxVal,rangeVal / 15)
    #ax.set_xticks(xinterval)
    ax.margins(x=0)
    #axes = plt.gca()
    #axes.get_xaxis().set_major_formatter(ticker.FormatStrFormatter("%x"))   #format x axis as hex
    fig.tight_layout()  #prevent y-axis label getting clipped
    plt.show()
