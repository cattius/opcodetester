#kitLocation = xed/kits/xed-install-base-2018-06-03-lin-x86-64#linux VM
kitLocation = ../../xed/kits/xed-install-base-2018-05-11-lin-x86-64#laptop
#kitLocation = ../xed/kits/xed-install-base-2018-06-11-lin-x86-64#windowsVM
# change this for your system after installing XED

all: clean test

test:
	gcc -include$(kitLocation)/include/xed/xed-interface.h -D_GNU_SOURCE -c test.c
	gcc -o test test.o $(kitLocation)/lib/libxed.a
 
clean:
	rm -f test
