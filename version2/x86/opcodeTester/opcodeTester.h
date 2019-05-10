#ifndef OPCODE_TESTER_H
#define OPCODE_TESTER_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>
#include <signal.h>
#include <setjmp.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sched.h>
#include <bsd/stdlib.h>
#include <sys/wait.h>


//CONFIGURATION
//=============

//configure all
#define B32 0							//set if using 32-bit CPU/mode
#define PRINT_STATE_CHANGE 0					//print registers which changed (only for instructions which run)
#define MICROARCH XED_CHIP_BROADWELL				//put the XED name for your CPU here
#define USE_TRAP_FLAG 1 					//much more accurate and stable instruction testing, but **no point using PRINT_STATE_CHANGE if this is set**

//configure opcodeTester.c
#define NUM_INSTRS_PER_ROUND 10					//how many instrs to read from input file at once
#define RANDOM_FUZZING 0					//if on, test random instructions infinitely, Ctrl+C to quit
								//if random fuzzing is off, default is still to read a Sandsifter log file
#define TEST_OPCODE_MAP_BLANKS 0				//if on, will test premade opcode map blanks file

//configures timing.c
#define RUN_DOCUMENTED 0				     	//test valid instructions to get cycle counts (increases likelihood of crashes)
#define INSTR_MAX 15					     	//generate instructions with length ranging from 1 to this value
#define NUM_SAMPLES 1000				     	//number of instructions to test before stopping
#define RUN_INFINITELY 1			             	//if true, don't stop after NUM_SAMPLES
#define IGNORE_SIGILL 0				             	//don't output cycle counts for instructions which faulted with #UD
#define CYCLE_THRESHOLD 1000				     	//value you consider an interestingly high number of cycles
#define TIMING_ATTACK 1
#define PRINT_DISASSEMBLY 0
#define CARE_ABOUT_RUNNING_ONLY 0				//only output instructions which run, overrides (IGNORE_SIGILL false)
#define CARE_ABOUT_CYCLES 1					//if false, don't profile cycles accurately - find interesting instrs (undoc which run) much faster

#if CARE_ABOUT_CYCLES
	#define NUM_TESTS 10000
#else
	#define NUM_TESTS 10				     	//lower if you want to test as fast as possible, but cycle counts will be dodgy. must be >=3 to avoid divide by zero error (how I calc outliers in cycle counts)
#endif

//==============

#define SIZE_XED_IFORM_ENUM 6291

#if USE_TRAP_FLAG && B32
#define MAX_INSTR_LENGTH 9+15
#elif USE_TRAP_FLAG
#define MAX_INSTR_LENGTH 10+15
#else
#define MAX_INSTR_LENGTH 1+15
#endif

