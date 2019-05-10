#include "opcodeTesterRiscv.h"

// CONFIGURATION
// =============

#define NUM_TESTS 1		//number of times to repeat testing (using separate child process if not INFER_FUNCTIONALITY, same process otherwise)
#define BRUTE_FORCE 0		//brute force search. will override RESERVED_SPECIAL
#define RESERVED_SPECIAL 1	//test only undocumented reserved instructions
				//if both are 0 then will test functional patterns instead
#define PRINT_STATE_CHANGE 1    //check for register state change
#define PRINT_DOCUMENTED 0	//print disassembly of documented instructions
#define INFER_FUNCTIONALITY 0	//try to guess what the instruction does. bit messy. note if you use this program expects 5 command line args: numTests forceTestFaultingInstrs printMachineReadable printHumanReadable beQuiet

// =============


//using more heap global vars than usual to avoid corruption in child process (programming the weird machine...)
volatile sig_atomic_t penultimateSig = 0;
unsigned char execInstruction[6];
uint32_t bitPattern = 0;
int stat_loc;
unsigned int i,j,k,l = 0;
char output_buf[128] = { 0 };
char output_buf_spare[128] = { 0 };
uint64_t converted_instr = 0;
uint64_t lastState[SIZE_STATE] = {0};
uint32_t loadsBefore, loadsAfter, storesBefore, storesAfter, tempLoadsCount, tempStoresCount, loadsCount, storesCount = 0;
int c = 0;		//loop counter was getting corrupted on stack
int state = 0;    	//as above
int instr_count = 0;

//config - defined by user with command-line args
int numTests, forceTestFaultingInstrs, printMachineReadable, printHumanReadable, beQuiet = 0;

#if INFER_FUNCTIONALITY
char* printSrcRegs(bool isSrcReg[SIZE_STATE], bool humanReadable){
	
	char* output = malloc(sizeof(char) * 50);		//most instrs have only one possible src reg if testing *once* only, but if you test multiple times you get really bizarre behaviour
	int charsLeft = 49;	
	output[0] = '\0';	

	for(int r=0; r<SIZE_STATE; r++){
		if(isSrcReg[r]){

			//Note: strncat overwrites previous null terminator at each append

			if(humanReadable){
				int len = strlen(getStateName(r)) + 1;		 //incl. space but not null terminator
				if( charsLeft > len ){
					strncat(output, getStateName(r), len+1);
					strncat(output, " ", 2);
					charsLeft -= len;
				}
				else{
					printf("\nWARNING: ran out of characters to print src regs\n");
					break;
				}
			}
	
			else{
				char num[3];
				sprintf(num, "%d", r);
				int len = strlen(num);
				if( charsLeft > len + 1 ){			//incl. space but not null terminator
					strncat(output, num, len+1);
					strncat(output, " ", 2);
					charsLeft -= len;
				}
				else{
					printf("\nWARNING: ran out of characters to print src regs\n");
					break;
				}
			}

		}
	}

	return output;
} 


