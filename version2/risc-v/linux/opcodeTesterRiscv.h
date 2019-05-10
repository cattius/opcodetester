#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>
#include <signal.h>
#include <sys/mman.h>	//for mprotect
#include <unistd.h>	//for sysconf - page size
#include <string.h>
#include <sys/wait.h>
#include <inttypes.h>
#include <assert.h>
#include <time.h>
#include <stdbool.h>
#include <limits.h>
#include <endian.h>
#include <sched.h>
#include "riscv-disas.h"

#define SIZE_STATE 66
#define GET_DIFFERENCE_UNSIGNED_IGNORE_NEGATIVE(a, b, s) { if(a > b) {s = a - b;} else {s = b - a;} }
#define GET_DIFFERENCE_UNSIGNED(a, b, s, n) { if(a > b) {s = a - b;} else {s = b - a; n = true;} }
#define PRINT_ARRAY(a, len) { for(int x=0; x<len; x++){ printf("a[x] "); } }

sigjmp_buf buf;
stack_t sighandlerStack;
volatile sig_atomic_t executingNow = 0;
volatile sig_atomic_t handlerHasAlreadyRun = 0;
volatile sig_atomic_t lastSig = 0;
struct sigaction handler;
uint64_t beforeState[SIZE_STATE], afterState[SIZE_STATE];

