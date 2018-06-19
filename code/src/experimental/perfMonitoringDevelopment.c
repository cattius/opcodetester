/******************************************************************************
 * perfMonitoringUser.c                                                       *
 * Automated Intel x86_64 CPU undocumented instruction testing/analysis       *
 * (when used in tandem with Sandsifter)                                      *
 *                                                                            *
 * This code monitors performance counters to guess the functionality of      *
 * undocumented instructions. It is part of the OpcodeTester tool (see        *
 * opcodeTester.c).                                                           *
 *                                                                            *
 * NOTE: compatible ONLY with Intel x86_64. Currently supported               *
 * microarchitectures: Sandy/Ivy Bridge, Haswell/Broadwell, Skylake.          *
 * Tested on i7-5600U with Ubuntu 17.10; this code depends on _GNU_SOURCE     *
 * definitions. Expect undocumented instructions to execute differently in    *
 * VMs/emulators.                                                             *
 *                                                                            *
 * By Catherine Easdon, 2018                                                  *
 ******************************************************************************/

#include "../../include/perfMonitoringUser.h"

long double noise[MAX_NUM_COUNTERS];
int counters[MAX_NUM_COUNTERS] = {0};
unsigned cyclesStartLow, cyclesStartHigh, cyclesEndLow, cyclesEndHigh; //necessary for GET_START_TIME, GET_END_TIME
//extern unsigned int counterConfigs[MAX_NUM_COUNTERS];
unsigned int counterConfigs[MAX_NUM_COUNTERS]; //DEBUG
uint64_t counterOutputs[MAX_NUM_COUNTERS];
//extern int microarch;   //default microarch
int microarch; //DEBUG
int numPorts;
int numCounters = 0;
//extern unsigned char execInstruction[MAX_INSTR_LENGTH];		//defined in opcodeTester.c;
unsigned char execInstruction[MAX_INSTR_LENGTH];	//DEBUG
FILE* outputFile;
extern char perfResume; //defined in ASM
int oldScheduler;
bool guessedFunctionality;
int threshold = 5;  //heuristic for when a number of uops on a port (after removing noise) is significant

long perf_event_open(struct perf_event_attr *hw_event, pid_t pid, int cpu, int group_fd, unsigned long flags){
    int ret;
    ret = syscall(__NR_perf_event_open, hw_event, pid, cpu, group_fd, flags);
    return ret;
}

/* HELPER FUNCTIONS
===============================================================================*/

//not in the header because it should not be used in opcodeTester
//not actually sure if this is worth it - doesn't seem to make much of a difference to results
void startRealTimeScheduling(){
  pid_t pid = getpid();
  oldScheduler = sched_getscheduler(pid);
  struct sched_param newParams;
  newParams.sched_priority = 98;
  int success = sched_setscheduler(pid, SCHED_FIFO, &newParams);
  if(success == -1){
    printf("Failed to get real time scheduling\n");
    perror("sched_setscheduler");
  }
}

//not in the header because it should not be used in opcodeTester
void endRealTimeScheduling(){
  pid_t pid = getpid();
  struct sched_param oldParams;
  oldParams.sched_priority = 0; //all non real-time scheduling policies use 0
  int success = sched_setscheduler(pid, oldScheduler, &oldParams);
  if(success == -1){
    printf("Failed to restore old scheduling\n");
    perror("sched_setscheduler");
  }
}

