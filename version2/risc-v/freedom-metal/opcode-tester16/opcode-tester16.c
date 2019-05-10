#include <stdio.h>
#include <stdint.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <metal/cpu.h>
#include <unistd.h>
#include <sys/types.h>
#include <setjmp.h>
#include "riscv-disas.h"


//CONFIG
//==================================
#define doSlowDown 0				//use the wait function to slow down testing to a pace that's easier to watch/debug
#define slowFactor 10000			//num repetitions of wait function - increase to slow down more. 10000 reps makes it easy to read
#define BRUTE_FORCE 1				//test entire 16-bit instruction space. otherwise test reserved only
//==================================


//keep vars global on the heap to avoid corruption issues
unsigned char execInstruction[4];
unsigned int i,j = 0;
uint16_t bitPattern = 0; 
int lastSig = 0;
char output_buf[128] = { 0 };
jmp_buf buf;
uint32_t ranCount = 0;
uint32_t exceptCount = 0;
uint32_t totalCount = 0;
uint32_t instr_count = 0;


void wait(int n){
	int j=10;
	int k=20;
	for(int i=0; i<n; i++){
		j = k;
		k = j;
	}
}


void exception_handler(struct metal_cpu *cpu, int ecode) {
   //can't find any way to deregister an exception handler if we end up looping :(
   lastSig = ecode;
   longjmp(buf, 1);
}


//adapted from https://stackoverflow.com/questions/1024389/print-an-int-in-binary-representation-using-c
char* uint2bin(uint16_t i)
{
    size_t bits = sizeof(uint16_t) * CHAR_BIT;

    char * str = malloc(bits + 1);
    if(!str) return NULL;
    str[bits] = '\0';

    for(; bits--; i >>= 1)
        str[bits] = i & 1 ? '1' : '0';

    return str;
}