//no point reading x0 as it's always 0
//sd (store from reg to mem) is our equivalent of x86 mov here as mv is only possible reg-reg - we don't want to clobber another reg as we read one of them!
#define GET_STATE_BEFORE() \
		memset(beforeState, 0, sizeof(beforeState)); \
		memset(afterState, 0, sizeof(afterState)); \
		asm volatile ("sd ra, %0" : "=m"(beforeState[0]) : :); \
		asm volatile ("sd sp, %0" : "=m"(beforeState[1]) : :); \
		asm volatile ("sd gp, %0" : "=m"(beforeState[2]) : :); \
		asm volatile ("sd tp, %0" : "=m"(beforeState[3]) : :); \
		asm volatile ("sd t0, %0" : "=m"(beforeState[4]) : :); \
		asm volatile ("sd t1, %0" : "=m"(beforeState[5]) : :); \
		asm volatile ("sd t2, %0" : "=m"(beforeState[6]) : :); \
		asm volatile ("sd s0, %0" : "=m"(beforeState[7]) : :); \
		asm volatile ("sd s1, %0" : "=m"(beforeState[8]) : :); \
		asm volatile ("sd a0, %0" : "=m"(beforeState[9]) : :); \
		asm volatile ("sd a1, %0" : "=m"(beforeState[10]) : :); \
		asm volatile ("sd a2, %0" : "=m"(beforeState[11]) : :); \
		asm volatile ("sd a3, %0" : "=m"(beforeState[12]) : :); \
		asm volatile ("sd a4, %0" : "=m"(beforeState[13]) : :); \
		asm volatile ("sd a5, %0" : "=m"(beforeState[14]) : :); \
		asm volatile ("sd a6, %0" : "=m"(beforeState[15]) : :); \
		asm volatile ("sd a7, %0" : "=m"(beforeState[16]) : :); \
		asm volatile ("sd s2, %0" : "=m"(beforeState[17]) : :); \
		asm volatile ("sd s3, %0" : "=m"(beforeState[18]) : :); \
		asm volatile ("sd s4, %0" : "=m"(beforeState[19]) : :); \
		asm volatile ("sd s5, %0" : "=m"(beforeState[20]) : :); \
		asm volatile ("sd s6, %0" : "=m"(beforeState[21]) : :); \
		asm volatile ("sd s7, %0" : "=m"(beforeState[22]) : :); \
		asm volatile ("sd s8, %0" : "=m"(beforeState[23]) : :); \
		asm volatile ("sd s9, %0" : "=m"(beforeState[24]) : :); \
		asm volatile ("sd s10, %0" : "=m"(beforeState[25]) : :); \
		asm volatile ("sd s11, %0" : "=m"(beforeState[26]) : :); \
		asm volatile ("frcsr t3" : : :); \
		asm volatile ("frrm t4" : : :); \
		asm volatile ("frflags t5" : : :); \
		asm volatile ("sd t3, %0" : "=m"(beforeState[63]) : :); \
		asm volatile ("sd t4, %0" : "=m"(beforeState[64]) : :); \
		asm volatile ("sd t5, %0" : "=m"(beforeState[65]) : :); \
		asm volatile ("sd t3, %0" : "=m"(beforeState[27]) : :); \
		asm volatile ("sd t4, %0" : "=m"(beforeState[28]) : :); \
		asm volatile ("sd t5, %0" : "=m"(beforeState[29]) : :); \
		asm volatile ("sd t6, %0" : "=m"(beforeState[30]) : :); \
		asm volatile ("fsd ft0, %0" : "=m"(beforeState[31]) : :); \
		asm volatile ("fsd ft1, %0" : "=m"(beforeState[32]) : :); \
		asm volatile ("fsd ft2, %0" : "=m"(beforeState[33]) : :); \
		asm volatile ("fsd ft3, %0" : "=m"(beforeState[34]) : :); \
		asm volatile ("fsd ft4, %0" : "=m"(beforeState[35]) : :); \
		asm volatile ("fsd ft5, %0" : "=m"(beforeState[36]) : :); \
		asm volatile ("fsd ft6, %0" : "=m"(beforeState[37]) : :); \
		asm volatile ("fsd ft7, %0" : "=m"(beforeState[38]) : :); \
		asm volatile ("fsd fs0, %0" : "=m"(beforeState[39]) : :); \
		asm volatile ("fsd fs1, %0" : "=m"(beforeState[40]) : :); \
		asm volatile ("fsd fa0, %0" : "=m"(beforeState[41]) : :); \
		asm volatile ("fsd fa1, %0" : "=m"(beforeState[42]) : :); \
		asm volatile ("fsd fa2, %0" : "=m"(beforeState[43]) : :); \
		asm volatile ("fsd fa3, %0" : "=m"(beforeState[44]) : :); \
		asm volatile ("fsd fa4, %0" : "=m"(beforeState[45]) : :); \
		asm volatile ("fsd fa5, %0" : "=m"(beforeState[46]) : :); \
		asm volatile ("fsd fa6, %0" : "=m"(beforeState[47]) : :); \
		asm volatile ("fsd fa7, %0" : "=m"(beforeState[48]) : :); \
		asm volatile ("fsd fs2, %0" : "=m"(beforeState[49]) : :); \
		asm volatile ("fsd fs3, %0" : "=m"(beforeState[50]) : :); \
		asm volatile ("fsd fs4, %0" : "=m"(beforeState[51]) : :); \
		asm volatile ("fsd fs5, %0" : "=m"(beforeState[52]) : :); \
		asm volatile ("fsd fs6, %0" : "=m"(beforeState[53]) : :); \
		asm volatile ("fsd fs7, %0" : "=m"(beforeState[54]) : :); \
		asm volatile ("fsd fs8, %0" : "=m"(beforeState[55]) : :); \
		asm volatile ("fsd fs9, %0" : "=m"(beforeState[56]) : :); \
		asm volatile ("fsd fs10, %0" : "=m"(beforeState[57]) : :); \
		asm volatile ("fsd fs11, %0" : "=m"(beforeState[58]) : :); \
		asm volatile ("fsd ft8, %0" : "=m"(beforeState[59]) : :); \
		asm volatile ("fsd ft9, %0" : "=m"(beforeState[60]) : :); \
		asm volatile ("fsd ft10, %0" : "=m"(beforeState[61]) : :); \
		asm volatile ("fsd ft11, %0" : "=m"(beforeState[62]) : :); \

