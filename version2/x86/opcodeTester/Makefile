kitLocation = xed/kits/xed-install-base-2019-04-05-lin-x86-64
# change this for your system after installing XED

all: clean user specpoline experimentSpecpoline timing tsx

user:
	gcc -D_GNU_SOURCE -ggdb -include$(kitLocation)/include/xed/xed-interface.h -c opcodeTester.c
	gcc -o opcodeTester opcodeTester.o $(kitLocation)/lib/libxed.a -lbsd

specpoline:
	gcc -D_GNU_SOURCE -ggdb -include$(kitLocation)/include/xed/xed-interface.h -masm=intel -c specpoline.c
	gcc -o specpoline specpoline.o $(kitLocation)/lib/libxed.a -lbsd

experimentSpecpoline:
	gcc -o specpoline experimentSpecpoline.c

timing:
	gcc -D_GNU_SOURCE -ggdb -include$(kitLocation)/include/xed/xed-interface.h -c timing.c
	gcc -o timing timing.o $(kitLocation)/lib/libxed.a -lbsd

tsx:
	gcc -o tsx tsx.c

clean:
	rm -f opcodeTester
	rm -f opcodeTester.o
	rm -f specpoline
	rm -f specpoline.o
	rm -f timing
	rm -f timing.o

