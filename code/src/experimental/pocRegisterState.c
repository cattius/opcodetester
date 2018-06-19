#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <stdbool.h>
#include "../include/opcodeTesterUser.h"

int instructionFailed = 0;
int lastSignal = 0;
int signalCounts[7] = {0};
extern char resume;
long state[SIZE_STATE] = {0};

void signalHandler(int sig, siginfo_t* siginfo, void* context){

	READ_PROCESSOR_STATE(state);
	printf("\nException thrown!\n\nState after: \n");
	for(int i=0; i < SIZE_STATE; i++){
		printf("%ld\n", state[i]);
	}

	//also abort if couldn't restore context last time - instructionFailed increasing means we are stuck in a loop
	if(instructionFailed > 3 || sig == 6){
		printf("Aborting, too many signals.\n");
		exit(1);
	}
	else{
		switch(sig){
			case 4:
				lastSignal = OPCODE_SIGILL;
        printf("SIGILL\n");
				break;
			case 5:
				lastSignal = OPCODE_SIGTRAP;
        printf("SIGTRAP\n");
				break;
			case 7:
				lastSignal = OPCODE_SIGBUS;
        printf("SIGBUS\n");
				break;
			case 8:
				lastSignal = OPCODE_SIGFPE;
        printf("SIGFPE\n");
				break;
			case 10:
				lastSignal = OPCODE_SIGBUS;
        printf("SIGBUS\n");
				break;
			case 11:
				lastSignal = OPCODE_SIGSEGV;
        printf("Instruction caused signal SIG_SEGV with si code %s\n", getSiCodeString(OPCODE_SIGSEGV, siginfo->si_code));
				break;
			default:
				lastSignal = OPCODE_SIGOTHER;
        printf("other signal\n");
				break;
		}

		instructionFailed++;

		mcontext_t* mcontext = &((ucontext_t*)context)->uc_mcontext;      // get execution context
		mcontext->gregs[IP]=(uintptr_t)&resume; 													//skip faulting instruction
		mcontext->gregs[REG_EFL]&=~0x100;       													//sign flag
	}

}

int main(){

	//attempt to handle errors gracefully
	struct sigaction handler;
	memset(&handler, 0, sizeof(handler));
	handler.sa_sigaction = signalHandler;
	handler.sa_flags = SA_SIGINFO;
	if (sigaction(SIGILL, &handler, NULL) < 0   || \
			sigaction(SIGFPE, &handler, NULL) < 0   || \
			sigaction(SIGSEGV, &handler, NULL) < 0  || \
			sigaction(SIGBUS, &handler, NULL) < 0   || \
			sigaction(SIGTRAP, &handler, NULL) < 0  || \
			sigaction(SIGABRT, &handler, NULL) < 0  ) {
		perror("sigaction");
		return 1;
	}

	unsigned char execInstruction[18] = {0};

	//get system's page size and calculate pagestart addr
	size_t pagesize = sysconf(_SC_PAGESIZE);
	uintptr_t pagestart = ((uintptr_t) &execInstruction) & -pagesize;
	if(mprotect((void*) pagestart, pagesize, PROT_READ|PROT_WRITE|PROT_EXEC)){
		perror("mprotect");
		printf("Failed to obtain executable memory, exiting.\n");
		return 1;
	}

	execInstruction[0] = 0x55;
	execInstruction[1] = 0x4a;
	execInstruction[2] = 0x0f;
  execInstruction[3] = 0xe7;
  execInstruction[4] = 0xaa;
  execInstruction[5] = 0xdd;
	execInstruction[6] = 0x5d;
	execInstruction[7] = 0xc3;

	//TODO: this will never be useful for testing bc ovc register state is changed by all sorts of other things
	//need to run it in the kernel to even have a chance of seeing the reg state stay the same...

	printf("State before: \n");
	READ_PROCESSOR_STATE(state);
	for(int i=0; i < SIZE_STATE; i++){
		printf("%ld\n", state[i]);
	}
	((void(*)())execInstruction)();
	__asm__ __volatile__ ("\
		.global resume   \n\
		resume:          \n\
		"
		);
	;
	if(!instructionFailed) printf("Instruction did not throw an exception! Try another.\n");
	return 0;
}
