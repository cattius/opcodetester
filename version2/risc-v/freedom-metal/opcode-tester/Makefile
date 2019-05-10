override CFLAGS += riscv-disas.o

hello:	opcode-tester.c

clean::
	rm -f opcode-tester opcode-tester.hex

# use the following command to compile riscv-disas.o manually:
# $(TOOLCHAIN)/riscv64-unknown-elf-gcc -march=rv32imac -mabi=ilp32 -mcmodel=medlow -ffunction-sections -fdata-sections -c riscv-disas.c
# where toolchain is your riscv-tools or freedom-e-sdk toolchain's filepath

#copy the opcode-tester folder into your freedom-e-sdk's software folder (the folder must have the same name as the program) and run this Makefile from the top level SDK folder with:

#	make PROGRAM=opcode-tester TARGET=sifive-hifive1 CONFIGURATION=debug software  
#	compiles only

#	make PROGRAM=opcode-tester TARGET=sifive-hifive1 CONFIGURATION=debug upload  
#	compiles and uploads to a (connected) HiFive1

#	make PROGRAM=opcode-tester TARGET=sifive-hifive1 CONFIGURATION=release debug  
#	starts debugging (connected) HiFive1 with GDB via OpenOCD

# switch to opcode-tester-16 folder to use a 16-bit rather than 32-bit test mechanism
# to choose which test runs, modify the CONFIG section at the top of opcode-tester.c