#define GET_STATE_AFTER() \
		asm volatile ("sd ra, %0" : "=m"(afterState[0]) : :); \
		asm volatile ("sd sp, %0" : "=m"(afterState[1]) : :); \
		asm volatile ("sd gp, %0" : "=m"(afterState[2]) : :); \
		asm volatile ("sd tp, %0" : "=m"(afterState[3]) : :); \
		asm volatile ("sd t0, %0" : "=m"(afterState[4]) : :); \
		asm volatile ("sd t1, %0" : "=m"(afterState[5]) : :); \
		asm volatile ("sd t2, %0" : "=m"(afterState[6]) : :); \
		asm volatile ("sd s0, %0" : "=m"(afterState[7]) : :); \
		asm volatile ("sd s1, %0" : "=m"(afterState[8]) : :); \
		asm volatile ("sd a0, %0" : "=m"(afterState[9]) : :); \
		asm volatile ("sd a1, %0" : "=m"(afterState[10]) : :); \
		asm volatile ("sd a2, %0" : "=m"(afterState[11]) : :); \
		asm volatile ("sd a3, %0" : "=m"(afterState[12]) : :); \
		asm volatile ("sd a4, %0" : "=m"(afterState[13]) : :); \
		asm volatile ("sd a5, %0" : "=m"(afterState[14]) : :); \
		asm volatile ("sd a6, %0" : "=m"(afterState[15]) : :); \
		asm volatile ("sd a7, %0" : "=m"(afterState[16]) : :); \
		asm volatile ("sd s2, %0" : "=m"(afterState[17]) : :); \
		asm volatile ("sd s3, %0" : "=m"(afterState[18]) : :); \
		asm volatile ("sd s4, %0" : "=m"(afterState[19]) : :); \
		asm volatile ("sd s5, %0" : "=m"(afterState[20]) : :); \
		asm volatile ("sd s6, %0" : "=m"(afterState[21]) : :); \
		asm volatile ("sd s7, %0" : "=m"(afterState[22]) : :); \
		asm volatile ("sd s8, %0" : "=m"(afterState[23]) : :); \
		asm volatile ("sd s9, %0" : "=m"(afterState[24]) : :); \
		asm volatile ("sd s10, %0" : "=m"(afterState[25]) : :); \
		asm volatile ("sd s11, %0" : "=m"(afterState[26]) : :); \
		asm volatile ("sd t3, %0" : "=m"(afterState[27]) : :); \
		asm volatile ("sd t4, %0" : "=m"(afterState[28]) : :); \
		asm volatile ("sd t5, %0" : "=m"(afterState[29]) : :); \
		asm volatile ("sd t6, %0" : "=m"(afterState[30]) : :); \
		asm volatile ("fsd ft0, %0" : "=m"(afterState[31]) : :); \
		asm volatile ("fsd ft1, %0" : "=m"(afterState[32]) : :); \
		asm volatile ("fsd ft2, %0" : "=m"(afterState[33]) : :); \
		asm volatile ("fsd ft3, %0" : "=m"(afterState[34]) : :); \
		asm volatile ("fsd ft4, %0" : "=m"(afterState[35]) : :); \
		asm volatile ("fsd ft5, %0" : "=m"(afterState[36]) : :); \
		asm volatile ("fsd ft6, %0" : "=m"(afterState[37]) : :); \
		asm volatile ("fsd ft7, %0" : "=m"(afterState[38]) : :); \
		asm volatile ("fsd fs0, %0" : "=m"(afterState[39]) : :); \
		asm volatile ("fsd fs1, %0" : "=m"(afterState[40]) : :); \
		asm volatile ("fsd fa0, %0" : "=m"(afterState[41]) : :); \
		asm volatile ("fsd fa1, %0" : "=m"(afterState[42]) : :); \
		asm volatile ("fsd fa2, %0" : "=m"(afterState[43]) : :); \
		asm volatile ("fsd fa3, %0" : "=m"(afterState[44]) : :); \
		asm volatile ("fsd fa4, %0" : "=m"(afterState[45]) : :); \
		asm volatile ("fsd fa5, %0" : "=m"(afterState[46]) : :); \
		asm volatile ("fsd fa6, %0" : "=m"(afterState[47]) : :); \
		asm volatile ("fsd fa7, %0" : "=m"(afterState[48]) : :); \
		asm volatile ("fsd fs2, %0" : "=m"(afterState[49]) : :); \
		asm volatile ("fsd fs3, %0" : "=m"(afterState[50]) : :); \
		asm volatile ("fsd fs4, %0" : "=m"(afterState[51]) : :); \
		asm volatile ("fsd fs5, %0" : "=m"(afterState[52]) : :); \
		asm volatile ("fsd fs6, %0" : "=m"(afterState[53]) : :); \
		asm volatile ("fsd fs7, %0" : "=m"(afterState[54]) : :); \
		asm volatile ("fsd fs8, %0" : "=m"(afterState[55]) : :); \
		asm volatile ("fsd fs9, %0" : "=m"(afterState[56]) : :); \
		asm volatile ("fsd fs10, %0" : "=m"(afterState[57]) : :); \
		asm volatile ("fsd fs11, %0" : "=m"(afterState[58]) : :); \
		asm volatile ("fsd ft8, %0" : "=m"(afterState[59]) : :); \
		asm volatile ("fsd ft9, %0" : "=m"(afterState[60]) : :); \
		asm volatile ("fsd ft10, %0" : "=m"(afterState[61]) : :); \
		asm volatile ("fsd ft11, %0" : "=m"(afterState[62]) : :); \
		asm volatile ("frcsr t3" : : :); \
		asm volatile ("frrm t4" : : :); \
		asm volatile ("frflags t5" : : :); \
		asm volatile ("sd t3, %0" : "=m"(afterState[63]) : :); \
		asm volatile ("sd t4, %0" : "=m"(afterState[64]) : :); \
		asm volatile ("sd t5, %0" : "=m"(afterState[65]) : :); \