#if !B32
	#define PRINTER PRIu64
	#define SIZE_STATE 23
	typedef uint64_t reg;
	reg beforeState[SIZE_STATE];
	reg afterState[SIZE_STATE];
	uint64_t highB, lowB, highA, lowA;		

	#define GET_STATE_BEFORE() \
		memset(beforeState, 0, sizeof(beforeState)); \
		memset(afterState, 0, sizeof(afterState)); \
		highB = 0; \
		lowB = 0; \
		highA = 0; \
		lowA = 0; \
		asm volatile ("mov %%rsi, %0" : "=m"(beforeState[4]) : :); \
		asm volatile ("mov %%rdi, %0" : "=m"(beforeState[5]) : :); \
		asm volatile ("mov %%r8, %0" : "=m"(beforeState[6]) : :); \
		asm volatile ("mov %%r9, %0" : "=m"(beforeState[7]) : :); \
		asm volatile ("mov %%r10, %0" : "=m"(beforeState[8]) : :); \
		asm volatile ("mov %%r11, %0" : "=m"(beforeState[9]) : :); \
		asm volatile ("mov %%r12, %0" : "=m"(beforeState[10]) : :); \
		asm volatile ("mov %%r13, %0" : "=m"(beforeState[11]) : :); \
		asm volatile ("mov %%r14, %0" : "=m"(beforeState[12]) : :); \
		asm volatile ("mov %%r15, %0" : "=m"(beforeState[13]) : :); \
		asm volatile ("mov %%ss, %0" : "=m"(beforeState[14]) : :); \
		asm volatile ("mov %%cs, %0" : "=m"(beforeState[15]) : :); \
		asm volatile ("mov %%ds, %0" : "=m"(beforeState[16]) : :); \
		asm volatile ("mov %%es, %0" : "=m"(beforeState[17]) : :); \
		asm volatile ("mov %%fs, %0" : "=m"(beforeState[18]) : :); \
		asm volatile ("mov %%gs, %0" : "=m"(beforeState[19]) : :); \
		asm volatile ("pushfq"); \
		asm volatile ("popq %0" : "=m"(beforeState[20]) : :); \
		asm volatile ("mov %%rbp, %0" : "=m"(beforeState[21]) : :); \
		asm volatile ("mov %%rsp, %0" : "=m"(beforeState[22]) : :); \
		asm volatile ("cpuid; rdtsc; mov %%rdx, %0; mov %%rax, %1" : "=r" (highB), "=r" (lowB) : : "%rax", "%rbx", "%rcx", "%rdx"); \
		asm volatile ("mov %%rax, %0" : "=m"(beforeState[0]) : :); \
		asm volatile ("mov %%rbx, %0" : "=m"(beforeState[1]) : :); \
		asm volatile ("mov %%rcx, %0" : "=m"(beforeState[2]) : :); \
		asm volatile ("mov %%rdx, %0" : "=m"(beforeState[3]) : :);


	#define GET_STATE_AFTER() \
		asm volatile ("mov %%rax, %0" : "=m"(afterState[0]) : :); \
		asm volatile ("mov %%rbx, %0" : "=m"(afterState[1]) : :); \
		asm volatile ("mov %%rcx, %0" : "=m"(afterState[2]) : :); \
		asm volatile ("mov %%rdx, %0" : "=m"(afterState[3]) : :); \
		asm volatile ("rdtscp; mov %%rdx, %0; mov %%rax, %1; cpuid" : "=r" (highA), "=r" (lowA) : : "%rax", "%rbx", "%rcx", "%rdx"); \
		asm volatile ("mov %%rsi, %0" : "=m"(afterState[4]) : :); \
		asm volatile ("mov %%rdi, %0" : "=m"(afterState[5]) : :); \
		asm volatile ("mov %%r8, %0" : "=m"(afterState[6]) : :); \
		asm volatile ("mov %%r9, %0" : "=m"(afterState[7]) : :); \
		asm volatile ("mov %%r10, %0" : "=m"(afterState[8]) : :); \
		asm volatile ("mov %%r11, %0" : "=m"(afterState[9]) : :); \
		asm volatile ("mov %%r12, %0" : "=m"(afterState[10]) : :); \
		asm volatile ("mov %%r13, %0" : "=m"(afterState[11]) : :); \
		asm volatile ("mov %%r14, %0" : "=m"(afterState[12]) : :); \
		asm volatile ("mov %%r15, %0" : "=m"(afterState[13]) : :); \
		asm volatile ("mov %%ss, %0" : "=m"(afterState[14]) : :); \
		asm volatile ("mov %%cs, %0" : "=m"(afterState[15]) : :); \
		asm volatile ("mov %%ds, %0" : "=m"(afterState[16]) : :); \
		asm volatile ("mov %%es, %0" : "=m"(afterState[17]) : :); \
		asm volatile ("mov %%fs, %0" : "=m"(afterState[18]) : :); \
		asm volatile ("mov %%gs, %0" : "=m"(afterState[19]) : :); \
		asm volatile ("mov %%rbp, %0" : "=m"(beforeState[21]) : :); \
		asm volatile ("mov %%rsp, %0" : "=m"(beforeState[22]) : :); \
		asm volatile ("pushfq"); \
		asm volatile ("popq %0" : "=m"(afterState[20]) : :);

