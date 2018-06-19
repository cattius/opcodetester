#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <stdbool.h>

#define EXECUTE_INSTRUCTIONS 0
//below used if EXECUTE_INSTRUCTIONS is 0
#define OTHER_ARCH_MODE64 1
#define THIS_ARCH_OTHER_MODES 1
#define OTHER_ARCH_OTHER_MODES 0

//Usage: set EXECUTE_INSTRUCTIONS to 0 and pipe to file to get a list of undocumented instructions: ./test > undocumentedOpcodes.txt
//or set EXECUTE_INSTRUCTIONS to 1 to actually execute them, piping to file is optional (summary printed at end if it doesn't crash)

#if __x86_64__
	#define IP REG_RIP
#else
 	#define IP REG_EIP
#endif

#define blacklist(iVal, jVal, kVal) ({ \
	if(i == iVal && j == jVal && k == kVal){ \
		skippedInstrs++; \
		continue; \
	} \
})

#define blacklistJBlock(iVal, jVal) ({ \
	if(i == iVal && j == jVal){ \
		skippedInstrs++; \
		continue; \
	} \
})

#define blacklistIBlock(iVal) ({ \
	if(i == iVal){ \
		skippedInstrs++; \
		continue; \
	} \
})

#define blacklistKBlock(kVal) ({ \
	if(k == kVal){ \
		skippedInstrs++; \
		continue; \
	} \
})

#define blacklistAllJBlock(jVal) ({ \
	if(j == jVal){ \
		skippedInstrs++; \
		continue; \
	} \
})

enum signalTypes {
  OPCODE_SIGOTHER,
  OPCODE_SIGILL,
  OPCODE_SIGTRAP,
  OPCODE_SIGBUS,
  OPCODE_SIGFPE,
  OPCODE_SIGSEGV,
  OPCODE_NOSIG
};

int instructionFailed = 0;
int lastSignal = 0;
int signalCounts[7] = {0};
extern char resume;

void signalHandler(int sig, siginfo_t* siginfo, void* context){

	//also abort if couldn't restore context last time - instructionFailed increasing means we are stuck in a loop
	if(instructionFailed > 3 || sig == 6 || !EXECUTE_INSTRUCTIONS){
		printf("Aborting, too many signals.\n");
		exit(1);
	}
	else if(EXECUTE_INSTRUCTIONS){
		switch(sig){
			case 4:
				lastSignal = OPCODE_SIGILL;
				break;
			case 5:
				lastSignal = OPCODE_SIGTRAP;
				break;
			case 7:
				lastSignal = OPCODE_SIGBUS;
				break;
			case 8:
				lastSignal = OPCODE_SIGFPE;
				break;
			case 10:
				lastSignal = OPCODE_SIGBUS;
				break;
			case 11:
				lastSignal = OPCODE_SIGSEGV;
				break;
			default:
				lastSignal = OPCODE_SIGOTHER;
				break;
		}

		instructionFailed++;

		mcontext_t* mcontext = &((ucontext_t*)context)->uc_mcontext;      // get execution context
		mcontext->gregs[IP]=(uintptr_t)&resume; 													//skip faulting instruction
		mcontext->gregs[REG_EFL]&=~0x100;       													//sign flag
	}

}