char* getStateName(int s){
	if(s == 0) return "ra";
	else if(s == 1) return "sp";
	else if(s == 2) return "gp";
	else if(s == 3) return "tp";
	else if(s == 4) return "t0";
	else if(s == 5) return "t1";
	else if(s == 6) return "t2";
	else if(s == 7) return "s0";
	else if(s == 8) return "s1";
	else if(s == 9) return "a0";
	else if(s == 10) return "a1";
	else if(s == 11) return "a2";
	else if(s == 12) return "a3";
	else if(s == 13) return "a4";
	else if(s == 14) return "a5";
	else if(s == 15) return "a6";
	else if(s == 16) return "a7";
	else if(s == 17) return "s2";
	else if(s == 18) return "s3";
	else if(s == 19) return "s4";
	else if(s == 20) return "s5";
	else if(s == 21) return "s6";
	else if(s == 22) return "s7";
	else if(s == 23) return "s8";
	else if(s == 24) return "s9";
	else if(s == 25) return "s10";
	else if(s == 26) return "s11";
	else if(s == 27) return "t3";
	else if(s == 28) return "t4";
	else if(s == 29) return "t5";
	else if(s == 30) return "t6";
	else if(s == 31) return "ft0";
	else if(s == 32) return "ft1";
	else if(s == 33) return "ft2";
	else if(s == 34) return "ft3";
	else if(s == 35) return "ft4";
	else if(s == 36) return "ft5";
	else if(s == 37) return "ft6";
	else if(s == 38) return "ft7";
	else if(s == 39) return "fs0";
	else if(s == 40) return "fs1";
	else if(s == 41) return "fa0";
	else if(s == 42) return "fa1";
	else if(s == 43) return "fa2";
	else if(s == 44) return "fa3";
	else if(s == 45) return "fa4";
	else if(s == 46) return "fa5";
	else if(s == 47) return "fa6";
	else if(s == 48) return "fa7";
	else if(s == 49) return "fs2";
	else if(s == 50) return "fs3";
	else if(s == 51) return "fs4";
	else if(s == 52) return "fs5";
	else if(s == 53) return "fs6";
	else if(s == 54) return "fs7";
	else if(s == 55) return "fs8";
	else if(s == 56) return "fs9";
	else if(s == 57) return "fs10";
	else if(s == 58) return "fs11";
	else if(s == 59) return "ft8";
	else if(s == 60) return "ft9";
	else if(s == 61) return "ft10";
	else if(s == 62) return "ft11";
	else if(s == 63) return "fcsr";
	else if(s == 64) return "frm";
	else if(s == 65) return "fflags";
	else return "error";
}


void signalHandler(int sig, siginfo_t* siginfo, void* context){
  if(!executingNow || handlerHasAlreadyRun){         //prevent looping
    signal(SIGILL, SIG_DFL);
    signal(SIGFPE, SIG_DFL);
    signal(SIGSEGV, SIG_DFL);
    signal(SIGBUS, SIG_DFL);
    signal(SIGTRAP, SIG_DFL);
  }
  handlerHasAlreadyRun = 1;
  lastSig = sig;
  siglongjmp(buf, 1);
}