void testInstruction(){
	lastSig = 0;
	penultimateSig = 1;
	uint64_t difference0[SIZE_STATE] = {0};	//must be unsigned as otherwise we get overflow - but watch for negatives (->overflow)
	uint64_t difference1[SIZE_STATE] = {0};	//as above
	uint64_t initialVal[SIZE_STATE] = {0};
	uint64_t temp = 0;
	bool probablyConst[SIZE_STATE] = {false};
	bool probablyIncr[SIZE_STATE] = {false};
	bool negDifference0[SIZE_STATE] = {false};
	bool negDifference1[SIZE_STATE] = {false};
	bool gotFunctionality = false;

	int destReg = 0;
	int instrType = 0;
	int ran = 0;
	int segv = 0;
	int ill = 0;
	int sigother = 0;
	int warnLate = 0;
	int srcReg = -1;
	bool isSrcReg[SIZE_STATE] = {false};
	uint64_t instrOperand1, instrOperand2 = 0;		//op2 is inc/dec amount for types 3 and 4

	/* Instr types:
	   ============
	* no state change or unknown 0
	* add 1
	* sub 2
	* mv/ld and increment 3
	* mv/ld and decrement 4
	* mov/ld constant 5
	*/

	bool stateChange = false;
	for(c = 0; c<numTests; c++){
		if(!sigsetjmp (buf, 1)){
			handlerHasAlreadyRun = 0;
			executingNow = 1;
			GET_STATE_BEFORE()
			((void(*)())execInstruction)();
			GET_STATE_AFTER()
			asm volatile ("fence");
			asm volatile ("fence.i");
			executingNow = 0;

			//need to handle multiple dest+src regs - they do occur - but only with repeated tests?
			for(state=1; state<SIZE_STATE; state++){
				if( !gotFunctionality && beforeState[state] != afterState[state] && state != 14){	
					stateChange = true;
					if(!beQuiet && destReg != 0 && destReg != state) printf("\nWARNING: multiple registers changed state %" PRIu64 ", assumed this wouldn't happen %s %s\n", converted_instr, getStateName(destReg), getStateName(state));						
					destReg = state;

					//for some inexplicable reason, c=0 often appears to not have run, but c>0 does? so c==1 is also an 'init' run, but need to handle separately
					if(c < 2){
						if(c == 0){
							GET_DIFFERENCE_UNSIGNED(afterState[state], beforeState[state], difference0[state], negDifference0[state])
							initialVal[state] = afterState[state];
							//make an initial guess at what it does - some instructions fail before we can test again
							//assume it's a ld/ldi unless we see it incrementing/decrementing later
							instrType = 5;
							instrOperand1 = difference0[state];
							if(afterState[state] != 0){	//blacklist zero as too many possible regs
								for(int reg=1; reg<SIZE_STATE; reg++){
									if(reg != state && difference0[state] != 0 && beforeState[reg] == difference0[state]){
										srcReg = reg;
										isSrcReg[reg] = true;
									}
								}
							}
							else srcReg = 0;
						}
						else if(c == 1) GET_DIFFERENCE_UNSIGNED(afterState[state], beforeState[state], difference1[state], negDifference1[state])
							if(afterState[state] != 0){	//blacklist zero as too many possible regs
								for(int reg=1; reg<SIZE_STATE; reg++){
									if(reg != state && difference1[state] != 0 && beforeState[reg] == difference1[state]){
										srcReg = reg;
										isSrcReg[reg] = true;
									}
								}
							}
							else srcReg = 0;
					}
					else{
						GET_DIFFERENCE_UNSIGNED_IGNORE_NEGATIVE(afterState[state], lastState[state], temp)
						//by focusing on afterState and ignoring beforeState, we get around the weird cases where beforeState seems to get clobbered (e.g. a6 reg) - but maybe be missing some aspect of the instruction's functionality because of that?
						if(temp == difference0[state]){	//probably an incremental ADD/SUB
							if(!probablyIncr[state]) probablyIncr[state] = true;
								else{
									if(c > 3) warnLate = c;
									//deliberately not an else if
									if(negDifference0[state]) instrType = 2;	//decrementing ADD/SUB
									else instrType = 1;				//incrementing ADD/SUB
									instrOperand1 = difference0[state];
									gotFunctionality = true;
								}
						}
						if(temp == difference1[state] && beforeState != 0){	
							if(!probablyIncr[state]) probablyIncr[state] = true;
							else{
								if(c > 3) warnLate = c;
								//deliberately not an else if
								if(difference0[state] != 0){
									if(negDifference1[state]) instrType = 4;  //mv/ld and decrement
									else instrType = 3;			  //mv/ld and increment
									instrOperand1 = initialVal[state];
									instrOperand2 = difference1[state];
								}
								else{
									if(negDifference1[state]) instrType = 2;  //decrementing ADD/SUB
									else instrType = 1;			  //incrementing ADD/SUB
									instrOperand1 = difference1[state];
								}
								gotFunctionality = true;
							}
						}
						else if(temp == 0){		
							if(!probablyConst[state]) probablyConst[state] = true;
							else{
								instrType = 5;					  //constant MV/LD(I)
								instrOperand1 = afterState[state];
								gotFunctionality = true;
							}
						}			
					}
					lastState[state] = afterState[state];
				}
			} 
		}
		if(lastSig == 0) ran++;
		else if(lastSig == 11) segv++;
		else if(lastSig == 4) ill++;
		else sigother++;

		if(!forceTestFaultingInstrs && lastSig != 0) break;
	}
			
	asm("fence");			

	if(!beQuiet && printHumanReadable){
		char* srcRegs;
		if(srcReg == 0) srcRegs = "zero";
		else if(instrType > 0 && srcReg != -1) srcRegs = printSrcRegs(isSrcReg, true);
		else srcRegs = "unk";
		if(instrType == 0 && stateChange){
			printf(" UNK WILL REPEAT rd: %s, rs: %s ", getStateName(destReg), (srcReg == -1) ? "unk" : getStateName(srcReg));
			//we need to run the test again and print output
			printf("Repeating instruction...\n");
			for(c = 0; c<numTests; c++){
				handlerHasAlreadyRun = 0;
				executingNow = 1;
				GET_STATE_BEFORE()
				((void(*)())execInstruction)();
				GET_STATE_AFTER()
				asm volatile ("fence");
				asm volatile ("fence.i");
				executingNow = 0;
				for(state=1; state<SIZE_STATE; state++){
					if( beforeState[state] != afterState[state] && state != 14){	
						printf("State change %d %s before: %" PRIu64 " after: %" PRIu64 "\n", c, getStateName(state), beforeState[state], afterState[state]);
						fflush(stdout);
					}
				}
			}
		}
		else if(instrType == 0) printf(" no state change ");
		else if(instrType == 1) printf(" ADD? rd: %s, rs: %s, add-op: %" PRIu64 " ", getStateName(destReg), srcRegs, instrOperand1);
		else if(instrType == 2) printf(" SUB? rd: %s, rs: %s, sub-op: %" PRIu64 " ", getStateName(destReg), srcRegs, instrOperand1);
		else if(instrType == 3)	printf(" MV/LD + ADD? rd: %s, rs: %s, ld-op: %" PRIu64 ", add-op: %" PRIu64 " ", getStateName(destReg), srcRegs, instrOperand1, instrOperand2);
		else if(instrType == 4) printf(" MV/LD + SUB? rd: %s, rs: %s, ld-op: %" PRIu64 ", sub-op: %" PRIu64 " ", getStateName(destReg), srcRegs, instrOperand1, instrOperand2);
		else printf(" MV/LD? rd: %s, rs: %s, ld-op: %" PRIu64 " ", getStateName(destReg), srcRegs, instrOperand1);

		if(ran) printf("RAN: %d ", ran);
		else if(segv) printf("EXCPT 11: %d ", segv);
		else if(ill) printf("EXCPT 4: %d ", ill);
		else if(sigother) printf("SIGOTHER: %d ", sigother);
		
		if(warnLate) printf("warn-late-test");

		if(instrType > 0 && srcReg != -1) free(srcRegs);
		//printf("\n");
	}

	if(printMachineReadable){
		char* srcRegs;
		if(srcReg == 0) srcRegs = "zero";
		else if(instrType > 0 && srcReg != -1) srcRegs = printSrcRegs(isSrcReg, false);
		else srcRegs = "unk";
		printf(" %d %d %" PRIu64 " %" PRIu64 " %d %d %d %d %d %d\n", destReg, srcReg, instrOperand1, instrOperand2, instrType, ran, segv, ill, sigother, warnLate);
		if(instrType > 0 && srcReg != -1) free(srcRegs);	
	}		
	fflush(stdout);
}
#endif


