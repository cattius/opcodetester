/******************************************************************************
 * opcodeTester.c                                                             *
 * Automated Intel x86_64 CPU undocumented instruction testing/analysis       *
 * (when used in tandem with Sandsifter)                                      *
 *                                                                            *
 * This program is designed to test undocumented Intel x86_64 instructions    *
 * found by the Sandsifter tool. It takes a Sandsifter log file as input and  *
 * runs the undocumented instructions again, attempting to discover their     *
 * functionality by monitoring the CPU performance counters. Instructions are *
 * run first at ring 3; if they fail at ring 3 and the opcodeTesterKernel     *
 * driver is loaded, they will also be tested at ring 0.                      *
 *                                                                            *
 * NOTE: compatible ONLY with Intel x86_64. Currently supported               *
 * microarchitectures: Sandy/Ivy Bridge, Haswell/Broadwell, Skylake.          *
 * Tested on i7-5600U with Ubuntu 17.10; this code depends on _GNU_SOURCE     *
 * definitions. Expect undocumented instructions to execute differently in    *
 * VMs/emulators.                                                             *
 *                                                                            *
 * By Catherine Easdon, 2018                                                  *
 ******************************************************************************/

#include "../../include/opcodeTesterUser.h"

volatile int instructionFailed = 0;
bool instructionsExecutingInRing3 = false;
extern char resume;   //so we can jump IP to after faulting instruction; defined in ASM after instruction call
FILE *inputFile;
FILE *outputFile;
FILE *instrLog;
FILE *wordCloudLog;
int kernelDriver;   //for running code as ring 0
unsigned int counterConfigs[20];;
int validInstrs = 0;
int undocumentedInstrs = 0;
int archUndocumentedInstrs = 0;
int validOtherModesInstrs = 0;
int iformValid[SIZE_XED_IFORM_ENUM] = {};
int iformArch[SIZE_XED_IFORM_ENUM] = {};
int lastSignal = 0;
int signalCounts[4][7] = {};
int kernelSignalCounts[4][7] = {};
int functionAnalysis;
int microarch = XED_CHIP_HASWELL;   //default microarch
char *outputFileName;
unsigned char execInstruction[MAX_INSTR_LENGTH];
bool skipToLastLogInstr = false;
int instrsExecutedCount = 0;
bool instrCurrentlyExecuting = false;
unsigned char blacklist[10][MAX_INSTR_LENGTH] = {{0x66,0x0f,0x0d,0x97,0xed,0x00,0x00,0x00,0x00,0x00}};
#define NUM_BLACKLISTED_INSTRS (sizeof(blacklist)/sizeof(unsigned char))

void printResults(bool testValid, bool testInvalid, bool testUndocumented, bool testValidOtherModes);	//forward declare so signalHandler can use it

void exitCleanup(){
	if((functionAnalysis && !instructionsExecutingInRing3) || (!functionAnalysis && signalCounts[0][OPCODE_NOSIG] == 0 && signalCounts[1][OPCODE_NOSIG] == 0 && signalCounts[2][OPCODE_NOSIG] == 0)){
		printf("No instructions successfully executed in ring 3.\nThis is not typical behaviour for Sandsifter logs!\nOften this can be resolved by running the program again.\nIf you don't see this message next time, it has worked correctly.\n");
	}
	if(functionAnalysis) endPerformanceMonitoring();
	if(inputFile != NULL) fclose(inputFile);
	if(outputFile != NULL) fclose(outputFile);
	if(instrLog != NULL) fclose(instrLog);
	if(wordCloudLog != NULL) fclose(wordCloudLog);
}

