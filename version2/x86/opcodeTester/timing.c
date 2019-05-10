#include "opcodeTester.h"

#if USE_TRAP_FLAG && B32 && TIMING_ATTACK
#define EXEC_INSTRUCTION_LENGTH 9+3
#elif USE_TRAP_FLAG && TIMING_ATTACK
#define EXEC_INSTRUCTION_LENGTH 10+3
#elif TIMING_ATTACK
#define EXEC_INSTRUCTION_LENGTH 4
#elif USE_TRAP_FLAG && B32
#define EXEC_INSTRUCTION_LENGTH 9+INSTR_MAX
#elif USE_TRAP_FLAG
#define EXEC_INSTRUCTION_LENGTH 10+INSTR_MAX
#else
#define EXEC_INSTRUCTION_LENGTH 1+INSTR_MAX
#endif

  #if USE_TRAP_FLAG && B32
  #define offset 9
  #elif USE_TRAP_FLAG
  #define offset 10
  #else
  #define offset 0
  #endif

struct sigaction handler;
struct sigaction eHandler;
stack_t sig_stack;
unsigned char execInstruction[EXEC_INSTRUCTION_LENGTH];
int stat_loc;
//it's important that these are on the heap rather than the stack, otherwise they can get corrupted in child process
unsigned int i,j,k,l = 0;
uint64_t cyclesAfter, cyclesBefore, cyclesCount, tempCyclesCount, instrMin, instrMax = 0;
int t, actualTests, instrMaxTest, instrMinTest = 0;
xed_error_enum_t err;
uint64_t numSamples = 0;
int len = 0;			     	    //NOTE: may overflow during long tests (not a critical issue, just exit count of number tested will be lower than expected)
uint32_t bitPattern = 0;


void printInstr(unsigned char* execInstruction, int length){
  if(length < 1 || length > 15){
	printf("Error: bad instruction length\n");
	return;
  }
  for(int l=offset; l<(offset+length); l++){
    printf("%02x", execInstruction[l]);
  }
  printf(" ");
}

void exitHandler(int sig, siginfo_t* siginfo, void* context){
  printf("Num random instrs tested (NOT necessarily unique): %" PRIu64 "\n", numSamples);
  free(sig_stack.ss_sp);
  exit(0);
}


