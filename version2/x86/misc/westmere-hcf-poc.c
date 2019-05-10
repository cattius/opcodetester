#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/mman.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>
#include <setjmp.h>
#include <sys/wait.h>

unsigned char execInstruction[6];
sigjmp_buf buf;
struct sigaction handler;
int stat_loc;
unsigned int i,j,k = 0;

void signalHandler(int sig, siginfo_t* siginfo, void* context){
	siglongjmp(buf, 1);
}

int main(){

	//giving handler its own stack allows it to continue handling even in case stack pointer has ended up wildly out
	stack_t sigstack;
  sigstack.ss_sp = malloc(SIGSTKSZ);
  if (sigstack.ss_sp == NULL) {
  	perror("malloc");
    return 1;
  }
  sigstack.ss_size = SIGSTKSZ;
  sigstack.ss_flags = 0;
  if (sigaltstack(&sigstack, NULL) == -1) {
  	perror("sigaltstack");
    return 1;
  }

	memset(&handler, 0, sizeof(handler));
	handler.sa_flags = SA_SIGINFO | SA_ONSTACK;
	handler.sa_sigaction = signalHandler;
  if (sigaction(SIGILL, &handler, NULL) < 0   || \
      	sigaction(SIGFPE, &handler, NULL) < 0   || \
      	sigaction(SIGSEGV, &handler, NULL) < 0  || \
      	sigaction(SIGBUS, &handler, NULL) < 0   || \
      	sigaction(SIGTRAP, &handler, NULL) < 0 ) {
    	    perror("sigaction");
    	    return 1;
	}

 	//get system's page size and calculate pagestart addr
	size_t pagesize = sysconf(_SC_PAGESIZE);
	uintptr_t pagestart = ((uintptr_t) &execInstruction) & -pagesize;
	if(mprotect((void*) pagestart, pagesize, PROT_READ|PROT_WRITE|PROT_EXEC)){
		perror("mprotect");
		printf("Failed to obtain executable memory, exiting.\n");
		return 1;
	}

	printf("HCF in 3, 2, 1...\n");
	i = 0x01;
	j = 0x52;
	k = 0x96;
	//for(k=0x8e; k<0x97; k++){
		printf("%02x%02x%02x\n", i, j, k);
		execInstruction[0] = 0x55;        //mini function prologue, (push %rbp)
		execInstruction[1] = i;
		execInstruction[2] = j;
		execInstruction[3] = k;
		execInstruction[4] = 0x5d;        //mini function epilogue, (pop %rbp, retq)
		execInstruction[5] = 0xc3;

		pid_t pid = fork();
		if(pid == 0) {										//we are the child process
			if(!sigsetjmp (buf, 1)){
				((void(*)())execInstruction)();
			}
			exit(0);
		}
		else{
			waitpid(pid, &stat_loc, 0);
		//	uint8_t child_sig = WEXITSTATUS(stat_loc);			//this only gets (lower?) 8 bytes so can't send length in it
		}
	//}

	printf("Doneee\n");
	free(sigstack.ss_sp);
	return 0;
}