int main(int argc, char* argv[]){

  #if INFER_FUNCTIONALITY
  if(argc < 6){
	printf("Usage: ./reserved numTests forceTestFaultingInstrs printMachineReadable printHumanReadable beQuiet\n");
	return 1;
  }	
  numTests = atoi(argv[1]);
  forceTestFaultingInstrs = atoi(argv[2]);
  printMachineReadable = atoi(argv[3]);
  printHumanReadable = atoi(argv[4]);
  beQuiet = atoi(argv[5]);
  printf("Running with config: %d tests, force test faulting instrs %d, machine readable %d, human readable %d, be quiet %d\n", numTests, forceTestFaultingInstrs, printMachineReadable, printHumanReadable, beQuiet);
  #endif

  //do not move init section into separate function!

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

  #if BRUTE_FORCE
  bool testing = true;
  while(testing){
	if(instr_count != 0) bitPattern++;
	if(bitPattern == UINT32_MAX) testing = false;
	instr_count++;

  #elif RESERVED_SPECIAL
//test all possible combinations of reserved encoding 100???????????00
  for(uint32_t twelve=0; twelve<2; twelve++){
  for(uint32_t eleven=0; eleven<2; eleven++){
  for(uint32_t ten=0; ten<2; ten++){
  for(uint32_t nine=0; nine<2; nine++){
  for(uint32_t eight=0; eight<2; eight++){
  for(uint32_t seven=0; seven<2; seven++){
  for(uint32_t six=0; six<2; six++){
  for(uint32_t five=0; five<2; five++){
  for(uint32_t four=0; four<2; four++){
  for(uint32_t three=0; three<2; three++){
  for(uint32_t two=0; two<2; two++){

        instr_count++;

	bitPattern |= ((uint32_t)1 << 15);			//15th byte is always 1

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

  #else
  //test all possible functional patterns
  for(uint32_t thirtyone=0; thirtyone<2; thirtyone++){
  for(uint32_t thirty=0; thirty<2; thirty++){
  for(uint32_t twentynine=0; twentynine<2; twentynine++){
  for(uint32_t twentyeight=0; twentyeight<2; twentyeight++){
  for(uint32_t twentyseven=0; twentyseven<2; twentyseven++){
  for(uint32_t twentysix=0; twentysix<2; twentysix++){
  for(uint32_t twentyfive=0; twentyfive<2; twentyfive++){
  for(uint32_t fifteen=0; fifteen<2; fifteen++){
  for(uint32_t fourteen=0; fourteen<2; fourteen++){
  for(uint32_t thirteen=0; thirteen<2; thirteen++){
  for(uint32_t twelve=0; twelve<2; twelve++){
  for(uint32_t six=0; six<2; six++){
  for(uint32_t five=0; five<2; five++){
  for(uint32_t four=0; four<2; four++){
  for(uint32_t three=0; three<2; three++){
  for(uint32_t two=0; two<2; two++){
  for(uint32_t one=0; one<2; one++){
  for(uint32_t zero=0; zero<2; zero++){

	instr_count++;
  	
	if(zero) bitPattern |= (1UL << 0);
	else bitPattern &= ~(1UL << 0);
					        
	if(one) bitPattern |= (one << 1);
	else bitPattern &= ~(1UL << 1);

        if(two) bitPattern |= (two << 2);
	else bitPattern &= ~(1UL << 2);

        if(three) bitPattern |= (three << 3);
	else bitPattern &= ~(1UL << 3);
					        
        if(four) bitPattern |= (four << 4);
	else bitPattern &= ~(1UL << 4);

        if(five) bitPattern |= (five << 5);	
	else bitPattern &= ~(1UL << 5);

        if(six) bitPattern |= (six << 6);
	else bitPattern &= ~(1UL << 6);

        if(twelve) bitPattern |= (twelve << 12);
	else bitPattern &= ~(1UL << 12);

	if(thirteen) bitPattern |= (thirteen << 13);
	else bitPattern &= ~(1UL << 13);

	if(fourteen) bitPattern |= (fourteen << 14);
	else bitPattern &= ~(1UL << 14);

	if(fifteen) bitPattern |= (fifteen << 15);
	else bitPattern &= ~(1UL << 15);

	if(twentyfive) bitPattern |= (twentyfive << 25);
	else bitPattern &= ~(1UL << 25);

	if(twentysix) bitPattern |= (twentysix << 26);
	else bitPattern &= ~(1UL << 26);

	if(twentyseven) bitPattern |= (twentyseven << 27);
	else bitPattern &= ~(1UL << 27);

	if(twentyeight) bitPattern |= (twentyeight << 28);
	else bitPattern &= ~(1UL << 28);

	if(twentynine) bitPattern |= (twentynine << 29);
	else bitPattern &= ~(1UL << 29);

	if(thirty) bitPattern |= (thirty << 30);
	else bitPattern &= ~(1UL << 30);

	if(thirtyone) bitPattern |= (thirtyone << 31);
	else bitPattern &= ~(1UL << 31);
  #endif
			
	l = ( (bitPattern >> 24) & 0xff);			//uppermost 8 bits, mask 1111 1111 0000 0000 0000 0000 0000 0000
	k = ( (bitPattern >> 16) & 0xff);			//next 8 bits, mask      0000 0000 1111 1111 0000 0000 0000 0000 
	j = ( (bitPattern >> 8) & 0xff);			//next 8 bits, mask      0000 0000 0000 0000 1111 1111 0000 0000
	i = (bitPattern & 0xff);				//lowermost 8 bits, mask 0000 0000 0000 0000 0000 0000 1111 1111 */

	memset(execInstruction, 0, 6);

        execInstruction[0] = i;
	execInstruction[1] = j;
	execInstruction[2] = k;
	execInstruction[3] = l;
	execInstruction[4] = 0x82;
	execInstruction[5] = 0x80;

	bool documented = false;
	//if it has 32-bit pattern, disassemble as 32-bit, otherwise disassemble as two 16-bit instructions
	//no point trying both because disassembler blindly trusts the length encoding bits
	if( (bitPattern & (1 << 0)) && (bitPattern & (1 << 1)) ){
		converted_instr = 0;
	  	memcpy(&converted_instr, execInstruction, 4);
	  	memset(output_buf, 0, sizeof(output_buf));
	  	disasm_inst(output_buf, sizeof(output_buf), rv64, 0, converted_instr);
		if(strstr(output_buf, "illegal") == NULL) documented = true;
	}
	else{
		converted_instr = 0;
  		memcpy(&converted_instr, &execInstruction, 2);
		memset(output_buf, 0, sizeof(output_buf));
  		disasm_inst(output_buf, sizeof(output_buf), rv64, 0, converted_instr);

		converted_instr = 0;
  		memcpy(&converted_instr, &execInstruction[2], 2);
		memset(output_buf_spare, 0, sizeof(output_buf_spare));
  		disasm_inst(output_buf_spare, sizeof(output_buf_spare), rv64, 0, converted_instr);

		//important to skip any that have 2 valid compressed instrs or valid compressed instr in LSBs
		if(strstr(output_buf, "illegal") == NULL) documented = true;
		else if( (strstr(output_buf_spare, "illegal") == NULL) && (((bitPattern & (1UL << 16)) == 0) && ((bitPattern & (1UL << 17)) == 0)) ) documented = true;	//skip these as if it's a HINT disa doesn't recognise and then if C.ADDI4SPN it corrupts control flow
	}	

	if( (instr_count % 10000) == 0) fprintf(stderr, "heartbeat %" PRIu64 "\n", converted_instr);

	if(!documented){						//skip testing if this is a doc'd instr
		converted_instr = 0;
		fflush(stdout);						//stop child inheriting parent buffer
		asm volatile ("fence");					//being paranoid and running these before and after fork			
		asm volatile ("fence.i");

		#if INFER_FUNCTIONALITY
		for(int test=0; test<NUM_TESTS; test++){
			pid_t pid = fork();
			if(pid == 0) {	
				testInstruction();
				exit(0);
			}
			else {
				int childSig = 0;
				waitpid(pid, &childSig, 0);	
			}
		}
		#else
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
					for(state=1; state<SIZE_STATE; state++){
						if( beforeState[state] != afterState[state] && state != 14){	
							printf("\nState change of BELOW INSTR %d %s before: %" PRIu64 " after: %" PRIu64 " ", c, getStateName(state), beforeState[state], afterState[state]);								
							fflush(stdout);
						}
					}
					#endif	
					printf("\nRAN ");
					fflush(stdout);
				}
				//else if(lastSig == 4) printf("SIGILL" );
				if(lastSig != 4){
					if(lastSig == 11) printf("\nSIGSEGV ");
					else if(lastSig != 0) printf("\nOTHERSIG%d ", lastSig); //never seen
					memcpy(&converted_instr, execInstruction, 4);
					char* binStr = uint32ToBin(converted_instr);
					printf("%s", binStr);
					fflush(stdout);					
					free(binStr);
				}	
				exit(lastSig);
			}
			else{
				int childSig = 0;
				waitpid(pid, &childSig, 0);	
				//can't check signal here, childSig is ALWAYS 0 
			}
		}
		#endif		
	}
	else{
		#if PRINT_DOCUMENTED
		char* binStr = uint32ToBin(converted_instr);
		printf("DOCUMENTED %s %" PRIu64 " %s\n", binStr, converted_instr, output_buf);
		fflush(stdout);
		free(binStr);
		#endif
	}

  #if BRUTE_FORCE
  }
  #elif RESERVED_SPECIAL
  } } } } } } } } } } } 	
  #else
  } } } } } } } } } } } } } } } } } }	//sorry!
  #endif

  printf("\nTest complete!\n");
  printf("Total num instructions scanned (not necessarily tested): %d\n", instr_count);
  endTest();
  return 0;
}