void signalHandler(int sig, siginfo_t* siginfo, void* context){

	if(sig == SIGINT){	//Ctrl+C
		fprintf(outputFile, "Keyboard interrupt - exiting.\n");
		printf("Keyboard interrupt - exiting.\n");
		printResults(true, true, true, true);
		exitCleanup();
		exit(0);
	}

	//attempt to save input and output files if terminated with SIG_ABRT - unfortunately can't catch SIG_KILL
	//also abort if couldn't restore context last time - instructionFailed increasing means we are stuck in a loop
	else if(instrCurrentlyExecuting){
		if(instructionFailed > 3 || sig == 6){
			fprintf(outputFile, "Signal handler could not restore context; aborting.\n");
			printf("Signal handler could not restore context; aborting.\n");
			printResults(true, true, true, true);
			exitCleanup();
			exit(1);
		}
		else{
			switch(sig){
				case 4:
					if(SIG_HANDLER_DETAILED_OUTPUT && outputFile != NULL) fprintf(outputFile, "Instruction caused signal SIG_ILL with si code %s\n", getSiCodeString(OPCODE_SIGILL, siginfo->si_code));
					//printf("Instruction caused signal SIG_ILL with si code %s\n", getSiCodeString(OPCODE_SIGILL, siginfo->si_code));
					lastSignal = OPCODE_SIGILL;
					break;
				case 5:
					if(SIG_HANDLER_DETAILED_OUTPUT && outputFile != NULL) fprintf(outputFile, "Instruction caused signal SIG_TRAP with si code %s\n", getSiCodeString(OPCODE_SIGTRAP, siginfo->si_code));
					//printf("Instruction caused signal SIG_TRAP with si code %s\n", getSiCodeString(OPCODE_SIGTRAP, siginfo->si_code));
					lastSignal = OPCODE_SIGTRAP;
					break;
				case 7:
					if(SIG_HANDLER_DETAILED_OUTPUT && outputFile != NULL) fprintf(outputFile, "Instruction caused signal SIG_BUS with si code %s\n", getSiCodeString(OPCODE_SIGBUS, siginfo->si_code));
					//printf("Instruction caused signal SIG_BUS with si code %s\n", getSiCodeString(OPCODE_SIGBUS, siginfo->si_code));
					lastSignal = OPCODE_SIGBUS;
					break;
				case 8:
					if(SIG_HANDLER_DETAILED_OUTPUT && outputFile != NULL) fprintf(outputFile, "Instruction caused signal SIG_FPE with si code %s\n", getSiCodeString(OPCODE_SIGFPE, siginfo->si_code));
					//printf("Instruction caused signal SIG_FPE with si code %s\n", getSiCodeString(OPCODE_SIGFPE, siginfo->si_code));
					lastSignal = OPCODE_SIGFPE;
					break;
				case 10:
					if(SIG_HANDLER_DETAILED_OUTPUT && outputFile != NULL) fprintf(outputFile, "Instruction caused signal SIG_BUS with si code %s\n", getSiCodeString(OPCODE_SIGBUS, siginfo->si_code));
					//printf("Instruction caused signal SIG_BUS with si code %s\n", getSiCodeString(OPCODE_SIGBUS, siginfo->si_code));
					lastSignal = OPCODE_SIGBUS;
					break;
				case 11:
					if(SIG_HANDLER_DETAILED_OUTPUT && outputFile != NULL) fprintf(outputFile, "Instruction caused signal SIG_SEGV with si code %s\n", getSiCodeString(OPCODE_SIGSEGV, siginfo->si_code));
					//printf("Instruction caused signal SIG_SEGV with si code %sV\n", getSiCodeString(OPCODE_SIGSEGV, siginfo->si_code));
					lastSignal = OPCODE_SIGSEGV;
					break;
				default:
					if(SIG_HANDLER_DETAILED_OUTPUT && outputFile != NULL) fprintf(outputFile, "Instruction caused signal %d with si code %d\n", sig, siginfo->si_code);
					//printf("Instruction caused signal %d with si code %d\n", sig, siginfo->si_code);
					lastSignal = OPCODE_SIGOTHER;
					break;
			}

			instructionFailed++;

			mcontext_t* mcontext = &((ucontext_t*)context)->uc_mcontext;      // get execution context
			mcontext->gregs[IP]=(uintptr_t)&resume; 													//skip faulting instruction
			mcontext->gregs[REG_EFL]&=~0x100;       													//sign flag
		}
	}
	else{
		printf("Something went wrong, OpcodeTester got a signal at an unexpected time (not during opcode execution). Restoring default signal handlers for all signals.\n");
		signal(SIGILL, SIG_DFL); 		signal(SIGFPE, SIG_DFL);		signal(SIGSEGV, SIG_DFL);
		signal(SIGBUS, SIG_DFL);		signal(SIGTRAP, SIG_DFL);		signal(SIGABRT, SIG_DFL);
	}

}