bool profileKnownInstruction(int argCount, void (*function)()){
  long double portValues[MAX_NUM_COUNTERS] = {0};
  for(int i=0; i<numCounters; i++){
    long long totalCount = 0;
    long long tempCount = 0;

    if(argCount == 0){
      for(int j=0; j<NUM_COUNTER_REPS; j++){
        tempCount = 0;
        ioctl(counters[i], PERF_EVENT_IOC_RESET, 0);
        GET_START_TIME();  //serialise
        ioctl(counters[i], PERF_EVENT_IOC_ENABLE, 0);
        function();
        ioctl(counters[i], PERF_EVENT_IOC_DISABLE, 0); \
        GET_END_TIME();   //serialise - I would like to have this before we disable ioctl but it would affect counter values too much
        if(read(counters[i], &tempCount, sizeof(long long)) < 0){
          printf("error in counter profiling\n");
          perror("read");
          endPerformanceMonitoring();
          return false;
        }
        totalCount += tempCount;
      }
    }
    else if(argCount == 1){
      int var;
      for(int j=0; j<NUM_COUNTER_REPS; j++){
        tempCount = 0;
        var = 1;
        ioctl(counters[i], PERF_EVENT_IOC_RESET, 0);
        GET_START_TIME();  //serialise
        ioctl(counters[i], PERF_EVENT_IOC_ENABLE, 0);
        function(var);
        ioctl(counters[i], PERF_EVENT_IOC_DISABLE, 0); \
        GET_END_TIME();   //serialise - I would like to have this before we disable ioctl but it would affect counter values too much
        if(read(counters[i], &tempCount, sizeof(long long)) < 0){
          printf("error in counter profiling\n");
          perror("read");
          endPerformanceMonitoring();
          return false;
        }
        totalCount += tempCount;
      }
    }
    else if(argCount == 2){
      int var, var1;
      for(int j=0; j<NUM_COUNTER_REPS; j++){
        tempCount = 0;
        var = 1; var1 = 0;
        ioctl(counters[i], PERF_EVENT_IOC_RESET, 0);
        GET_START_TIME();  //serialise
        ioctl(counters[i], PERF_EVENT_IOC_ENABLE, 0);
        function(var, var1);
        ioctl(counters[i], PERF_EVENT_IOC_DISABLE, 0); \
        GET_END_TIME();   //serialise - I would like to have this before we disable ioctl but it would affect counter values too much
        if(read(counters[i], &tempCount, sizeof(long long)) < 0){
          printf("error in counter profiling\n");
          perror("read");
          endPerformanceMonitoring();
          return false;
        }
        totalCount += tempCount;
      }

    }
    else if(argCount == 3){
      int var, var1, var2;
      for(int j=0; j<NUM_COUNTER_REPS; j++){
        tempCount = 0;
        var = 1; var1 = 2; var2 = 0;
        ioctl(counters[i], PERF_EVENT_IOC_RESET, 0);
        GET_START_TIME();  //serialise
        ioctl(counters[i], PERF_EVENT_IOC_ENABLE, 0);
        function(var, var1, var2);
        ioctl(counters[i], PERF_EVENT_IOC_DISABLE, 0); \
        GET_END_TIME();   //serialise - I would like to have this before we disable ioctl but it would affect counter values too much
        if(read(counters[i], &tempCount, sizeof(long long)) < 0){
          printf("error in counter profiling\n");
          perror("read");
          endPerformanceMonitoring();
          return false;
        }
        totalCount += tempCount;
      }
    }
    else{
      printf("Unsupported arg count for profileKnownInstruction!\n");
      return false;
    }
    long double temp = ceill(totalCount/(long double)NUM_COUNTER_REPS)-noise[i];
    if (temp < 0) temp = 0;
    printf("Port %d uops: %Lf\n", i, temp);    //DEBUG
    portValues[i] = temp;
  }

  //adjust threshold if no port count reached it for this instruction
  //helps if noise was overestimated. this assumes we are not profiling NOPs!
  int maxVal = 0;
  bool goodThreshold = false;
  for(int i=0; i<numPorts; i++){
    if(portValues[i] > threshold){
      goodThreshold = true;
      break;
    }
    else if(portValues[i] > maxVal) maxVal = portValues[i];
  }
  if(maxVal == 0 && !goodThreshold) printf("Error in performance monitoring attempting to adjust threshold: known instruction recorded no uops.\n");
  else threshold = maxVal;

  printf("\n");
  return true;
}

/*MAIN FUNCTIONS
==============================================================================/*/

