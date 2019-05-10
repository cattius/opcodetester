//I started this at the very end of the project and it's not quite there yet, still debugging issues with the machine code - got it working on its own but not in a larger framework

#include <stdio.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <linux/perf_event.h>
#include <sched.h>
#include <string.h>
#include <sys/syscall.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <signal.h>
#include <setjmp.h>
#include <inttypes.h>
#include <stdbool.h>
#include <bsd/stdlib.h>

//CONFIGURATION
//======================
#define NUM_REPS 100000
#define MICROARCH XED_CHIP_BROADWELL	      //your microarchitecture's XED name here

//if you turn this on, must define INSTR_LEN and code array
//otherwise random instructions will be generated and tested
#define TEST_ONE_INSTRUCTION_ONLY 1

#ifdef TEST_ONE_INSTRUCTION_ONLY
#define INSTR_LEN 1			      //modify this to match your code length
unsigned char code[INSTR_LEN] = {0x90}; //put your code here
//======================

#else //random testing
#include <bsd/stdlib.h>
#endif

#define NUM_COUNTERS 27
int counters[NUM_COUNTERS] = {0};
long long results[NUM_COUNTERS] = {0};
#define BYTES_LENGTH 23+15
unsigned char execInstruction[BYTES_LENGTH];
sigjmp_buf buf;
struct sigaction handler;

long perf_event_open(struct perf_event_attr *hw_event, pid_t pid, int cpu, int group_fd, unsigned long flags){
    int ret;
    ret = syscall(__NR_perf_event_open, hw_event, pid, cpu, group_fd, flags);
    return ret;
}

void endPerformanceMonitoring(){
  for(int i=0; i<NUM_COUNTERS; i++){
    if(counters[i] > 0) close(counters[i]);
  }
}

void signalHandler(int sig, siginfo_t* siginfo, void* context){
  siglongjmp(buf, 1);
}