void openLogsAndDriver(char* outputFileName, char* instrLogFileName, char* inputFileName){
	//init access to kernel driver. driver must be loaded before this program is run
	if(USE_RING_0){
		kernelDriver = open("/dev/opcodeTesterKernel_dev", O_RDWR);
		if(kernelDriver < 0){
			perror("open");
			exit(1);
		}
	}
	outputFile = fopen(outputFileName, "a+");
	if(outputFile == NULL){
		perror("fopen");
		printf("Could not open output file, aborting.\n");
		exitCleanup();
		exit(1);
	}
	instrLog = fopen(instrLogFileName, "a+");
	if(instrLog == NULL){
		perror("fopen");
		printf("Could not open instruction log, aborting.\n");
		exitCleanup();
		exit(1);
	}
	inputFile = fopen(inputFileName, "r");
	if(inputFile == NULL){
		perror("fopen");
		printf("Could not open input file, aborting.\n");
		exitCleanup();
		exit(1);
	}
	if(MAKE_WORD_CLOUDS){
		wordCloudLog = fopen("wordcloud.txt", "w");
		if(wordCloudLog == NULL){
			perror("fopen");
			printf("Could not open word cloud log, aborting.\n");
			exitCleanup();
			exit(1);
		}
	}
}

void setupPerformanceMonitoring(char **argv){
	if(strncmp(argv[5], "Sandybridge", 11) == 0){
		microarch = XED_CHIP_SANDYBRIDGE;
	}
	else if(strncmp(argv[5], "Ivybridge", 9) == 0){
		microarch = XED_CHIP_IVYBRIDGE;
	}
	else if(strncmp(argv[5], "Haswell", 7) == 0){
		microarch = XED_CHIP_HASWELL;
	}
	else if(strncmp(argv[5], "Broadwell", 9) == 0){
		microarch = XED_CHIP_BROADWELL;
	}
	else if(strncmp(argv[5], "Skylake", 7) == 0){
		microarch = XED_CHIP_SKYLAKE;
	}

	if(functionAnalysis == 1){   //only 5 ports
		counterConfigs[0] = strtoul(argv[11], NULL, 16);
		counterConfigs[1] = strtoul(argv[12], NULL, 16);
		counterConfigs[2] = strtoul(argv[13], NULL, 16);
		counterConfigs[3] = strtoul(argv[14], NULL, 16);
		counterConfigs[4] = strtoul(argv[15], NULL, 16);
		counterConfigs[5] = strtoul(argv[16], NULL, 16);
		bool success = initPerformanceMonitoring(6, outputFile);
		if(!success){
			printf("Performance monitoring initialisation failed, continuing without performance data.\n");
			functionAnalysis = 0;
		}
		//printf("Performance counter config codes provided: 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x.\n", counterConfigs[0], counterConfigs[1], counterConfigs[2], counterConfigs[3], counterConfigs[4], counterConfigs[5]);
	}
	else if(functionAnalysis == 2){ //7 ports
		counterConfigs[0] = strtoul(argv[11], NULL, 16);
		counterConfigs[1] = strtoul(argv[12], NULL, 16);
		counterConfigs[2] = strtoul(argv[13], NULL, 16);
		counterConfigs[3] = strtoul(argv[14], NULL, 16);
		counterConfigs[4] = strtoul(argv[15], NULL, 16);
		counterConfigs[5] = strtoul(argv[16], NULL, 16);
		counterConfigs[6] = strtoul(argv[17], NULL, 16);
		counterConfigs[7] = strtoul(argv[18], NULL, 16);
		bool success = initPerformanceMonitoring(8, outputFile);
		if(!success){
			printf("Performance monitoring initialisation failed, continuing without performance data.\n");
			functionAnalysis = 0;
		}
		//printf("Performance counter config codes provided: 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x.\n", counterConfigs[0], counterConfigs[1], counterConfigs[2], counterConfigs[3], counterConfigs[4], counterConfigs[5], counterConfigs[6], counterConfigs[7]);
	}
}

