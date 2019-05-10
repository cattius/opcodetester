#define _GNU_SOURCE 
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

#define NUM_COUNTERS 4+8+6+6+6+1
#define NUM_REPS 100000
int counters[NUM_COUNTERS] = {0};
long long results[NUM_COUNTERS] = {0};
unsigned char execInstruction[15];
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

int main(){
	
	//compile with gcc -o experiment experimentSpecpoline.c

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
	size_t pagesize = sysconf(_SC_PAGESIZE);
	uintptr_t pagestart = ((uintptr_t) &execInstruction) & -pagesize;
	if(mprotect((void*) pagestart, pagesize, PROT_READ|PROT_WRITE|PROT_EXEC)){
		printf("Failed to obtain executable memory, exiting.\n");
		return 1;
	}

	//badSpec = UOPS_ISSUED.ANY – UOPS_RETIRED.RETIRE_SLOTS + 4 *INT_MISC.RECOVERY_CYCLES
	//percentBadSpec = (badSpec / 4*CPU_CLK_UNHALTED.THREAD) * 100
	//retired = UOPS_RETIRED.RETIRE_SLOTS 

	struct perf_event_attr events[NUM_COUNTERS];	
	unsigned int counterConfigs[NUM_COUNTERS] = {0x10e, 0x2c2, 0x100030d, 0x3c, 0x1a1, 0x2a1, 0x4a1, 0x10a1, 0x20a1, 0x40a1, 0x40a1, 0x80a1, 0x879, 0x479, 0x3079, 0x19c, 0x10a2, 0x4a2, 0xff88, 0x1a8, 0x40c1, 0x15e, 0x4c3, 0x18001c2, 0xff24, 0x151, 0xc0, 0x180010e, 0x80d, 0x40004a3, 0x279};
	char* counterNames[NUM_COUNTERS] = {"UOPS_ISSUED.ANY", "UOPS_RETIRED.RETIRE_SLOTS", "INT_MISC.RECOVERY_CYCLES", "CPU_CLK_UNHALTED.THREAD", "UOPS_DISPATCHED_PORT.PORT_0", "UOPS_DISPATCHED_PORT.PORT_1", "UOPS_DISPATCHED_PORT.PORT_2", "UOPS_DISPATCHED_PORT.PORT_3", "UOPS_DISPATCHED_PORT.PORT_4", "UOPS_DISPATCHED_PORT.PORT_5", "UOPS_DISPATCHED_PORT.PORT_6", "UOPS_DISPATCHED_PORT.PORT_7", "IDQ.DSB_UOPS", "IDQ.MITE_UOPS", "IDQ.MS_UOPS", "IDQ_UOPS_NOT_DELIVERED.CORE", "RESOURCE_STALLS.ROB", "RESOURCE_STALLS.RS", "BR_INST_EXEC.ALL_BRANCHES", "LSD.UOPS", "OTHER_ASSISTS.ANY_WB_ASSIST", "RS_EVENTS.EMPTY_CYCLES", "MACHINE_CLEARS.SMC", "UOPS_RETIRED.STALL_CYCLES", "L2_RQSTS.REFERENCES", "L1D.REPLACEMENT", "INST_RETIRED.ANY_P", "UOPS_ISSUED.STALL_CYCLES", "INT_MISC.RAT_STALL_CYCLES", "CYCLE_ACTIVITY.CYCLES_NO_EXECUTE", "IDQ.EMPTY"};

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



	//MODIFY me and don't touch code below
	//can use different lengths, just change instrLen
	#define instrLen 2
	unsigned char code[instrLen] = {0xc3};


	int index = 0;
	//can freely insert code above this point without modifying code below, all relative addrs
	execInstruction[index++] = 0x48;
	execInstruction[index++] = 0x8d;
	execInstruction[index++] = 0x05;
	execInstruction[index++] = (0x0f+instrLen);	//if addr changes get val from disassembly with asm volatile("push %0" : : "r"(&&escape) :);
	execInstruction[index++] = 0x00;
	execInstruction[index++] = 0x00;
	execInstruction[index++] = 0x00;
	execInstruction[index++] = 0x50;
	execInstruction[index++] = 0xe8;
	execInstruction[index++] = (0x02 + instrLen);
	execInstruction[index++] = 0x00;
	execInstruction[index++] = 0x00;
	execInstruction[index++] = 0x00;
	for(index; index < (13+instrLen); index++){
		execInstruction[index] = code[index-13]; //TODO instr bytes
	}
	execInstruction[index++] = 0xeb;
	execInstruction[index++] = (0xfe - instrLen);
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
			/*asm volatile("push %0; \n\
					     call trampoline; \n\
					     speculoop: \n\
					     lfence; pause; \n\
					     jmp speculoop; \n\
					     trampoline: \n\
					     pause; \n\
					     lea rsp, [rsp+8] \n\
					     ret" : : "r"(&&escape) :); */
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
	return 0;
}

