#include "opcodeTesterRiscv.h"

// CONFIGURATION
// =============

#define NUM_TESTS 1
#define RESERVED_ONLY 0		//test suspicious reserved quadrant 0 bit pattern only (otherwise brute force search)
#define PRINT_STATE_CHANGE 1    //check for register state change
#define PRINT_DOCUMENTED 0	//print disassembly of documented instructions

// =============

//using more heap global vars than usual to avoid corruption in child process (programming the weird machine...)
unsigned char execInstruction[4];
uint16_t bitPattern = 0;
int stat_loc;
unsigned int i,j,k,l = 0;
char output_buf[128] = { 0 };
uint64_t converted_instr = 0;
int instr_count = 0;

int main(int argc, char* argv[]){

  //lock to CPU core 0 to prevent cross-modifying code
  //can do this with higher level interface 'taskset -c 0' instead on Debian, but taskset isn't supported on buildroot, and good to have it anyway in case we forget to run with taskset
  cpu_set_t mask;
  CPU_ZERO(&mask);
  CPU_SET(0, &mask);
  if(sched_setaffinity(0, sizeof(cpu_set_t), &mask) != 0){
	printf("Couldn't lock to CPU core 0.\nContinuing, but can't guarantee there will be no code cross-modification between cores.\n");
	sleep(2);
  }

  //giving handler its own stack allows it to continue handling even in case stack pointer has ended up wildly out
  sighandlerStack.ss_sp = malloc(SIGSTKSZ);
  if (sighandlerStack.ss_sp == NULL) {
  	printf("Couldn't allocate memory for alt handler stack, exiting.\n");
	return 1;
  }
  sighandlerStack.ss_size = SIGSTKSZ;
  sighandlerStack.ss_flags = 0;
  if (sigaltstack(&sighandlerStack, NULL) == -1) {
	printf("Couldn't set alt handler stack, exiting.\n");
	endTest();
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
		printf("Couldn't set signal handler, exiting.\n");
		endTest();
	    	return 1;
  }

  //this is essential, otherwise the child processes live on as zombies and we can't fork any more (even if we kill them) bc we hit the process limit
  signal(SIGCHLD, SIG_IGN);

  //get system's page size and calculate pagestart addr
  size_t pagesize = sysconf(_SC_PAGESIZE);
  uintptr_t pagestart = ((uintptr_t) &execInstruction) & -pagesize;
  if(mprotect((void*) pagestart, pagesize, PROT_READ|PROT_WRITE|PROT_EXEC)){
	    printf("Failed to obtain executable memory, exiting.\n");
	    endTest();
	    return 1;
  }

  #if !RESERVED_ONLY
  bool testing = true;
  while(testing){
	if(instr_count != 0) bitPattern++;
	if(bitPattern == UINT16_MAX) testing = false;
	instr_count++;

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

        instr_count++;

	bitPattern |= ((uint16_t)1 << 15);			//15th byte is always 1

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
	i = (bitPattern & 0xff);				//lowermost 8 bits, mask 0000 0000 0000 0000 0000 0000 1111 1111 */

	memset(execInstruction, 0, 4);
        execInstruction[0] = i;
	execInstruction[1] = j;
	execInstruction[2] = 0x82;
	execInstruction[3] = 0x80;

	converted_instr = 0;
  	memcpy(&converted_instr, &execInstruction, 2);
  	memset(output_buf, 0, sizeof(output_buf));
  	disasm_inst(output_buf, sizeof(output_buf), rv64, 0, converted_instr);

	if(strstr(output_buf, "illegal") != NULL){		//skip testing if this is a doc'd instr
		usleep(500);
		char* binStr = uint16ToBin(converted_instr);
		printf("\n%s ", binStr);
		fflush(stdout);					//IMPORTANT otherwise children inherit unflushed buffer
		free(binStr);	
		asm("fence");			
		asm("fence.i"); 	

		for(int test=0; test<NUM_TESTS; test++){
			pid_t pid = fork();
			if(pid == 0) {					//we are the child process				
				if(!sigsetjmp (buf, 1)){
					handlerHasAlreadyRun = 0;
					executingNow = 1;
					#if PRINT_STATE_CHANGE
					GET_STATE_BEFORE()
					#endif
					((void(*)())execInstruction)();
					#if PRINT_STATE_CHANGE
					GET_STATE_AFTER()
					#endif
					asm volatile ("fence");
					asm volatile ("fence.i");
					executingNow = 0;
					#if PRINT_STATE_CHANGE
					for(int state=1; state<SIZE_STATE; state++){
						if( beforeState[state] != afterState[state] && state != 14){	
							printf("%s before: %" PRIu64 " after: %" PRIu64 " ", getStateName(state), beforeState[state], afterState[state]);
							fflush(stdout);
						}
					}
					#endif
					printf("RAN");
					fflush(stdout);
					} 
					else if(lastSig == 4) printf("SIGILL");
					else if(lastSig == 11) printf("SIGSEGV");
					else printf("OTHERSIG%d", lastSig); //never seen
				asm volatile("fence");
				exit(lastSig);
			}
			else{
				int childSig = 0;
				waitpid(pid, &childSig, 0);
				//can't check signal here, childSig is ALWAYS 0 
			}
		}
		
	}
	else{
		#if PRINT_DOCUMENTED
		char* binStr = uint16ToBin(converted_instr);
		printf("DOCUMENTED %s %" PRIu64 " %s\n", binStr, converted_instr, output_buf);
		fflush(stdout);
		free(binStr);
		#endif
	}

  #if !RESERVED_ONLY
  }
  #else
  } } } } } } } } } } } 	//sorry!
  #endif

  printf("\nTest complete!\n");
  printf("Total num instructions tested: %d\n", instr_count);
  endTest();
  return 0;
}