void getLastInstr(unsigned char* lastSkipInstr, char *instrLogFileName){
	char *logLine = NULL;
	ssize_t logRead;
	size_t logLen = 0;
	logRead = getline(&logLine, &logLen, instrLog);  //skip 1st line of output log because it is always blank
	char command[20+strlen(instrLogFileName)];
	strcpy(command, "/usr/bin/tail -n 1 ");	//need to find a better method than this! turns out getting the last line is a complete pain...
	strcat(command, instrLogFileName);
	FILE *fp = popen(command, "r");
	if(fp == NULL){
		 printf("Failed to run tail command on instrLog\n");
		 perror("popen");
		 printf("Could not open instruction log, aborting. Is the file empty? If so, run again with skipToLastLogInstr=0.\n");
		 exitCleanup();
		 exit(1);
	}
	if(getline(&logLine, &logLen, fp) == -1){
		printf("Failed to get last line of instrLog.\n");
		pclose(fp);
		exitCleanup();
		exit(1);
	};
	pclose(fp);
	for(int c=0; c<MAX_INSTR_LENGTH; c++){
		if(logLine[c] == ',') break;
		lastSkipInstr[c] = logLine[c];
	}
	unsigned char *buf = NULL;
	str2bin(lastSkipInstr, &buf);
	memcpy(lastSkipInstr, buf, MAX_INSTR_LENGTH);
	free(buf);
	for(int d=0; d<MAX_INSTR_LENGTH; d++){
		 printf("%02x", lastSkipInstr[d]);
	}
	printf("\n");
}

int parseInstructions(unsigned char instructions[NUM_INSTRS_PER_ROUND][MAX_INSTR_LENGTH], unsigned int instructionLengths[NUM_INSTRS_PER_ROUND], unsigned char lastSkipInstr[MAX_INSTR_LENGTH], bool instrMatchesLastLog){
	ssize_t inputRead;;
	size_t inputLen = 0;
	bool endOfFile = false;
	int instrsProcessedCount = 0;

	char inputLine[100];
	int v, instrLen = 0;
	unsigned char instr[MAX_INSTR_LENGTH];

	for(int i=0; i < NUM_INSTRS_PER_ROUND; i++){
		if(fgets(inputLine, sizeof(inputLine), inputFile) == NULL){  //stops at newline or EOF
			printf("Reached end of file\n");
			endOfFile = true;
			break;
		}
		if(inputLine[0]=='#'){
			printf("Skipping commented line (#)\n");
			continue;
		}
		if(sscanf(inputLine, "%s %d %d", instr, &v, &instrLen) == EOF){
			printf("Reached end of file\n");
			endOfFile = true;
			break;
		}
		if(instrLen > MAX_INSTR_LENGTH || instrLen <= 0){
			printf("Opcode %s length %d invalid, skipping\n", instr, instrLen);
			continue;
		}

		unsigned char *buf = NULL;
		int hexLen = str2bin(instr, &buf);
		if(hexLen < instrLen){
			printf("Error: instruction was shorter than expected when converted to hex, skipping.\n");
			free(buf);
			continue;
		}

		if(!instrMatchesLastLog && skipToLastLogInstr){
			bool match = true;
			for(int c=0; c<instrLen; c++){
				if(buf[c] != lastSkipInstr[c]){
					match = false;
				}
			}
			if(match){
				instrMatchesLastLog = true;
			}
			free(buf);
		}
		else{
			memcpy(instructions[i], buf, instrLen);
			instructionLengths[i] = instrLen;
			free(buf);
			instrsProcessedCount++;
		}
	}
	if(endOfFile && instrsProcessedCount==0) return -1;
	else return instrsProcessedCount;
}

