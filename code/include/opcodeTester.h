#ifndef OPCODE_TESTER_H
#define OPCODE_TESTER_H

//modify these as needed
#define SIZE_OF_DATA_FOR_USER sizeof(uint64_t)*4
#define MAX_INSTR_LENGTH 15
#define NUM_INSTRS_PER_ROUND 10
#define USE_RING_0 1
#define PAUSE_AFTER_EACH_ROUND 0
#define PORT_ANALYSIS_THRESHOLD 5
#define NUM_COUNTER_REPS 1000        //number of times to test each hardware counter
#define MAX_NUM_COUNTERS 20
#define SIZE_XED_IFORM_ENUM 6291
#define XED_DETAILED_OUTPUT 0         //shows detail opcode by opcode but makes log file enormous
#define XED_OUTPUT_MACHINE_READABLE 1
#define SIG_HANDLER_DETAILED_OUTPUT 0
#define SHOW_REGISTER_STATE 0
#define MAKE_WORD_CLOUDS 1

enum signalTypes {
  OPCODE_SIGOTHER,
  OPCODE_SIGILL,
  OPCODE_SIGTRAP,
  OPCODE_SIGBUS,
  OPCODE_SIGFPE,
  OPCODE_SIGSEGV,
  OPCODE_NOSIG
};

#if __x86_64__
	//cpuid before acts as a barrier (serialising instr) so other instrs must have finished before we start measuring; then we measure time before code start
	#define GET_START_TIME() ({ \
	  asm volatile ("cpuid \n\
		         rdtsc \n\
		         mov %%edx, %0 \n\
		         mov %%eax, %1" \
		         : "=r" (cyclesStartHigh), "=r" (cyclesStartLow) \
		         :: "%rax", "%rbx", "%rcx", "%rdx"); \
})
	//rdtscp is serialising (code under test must have finished first), get time after, movs serialised as dependent on rdtscp, and then cpuid acts as serialising barrier at end
	#define GET_END_TIME() ({ \
	  asm volatile("rdtscp \n\
		       mov %%edx, %0 \n\
		       mov %%eax, %1 \n\
		       cpuid " \
		       : "=r" (cyclesEndHigh), "=r" (cyclesEndLow) \
		       :: "%rax", "%rbx", "%rcx", "%rdx"); \
	})
#else   //assuming x86
	#define GET_START_TIME() ({ \
	  asm volatile ("cpuid \n\
		         rdtsc \n\
		         mov %%edx, %0 \n\
		         mov %%eax, %1" \
		         : "=r" (cyclesStartHigh), "=r" (cyclesStartLow) \
		         :: "%eax", "%ebx", "%ecx", "%edx"); \
})
	#define GET_END_TIME() ({ \
	  asm volatile("rdtscp \n\
		       mov %%edx, %0 \n\
		       mov %%eax, %1 \n\
		       cpuid " \
		       : "=r" (cyclesEndHigh), "=r" (cyclesEndLow) \
		       :: "%eax", "%ebx", "%ecx", "%edx"); \
	})

#endif
//NOTE: the movs should use edx, eax even for 64-bit

#if __x86_64__
  #define IP REG_RIP
  #define SIZE_STATE 14
  #define READ_PROCESSOR_STATE() ({ \
    __asm__ __volatile__ ("\
      movq %%rax, %[rax] \n\
      movq %%rbx, %[rbx] \n\
      movq %%rcx, %[rcx] \n\
      movq %%rdx, %[rdx] \n\
      movq %%rsi, %[rsi] \n\
      movq %%rdi, %[rdi] \n\
      movq %%r8, %[r8]   \n\
      movq %%r9, %[r9]   \n\
      movq %%r10, %[r10]  \n\
      movq %%r11, %[r11]  \n\
      movq %%r12, %[r12]  \n\
      movq %%r13, %[r13]  \n\
      movq %%r14, %[r14]  \n\
      movq %%r15, %[r15]  \n\
      " \
      : /* no output operands */ \
      : /* input operands */ \
      [rax]"m"(state[0]), \
      [rbx]"m"(state[1]), \
      [rcx]"m"(state[2]), \
      [rdx]"m"(state[3]), \
      [rsi]"m"(state[4]), \
      [rdi]"m"(state[5]), \
      [r8]"m"(state[6]), \
      [r9]"m"(state[7]), \
      [r10]"m"(state[8]), \
      [r11]"m"(state[9]), \
      [r12]"m"(state[10]), \
      [r13]"m"(state[11]), \
      [r14]"m"(state[12]), \
      [r15]"m"(state[13]) \
      ); \
    })
#else   //assuming x86
  #define IP REG_EIP
  #define SIZE_STATE 7
  #define READ_PROCESSOR_STATE() ({ \
    __asm__ __volatile__ ("\
          movl %%eax, %[eax]  \n\
          movl %%ebx, %[ebx]  \n\
          movl %%ecx, %[ecx]  \n\
          movl %%edx, %[edx]  \n\
          movl %%esi, %[esi]  \n\
          movl %%edi, %[edi]  \n\
          movl %%ebp, %[ebp]  \n\
          " \
          : /* no output operands */ \
          : /* input operands */ \
          [eax]"m"(state[0]), \
          [ebx]"m"(state[1]), \
          [ecx]"m"(state[2]), \
          [edx]"m"(state[3]), \
          [esi]"m"(state[4]), \
          [edi]"m"(state[5]), \
          [ebp]"m"(state[6]) \
        ); \
   })
#endif

#endif
