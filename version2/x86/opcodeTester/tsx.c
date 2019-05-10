#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <setjmp.h>
#include <signal.h>
#include <sys/mman.h>
#include <string.h>
#include <inttypes.h>
#include <unistd.h>
#include <stdlib.h>

sigjmp_buf buf;
struct sigaction handler;
unsigned char execInstruction[15];
//1000000 reps needed to get really reproducible results (variance +/- 10 cycles for TSX, bit more for without)
#define REPS 10000000

void signalHandler(int sig, siginfo_t* siginfo, void* context){
  siglongjmp(buf, 1);
}

int main()
{
    /*this is so cool! everything between xbegin and xend is a transaction.
      if we get an exception in the transaction, the entire transaction is rolled back and we go to the fallback path
      the fallback path here is the ABORT label - as this is in a while loop, we need to change control flow, otherwise
      i never gets updated and we just loop endlessly. but generally you don't need anything special after it. the cool
      thing here is the exception never gets reported to the OS (reduces timing overhead?) *and* we have our sigsetjmp built in:
      "On an RTM abort, the logical processor discards all architectural register and memory updates performed during the RTM execution and restores architectural state to that corresponding to the outermost XBEGIN instruction." */

    memset(&handler, 0, sizeof(handler));
    handler.sa_flags = SA_SIGINFO;
    handler.sa_sigaction = signalHandler;
    uint64_t lowA, lowB, highA, highB, cyclesBefore, cyclesAfter;
    uint64_t tsxCyclesCount = 0;
    int abortCode = 0;

    memset(execInstruction, 0, 15);
    size_t pagesize = sysconf(_SC_PAGESIZE);
    uintptr_t pagestart = ((uintptr_t) &execInstruction) & -pagesize;
    if(mprotect((void*) pagestart, pagesize, PROT_READ|PROT_WRITE|PROT_EXEC)){
	return 1;
    }

    //TSX does not support the trap flag (trying to modify it triggers an abort, and if we set it beforehand we also trigger an abort...)
    //but we can use INT 3 (0xcc), as it's designed for debugging *hopefully* decoders are capable of not misinterpreting it as part of a longer instruction
    execInstruction[1] = 0x90;
    execInstruction[0] = 0xcc;

    asm volatile("mfence; cpuid");

    //warm icache for TSX
    /*asm volatile ("cpuid; rdtsc; mov %%rdx, %0; mov %%rax, %1" : "=r" (highB), "=r" (lowB) : : "%rax", "%rbx", "%rcx", "%rdx");
    asm volatile ("xbegin ABORT1;");
    ((void(*)())execInstruction)();
    asm volatile("xend");
    asm volatile("ABORT1:");
    asm volatile("mov %%eax, %0;" : "=r" (abortCode) : :);		//abort status is returned in the eax register (not rax...higher bits are 'reserved')
    asm volatile ("rdtscp; mov %%rdx, %0; mov %%rax, %1; cpuid" : "=r" (highA), "=r" (lowA) : : "%rax", "%rbx", "%rcx", "%rdx");
    printf("Pre abort code is %d\n", abortCode); */

    int abortZero = 0;

    //time with TSX
    for(int i=0; i<REPS; i++){
	    asm volatile ("cpuid; rdtsc; mov %%rdx, %0; mov %%rax, %1" : "=r" (highB), "=r" (lowB) : : "%rax", "%rbx", "%rcx", "%rdx");
	    asm volatile ("xbegin ABORT2");
	    ((void(*)())execInstruction)();
	    asm volatile ("xend");
	    printf("RAN!\n");
	    asm volatile ("ABORT2:");
	    //abortCode is 16 if it hits 0xcc, 0 for any other exception
	    asm volatile("mov %%eax, %0;" : "=r" (abortCode) : :);
	    asm volatile ("rdtscp; mov %%rdx, %0; mov %%rax, %1; cpuid" : "=r" (highA), "=r" (lowA) : : "%rax", "%rbx", "%rcx", "%rdx");
	    highB = (highB<<32) | lowB;
	    highA = (highA<<32) | lowA;
	    cyclesBefore = highB;
	    cyclesAfter = highA;
	    tsxCyclesCount += (cyclesAfter - cyclesBefore);
	    if(abortCode == 0) abortZero++;
	    else if(abortCode != 16) printf("weird abort code %d\n", abortCode);
    }
    printf("TSX cycles: %" PRIu64 "\n", (tsxCyclesCount / REPS));
    printf("Abort zero count: %d\n", abortZero);

    //TODO: does this completely solve the problem of exception handling in the kernel?
    return 0;
}