//returns 0 for valid instruction for this architecture, 1 for valid instruction but undocumented on this architecture, and 2 for invalid instruction
int decodeWithXed(unsigned char instruction[MAX_INSTR_LENGTH], int instructionLength){
	xed_state_t dstate;
	xed_state_zero(&dstate);
	dstate.mmode=XED_MACHINE_MODE_LONG_64;
	dstate.stack_addr_width=XED_ADDRESS_WIDTH_64b;
	xed_error_enum_t xed_error;
	xed_decoded_inst_t xedd;
	xed_decoded_inst_zero(&xedd);	//is this 2x zeroing really necessary?
	xed_decoded_inst_zero_set_mode(&xedd, &dstate);
	xed_chip_features_t features;
	xed_get_chip_features(&features, microarch);
	xed_error = xed_decode_with_features(&xedd, XED_STATIC_CAST(const xed_uint8_t*,instruction), instructionLength, &features);

	bool instrValid = xed_decoded_inst_valid(&xedd) ? true : false;
	bool instrArchDocumented = (xed_error == XED_ERROR_NONE) ? true : false;

	//TODO: irritatingly this prints before the opcode
	bool instrValidOtherModes = false;
	if(instrValid && instrArchDocumented){
		if(MAKE_WORD_CLOUDS) fprintf(wordCloudLog, " %s ", xed_iform_enum_t2str(xed_decoded_inst_get_iform_enum(&xedd)));
		fprintf(outputFile, " 0 ");
		fprintf(outputFile, " %d ", (int)xed_decoded_inst_get_iform_enum(&xedd));
		fprintf(outputFile, " %d ", (int)xed_decoded_inst_get_attribute(&xedd, XED_ATTRIBUTE_RING0));
		validInstrs++;
		iformValid[(int)xed_decoded_inst_get_iform_enum(&xedd)]++;
		return 0;
	}
	else{
		if(instrValid){
			fprintf(outputFile, " 1 ");
			fprintf(outputFile, " %d ", (int)xed_decoded_inst_get_iform_enum(&xedd));
			fprintf(outputFile, " %d ", (int)xed_decoded_inst_get_attribute(&xedd, XED_ATTRIBUTE_RING0));
			archUndocumentedInstrs++;
			iformArch[(int)xed_decoded_inst_get_iform_enum(&xedd)]++;
		}
		else{
			//test to see if instruction is valid in other machine modes (it often is)
			for(int i=0; i<10; i++){
				xed_state_t dstate;
				xed_state_zero(&dstate);
				if(i < 1) dstate.mmode=XED_MACHINE_MODE_LONG_COMPAT_32;
				else if(i < 4) dstate.mmode=XED_MACHINE_MODE_LEGACY_32;
				else if(i < 6) dstate.mmode=XED_MACHINE_MODE_LONG_COMPAT_16;
				else if(i < 8) dstate.mmode=XED_MACHINE_MODE_LEGACY_16;
				else if(i < 10) dstate.mmode=XED_MACHINE_MODE_REAL_16;

				if(i % 2 == 0) dstate.stack_addr_width=XED_ADDRESS_WIDTH_32b;
				else dstate.stack_addr_width=XED_ADDRESS_WIDTH_16b;
				//16b and 32b stacks can be used with both 16b and 32b modes

			  xed_decoded_inst_zero_set_mode(&xedd, &dstate);
				xed_error = xed_decode_with_features(&xedd, XED_STATIC_CAST(const xed_uint8_t*,instruction), instructionLength, &features);
				instrValid = xed_decoded_inst_valid(&xedd) ? true : false;
				instrArchDocumented = (xed_error == XED_ERROR_NONE) ? true : false;
				if(instrValid && instrArchDocumented){
					if(MAKE_WORD_CLOUDS) fprintf(wordCloudLog, " %s ", xed_iform_enum_t2str(xed_decoded_inst_get_iform_enum(&xedd)) );
					validOtherModesInstrs++;
					instrValidOtherModes = true;
					break;
				}
			}
			if(!instrValidOtherModes){
				undocumentedInstrs++;
				instrValid = false;
				if(MAKE_WORD_CLOUDS) fprintf(wordCloudLog, " UNKNOWN ");
			}
		}
		if(XED_DETAILED_OUTPUT) fprintf(outputFile, "Error for your microarchitecture was: %s\n", xed_error_enum_t2str(xed_error));
		xed_error = xed_decode(&xedd, XED_STATIC_CAST(const xed_uint8_t*,instruction), instructionLength+1);
		char *errString = xed_error_enum_t2str(xed_error);
		if(XED_DETAILED_OUTPUT) fprintf(outputFile, "Error including all microarchitectures was: %s\n", errString);
		if(strncmp(errString, "BUFFER_TOO_SHORT", 16) == 0){
			//TODO
		}
		else if(strncmp(errString, "BAD_REX_PREFIX", 14) == 0){
			//TODO
		}
		if(XED_DETAILED_OUTPUT){
			fprintf(outputFile, "Instruction is: ");
			for(int i=1; i<instructionLength; i++){
				fprintf(outputFile, "%02x ", instruction[i]);
			}
			fprintf(outputFile, "\n");
		}
		if(instrValid & !instrValidOtherModes) return 1;
		else if (instrValidOtherModes) return 3;
		else return 2;
	}

}