//adapted from https://stackoverflow.com/questions/1024389/print-an-int-in-binary-representation-using-c
char* uint16ToBin(uint16_t num)
{
    size_t bits = sizeof(uint16_t) * CHAR_BIT;
    char * str = malloc(bits + 1);
    if(!str) return NULL;
    str[bits] = '\0';
    for(; bits--; num >>= 1)
        str[bits] = num & 1 ? '1' : '0';

    return str;
}


//adapted from https://stackoverflow.com/questions/1024389/print-an-int-in-binary-representation-using-c
char* uint32ToBin(uint32_t num)
{
    size_t bits = sizeof(uint32_t) * CHAR_BIT;
    char * str = malloc(bits + 1);
    if(!str) return NULL;
    str[bits] = '\0';
    for(; bits--; num >>= 1)
        str[bits] = num & 1 ? '1' : '0';

    return str;
}


void endTest(){
	//placeholder for further cleanup if needed
  	free(sighandlerStack.ss_sp);
}


/* Hardware performance monitoring */

//Can only use these if running in machine mode
//=============================================

#define countEventC3(eventMask) ({ asm ("csrw mhpmevent3, %0" : : "r"(eventMask)); })
#define countEventC4(eventMask) ({ asm ("csrwi mhpmevent4, %0" : : "r"(eventMask)); })


//For user mode - must have enabled already in machine mode (modify bootloader)
//Warning: they are unreliable and often don't even count anything
//=============================================================================

//don't use mcycle, minstret - they're the machine mode versions, segfault
#define cycles() ( { uint64_t __tmp; \
  asm ("csrr %0, cycle" : "=r"(__tmp)); \
  __tmp; })
#define instrsRetired() ( { uint64_t __tmp; \
  asm ("csrr %0, instret" : "=r"(__tmp)); \
  __tmp; })

#define readC3(reg) ({ uint64_t __tmp; \
  asm ("csrr %0, hpmcounter3" : "=r"(__tmp)); \
  __tmp; })

#define readC4(reg) ({ uint64_t __tmp; \
  asm ("csrr %0, hpmcounter4" : "=r"(__tmp)); \
  __tmp; })

//instr commit events, bits 7:0=0
#define EVENT_EXCEPTIONS 0x100			//100000000
#define EVENT_LOADS 0x200			//1000000000
#define EVENT_STORES 0x400			//10000000000
#define EVENT_ATOMIC_MEM 0x800			//100000000000
#define EVENT_SYSTEM 0x1000 			//1000000000000
#define EVENT_INT_ALU 0x2000			//10000000000000
#define EVENT_COND_BRANCH 0x4000		//100000000000000
#define EVENT_JAL 0x8000			//1000000000000000
#define EVENT_JALR 0x10000			//10000000000000000
#define EVENT_INT_MULT 0x20000			//100000000000000000
#define EVENT_INT_DIV 0x40000			//1000000000000000000

//uarch events, bits 7:0=1
#define EVENT_LDUSE_INTERLOCK 0x101		//100000001
#define EVENT_LONGLAT_INTERLOCK 0x201		//1000000001
#define EVENT_CSRRD_INTERLOCK 0x401		//10000000001
#define EVENT_ICACHE_BUSY 0x801			//100000000001
#define EVENT_DCACHE_BUSY 0x1001		//1000000000001
#define EVENT_BRANCHDIR_MISPREDICT 0x2001	//10000000000001
#define EVENT_BRANCHTGT_MISPREDICT 0x4001	//100000000000001
#define EVENT_CSRW_PIPELINE_FLUSH 0x8001	//1000000000000001
#define EVENT_OTHER_PIPELINE_FLUSH 0x10001	//10000000000000001
#define EVENT_INTMULT_INTERLOCK 0x20001		//100000000000000001

//memory events, bits 7:0=2
#define EVENT_ICACHE_MISS 0x102			//100000010
#define EVENT_MMAPIO 0x202			//1000000010

//====================================================