int main(){

   //lock to core 0 to avoid cross-modifying code
   cpu_set_t set;
   CPU_ZERO(&set);        // clear cpu mask
   CPU_SET(0, &set);      // set cpu 0
   if( sched_setaffinity(0, sizeof(cpu_set_t), &set) < -1){
	perror("sched_setaffinity");
	return 1;
   }
   else printf("Set processor affinity to core 0\n");

  //giving handler its own stack allows it to continue handling even in case stack pointer has ended up wildly out
  sig_stack.ss_sp = malloc(SIGSTKSZ);
  if (sig_stack.ss_sp == NULL) {
  	printf("Couldn't allocate memory for alt handler stack, exiting.\n");
    	return 1;
  }
  sig_stack.ss_size = SIGSTKSZ;
  sig_stack.ss_flags = 0;
  if (sigaltstack(&sig_stack, NULL) == -1) {
    	printf("Couldn't set alt handler stack, exiting.\n");
  	free(sig_stack.ss_sp);
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
	free(sig_stack.ss_sp);
    	return 1;
  }

  memset(&eHandler, 0, sizeof(eHandler));
  eHandler.sa_flags = SA_SIGINFO;
  eHandler.sa_sigaction = exitHandler;
  if (sigaction(SIGINT, &eHandler, NULL) < 0) {
        printf("Couldn't set exit handler, exiting.\n");
	free(sig_stack.ss_sp);
    	return 1;
  }

  //this is essential, otherwise the child processes live on as zombies and we can't fork any more (even if we kill them) bc we hit the process limit
  signal(SIGCHLD, SIG_IGN);

  //get system's page size and calculate pagestart addr
  size_t pagesize = sysconf(_SC_PAGESIZE);
  uintptr_t pagestart = ((uintptr_t) &execInstruction) & -pagesize;
  if(mprotect((void*) pagestart, pagesize, PROT_READ|PROT_WRITE|PROT_EXEC)){
  	printf("Failed to obtain executable memory, exiting.\n");
	free(sig_stack.ss_sp);
	return 1;
  }

  bool testing = true;
  bool documented = false;
  unsigned char xedInstr[15];
  xed_tables_init();

  printf("Testing...\n");
  while(testing){

	memset(execInstruction, 0, EXEC_INSTRUCTION_LENGTH);
	memset(xedInstr, 0, 15);
	int index = 0;

	#if TIMING_ATTACK
	len = 3;
	bitPattern++;
	if(bitPattern == 0xffffff) testing = false;		//test three-byte space only
	i = ( (bitPattern >> 16) & 0xff);			//next 8 bits, mask      1111 1111 0000 0000 0000 0000
	j = ( (bitPattern >> 8) & 0xff);			//next 8 bits, mask      0000 0000 1111 1111 0000 0000
	k = (bitPattern & 0xff);				//lowermost 8 bits, mask 0000 0000 0000 0000 1111 1111 */
	#endif

	#if USE_TRAP_FLAG
	execInstruction[index++] = 0x9c; //pushfq
	#if !B32
	execInstruction[index++] = 0x48; //REX prefix for 64-bit
	#endif
	execInstruction[index++] = 0x81; //orq    $0x100,(%rsp/esp)
	execInstruction[index++] = 0x0c;
	execInstruction[index++] = 0x24;
	execInstruction[index++] = 0x00;
	execInstruction[index++] = 0x01;
	execInstruction[index++] = 0x00;
	execInstruction[index++] = 0x00;
	execInstruction[index++] = 0x9d; //popfq
	#endif

	#if TIMING_ATTACK
	execInstruction[index++] = i;
	execInstruction[index++] = j;
	execInstruction[index++] = k;
	xedInstr[0] = i;
	xedInstr[1] = j;
	xedInstr[2] = k;
	#else
	if(numSamples % 10 == 0){
		arc4random_stir();					     //use data from urandom to mix up PRNG
	}
	len = arc4random_uniform(INSTR_MAX+1);
	if(len == 0) len = 1;
	for(int c=0; c<len; c++){
		execInstruction[index++] = arc4random_uniform(256);
		xedInstr[c] = execInstruction[c+offset];
	}
	#endif

	#if !USE_TRAP_FLAG
	execInstruction[index++] = 0xc3; //ret
	#endif

	//printf("%02x %02x %02x\n", xedInstr[0], xedInstr[1], xedInstr[2]); 

    	documented = false;
   	for(int d = 1; d < 16; d++){
		//MUST reinitialize these on each decode attempt in order for decode to work
		err = XED_ERROR_NONE;
		xed_decoded_inst_t xedd;
		xed_decoded_inst_zero(&xedd);
		#if B32
	    	xed_decoded_inst_set_mode(&xedd, XED_MACHINE_MODE_LEGACY_32, XED_ADDRESS_WIDTH_32b);
		#else
	    	xed_decoded_inst_set_mode(&xedd, XED_MACHINE_MODE_LONG_64, XED_ADDRESS_WIDTH_64b);
		#endif
		xed_chip_features_t features;
		xed_get_chip_features(&features, MICROARCH);
		err = xed_decode_with_features(&xedd, XED_STATIC_CAST(const xed_uint8_t*,xedInstr), d, &features);
		if (err == XED_ERROR_NONE){
		  documented = true;
		  #if PRINT_DISASSEMBLY
			  if(!RUN_DOCUMENTED){
				printInstr(execInstruction, len);
					printf("DOC 0 %s\n", xed_iform_enum_t2str(xed_decoded_inst_get_iform_enum (&xedd)));
			  }
		  #endif
		  break;
		}
        }

    	if(RUN_DOCUMENTED || !documented){
     		fflush(stdout);			//prevent child inheriting parent's printf buffer
		asm volatile("mfence; cpuid");	//serialise
      		pid_t pid = fork();
      		if(pid == 0) {   	
			lastSig = 0;
			actualTests = 0;
			instrMax = 0;
			instrMaxTest = 0;
			instrMin = UINT32_MAX;
			instrMinTest = 0;
			#if B32
			usleep(10);		//on Atom N270 CPUID causes weird exception behaviour without a delay here. not sure if this affects other models too
			#endif
			for(t = 0; t<NUM_TESTS; t++){
				if(!sigsetjmp (buf, 1)){
					handlerHasAlreadyRun = 0;
					executingNow = 1;
					GET_STATE_BEFORE()
					((void(*)())execInstruction)();
				}
				GET_STATE_AFTER()

				//according to Intel we should do this shift even on 32-bit (contradicts other examples I've seen)
	 			highB = (highB<<32) | lowB;
				highA = (highA<<32) | lowA;
				cyclesBefore = highB;
				cyclesAfter = highA;
				tempCyclesCount = (cyclesAfter - cyclesBefore);

				/*discard tests 0 and 1 - still warming icache, normally outlier values
				* use test 2 value as baseline value to check later tests against
				* if t>2 only include value for average if it's not an extreme outlier (no more than 1000 cycles above baseline) - suggests process descheduled or CPU into SMM etc
				* this isn't a perfect strategy but trying to balance accuracy with speed
				* log min and max cycle counts too as they can be interesting, with some instructions the first and second tests actually have lower counts which is bizarre  */

				if(tempCyclesCount < instrMin) {
					instrMin = tempCyclesCount;
					instrMinTest = t;
				}
				if(tempCyclesCount > instrMax) {
					instrMax = tempCyclesCount;
					instrMaxTest = t;
				}

				uint64_t currentAvg = (t > 3) ? (cyclesCount / (t-3)) : cyclesCount;
				if( t == 2 || (t > 1 && ( tempCyclesCount < (currentAvg + 1000) )) ){
					actualTests++;
					cyclesCount += (cyclesAfter - cyclesBefore);
				}

			}
			cyclesCount /= actualTests;
			if(lastSig == 0){
				printInstr(execInstruction, len);
				printf("RAN 0 %s avg %" PRIu64 " min %" PRIu64 " test %d max %" PRIu64 " test %d\n", xed_error_enum_t2str(err), cyclesCount, instrMin, instrMinTest, instrMax, instrMaxTest);
			}
			else if( !CARE_ABOUT_RUNNING_ONLY && (!IGNORE_SIGILL || lastSig != 4 || (CARE_ABOUT_CYCLES && cyclesCount > CYCLE_THRESHOLD)) ) {
				printInstr(execInstruction, len);
				printf("EXCPT %d %s avg %" PRIu64 " min %" PRIu64 " test %d max %" PRIu64 " test %d\n", lastSig, xed_error_enum_t2str(err), cyclesCount, instrMin, instrMinTest, instrMax, instrMaxTest);
			}
			//ok to have state changes on a separate line, just skip them in python file parsing
			if(PRINT_STATE_CHANGE && lastSig == 0){
				bool stateChanged = false;
				for(int s=0; s<SIZE_STATE; s++){
					if(beforeState[s] != afterState[s] && s != 0 && s != 3 && s != 4 && s != 5 && s != 21 && s != 22){
						if(!stateChanged){
							printf("State change: ");
							stateChanged = true;
						}
						printf("%s: %" PRIu64 " %" PRIu64 ", ", getStateName(s), beforeState[s], afterState[s]);
					}
				}
				if(stateChanged) printf("\n");
			}
			fflush(stdout);
			exit(lastSig);
      		}
	      	else {
			int status = 0;
			waitpid(pid, &status, 0);		
			numSamples++;
	      	}
    	}

    	if( !RUN_INFINITELY && (numSamples == NUM_SAMPLES) ) testing = false;
  }

  printf("Done!\n");
  free(sig_stack.ss_sp);
  return 0;
}