static inline void showRegisterState(char *msg){
	long state[SIZE_STATE];
	memset(state, 0, sizeof(state));
	READ_PROCESSOR_STATE();
	fprintf(outputFile, "%s", msg);
	for(int i=0; i < SIZE_STATE; i++){
		fprintf(outputFile, "%ld\n", state[i]);
	}
}

bool isBlacklisted(unsigned char instruction[MAX_INSTR_LENGTH]){
	for(int i=0; i<NUM_BLACKLISTED_INSTRS; i++){
			if(instruction == blacklist[i]) return true;
	}
	return false;
}

bool testInstructionRing0(unsigned char* instruction, int instrLen, int xedResult){

	unsigned long sendInstrLen = (unsigned long) instrLen;
	int numValues = SIZE_OF_DATA_FOR_USER / sizeof(uint64_t);
	uint64_t receive[numValues];

	//NOTE: ignore compiler warning about this? seems to work well
	if(write(kernelDriver, sendInstrLen, sizeof(sendInstrLen)) < 0){
		printf("Failed to send the instruction len to the kernel driver.");
		perror("write");
		return false;
	}
	instrCurrentlyExecuting = true;
	if(write(kernelDriver, instruction, sendInstrLen) < 0){
		printf("Failed to send the opcode to the kernel driver.");
		perror("write");
		return false;
	}
	int err = read(kernelDriver, &receive, SIZE_OF_DATA_FOR_USER);
	if(err < 0){
		fprintf(outputFile, "Failed to read data from the kernel driver.");
		perror("read");
		return false;
	}
	instrCurrentlyExecuting = false;

	bool instrRanInRing0 = false;
	if(receive[0] == 0){
		if(receive[1] >= 0 && receive[1] < 7){
			kernelSignalCounts[xedResult][receive[1]]++;
			if(lastSignal != receive[1]) printf("Instruction produced a different signal in the kernel than in user mode!\n");
		}
		else{
			kernelSignalCounts[xedResult][OPCODE_SIGOTHER]++;
			printf("BUG: returned signal value from kernel was invalid, kernel signal stats will be inaccurate - receive[1] was %ld\n", receive[1]);
		}
	}
	else{
		kernelSignalCounts[xedResult][OPCODE_NOSIG]++;
		if(instructionFailed) printf("Instruction was successful in the kernel but not in ring 3!\n");	//this is rare enough that it isn't a problem printing to the console about it!
		instrRanInRing0 = true;
	}
	for(int i=0; i<numValues; i++){
		fprintf(outputFile, "%" PRIu64 " ", receive[i]);
	}
	return instrRanInRing0;
}