#else
	#define PRINTER PRIu32
	#define SIZE_STATE 15
	typedef uint32_t reg;
	reg beforeState[SIZE_STATE];
	reg afterState[SIZE_STATE];
	reg highB, highA, lowB, lowA;		    

	#define GET_STATE_BEFORE() \
		memset(beforeState, 0, sizeof(beforeState)); \
		memset(afterState, 0, sizeof(afterState)); \
		highB = 0; \
		lowB = 0; \
		highA = 0; \
		lowA = 0; \
		asm volatile ("mov %%esi, %0" : "=m"(beforeState[4]) : :); \
		asm volatile ("mov %%edi, %0" : "=m"(beforeState[5]) : :); \
		asm volatile ("mov %%ss, %0" : "=m"(beforeState[6]) : :); \
		asm volatile ("mov %%cs, %0" : "=m"(beforeState[7]) : :); \
		asm volatile ("mov %%ds, %0" : "=m"(beforeState[8]) : :); \
		asm volatile ("mov %%es, %0" : "=m"(beforeState[9]) : :); \
		asm volatile ("mov %%fs, %0" : "=m"(beforeState[10]) : :); \
		asm volatile ("mov %%gs, %0" : "=m"(beforeState[11]) : :); \
		asm volatile ("pushfl"); \
		asm volatile ("popl %0" : "=m"(beforeState[12]) : :); \
		asm volatile ("mov %%ebp, %0" : "=m"(beforeState[13]) : :); \
		asm volatile ("mov %%esp, %0" : "=m"(beforeState[14]) : :); \
		asm volatile ("cpuid; rdtsc; mov %%edx, %0; mov %%eax, %1" : "=r" (highB), "=r" (lowB) : : "%eax", "%ebx", "%ecx", "%edx"); \
		asm volatile ("mov %%eax, %0" : "=m"(beforeState[0]) : :); \
		asm volatile ("mov %%ebx, %0" : "=m"(beforeState[1]) : :); \
		asm volatile ("mov %%ecx, %0" : "=m"(beforeState[2]) : :); \
		asm volatile ("mov %%edx, %0" : "=m"(beforeState[3]) : :);


	//assume older CPU so rdtscp not supported, can't use Intel recommended method (cr0) as need kernel privs
	#define GET_STATE_AFTER() \
		asm volatile ("mov %%eax, %0" : "=m"(afterState[0]) : :); \
		asm volatile ("mov %%ebx, %0" : "=m"(afterState[1]) : :); \
		asm volatile ("mov %%ecx, %0" : "=m"(afterState[2]) : :); \
		asm volatile ("mov %%edx, %0" : "=m"(afterState[3]) : :); \
		asm volatile ("rdtsc; mov %%edx, %0; mov %%eax, %1; cpuid" : "=r" (highA), "=r" (lowA) : : "%eax", "%ebx", "%ecx", "%edx"); \
		asm volatile ("mov %%esi, %0" : "=m"(afterState[4]) : :); \
		asm volatile ("mov %%edi, %0" : "=m"(afterState[5]) : :); \
		asm volatile ("mov %%ss, %0" : "=m"(afterState[6]) : :); \
		asm volatile ("mov %%cs, %0" : "=m"(afterState[7]) : :); \
		asm volatile ("mov %%ds, %0" : "=m"(afterState[8]) : :); \
		asm volatile ("mov %%es, %0" : "=m"(afterState[9]) : :); \
		asm volatile ("mov %%fs, %0" : "=m"(afterState[10]) : :); \
		asm volatile ("mov %%gs, %0" : "=m"(afterState[11]) : :); \
		asm volatile ("mov %%ebp, %0" : "=m"(beforeState[13]) : :); \
		asm volatile ("mov %%esp, %0" : "=m"(beforeState[14]) : :); \
		asm volatile ("pushfl"); \
		asm volatile ("popl %0" : "=m"(afterState[12]) : :);