bool initPerformanceMonitoring(int numberOfPorts, FILE* thisOutputFile){

  printf("Please ensure the program is running locked to core 0; all performance monitoring assumes this.\n");

  numCounters += numberOfPorts;
  numPorts = numberOfPorts;
  outputFile = thisOutputFile;
	struct perf_event_attr events[MAX_NUM_COUNTERS];

	//initialise counters
	for(int i=0; i<numCounters; i++){
		memset(&events[i], 0, sizeof(struct perf_event_attr));
		events[i].type = PERF_TYPE_RAW;
		events[i].size = sizeof(struct perf_event_attr);
		events[i].config = counterConfigs[i];
		events[i].disabled = 1;  //start disabled until activated
		events[i].exclude_guest = 1; //exclude uops in VM
		events[i].exclude_kernel = 1; //exclude uops in kernel

		counters[i] = perf_event_open(&events[i], 0, 0, -1, 0);   //monitor this process on CPU core 0 only, -1 means this event is the group leader
		if (counters[i] == -1) {
			 fprintf(stderr, "Error opening counter %llx\n", events[i].config);
       endPerformanceMonitoring();
       return false;
		}
	}

  startRealTimeScheduling();

  //profile background noise and function prologue/epilogue overhead with NOPs. assuming system load doesn't change during program run
  //NOTE: it is **important** that this is not a function like the other profiling below, otherwise some compiler sorcery makes us overestimate noise

  execInstruction[0] = 0x55; //push rbp
  execInstruction[1] = 0x90; //nop
  execInstruction[2] = 0x5d; //pop rbp
  execInstruction[3] = 0xc3; //retq
  for(int i=0; i<numCounters; i++){
    long long totalCount = 0;
    long long tempCount = 0;
    int result = 0;
    for(int j=0; j<NUM_COUNTER_REPS; j++){
      tempCount = 0;
      result = 0;
      ioctl(counters[i], PERF_EVENT_IOC_RESET, 0);
      GET_START_TIME();  //serialise
      ioctl(counters[i], PERF_EVENT_IOC_ENABLE, 0);
      ((void(*)())execInstruction)();
      ioctl(counters[i], PERF_EVENT_IOC_DISABLE, 0);
      GET_END_TIME();   //serialise - I would like to have this before we disable ioctl but it would affect counter values too much
      if(read(counters[i], &tempCount, sizeof(long long)) < 0){
        printf("error in counter profiling\n");
        perror("read");
        endPerformanceMonitoring();
        return false;
      }
      totalCount += tempCount;
    }
    noise[i] = ceill(totalCount/(long double)NUM_COUNTER_REPS);
    printf("Port %d uops: %Lf\n", i, noise[i]);    //DEBUG
  }
  printf("\n");

	//profile execution ports with known ASM instructions
  if(!profileKnownInstruction(0, ASM_LZCNT)) return false;
  if(!profileKnownInstruction(1, ASM_PREFETCHW)) return false;
  if(!profileKnownInstruction(3, ASM_ADD)) return false;

  endRealTimeScheduling();
  return true; //init success

}

bool profileUnknownInstruction(bool instructionFailed){
  for(int i=0; i<numCounters; i++){
    long long totalCount = 0;
    long long tempCount = 0;
    int result = 0;
    if(!instructionFailed){
      for(int j=0; j<NUM_COUNTER_REPS; j++){
        tempCount = 0;
        result = 0;
        ioctl(counters[i], PERF_EVENT_IOC_RESET, 0);
        GET_START_TIME();  //serialise
        ioctl(counters[i], PERF_EVENT_IOC_ENABLE, 0);
        ((void(*)())execInstruction)();
        __asm__ __volatile__ ("\
            .global perfResume   \n\
            perfResume:          \n\
            "
            );
        ;
        ioctl(counters[i], PERF_EVENT_IOC_DISABLE, 0); \
        GET_END_TIME();   //serialise - I would like to have this before we disable ioctl but it would affect counter values too much
        if(read(counters[i], &tempCount, sizeof(long long)) < 0){
          printf("error in counter profiling\n");
          perror("read");
          endPerformanceMonitoring();
          return false;
        }
        totalCount += tempCount;
      }
      long double temp = ceill(totalCount/(long double)NUM_COUNTER_REPS)-noise[i];
      if (temp < 0) temp = 0;
      fprintf(outputFile, "%" PRIu64 " ", counterOutputs[i]);
      counterOutputs[i] = temp;
    }
    else {
      //TODO - use different counters - non-retired instructions?? microarchitectural traces
    }
  }
  return true;
}

//helper function
void writeFunctionalityGuess(char *msg){
  //fprintf(outputFile, " %s;", msg);
  printf("%s. ", msg); //DEBUG
  guessedFunctionality = true;
}