bool testInstruction(unsigned char *instr, size_t instrLen, int xedResult){

	instructionFailed = 0;

	int codeLen = instrLen + 3;
	execInstruction[0] = '\x55';        //mini function prelude, (push %rbp)
	int i = 1;
	fprintf(outputFile, "\n");
	for(i; i<instrLen+1; i++){
		execInstruction[i] = instr[i-1];
    fprintf(outputFile, "%02x", instr[i-1]);
	}
	fprintf(outputFile, " ");
	execInstruction[i] = '\x5d';        //mini function prologue, (pop %rbp, retq)
	execInstruction[i+1] = '\xc3';

	//INJECT_STATE();	//or don't...this causes a lot of issues
	instrCurrentlyExecuting = true;
	((void(*)())execInstruction)();
	__asm__ __volatile__ ("\
			.global resume   \n\
			resume:          \n\
			"
			);
	;
	instrCurrentlyExecuting = false;

	bool instrRanInRing0 = false;
	if(USE_RING_0) instrRanInRing0 = testInstructionRing0(execInstruction, codeLen, xedResult);

	if(instructionFailed) signalCounts[xedResult][lastSignal]++;
	else{
		signalCounts[xedResult][OPCODE_NOSIG]++;
	}

	if(SHOW_REGISTER_STATE) showRegisterState("Register state after instr:\n");

	if(functionAnalysis){
		profileUnknownInstruction(instructionFailed);
	}

	return instrRanInRing0;
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
	printf("\nTotal number of instructions only valid in other machine modes than 64-bit mode: %d\n", validOtherModesInstrs);

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
		printf("In ring 3:\n");
		printf("SIG_ILL : %d\n", signalCounts[i][OPCODE_SIGILL]);
		printf("SIG_TRAP : %d\n", signalCounts[i][OPCODE_SIGTRAP]);
		printf("SIG_BUS : %d\n", signalCounts[i][OPCODE_SIGBUS]);
		printf("SIG_FPE : %d\n", signalCounts[i][OPCODE_SIGFPE]);
		printf("SIG_SEGV : %d\n", signalCounts[i][OPCODE_SIGSEGV]);
		printf("Other signal: %d\n", signalCounts[i][OPCODE_SIGOTHER]);
		printf("Ran successfully: %d\n", signalCounts[i][OPCODE_NOSIG]);
		printf("\n");
		printf("In ring 0:\n");
		printf("SIG_ILL : %d\n", kernelSignalCounts[i][OPCODE_SIGILL]);
		printf("SIG_TRAP : %d\n", kernelSignalCounts[i][OPCODE_SIGTRAP]);
		printf("SIG_BUS : %d\n", kernelSignalCounts[i][OPCODE_SIGBUS]);
		printf("SIG_FPE : %d\n", kernelSignalCounts[i][OPCODE_SIGFPE]);
		printf("SIG_SEGV : %d\n", kernelSignalCounts[i][OPCODE_SIGSEGV]);
		printf("Other signal: %d\n", kernelSignalCounts[i][OPCODE_SIGOTHER]);
		printf("Ran successfully: %d\n", kernelSignalCounts[i][OPCODE_NOSIG]);
		printf("\n");
	}
}


