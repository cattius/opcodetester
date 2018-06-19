#kitLocation = xed/kits/xed-install-base-2018-06-03-lin-x86-64#VM
kitLocation = ../../xed/kits/xed-install-base-2018-05-11-lin-x86-64#laptop
# change this for your system after installing XED

all: clean user

user:
	gcc -include$(kitLocation)/include/xed/xed-interface.h -D_GNU_SOURCE -g -ggdb -c perfMonitoringUser.c
	gcc -o perfMon perfMonitoringUser.o $(kitLocation)/lib/libxed.a -lm
 
clean:
	rm -f perfMon