//See Intel optimisation manual chapter 2 for example opcodes for each category
//e.g. slow int includes mul, imul, bsr, rcl, shld, mulx, pdep for Haswell/Broadwell
bool doPortAnalysis(){

    //DEBUG
    for(int i=0; i<8; i++){
      printf("Port %d uops: %ld\n", i, counterOutputs[i]);
    }

    guessedFunctionality = false;
    //noise already subtracted from outputs
    bool port0 = counterOutputs[0] > threshold ? true : false;
    bool port1 = counterOutputs[1] > threshold ? true : false;
    bool port2 = counterOutputs[2] > threshold ? true : false;
    bool port3 = counterOutputs[3] > threshold ? true : false;
    bool port4 = counterOutputs[4] > threshold ? true : false;
    bool port5 = counterOutputs[5] > threshold ? true : false;
    bool port6, port7, fp = false;
    if(numPorts == 8){
      port6 = counterOutputs[6] > threshold ? true : false;
      port7 = counterOutputs[7] > threshold ? true : false;
      //combine single and double precision FP counters. floating point counters count instructions, not uops, so threshold here is 0.
      fp = (counterOutputs[8] > threshold) || (counterOutputs[9] > threshold) ? true : false;
    }
    else{
      fp = (counterOutputs[6] > threshold) || (counterOutputs[7] > threshold) ? true : false;
    }
    if(!port0 && !port1 && !port2 && !port3 && !port4 && !port5 && !port6 && !port7){
      writeFunctionalityGuess("nop or opcodeTester error");
      printf("\n");
      return true;  //should be false if error but it being a nop is a legit case and seems quite common with Sandsifter logs
    }

		if(microarch == XED_CHIP_SANDYBRIDGE || microarch == XED_CHIP_IVYBRIDGE){

      //TODO: Sandy uses three separate execution stacks (x87), these need to be taken into account

      //Memory operations
      if(port4) writeFunctionalityGuess("store data");
      if(port2 || port3) writeFunctionalityGuess("load or store address");

      //Other operations
      //TODO: categories incomplete
      if(port0 && port1 && port5) writeFunctionalityGuess("integer ALU");
      else if(port0 && port1) writeFunctionalityGuess("integer ALU or V-shuffle");
      else if(port1 && port5) writeFunctionalityGuess("integer ALU");
      else if(port0 && port5) writeFunctionalityGuess("integer ALU or 256-FP blend");
      else if(port0) writeFunctionalityGuess("vector/256-FP multiply or fdiv");
      else if(port1) writeFunctionalityGuess("vector add or conversion (CVT) or LEA");
      else if(port5) writeFunctionalityGuess("jump or 256-FP shuffle/bool");

		}

		else if(microarch == XED_CHIP_HASWELL || microarch == XED_CHIP_BROADWELL || microarch == XED_CHIP_SKYLAKE){

      //Note: we assume we repeat the instruction enough times to saturate all
      //the relevant ports.

      //Memory operations - no need to check memory perf counters as these seem consistent enough
      //This is identical for Haswell/Broadwell/Skylake.
      if(port4) writeFunctionalityGuess("store data");
      if(port2 && port3 && port7) writeFunctionalityGuess("store address (+ possible load)");
      else{
        if(port2 && port3) writeFunctionalityGuess("load");
        if(port7) writeFunctionalityGuess("store address");
      }

      //Other operations - assume only one main operation to make a "best guess"
      if(microarch == XED_CHIP_HASWELL || microarch == XED_CHIP_BROADWELL){
        if(port0 && port1 && port5 && port6) writeFunctionalityGuess("integer ALU");
        else{
          if(port0 && port6) writeFunctionalityGuess("shift or branch");
          else if(port6 && !port0) writeFunctionalityGuess("branch");       //port6 is primary port for branch but if we are saturating ports this condition may never be met

          if(port0 && port1 && port5) writeFunctionalityGuess("integer ALU or vector logical");
          else if(port0 && port1 && !port5 && fp) writeFunctionalityGuess("FP multiply or FMA or integer ALU"); //only on port0 and port1
          else if(!port0 && port1 && port5) writeFunctionalityGuess("fast LEA or integer/vector ALU");  //only on port1 and port5
          else if(port0 && !port1 && port5) writeFunctionalityGuess("integer ALU");
          else if(port0 && !port6) writeFunctionalityGuess("vector shift or divide or SSTNI");  //only on port0
          else if(port1){  //only on port1
            if(fp) writeFunctionalityGuess("FP add");
            else writeFunctionalityGuess("slow int");
          }
          else if(port5) writeFunctionalityGuess("vector shuffle");  //only on port5
        }
      }
      else{ //Skylake
        if(port0 && port1 && port5 && port6) writeFunctionalityGuess("integer ALU");
        else{
          if(port6) writeFunctionalityGuess("branch");

          if(port0 && port1 && port5) writeFunctionalityGuess("integer/vector ALU");
          else if(port0 && port1 && !port5) writeFunctionalityGuess("vector {FMA or multiply or add or shift}");
          else if(!port0 && port1 && port5) writeFunctionalityGuess("fast LEA");
          else if(port0 && !port1 && port5) writeFunctionalityGuess("integer ALU");
          else if(port0 && !port6) writeFunctionalityGuess("divide");
          else if(port1) writeFunctionalityGuess("integer multiply or slow LEA");
          else if(port5) writeFunctionalityGuess("conversion (CVT)");
          //TODO: no documentation for floating point ports??!!
        }
      }
		}

    //TODO: develop this further and support more architectures, tailored profiling per system (detect unusual port behaviour with known instrs?)

    if(guessedFunctionality) printf("\n");
    return guessedFunctionality;
    //TODO: use this to calculate % of instrs we were able to guess functionality for - track improvements

}