//NOTE: skipping checks of other machine modes here
int instructionIsValid(unsigned char instruction[], int instructionLength){
	xed_state_t dstate;
	xed_state_zero(&dstate);
	dstate.mmode=XED_MACHINE_MODE_LONG_64;  //NOTE: as always, assuming 64-bit
	dstate.stack_addr_width=XED_ADDRESS_WIDTH_64b;
	xed_error_enum_t xed_error;
	xed_decoded_inst_t xedd;
	xed_decoded_inst_zero(&xedd);	//is the 2x zeroing really necessary
	xed_decoded_inst_zero_set_mode(&xedd, &dstate);
	//xed_error = xed_decode(&xedd, XED_STATIC_CAST(const xed_uint8_t*,instruction), instructionLength);  //1 unsigned char = 1 byte
	xed_chip_features_t features;
	xed_get_chip_features(&features, XED_CHIP_BROADWELL);     
	xed_error = xed_decode_with_features(&xedd, XED_STATIC_CAST(const xed_uint8_t*,instruction), instructionLength, &features);

	bool instrValid = xed_decoded_inst_valid(&xedd) ? true : false;
	bool instrArchDocumented = (xed_error == XED_ERROR_NONE) ? true : false;

	if(instrValid && instrArchDocumented){
		return 1;
	}
	else if(OTHER_ARCH_MODE64 && instrValid){		//undocumented for arch...but may still run? TODO: TEST, ignoring for now
		return 2;
	}
	else if(THIS_ARCH_OTHER_MODES){
		//test to see if instruction is valid in other machine modes
		for(int i=0; i<10; i++){
			xed_state_t dstate;
			xed_state_zero(&dstate);
			if(i < 2) dstate.mmode=XED_MACHINE_MODE_LONG_COMPAT_32;		//TODO: this was a bug - set to 1 in opcodeTester.c, fix
			else if(i < 4) dstate.mmode=XED_MACHINE_MODE_LEGACY_32;
			else if(i < 6) dstate.mmode=XED_MACHINE_MODE_LONG_COMPAT_16;
			else if(i < 8) dstate.mmode=XED_MACHINE_MODE_LEGACY_16;
			else if(i < 10) dstate.mmode=XED_MACHINE_MODE_REAL_16;
			//16b and 32b stacks can be used with both 16b and 32b modes, so test both
			if(i % 2 == 0) dstate.stack_addr_width=XED_ADDRESS_WIDTH_32b;
			else dstate.stack_addr_width=XED_ADDRESS_WIDTH_16b;
		  	xed_decoded_inst_zero_set_mode(&xedd, &dstate);
			xed_error = xed_decode_with_features(&xedd, XED_STATIC_CAST(const xed_uint8_t*,instruction), instructionLength, &features);
			instrValid = xed_decoded_inst_valid(&xedd) ? true : false;
			instrArchDocumented = (xed_error == XED_ERROR_NONE) ? true : false;
			if(OTHER_ARCH_OTHER_MODES && instrValid){
				return 3;
			}
			else if(instrValid && instrArchDocumented){
				return 4;
			}
		}
	}
	return 0;
}