#endif

char* getStateName(int s){
	#if !B32
	if(s == 0) return "rax";  //ignore
	else if(s == 1) return "rbx";
	else if(s == 2) return "rcx";
	else if(s == 3) return "rdx"; //ignore
	else if(s == 4) return "rsi"; //ignore
	else if(s == 5) return "rdi"; //ignore
	else if(s == 6) return "r8";
	else if(s == 7) return "r9";
	else if(s == 8) return "r10";
	else if(s == 9) return "r11";
	else if(s == 10) return "r12";
	else if(s == 11) return "r13";
	else if(s == 12) return "r14";
	else if(s == 13) return "r15";
	else if(s == 14) return "ss";
	else if(s == 15) return "cs";
	else if(s == 16) return "ds";
	else if(s == 17) return "es";
	else if(s == 18) return "fs";
	else if(s == 19) return "gs";
	else if(s == 20) return "rflags";
	else if(s == 21) return "rbp"; //ignore
	else if(s == 22) return "rsp"; //ignore
	else return "UNKNOWN";
	#else
	if(s == 0) return "eax";  //ignore
	else if(s == 1) return "ebx";
	else if(s == 2) return "ecx";
	else if(s == 3) return "edx"; //ignore
	else if(s == 4) return "esi"; //ignore
	else if(s == 5) return "edi"; //ignore
	else if(s == 6) return "ss";
	else if(s == 7) return "cs";
	else if(s == 8) return "ds";
	else if(s == 9) return "es";
	else if(s == 10) return "fs";
	else if(s == 11) return "gs";
	else if(s == 12) return "eflags";
	else if(s == 13) return "ebp"; //ignore
	else if(s == 14) return "esp"; //ignore
	else return "UNKNOWN";
	#endif
}

//*****************************************************************************
/* Hex helper functions
Taken from https://stackoverflow.com/questions/21869321/treat-input-as-hex-values
(slightly adapted) */

//Lookup hex char
static unsigned hexval(unsigned char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return ~0;
}

//Convert string to hex
int str2bin(unsigned char *data, unsigned char **buf_ptr)
{
        int len= 0;
        unsigned int val;
        unsigned char *buf;

        buf = malloc(strlen(data) + 1);		//ok to use strlen here - fine with text, hex 00 is the problem
        *buf_ptr = buf;

        for (; *data; ) {
                val = (hexval(data[0]) << 4) | hexval(data[1]);
                if (val & ~0xff) return -1;  // If not valid hex, return error
                *buf++ = val;
                data += 2;
        }
        len = buf - *buf_ptr;

        return len;
}
//*****************************************************************************

enum signalTypes {
  OPCODE_SIGOTHER,
  OPCODE_SIGILL,
  OPCODE_SIGTRAP,
  OPCODE_SIGBUS,
  OPCODE_SIGFPE,
  OPCODE_SIGSEGV,
  OPCODE_NOSIG
};

volatile sig_atomic_t executingNow = 0;
volatile sig_atomic_t handlerHasAlreadyRun = 0;
volatile sig_atomic_t lastSig = 0;
sigjmp_buf buf;

void signalHandler(int sig, siginfo_t* siginfo, void* context){
	if(!executingNow || handlerHasAlreadyRun){         //prevent looping
		printf("double exception\n");
	    	signal(SIGILL, SIG_DFL);
	    	signal(SIGFPE, SIG_DFL);
	    	signal(SIGSEGV, SIG_DFL);
	    	signal(SIGBUS, SIG_DFL);
	    	signal(SIGTRAP, SIG_DFL);
  	}
  	handlerHasAlreadyRun = 1;
	lastSig = sig;	
	//do not ignore traps
	siglongjmp(buf, 1);
}

#endif
