#include "opcodeTester.h"

struct sigaction handler;
struct sigaction eHandler;
stack_t sig_stack;
int stat_loc;
FILE *inputFile;
int validInstrs = 0;
int undocumentedInstrs = 0;
int archUndocumentedInstrs = 0;
int validOtherModesInstrs = 0;
int iformValid[SIZE_XED_IFORM_ENUM] = {};
int iformArch[SIZE_XED_IFORM_ENUM] = {};
int signalCounts[4][7] = {};
unsigned char execInstruction[MAX_INSTR_LENGTH];
int instrsExecutedCount = 0;
unsigned long currentInputFilePosition = 0;
static char* inputFileName;

void exitCleanup(){
	if(inputFile != NULL) fclose(inputFile);
	free(sig_stack.ss_sp);
}

void exitHandler(int sig, siginfo_t* siginfo, void* context){
  printf("Num random instrs tested (NOT necessarily unique): %d\n", instrsExecutedCount);
  exitCleanup();
  exit(0);
}

//if you want to use a different log file format than Sandsifter, modify the sscanf line
int parseInstructions(unsigned char instructions[NUM_INSTRS_PER_ROUND][15], unsigned int instructionLengths[NUM_INSTRS_PER_ROUND]){

	//must open and close file each time saving position, otherwise child processes ruin everything inheriting the file
	#if !TEST_OPCODE_MAP_BLANKS
	inputFile = fopen(inputFileName, "r");
	if(inputFile == NULL){
		perror("fopen");
		printf("Could not open input file, aborting.\n");
		exitCleanup();
		exit(1);
	}
	#else
	inputFile = fopen("directed-search-opcode-map-blanks.txt", "r");
	if(inputFile == NULL){
		perror("fopen");
		printf("Could not open opcode map blanks file, aborting.\n");
		exitCleanup();
		exit(1);
	}
	#endif
	fseek(inputFile,currentInputFilePosition,SEEK_SET);

	ssize_t inputRead;
	size_t inputLen = 0;
	bool endOfFile = false;
	int instrsProcessedCount = 0;
	char inputLine[100];
	int v, instrLen = 0;
	unsigned char instr[15];

	for(int i=0; i < NUM_INSTRS_PER_ROUND; i++){
		if(fgets(inputLine, sizeof(inputLine), inputFile) == NULL){  //stops at newline or EOF
			printf("Reached end of file\n");
			endOfFile = true;
			break;
		}
		if(inputLine[0]=='#'){
			printf("Skipping commented line (#)\n");
			i -= 1;
			continue;
		}
		#if TEST_OPCODE_MAP_BLANKS
		if(sscanf(inputLine, "%s %d", instr, &instrLen) == EOF){
		#else
		if(sscanf(inputLine, "%s %d %d", instr, &v, &instrLen) == EOF){
		#endif
			printf("Reached end of file\n");
			endOfFile = true;
			break;
		}
		if(instrLen > 15 || instrLen <= 0){
			printf("Opcode %s length %d invalid, skipping\n", instr, instrLen);
			i -= 1;
			continue;
		}
		unsigned char *buf = NULL;
		int hexLen = str2bin(instr, &buf);
		if(hexLen < instrLen){
			printf("Error: instruction was shorter than expected when converted to hex, skipping.\n");
			free(buf);
			i -= 1;
			continue;
		}
		memcpy(instructions[i], buf, instrLen);
		instructionLengths[i] = instrLen;	
		free(buf);
		instrsProcessedCount++;
	}

	fflush(inputFile);
	currentInputFilePosition = ftell(inputFile);
	fclose(inputFile);

	if(endOfFile && instrsProcessedCount==0) return -1;
	else return instrsProcessedCount;
} 


//returns 0 for valid instruction for this architecture, 1 for valid instruction but undocumented on this architecture, and 2 for invalid instruction
int decodeWithXed(unsigned char instruction[15], int instructionLength){
	xed_state_t dstate;
	xed_state_zero(&dstate);
	#if !B32
	dstate.mmode=XED_MACHINE_MODE_LONG_64;
	dstate.stack_addr_width=XED_ADDRESS_WIDTH_64b;
	#else
	dstate.mmode=XED_MACHINE_MODE_LEGACY_32;	
	dstate.stack_addr_width=XED_ADDRESS_WIDTH_32b;
	#endif
	xed_error_enum_t xed_error;
	xed_decoded_inst_t xedd;
	xed_decoded_inst_zero(&xedd);
	xed_decoded_inst_zero_set_mode(&xedd, &dstate);
	xed_chip_features_t features;
	xed_get_chip_features(&features, MICROARCH);
	xed_error = xed_decode_with_features(&xedd, XED_STATIC_CAST(const xed_uint8_t*,instruction), instructionLength, &features);

	bool instrValid = xed_decoded_inst_valid(&xedd) ? true : false;
	bool instrArchDocumented = (xed_error == XED_ERROR_NONE) ? true : false;

	bool instrValidOtherModes = false;
	if(instrValid && instrArchDocumented){
		validInstrs++;
		iformValid[(int)xed_decoded_inst_get_iform_enum(&xedd)]++;
		return 0;
	}
	else{
		if(instrValid){
			archUndocumentedInstrs++;
			iformArch[(int)xed_decoded_inst_get_iform_enum(&xedd)]++;
		}
		else{
			//test to see if instruction is valid in other machine modes
			for(int i=0; i<5; i++){
				xed_state_t dstate;
				xed_state_zero(&dstate);
				if(i == 0 && !B32) dstate.mmode=XED_MACHINE_MODE_LONG_COMPAT_32;
				else if(i == 1 && !B32) dstate.mmode=XED_MACHINE_MODE_LEGACY_32;
				else if(i == 2 && !B32) dstate.mmode=XED_MACHINE_MODE_LONG_COMPAT_16;
				else if(i == 3) dstate.mmode=XED_MACHINE_MODE_LEGACY_16;
				else if(i == 4) dstate.mmode=XED_MACHINE_MODE_REAL_16;
				if(i < 2) dstate.stack_addr_width=XED_ADDRESS_WIDTH_32b;
				else dstate.stack_addr_width=XED_ADDRESS_WIDTH_16b;
			  	xed_decoded_inst_zero_set_mode(&xedd, &dstate);
				xed_error = xed_decode_with_features(&xedd, XED_STATIC_CAST(const xed_uint8_t*,instruction), instructionLength, &features);
				instrValid = xed_decoded_inst_valid(&xedd) ? true : false;
				instrArchDocumented = (xed_error == XED_ERROR_NONE) ? true : false;
				if(instrValid && instrArchDocumented){
					validOtherModesInstrs++;
					instrValidOtherModes = true;
					break;
				}
			}
			if(!instrValidOtherModes){
				undocumentedInstrs++;
				instrValid = false;
			}
		}
		xed_error = xed_decode(&xedd, XED_STATIC_CAST(const xed_uint8_t*,instruction), instructionLength+1);
		if(instrValid & !instrValidOtherModes) return 1;
		else if (instrValidOtherModes) return 3;
		else{
		  printf("UNDOC: ");
		  for(int k=0; k < instructionLength; k++){
		    printf("%02x", instruction[k]);
		  } 
		  printf("\n");
		  return 2;
		}
	}
}

bool testInstruction(unsigned char *instr, size_t instrLen, int xedResult){
	for(int i=0; i<instrLen; i++){
		printf("%02x", instr[i]);
	}
	printf(" ");
	
	int index = 0;
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
	for(int i=0; i<instrLen; i++){
		execInstruction[index++] = instr[i];
	}
	#if !USE_TRAP_FLAG
	execInstruction[index++] = 0xc3; //ret
	#endif

	fflush(stdout);		//stop child inheriting parent buffer
	asm volatile("mfence; cpuid");

	//#if RANDOM_FUZZING
	pid_t pid = fork();  //cannot fork if reading from file, too buggy - will fix
	if(pid == 0){
	//#endif
		if(!sigsetjmp (buf, 1)){
			lastSig = 0;
			handlerHasAlreadyRun = 0;
			executingNow = 1;
			#if PRINT_STATE_CHANGE
			GET_STATE_BEFORE()
			#endif
			((void(*)())execInstruction)();
		}
		#if PRINT_STATE_CHANGE
		GET_STATE_AFTER()
		#endif

		#if USE_TRAP_FLAG
		if(lastSig != 5) printf("EXCPT %d\n", lastSig);
		#else
		if(lastSig != 0) printf("EXCPT %d\n", lastSig);
		#endif
		else printf("RAN\n");
		if(PRINT_STATE_CHANGE && lastSig == 0){
			bool stateChanged = false;
			for(int s=0; s<SIZE_STATE; s++){
				if(beforeState[s] != afterState[s] && s != 0 && s != 3 && s != 4 && s != 5 && s != 21 && s != 22){
					if(!stateChanged){
						printf("State change: ");
						stateChanged = true;
					}
					printf("%s: %" PRINTER " %" PRINTER ", ", getStateName(s), beforeState[s], afterState[s]);
				}
			}
			if(stateChanged) printf("\n");
		}
		fflush(stdout);
	//#if RANDOM_FUZZING
		exit(lastSig);
	}
	else{
		int status = 0;
        	waitpid(pid, &status, 0);
		if(status == 0) signalCounts[xedResult][OPCODE_NOSIG]++;
		else signalCounts[xedResult][status]++;
	}
	//#endif
	instrsExecutedCount++;
}

void printResults(bool testValid, bool testInvalid, bool testUndocumented, bool testValidOtherModes){
	printf("\nTotal number of valid instructions in the logfile - likely unknown to Capstone: %d\n", validInstrs);
	printf("These instructions were of the following iforms:\n");
	for(int i=0; i<SIZE_XED_IFORM_ENUM; i++){
		if(iformValid[i] > 0) printf("%s, ", xed_iform_enum_t2str(i));
	}

	printf("\n\nTotal number of valid instructions supposedly unsupported for your microarchitecture: %d\n", archUndocumentedInstrs);
	printf("These instructions were of the following iforms:\n");
	for(int i=0; i<SIZE_XED_IFORM_ENUM; i++){
		if(iformArch[i] > 0) printf("%s, ", xed_iform_enum_t2str(i));
	}

	printf("\n\nTotal number of instructions unknown to XED: %d\n", undocumentedInstrs);
	if(!B32) printf("\nTotal number of instructions only valid in other machine modes than long 64-bit mode: %d\n", validOtherModesInstrs);
	else printf("\nTotal number of instructions only valid in other machine modes than 32-bit mode (32-bit stack addressing): %d\n", validOtherModesInstrs);

	printf("\nInstructions were tested with the following results:\n");
	for(int i=0; i<4; i++){
		if(i==0){
			if(testValid) printf("\nSupported, documented instructions:\n");
			else{
				printf("Supported, documented instructions not tested (testValid set to 0).\n");
				continue;
			}
		}
		else if(i==1){
			if(testInvalid) printf("\nDocumented instructions which should be unsupported by your processor\n");
			else{
				printf("Documented instructions which should be unsupported by your processor not tested (testInvalid set to 0).\n");
				continue;
			}
		}
		else if(i==2){
			if(testUndocumented) printf("\nUndocumented instructions:\n");
			else{
				printf("Undocumented instructions not tested (testUndocumented set to 0).\n");
				continue;
			}
		}
		else if(i==3){
			if(testValidOtherModes) printf("\nInstructions undocumented in 64-bit mode, but documented in other modes:\n");
			else{
				printf("Instructions valid in other modes not tested (testValidOtherModes set to 0).\n");
				continue;
			}
		}
		printf("SIG_ILL : %d\n", signalCounts[i][OPCODE_SIGILL]);
		#if USE_SIG_TRAP
		printf("Ran succesfully (trap flag) : %d\n", signalCounts[i][OPCODE_SIGTRAP]);
		#else
		printf("SIG_TRAP : %d\n", signalCounts[i][OPCODE_SIGTRAP]);
		#endif
		printf("SIG_BUS : %d\n", signalCounts[i][OPCODE_SIGBUS]);
		printf("SIG_FPE : %d\n", signalCounts[i][OPCODE_SIGFPE]);
		printf("SIG_SEGV : %d\n", signalCounts[i][OPCODE_SIGSEGV]);
		printf("Other signal: %d\n", signalCounts[i][OPCODE_SIGOTHER]);
		printf("Ran successfully: %d\n", signalCounts[i][OPCODE_NOSIG]);
	}
}

int main(int argc, char** argv)
{
	#if RANDOM_FUZZING || TEST_OPCODE_MAP_BLANKS
	if(argc != 5){
		printf("Usage: './opcodeTester testValid testInvalid testUndocumented testValidOtherModes\n");
		return 1;
	}
	#else
	if(argc != 6){
		printf("Usage: './opcodeTester testValid testInvalid testUndocumented testValidOtherModes inputLog\n");
		return 1;
	}
	#endif

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
		exitCleanup();
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
		exitCleanup();
	    	return 1;
	}

	//this is essential, otherwise the child processes live on as zombies and we can't fork any more (even if we kill them) bc we hit the process limit
	signal(SIGCHLD, SIG_IGN);

	//get system's page size and calculate pagestart addr
	size_t pagesize = sysconf(_SC_PAGESIZE);
	uintptr_t pagestart = ((uintptr_t) &execInstruction) & -pagesize;
	if(mprotect((void*) pagestart, pagesize, PROT_READ|PROT_WRITE|PROT_EXEC)){
		printf("Failed to obtain executable memory, exiting.\n");
		exitCleanup();
		return 1;
	}

	xed_tables_init();

	bool testValid = false, testInvalid = false, testUndocumented = false, testValidOtherModes = false;

	if (atoi(argv[1])){
		testValid = true;
		printf("\nTesting valid instructions (warning: this option often causes crashes).\n");
	}
	if (atoi(argv[2])){
		testInvalid = true;
		printf("Testing instructions which are documented, but officially unsupported for this architecture.\n");
	}
	if (atoi(argv[3])){
		testUndocumented = true;
		printf("Testing instructions which are undocumented for all architectures and all machine modes.\n");
	}
	if (atoi(argv[4])){
		testValidOtherModes = true;
		printf("Testing instructions which are documented for other machine modes, but not 64-bit mode.\n\n");
	}

	#if RANDOM_FUZZING

	memset(&eHandler, 0, sizeof(eHandler));
	eHandler.sa_flags = SA_SIGINFO;
	eHandler.sa_sigaction = exitHandler;
	if (sigaction(SIGINT, &eHandler, NULL) < 0) {
		printf("Couldn't set exit handler, exiting.\n");
		free(sig_stack.ss_sp);
	    	return 1;
	}

	while(true){
		if(instrsExecutedCount % 10 == 0){
		   arc4random_stir();					     //use data from urandom to mix up PRNG
		}
		int len = arc4random_uniform(16);
		if(len == 0) len = 1;
		int c=0;
		unsigned char code[15] = {0};
		for(int c=0; c<len; c++){
			code[c] = arc4random_uniform(256);
		}
		int xedResult = decodeWithXed(code, len);
		if( (xedResult == 0 && testValid) || (xedResult == 1 && testInvalid) || (xedResult == 2 && testUndocumented) || (xedResult == 3 && testValidOtherModes) ){
			testInstruction(code, len, xedResult);
			instrsExecutedCount++;	
		}
	}

	#else

	#if !TEST_OPCODE_MAP_BLANKS
	inputFileName = argv[5];
	#endif

	unsigned char instructions[NUM_INSTRS_PER_ROUND][15];
	unsigned int instructionLengths[NUM_INSTRS_PER_ROUND];
	int xedResult = 0;
	int instrsProcessedCount = 1;

	/* read in a round's worth of instructions and test them until input EOF. if skipToLastLogInstr, skip instructions in instrLog
	   we expect instrsProcessedCount to be 0 sometimes - e.g. at start when whole round is comment lines. -1 however means EOF    */
	while( (instrsProcessedCount = parseInstructions(instructions, instructionLengths)) >= 0 ){
		for(int i=0; i<instrsProcessedCount; i++){
				xedResult = decodeWithXed(instructions[i], instructionLengths[i]);
				if( (xedResult == 0 && testValid) || (xedResult == 1 && testInvalid) || (xedResult == 2 && testUndocumented) || (xedResult == 3 && testValidOtherModes) ){
					testInstruction(instructions[i], instructionLengths[i], xedResult);
					instrsExecutedCount++;
				}
		}
	}
	printResults(testValid, testInvalid, testUndocumented, testValidOtherModes);
	#endif

	exitCleanup();
	return 0;
}