int main(){


	/* investigating prefixes results:

	runs fine with 14x prefix 66 + NOP  (+ 3 byte function blah)
	segfaults if you take it to 15x so over 15 bytes
	(not >15 bytes bc function blah is 3 sep instrs!!)

	67 prefix same
	65 prefix same
	64 same
	26 same
	3E same
	36 same
	2E same
	F2 same
	F3 same
	F0 as expected throws SIG_ILL for any num

	are they being interpreted as one byte instrs?
	no for 64, 65, 66, 67 - no DOCUMENTED opcode w that value, just prefix

	int opcodeLen = 15;
	unsigned char execInstruction[opcodeLen+3];
	execInstruction[0] = '\x55';        //mini function prelude, (push %rbp)

	//opcode goes here, remember to change opcodeLen
	for(int i=1; i<opcodeLen; i++){
		execInstruction[i] = '\x66';
		//so num of prefixes is opcodeLen - 1
	}
	execInstruction[opcodeLen] = '\x90';         //NOP

	execInstruction[opcodeLen+1] = '\x5d';        //mini function prologue, (pop %rbp, retq)
	execInstruction[opcodeLen+2] = '\xc3'; */



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

	xed_tables_init();

	execInstruction[0] = 0x55;
	execInstruction[1] = 144;
	execInstruction[2] = 93;
	execInstruction[3] = 195;
	instructionFailed = 0;
	if(EXECUTE_INSTRUCTIONS) ((void(*)())execInstruction)();
	unsigned char opcode[15] = {0x90, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
	if(!instructionFailed && instructionIsValid(opcode, 15)){
		if(EXECUTE_INSTRUCTIONS) printf("Passed startup test\n");
	}
	else{
		printf("Failed startup test, there is a problem with XED, exceptions, and/or instr formatting.\n");
		exit(1);
	}


	//TODO: Generate three opcode bytes - some versions will need operands
	int opcodeLen;
	int skippedInstrs = 0;
	for(int i = 0; i < 256; i++){		//skip 0 - invalid + stack smashing   	TODO: 0 CAN be valid, it's add
		for(int j = 0; j < 256; j++) {
			for(int k = 0; k < 256; k++) {
				instructionFailed = 0;
				execInstruction[1] = i;
				if(j == 0){
					execInstruction[2] = 0x5d;
					execInstruction[3] = 0x90;
					opcodeLen = 1;
					unsigned char opcode[15] = {i, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
					if(instructionIsValid(opcode, opcodeLen)){
						break; 	//avoid testing 01 256x ...
					}
				}
				else if(k == 0){
					execInstruction[2] = j;
					execInstruction[3] = 0x5d;
					execInstruction[4] = 0x90;
					opcodeLen = 2;
					unsigned char opcode[15] = {i, j, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
					if(instructionIsValid(opcode, opcodeLen)){
						continue;
					}
				}
				else{
					execInstruction[2] = j;
					execInstruction[3] = k;
					execInstruction[4] = 0x5d;
					execInstruction[5] = 0x90;
					opcodeLen = 3;
					unsigned char opcode[15] = {i, j, k, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
					if(instructionIsValid(opcode, opcodeLen)){
						continue;
					}
				}
				for(int l = 1; l <= opcodeLen; l++){
					printf("%02x", execInstruction[l]);
				}
				printf("\n");
				//TODO group instructions by category
				if(!EXECUTE_INSTRUCTIONS && j == 0) break;	//avoid testing 01 256x ...

				//TODO: review blacklist now I'm skipping known instrs with XED
				if(EXECUTE_INSTRUCTIONS){


					//added since XED
					//blacklistJBlock(0x03, 0x61);
					//blacklistJBlock(0x03, 0x62);

					//WOW 26 has interesting effects, as does 36...prefix on its own seems to be an issue. also 3e...also 70 or 71!!
					blacklistIBlock(0x26);
					blacklistIBlock(0x2e);
					blacklistJBlock(0x33, 0xa4);
					blacklistJBlock(0x33, 0xac);
					blacklistIBlock(0x36);
					blacklistIBlock(0x3e);
					blacklistIBlock(0xf0);
					blacklistIBlock(0xf2);
					blacklistIBlock(0xf3);
					blacklistIBlock(0x2e);
					blacklistIBlock(0x3e);
					blacklistIBlock(0x64);
					blacklistIBlock(0x65);
					blacklistIBlock(0x66);
					blacklistIBlock(0x67);
					blacklistIBlock(0x63);
					blacklistIBlock(0x69);
					blacklistIBlock(0x6a);
					blacklistIBlock(0x6b);				//WHY is this not working???
					blacklistIBlock(0x70);
					blacklistIBlock(0x71);
					blacklistJBlock(0x80, 0x26);			//also same repl effect - prefix as 2nd byte
					//blacklistJBlock(0x2e, 0x03);
					//blacklistJBlock(0x2e, 0x0b);
					//blacklistIBlock(0x25);
					//blacklistIBlock(0x24);


					blacklistIBlock(0x03);
					blacklistIBlock(0x23);
					blacklistAllJBlock(0x50);
					blacklistAllJBlock(0x51);
					blacklistAllJBlock(0x52);
					blacklistAllJBlock(0x53);
					blacklistAllJBlock(0x54);
					blacklistAllJBlock(0x55);
					blacklistAllJBlock(0x56);
					blacklistAllJBlock(0x57);
					blacklistAllJBlock(0x58);
					blacklistAllJBlock(0x59);
					blacklistAllJBlock(0x60);
					blacklistAllJBlock(0x61);
					blacklistAllJBlock(0x62);
					blacklistAllJBlock(0x63);
					blacklistAllJBlock(0x64);
					blacklistAllJBlock(0x65);
					blacklistAllJBlock(0x66);
					blacklistAllJBlock(0x67);
					blacklistAllJBlock(0x68);
					blacklistAllJBlock(0x69);
					blacklistAllJBlock(0x6a);
					blacklistAllJBlock(0x6b);
					blacklistAllJBlock(0x6c);
					blacklistAllJBlock(0x6c);
					blacklistAllJBlock(0x6d);
					blacklistAllJBlock(0x6e);
					blacklistAllJBlock(0x6f);
					//blacklistJBlock(0x0f, 0x40);
					//blacklistJBlock(0x0f, 0x41);
					//blacklistJBlock(0x0f, 0x42);
					//blacklistJBlock(0x0f, 0x43);
					blacklistIBlock(0x13);
					blacklistIBlock(0x0f);
					blacklistIBlock(0x0b);
					blacklistKBlock(0x50);
					blacklistKBlock(0x51);
					blacklistKBlock(0x52);
					blacklistKBlock(0x53);
					blacklistKBlock(0x54);
					blacklistKBlock(0x55);
					blacklistKBlock(0x56);
					blacklistKBlock(0x57);
					blacklistKBlock(0x58);
					blacklistKBlock(0x59);
					blacklistKBlock(0x60);
					blacklistKBlock(0x61);
					blacklistKBlock(0x62);
					blacklistKBlock(0x63);
					blacklistKBlock(0x64);
					blacklistKBlock(0x65);
					blacklistKBlock(0x66);
					blacklistKBlock(0x67);
					blacklistKBlock(0x68);
					blacklistKBlock(0x69);
					blacklistKBlock(0x6a);
					blacklistKBlock(0x6b);
					blacklistKBlock(0x6c);
					blacklistKBlock(0x6d);
					blacklistKBlock(0x6e);
					blacklistKBlock(0x6f);
					blacklist(0x3c, 0x0f, 0xfb);

					//added before XED
					blacklistIBlock(0x1b);
					blacklistIBlock(0x1c);
					blacklistIBlock(0x2b);
					blacklistIBlock(0x2c);
					blacklistIBlock(0x11);		//12 01 52 seems to be the trigger for incrementing 0x11 to 0x00
					blacklistIBlock(0x12);
					blacklistJBlock(0x3c, 0x01);
					blacklistJBlock(0xb1, 0x2e);
					blacklistJBlock(0x01, 0x02);
					blacklistJBlock(0x01, 0x06);



					//these need non-standard logic
					if(i == 0x2e && j == 0 && k == 0){
						skippedInstrs++;
						break;	//this *should* be break, not continue
					}
					if(k == 0x50 || k == 0x51){
						skippedInstrs++;
						continue;
					}


					((void(*)())execInstruction)();
					__asm__ __volatile__ ("\
						.global resume   \n\
						resume:          \n\
						"
						);
					;
					if(!instructionFailed){
						signalCounts[OPCODE_NOSIG]++;
					}
					else signalCounts[lastSignal]++;
					if(j == 0) break; 	//avoid testing 01 256 times ...
					printf("So far %d undocumented opcodes have executed successfully!!!!\n", signalCounts[OPCODE_NOSIG]);
					//TODO: why is the behaviour SO different if you work thru instrs sequentially vs. starting at or near instr?
					//if(j==1 && k==0) sleep(2);		//DEBUG
				}
			}
		}
	}

	if(EXECUTE_INSTRUCTIONS){
		printf("Done! Tested all undocumented opcodes apart from %d blacklisted instructions\n", skippedInstrs);
		printf("Successfully executed: %d instructions\n", signalCounts[OPCODE_NOSIG]);
		printf("Threw SIG_ILL: %d instructions\n", signalCounts[OPCODE_SIGILL]);
		printf("Threw SIG_TRAP: %d instructions\n", signalCounts[OPCODE_SIGTRAP]);
		printf("Threw SIG_BUS: %d instructions\n", signalCounts[OPCODE_SIGBUS]);
		printf("Threw SIG_FPE: %d instructions\n", signalCounts[OPCODE_SIGFPE]);
		printf("Threw SIG_SEGV: %d instructions\n", signalCounts[OPCODE_SIGSEGV]);
		printf("Threw another signal: %d instructions\n", signalCounts[OPCODE_SIGOTHER]);
	}
	else{
		//unused but necessary to keep compiler happy
		__asm__ __volatile__ ("\
			.global resume   \n\
			resume:          \n\
			"
			);
		;
	}


	return 0;
}