int main() {


    //init exception handling
    //=======================
    struct metal_cpu *cpu0 = metal_cpu_get(0);
    if(cpu0 == NULL) { 	
	printf("Error acquiring CPU hart 0 handle, exiting.\n");
	return 1;
    }
    struct metal_interrupt *cpu_int = metal_cpu_interrupt_controller(cpu0);
    if(!cpu_int) {
  	printf("Error acquiring CPU interrupt controller, exiting.\n");
	return 1;
    }
    metal_interrupt_init(cpu_int);

    //Catch all possible exception codes (8-10 are reserved with no documented functionality, 12-31 are also reserved but return an error if you try to register a handler for them on HiFive1)
    if( (metal_cpu_exception_register(cpu0, 0, exception_handler) != 0)
    ||  (metal_cpu_exception_register(cpu0, 1, exception_handler) != 0)
    ||  (metal_cpu_exception_register(cpu0, 2, exception_handler) != 0)
    ||  (metal_cpu_exception_register(cpu0, 3, exception_handler) != 0)
    ||  (metal_cpu_exception_register(cpu0, 4, exception_handler) != 0)
    ||  (metal_cpu_exception_register(cpu0, 5, exception_handler) != 0)
    ||  (metal_cpu_exception_register(cpu0, 6, exception_handler) != 0)
    ||  (metal_cpu_exception_register(cpu0, 7, exception_handler) != 0)
    ||  (metal_cpu_exception_register(cpu0, 8, exception_handler) != 0)
    ||  (metal_cpu_exception_register(cpu0, 9, exception_handler) != 0)
    ||  (metal_cpu_exception_register(cpu0, 10, exception_handler) != 0)
    ||  (metal_cpu_exception_register(cpu0, 11, exception_handler) != 0)){
	printf("Error registering exception handler, exiting.\n");
	return 1;
    }

    //test instructions
    //=================

  #if BRUTE_FORCE
  bool testing = true;
  bitPattern = 53000;
  while(testing){
	if(totalCount != 0) bitPattern++;
	if(bitPattern == UINT16_MAX) testing = false;
	totalCount++;	

  #else
  //test all possible combinations of reserved encoding 100???????????00
  for(uint16_t twelve=0; twelve<2; twelve++){
  for(uint16_t eleven=0; eleven<2; eleven++){
  for(uint16_t ten=0; ten<2; ten++){
  for(uint16_t nine=0; nine<2; nine++){
  for(uint16_t eight=0; eight<2; eight++){
  for(uint16_t seven=0; seven<2; seven++){
  for(uint16_t six=0; six<2; six++){
  for(uint16_t five=0; five<2; five++){
  for(uint16_t four=0; four<2; four++){
  for(uint16_t three=0; three<2; three++){
  for(uint16_t two=0; two<2; two++){

	bitPattern |= (1UL << 15);			//15th byte is always 1

	if(twelve) bitPattern |= (twelve << 12);
	else bitPattern &= ~(1UL << 12);

	if(eleven) bitPattern |= (eleven << 11);
	else bitPattern &= ~(1UL << 11);

	if(ten) bitPattern |= (ten << 10);
	else bitPattern &= ~(1UL << 10);

	if(nine) bitPattern |= (nine << 9);
	else bitPattern &= ~(1UL << 9);

	if(eight) bitPattern |= (eight << 8);
	else bitPattern &= ~(1UL << 8);

	if(seven) bitPattern |= (seven << 7);
	else bitPattern &= ~(1UL << 7);

	if(six) bitPattern |= (six << 6);
	else bitPattern &= ~(1UL << 6);

	if(five) bitPattern |= (five << 5);
	else bitPattern &= ~(1UL << 5);

	if(four) bitPattern |= (four << 4);
	else bitPattern &= ~(1UL << 4);

	if(three) bitPattern |= (three << 3);
	else bitPattern &= ~(1UL << 3);

	if(two) bitPattern |= (two << 2);
	else bitPattern &= ~(1UL << 2);
  #endif

	j = ( (bitPattern >> 8) & 0xff);			//next 8 bits, mask      0000 0000 0000 0000 1111 1111 0000 0000 
	i = (bitPattern & 0xff);				//lowermost 8 bits, mask 0000 0000 0000 0000 0000 0000 1111 1111 

	instr_count++;
	//bizarrely, even with documented instrs not running and fence and fence.i in place (even if I sprinkle them liberally - but that slows things down) these DOCUMENTED jump instructions mess up the control flow! have tested many times now
	//also seen issues with i == 0x67 and i == 0xe7
	if(bitPattern > 20000 && bitPattern < 25000) continue;
	if(bitPattern > 53330 && bitPattern < 54000) continue;

        execInstruction[0] = i; 
        execInstruction[1] = j;
        execInstruction[2] = 0x82;
        execInstruction[3] = 0x80;

	uint64_t converted_instr = 0;
	bool documented = false;
	
	converted_instr = 0;
  	memcpy(&converted_instr, &execInstruction, 2);
	memset(output_buf, 0, sizeof(output_buf));
  	disasm_inst(output_buf, sizeof(output_buf), rv32, 0, converted_instr);
	if(strstr(output_buf, "illegal") == NULL) documented = true;

	//check if with ret bytes it's going to get interpreted as a valid 32bit instr - cause control flow issues
	converted_instr = 0;
  	memcpy(&converted_instr, &execInstruction, 4);
	memset(output_buf, 0, sizeof(output_buf));
  	disasm_inst(output_buf, sizeof(output_buf), rv32, 0, converted_instr);
	if(strstr(output_buf, "illegal") == NULL) documented = true;

	//I still don't understand why this is necessary, but occasionally converted_instr gets corrupted to 0 after disasm_inst. As far as I can tell the corruption doesn't occur in the dissasembly function as the value is still fine at the end when checked with GDB - how is it being corrupted at function return?
	memset(&converted_instr, 0, 4);
	memcpy(&converted_instr, &execInstruction, 2);
	asm volatile("fence");

	#if doSlowDown
	wait(slowFactor);
	#endif

	if(!documented){	//only test instruction if undocumented

		//can't use fork, always returns -1 as no support for processes

		if((instr_count % 1000) == 0) printf("heartbeat %" PRIu64 "\n", converted_instr);
		totalCount++;
		int canary = 42;	//check for stack corruption

		if(!setjmp(buf)){
			lastSig = 0;
			asm volatile("fence");
			asm volatile ("fence.i");
			((void(*)())execInstruction)();
		}
		if(lastSig == 0){
			char* binStr = uint2bin(converted_instr);
			printf("%s %" PRIu64 " ", binStr, converted_instr);
			free(binStr);
			printf("%d RAN\n", canary);
			ranCount++;
		}
		//skip printing SIGILLs to avoid excessive output on serial console
		else if(lastSig != 2){
			char* binStr = uint2bin(converted_instr);
			printf("%s %" PRIu64 " ", binStr, converted_instr);
			free(binStr);
			printf("%d EXCPT %d\n", canary, lastSig);
			exceptCount++;
		}
		//fflush(stdout);			//can't use, makes it hang
	}
	else printf("documented %" PRIu64 "\n", converted_instr);
 
  #if BRUTE_FORCE
  }
  #else
  } } } } } } } } } } } 	//sorry!
  #endif

    printf("Finished! Tested %" PRIu32 "instructions, %" PRIu32 " ran, %" PRIu32 " caused exceptions other than SIGILL.\n", totalCount, ranCount, exceptCount);
    return 0;
}