void endPerformanceMonitoring(){
  for(int i=0; i<numCounters; i++){
    if(counters[i] > 0) close(counters[i]);
  }
}


//Must test in main opcodeTester from now on
/*int main(){

	outputFile = fopen("output.txt", "w");
	if(outputFile == NULL){
		perror("fopen");
		return 1;
	}

	//get system's page size and calculate pagestart addr
	execInstruction[0] = 0x55;  //prelude
	execInstruction[1] = 0x90;  //nop
	execInstruction[2] = 0x5d;	//prologue
	execInstruction[3] = 0xc3;

	size_t pagesize = sysconf(_SC_PAGESIZE);
	uintptr_t pagestart = ((uintptr_t) &execInstruction) & -pagesize;
	if(mprotect((void*) pagestart, pagesize, PROT_READ|PROT_WRITE|PROT_EXEC)){
		perror("mprotect");
		return 1;
	}

	//init function analysis variables
	counterConfigs[0] = 0x1a1;
	counterConfigs[1] = 0x2a1;
	counterConfigs[2] = 0x4a1;
	counterConfigs[3] = 0x8a1;
	counterConfigs[4] = 0x10a1;
	counterConfigs[5] = 0x20a1;
	counterConfigs[6] = 0x40a1;
	counterConfigs[7] = 0x80a1;
	counterConfigs[8] =	0x15c7;
	counterConfigs[9] = 0x2ac7;

  microarch = XED_CHIP_BROADWELL;
	numPorts = (microarch == XED_CHIP_NEHALEM) ? 6 : 8;

  if(!initPerformanceMonitoring(numPorts, outputFile)){
    printf("Failed to init performance monitoring :(\n");
    return 1;
  }

  profileUnknownInstruction(false);
  if(!doPortAnalysis()) printf("Couldn't guess instruction functionality.\n");

	return 0;
}

/* TODO work this code into above, important changes like counting num times successful
long double instrCountsNoNoise[numCounters];
long double instrCounts[numCounters];
bool instructionSuccessOnce;
long long instrCount;
long long tempCount;
int numSuccessfulRuns;

for(int i=0; i<numCounters; i++){

	instrCount = 0;
	tempCount = 0;
	instructionSuccessOnce = false;
	numSuccessfulRuns = 0;

	//test instruction * NUM_COUNTER_REPS, recording counter i value each time if instruction runs successfully
	for(int j=0; j<NUM_COUNTER_REPS; j++){
		instructionFailed = 0;
		ioctl(counters[i], PERF_EVENT_IOC_RESET, 0);
		ioctl(counters[i], PERF_EVENT_IOC_ENABLE, 0);

		((void(*)())execInstruction)();

		ioctl(counters[i], PERF_EVENT_IOC_DISABLE, 0);
		tempCount = 0;
		if(!instructionFailed){   //this is set in signal handler
			if(read(counters[i], &tempCount, sizeof(long long)) < 0){
					printf("Error in port analysis reading counter\n");
					perror("read");
			}
			instrCount += tempCount;
			instructionSuccessOnce = true;
			numSuccessfulRuns++;
		}
	}

	if(instructionSuccessOnce){
		instrCounts[i] = ceill(instrCount/(long double)numSuccessfulRuns);
		instrCountsNoNoise[i] = instrCounts[i] - noise[i];
		if(instrCountsNoNoise[i] < 0) instrCountsNoNoise[i] = 0;
	}
	else{
		instrCounts[i] = 0;
		instrCountsNoNoise[i] = 0;
		fprintf(outputFile, "Could not run instr successfully to test counter %d, average values below will be wrong.\n", i);
	}

	if(!instructionsExecutingInRing3 && instructionSuccessOnce){
		instructionsExecutingInRing3 = true;
	}

}

	//output results
	fprintf(outputFile, "\n");
	fprintf(outputFile, "Average total uops:\n");
	for(int i=0; i<numCounters; i++){
		fprintf(outputFile, "Port %d: %.0Lf\n", i, instrCounts[i]);
	}
	fprintf(outputFile, "\n");
	fprintf(outputFile, "Estimated uops for code under test (noise removed):\n");        //these values seem to be remarkably consistent, only vary by 1 or 2 uops? system under identical load though
	for(int i=0; i<numCounters; i++){
		fprintf(outputFile, "Port %d: %.0Lf\n", i, instrCountsNoNoise[i]);
	}
	fprintf(outputFile, "\n");

	int maxPortNum = 0;
	int maxPortVal = 0;
	for(int i=0; i<numCounters; i++){
		if(instrCountsNoNoise[i] > maxPortVal){
			maxPortVal = instrCountsNoNoise[i];
			maxPortNum = i;
		}
	}
*/