int main(int argc, char** argv)
{

	functionAnalysis = atoi(argv[3]);

	if ( (functionAnalysis == 2 && argc != 19) || (functionAnalysis == 1 && argc != 17) || (!functionAnalysis && argc != 11) )
	{
		printf("Usage: './opcodeTester sandsifterLogFile outputFile portAnalysisOn instrLog microarch skipToLastLogInstr testValid testInvalid testUndocumented testValidOtherModes port0Counter port1Counter port2Counter port3Counter port4Counter port5Counter port6Counter port7Counter\n");
		printf("Note: counters are optional if portAnalysisOn == 0, and only port counters 0 - 5 are required if portAnalysisOn == 1.\n");
		return 1;
	}

	skipToLastLogInstr = (atoi(argv[6]) == 1) ? true : false;

	//attempt to handle errors gracefully
	struct sigaction handler;
	memset(&handler, 0, sizeof(handler));
	handler.sa_sigaction = signalHandler;
	handler.sa_flags = SA_SIGINFO;
	if (sigaction(SIGILL, &handler, NULL) < 0   || \
			sigaction(SIGINT, &handler, NULL) < 0 	|| \
			sigaction(SIGFPE, &handler, NULL) < 0   || \
			sigaction(SIGSEGV, &handler, NULL) < 0  || \
			sigaction(SIGBUS, &handler, NULL) < 0   || \
			sigaction(SIGTRAP, &handler, NULL) < 0  || \
			sigaction(SIGABRT, &handler, NULL) < 0  ) {
		perror("sigaction");
		return 1;
	}

	openLogsAndDriver(argv[2], argv[4], argv[1]);
	if(functionAnalysis) setupPerformanceMonitoring(argv);
	bool testValid = false, testInvalid = false, testUndocumented = false, testValidOtherModes = false;
	if (atoi(argv[7])){
		testValid = true;
		printf("\nTesting valid instructions (warning: this option often causes crashes).\n");
	}
	if (atoi(argv[8])){
		testInvalid = true;
		printf("Testing instructions which are documented, but officially unsupported for this architecture.\n");
	}
	if (atoi(argv[9])){
		testUndocumented = true;
		printf("Testing instructions which are undocumented for all architectures and all machine modes.\n");
	}
	if (atoi(argv[10])){
		testValidOtherModes = true;
		printf("Testing instructions which are documented for other machine modes, but not 64-bit mode.\n\n");
	}

	//get system's page size and calculate pagestart addr
	size_t pagesize = sysconf(_SC_PAGESIZE);
	uintptr_t pagestart = ((uintptr_t) &execInstruction) & -pagesize;
	if(mprotect((void*) pagestart, pagesize, PROT_READ|PROT_WRITE|PROT_EXEC)){
		perror("mprotect");
		printf("Failed to obtain executable memory, exiting.\n");
		exitCleanup();
		return 1;
	}

	//DO NOT enable null access, it always causes segfaults

	xed_tables_init();

	unsigned char instructions[NUM_INSTRS_PER_ROUND][MAX_INSTR_LENGTH];
	unsigned int instructionLengths[NUM_INSTRS_PER_ROUND];
	unsigned char lastSkipInstr[MAX_INSTR_LENGTH] = {0};
	if(skipToLastLogInstr) getLastInstr(lastSkipInstr, argv[4]);
	int xedResult = 0;
	bool instrMatchesLastLog = false;
	int instrsProcessedCount = 1;

	/* read in a round's worth of instructions and test them until input EOF. if skipToLastLogInstr, skip instructions in instrLog
	// we expect instrsProcessedCount to be 0 sometimes - e.g. at start when whole round is comment lines. -1 however means EOF */
	bool instrRanInRing0;
	while( (instrsProcessedCount = parseInstructions(instructions, instructionLengths, lastSkipInstr, instrMatchesLastLog)) >= 0 ){
		for(int i=0; i<instrsProcessedCount; i++){
				xedResult = decodeWithXed(instructions[i], instructionLengths[i]);
				if( (xedResult == 0 && testValid) || (xedResult == 1 && testInvalid) || (xedResult == 2 && testUndocumented) || (xedResult == 3 && testValidOtherModes) ){
					printf("Testing instruction %d: ", instrsExecutedCount);
					fprintf(instrLog, "\n");
					for(int k=0; k < instructionLengths[i]; k++){
						fprintf(instrLog, "%02x", instructions[i][k]);
						printf("%02x", instructions[i][k]);
					}
					printf("\n");
					if(skipToLastLogInstr){
						printf("Execute?\n");
						char input = getchar();
						if(input == 'n') continue;
					}
					instrRanInRing0 = testInstruction(instructions[i], instructionLengths[i], xedResult);
					fprintf(instrLog, ", 1"); //instr tested in ring 3 without crashing (doesn't mean it's valid)
					if(instrRanInRing0) fprintf(instrLog, ", 1");	//instr worked in ring 0 (does mean it's valid)
					instrsExecutedCount++;
				}
		}
		if(PAUSE_AFTER_EACH_ROUND) getchar();
	}

	printResults(testValid, testInvalid, testUndocumented, testValidOtherModes);

	/*printf("Produce opcode graphs? y/n\n");
	char input = getchar();
	if(input == 'y'){
		Py_SetProgramName(argv[0]);
  	Py_Initialize();
  	PyRun_SimpleFile(FILE* fp, char *filename);
  	Py_Finalize();
	} */

	exitCleanup();
	return 0;
}
