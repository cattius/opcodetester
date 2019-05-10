#modify this for your variable for your toolchain filepath
CROSSCOMPILE=~/Documents/freedom-sdk/freedom-u-sdk/work/buildroot_initramfs/host/bin/riscv64-linux-gcc

all: clean tester16 tester32

tester16:
	$(CROSSCOMPILE) -Wall -Wextra -c opcodeTester16.c
	$(CROSSCOMPILE) -c riscv-disas.c
	$(CROSSCOMPILE) -o opcodeTester16 opcodeTester16.o riscv-disas.o

tester32:
	$(CROSSCOMPILE) -Wall -Wextra -c opcodeTester32.c
	$(CROSSCOMPILE) -c riscv-disas.c
	$(CROSSCOMPILE) -o opcodeTester32 opcodeTester32.o riscv-disas.o

clean:
	rm -f opcodeTester16
	rm -f opcodeTester32
	rm -f opcodeTester16.o
	rm -f opcodeTester32.o
