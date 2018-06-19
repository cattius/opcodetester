#ifndef PERF_MONITORING_USER
#define PERF_MONITORING_USER

//TODO: clean these up
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <math.h>
#include <ctype.h>
#include <inttypes.h>
#include <sys/ioctl.h>
#include <linux/perf_event.h>
#include <asm/unistd.h>
#include <sched.h>
#include "opcodeTester.h"

long perf_event_open(struct perf_event_attr *hw_event, pid_t pid, int cpu, int group_fd, unsigned long flags);

bool initPerformanceMonitoring(int numPorts, FILE* thisOutputFile);

bool profileUnknownInstruction(bool instructionFailed);

bool doPortAnalysis();  //includes floating point

void doMemAnalysis();

void endPerformanceMonitoring();

//*****************************************************************************

/* ASM performance counter profiling instructions
Haswell/Broadwell can dispatch up to eight uops per cycle
Eight reps -> attempt to saturate all ports which can handle that instruction
(makes determining category much easier)
*/

static inline void ASM_ADD(int num1, int num2, int result) {
  __asm__ __volatile__ ( "add %%ebx, %%eax;" : "=a" (result) : "a" (num1) , "b" (num2) );
  __asm__ __volatile__ ( "add %%ebx, %%eax;" : "=a" (result) : "a" (num1) , "b" (num2) );
  __asm__ __volatile__ ( "add %%ebx, %%eax;" : "=a" (result) : "a" (num1) , "b" (num2) );
  __asm__ __volatile__ ( "add %%ebx, %%eax;" : "=a" (result) : "a" (num1) , "b" (num2) );
  __asm__ __volatile__ ( "add %%ebx, %%eax;" : "=a" (result) : "a" (num1) , "b" (num2) );
  __asm__ __volatile__ ( "add %%ebx, %%eax;" : "=a" (result) : "a" (num1) , "b" (num2) );
  __asm__ __volatile__ ( "add %%ebx, %%eax;" : "=a" (result) : "a" (num1) , "b" (num2) );
  __asm__ __volatile__ ( "add %%ebx, %%eax;" : "=a" (result) : "a" (num1) , "b" (num2) );
}

//ports 2 and 3 (on newer CPUs - does Sandy have this?)
static inline void ASM_PREFETCHW(int variable) {
  __asm__ __volatile__ ( "prefetchw %0;" : : "m" (variable));
  __asm__ __volatile__ ( "prefetchw %0;" : : "m" (variable));
  __asm__ __volatile__ ( "prefetchw %0;" : : "m" (variable));
  __asm__ __volatile__ ( "prefetchw %0;" : : "m" (variable));
  __asm__ __volatile__ ( "prefetchw %0;" : : "m" (variable));
  __asm__ __volatile__ ( "prefetchw %0;" : : "m" (variable));
  __asm__ __volatile__ ( "prefetchw %0;" : : "m" (variable));
  __asm__ __volatile__ ( "prefetchw %0;" : : "m" (variable));
}

//port 1 (v v obvious)
static inline void ASM_LZCNT() {
  __asm__ __volatile__ ( "lzcnt %%rax, %%rbx;" : : : "%rax", "%rbx" );
  __asm__ __volatile__ ( "lzcnt %%rax, %%rbx;" : : : "%rax", "%rbx" );
  __asm__ __volatile__ ( "lzcnt %%rax, %%rbx;" : : : "%rax", "%rbx" );
  __asm__ __volatile__ ( "lzcnt %%rax, %%rbx;" : : : "%rax", "%rbx" );
  __asm__ __volatile__ ( "lzcnt %%rax, %%rbx;" : : : "%rax", "%rbx" );
  __asm__ __volatile__ ( "lzcnt %%rax, %%rbx;" : : : "%rax", "%rbx" );
  __asm__ __volatile__ ( "lzcnt %%rax, %%rbx;" : : : "%rax", "%rbx" );
  __asm__ __volatile__ ( "lzcnt %%rax, %%rbx;" : : : "%rax", "%rbx" );;
}

#endif