int main(int argc, char** argv){

  	if(argc < (NUM_COUNTERS+1)){
    		printf("I need performance counters passed to me as arguments! Try running me with the launcher, or see the source code for the counters required.\n");
    		return 1;
  	}

	//lock to core 0 - important as all perf monitoring assumes we are on core 0
	cpu_set_t set;
	CPU_ZERO(&set);        // clear cpu mask
	CPU_SET(0, &set);      // set cpu 0
	if( sched_setaffinity(0, sizeof(cpu_set_t), &set) < -1){
		perror("sched_setaffinity");
		return 1;
	}
	else printf("Set processor affinity to core 0\n");

	//get system's page size and calculate pagestart addr
	//no speculative exec if page isn't executable
	size_t pagesize = sysconf(_SC_PAGESIZE);
	uintptr_t pagestart = ((uintptr_t) &execInstruction) & -pagesize;
	if(mprotect((void*) pagestart, pagesize, PROT_READ|PROT_WRITE|PROT_EXEC)){
		printf("Failed to obtain executable memory, exiting.\n");
		return 1;
	}

	unsigned int counterConfigs[NUM_COUNTERS] = {0};
	struct perf_event_attr events[NUM_COUNTERS];
	char* counterNames[NUM_COUNTERS] = {"IDQ.DSB_UOPS", "IDQ.MITE_UOPS", "IDQ.MS_UOPS", "IDQ_UOPS_NOT_DELIVERED.CORE", "IDQ.EMPTY", "UOPS_ISSUED.ANY", "UOPS_RETIRED.RETIRE_SLOTS", "INT_MISC.RECOVERY_CYCLES", "CPU_CLK_UNHALTED.THREAD", "UOPS_DISPATCHED_PORT.PORT_0", "UOPS_DISPATCHED_PORT.PORT_1", "UOPS_DISPATCHED_PORT.PORT_2", "UOPS_DISPATCHED_PORT.PORT_3", "UOPS_DISPATCHED_PORT.PORT_4", "UOPS_DISPATCHED_PORT.PORT_5", "UOPS_DISPATCHED_PORT.PORT_6", "UOPS_DISPATCHED_PORT.PORT_7", "RESOURCE_STALLS.ROB", "RESOURCE_STALLS.RS", "LSD.UOPS", "RS_EVENTS.EMPTY_CYCLES", "UOPS_RETIRED.STALL_CYCLES", "L2_RQSTS.REFERENCES", "L1D.REPLACEMENT", "UOPS_ISSUED.STALL_CYCLES", "INT_MISC.RAT_STALL_CYCLES", "CYCLE_ACTIVITY.CYCLES_NO_EXECUTE"};

	//initialise counters
	for(int i=0; i<NUM_COUNTERS; i++){
		memset(&events[i], 0, sizeof(struct perf_event_attr));
		events[i].type = PERF_TYPE_RAW;
		events[i].size = sizeof(struct perf_event_attr);
		events[i].config = counterConfigs[i];
		events[i].disabled = 1;  					//start disabled until activated
		events[i].exclude_guest = 1; 					//exclude uops in VM
		events[i].exclude_kernel = 1; 					//exclude uops in kernel (important: change if no longer running in userspace!!)
		counters[i] = perf_event_open(&events[i], 0, 0, -1, 0);   	//monitor this process on CPU core 0 only, -1 means this event is the group leader
		if (counters[i] == -1) {
			 fprintf(stderr, "Error opening counter %llx\n", events[i].config);
       			 endPerformanceMonitoring();
			 return 1;
		}
	}
	printf("\n");

	//can freely insert code above this point without modifying code below, all relative addrs

	/* below is the machine code for the following specpoline:
		     asm volatile("push %0; \n\
		     call trampoline; \n\
		     speculoop: \n\
		     jmp speculoop; \n\
		     trampoline: \n\
		     pause; \n\
		     lea rsp, [rsp+8] \n\
		     ret" : : "r"(&&escape) :);
	*/

  	#if TEST_ONE_INSTRUCTION_ONLY
	int index = 0;
	execInstruction[index++] = 0x48;
	execInstruction[index++] = 0x8d;
	execInstruction[index++] = 0x05;
	execInstruction[index++] = (0x10+INSTR_LEN);
	execInstruction[index++] = 0x00;
	execInstruction[index++] = 0x00;
	execInstruction[index++] = 0x00;
	execInstruction[index++] = 0x50;
	execInstruction[index++] = 0xe8;
	execInstruction[index++] = (0x02 + INSTR_LEN);
	execInstruction[index++] = 0x00;
	execInstruction[index++] = 0x00;
	execInstruction[index++] = 0x00;
	for(index; index < (13+INSTR_LEN); index++){
		execInstruction[index] = code[index-13];
	}
	execInstruction[index++] = 0xeb;
	execInstruction[index++] = (0xfe - INSTR_LEN);
	execInstruction[index++] = 0xf3;
	execInstruction[index++] = 0x90;
	execInstruction[index++] = 0x48;
	execInstruction[index++] = 0x8d;
	execInstruction[index++] = 0x64;
	execInstruction[index++] = 0x24;
	execInstruction[index++] = 0x08;
	execInstruction[index++] = 0xc3;

	long long tempCount = 0;
	for(int j=0; j<NUM_REPS; j++){
		for(int i=0; i<NUM_COUNTERS; i++){
			tempCount = 0;
			ioctl(counters[i], PERF_EVENT_IOC_RESET, 0);
			asm volatile("cpuid");
			ioctl(counters[i], PERF_EVENT_IOC_ENABLE, 0);
			((void(*)())execInstruction)();
			escape: ioctl(counters[i], PERF_EVENT_IOC_DISABLE, 0);
			asm volatile("cpuid");
		    	if(read(counters[i], &tempCount, sizeof(long long)) < 0){
				printf("Error in counter profiling\n");
				perror("read");
			}
			results[i] += tempCount;
		}
	}
	double avgResults[NUM_COUNTERS];
	for(int i=0; i<NUM_COUNTERS; i++){
		avgResults[i] = (results[i] / NUM_REPS);
		printf("%s: %.02f\n", counterNames[i], avgResults[i]);
	}
	double badSpec = 100 * ( (double)(avgResults[0] - avgResults[1] + (4*avgResults[2])) / (double)(4*avgResults[3]) );
	printf("Percentage bad speculation: %.02f\n", badSpec);
	printf("Done\n");

  	#else

	//CTRL+C to quit
	bool documented = false;
	unsigned char xedInstr[15];
	int numSamples = 0;
	while(true){
		memset(execInstruction, 0, BYTES_LENGTH);
	    	memset(xedInstr, 0, 15);
		documented = false;
		if(numSamples % 10 == 0){
			arc4random_stir();					     //use data from urandom to mix up PRNG
		}
		int len = arc4random_uniform(16);
		if(len == 0) len = 1;

		int index = 0;
	    	execInstruction[index++] = 0x48;
		execInstruction[index++] = 0x8d;
		execInstruction[index++] = 0x05;
		execInstruction[index++] = (0x10 + len);
		execInstruction[index++] = 0x00;
		execInstruction[index++] = 0x00;
		execInstruction[index++] = 0x00;
		execInstruction[index++] = 0x50;
		execInstruction[index++] = 0xe8;
		execInstruction[index++] = (0x02 + len);
		execInstruction[index++] = 0x00;
		execInstruction[index++] = 0x00;
		execInstruction[index++] = 0x00;
		for(index; index < (13+len); index++){
			execInstruction[index] = arc4random_uniform(256);
			xedInstr[(index-13)] = execInstruction[index];
		}
		execInstruction[index++] = 0xeb;
		execInstruction[index++] = (0xfe - len);
		execInstruction[index++] = 0xf3;
		execInstruction[index++] = 0x90;
		execInstruction[index++] = 0x48;
		execInstruction[index++] = 0x8d;
		execInstruction[index++] = 0x64;
		execInstruction[index++] = 0x24;
		execInstruction[index++] = 0x08;
		execInstruction[index++] = 0xc3;

	   	for(int d = 1; d < 16; d++){
			//MUST reinitialize these on each decode attempt in order for decode to work
			xed_error_enum_t err = XED_ERROR_NONE;
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
			  break;
			}
		}

	    	asm volatile("mfence; cpuid;");  //serialise self-modifying code

		if(!documented){
			long long tempCount = 0;
	    		for(int j=0; j<NUM_REPS; j++){
	      			for(int i=0; i<NUM_COUNTERS; i++){
					tempCount = 0;
					ioctl(counters[i], PERF_EVENT_IOC_RESET, 0);
					asm volatile("cpuid");
					ioctl(counters[i], PERF_EVENT_IOC_ENABLE, 0);
					((void(*)())execInstruction)();
					escape: ioctl(counters[i], PERF_EVENT_IOC_DISABLE, 0);
					asm volatile("cpuid");
		    			if(read(counters[i], &tempCount, sizeof(long long)) < 0){
			  			printf("Error in counter profiling\n");
			  			perror("read");
					}
					results[i] += tempCount;
	      			}
	    		}
	    		double avgResults[NUM_COUNTERS];
			for(int i=0; i<NUM_COUNTERS; i++){
				avgResults[i] = (results[i] / NUM_REPS);
				printf("%s: %.02f\n", counterNames[i], avgResults[i]);
			}
	    		double badSpec = 100 * ( (double)(avgResults[0] - avgResults[1] + (4*avgResults[2])) / (double)(4*avgResults[3]) );
	    		printf("Percentage bad speculation: %.02f\n", badSpec);
	    		numSamples++;
		}
		
	}

	#endif

  	endPerformanceMonitoring();
  	return 0;
}
