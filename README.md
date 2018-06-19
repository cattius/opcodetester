OpcodeTester: Analyse Undocumented Instructions on Intel x86-64
===============================================================

## Overview

OpcodeTester is a tool for executing undocumented instructions and attempting to determine their functionality. It was developed as part of my Master's project researching undocumented CPU behaviour at the IAIK at TU Graz. It includes a kernel driver with exception handling to allow you to test undocumented instructions in ring 0. The latest version of the kernel driver is stable and has not been observed to cause any crashes or data loss; if an exception occurs which the driver cannot handle, the user program is simply killed by the OS. However, please be aware that testing undocumented instructions (especially at a privileged execution level) is inherently risky as such instructions have unknown functionality.  

IMPORTANT: this code is only compatible with Intel x86-64 and has currently only been tested on Ubuntu 16.10 + 17.10. It depends on __GNU_SOURCE definitions. 

## Installation

### XED
OpcodeTester requires Intel XED to disassemble instructions and get information about the supported instruction set on different processor models. 

```git clone https://github.com/intelxed/xed.git xed```

```git clone https://github.com/intelxed/mbuild.git mbuild```

```cd xed```

```./mfile.py install```


### Sandsifter
OpcodeTester takes Sandsifter log files as input, so you should setup Sandsifter and run a scan on your computer before using OpcodeTester. Sandsifter and its documentation can be found on Github [here](https://github.com/xoreaxeaxeax/sandsifter), but for quick setup run the following: 

```sudo apt-get install libcapstone3 libcapstone-dev```

```sudo pip install capstone```

```git clone https://github.com/xoreaxeaxeax/sandsifter.git```

```cd sandsifter```

```make CC=clang```


Note: the CC=clang argument to make is not mentioned in Sandsifter's documentation, but is required as gcc fails to compile the code with the default Makefile. You can also add the -fPIC argument to the Makefile if you want to use gcc.

You can then run a scan for undocumented instructions using the tunneling algorithm (see Sandsifter's README for the full list of options): 

```sudo ./sifter.py --unk --save --sync -- -P1 -t```

A scan can take a long time (7+ hours) and is very intensive; try and keep your system cool during the scan to avoid a shutdown from overheating, particularly if using a laptop. Aside from this problem, Sandsifter is quite stable and rarely causes crashes. When the scan is complete you can find the log at data/log. A full scan log may be >150MB so examine the file using less and grep rather than in a text editor (or split it into multiple files). You can also use Sandsifter's built-in tool, which groups the instructions into categories to provide an overview of the results:

```./summarize.py data/log```

**Using non-Sandsifter input files**

You can also use lists of opcodes generated without Sandsifter, but they must be formatted similarly to a Sandsifter log file. A typical log file line looks like this:

```                        0f0d00  1  3  5  2 (0f0d0000000000000000000000000000)```

In the line above, there is first the opcode, then 1 or 0 to indicate if the instruction is valid, the instruction length, the signal number generated, the si_code of the signal, and then the opcode as a full 15-byte buffer. Of these values, opcodeTester only needs the opcode (short version) and the instruction length. These should be separated by spaces and a value inbetween (any number of characters) to simulate the instruction validity value. Lines to be ignored should start with a # character.

### OpcodeTester

Download OpcodeTester to the same directory you cloned the xed and mbuild repositories into.

```git clone https://github.com/cattius/opcodetester.git```

Next, replace the value of ```kitLocation``` in the top-level makefile with the name of the folder in xed/kits (by default, its name will include the date you installed it), and run ```make```.

## Running

Use launcher.sh to run opcodeTester. Replace ```input.txt``` with the address of your Sandsifter log file (relative to the current folder). 

The third argument controls whether port analysis is on or off - port analysis outputs a best guess of the instructions function using hardware performance counters, but makes testing *much* slower as each instruction must be tested many times for reliable results.

The final four arguments control which instructions are tested, in this order: valid instructions (Sandsifter false positives), instructions invalid for your microarchitecture but documented for a different microarchitecture, completely undocumented instructions, and instructions which are documented for your microarchitecture but only in other machine modes (not 64-bit).

```chmod +x launcher.sh```

```./launcher.sh input.txt logs/output.txt 1 logs/instrLog.txt 1 1 1 1```

You can disable testing in ring 0 by changing USE_RING_0 in include/opcodeTester.h to 0.

### Further Information

For more information, please see my thesis (also in this repo), which explains why the tool was developed, why automated CPU analysis is necessary, and discusses possibilities for further research in this area. I'm hoping to also publish some blog posts over the summer which will describe my research in a slightly less dense format!

# License

This code has no formal license and is provided with absolutely no warranty. You are free to modify it and use it in your own work provided you include attribution of the author (Catherine Easdon) and include a link to this repo. I hope you find this tool useful, and if you do your own research in this area get in touch - I'd love to hear about it!