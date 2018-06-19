/******************************************************************************
 * opcodeTesterUser.h                                                          *
 * Automated Intel x86_64 CPU fuzzing for undocumented instructions           *
 * (when used in tandem with Sandsifter)                                      *
 *                                                                            *
 * This program is designed to test undocumented Intel x86_64 instructions    *
 * found by the Sandsifter tool. It takes a Sandsifter log file as input and  *
 * runs the undocumented instructions again, attempting to discover their     *
 * functionality by monitoring the CPU performance counters. Instructions are *
 * run first at ring 3; if they fail at ring 3 and the opcodeTesterKernel     *
 * driver is loaded, they will also be tested at ring 0. Although attempts    *
 * have been made to handle errors, running undocumented instructions is      *
 * inherently risky, especially at ring 0. This software may crash or damage  *
 * your system; use at your own risk and backup all files beforehand.         *
 *                                                                            *
 * NOTE: compatible ONLY with Intel x86_64. Currently supported               *
 * microarchitectures: Nehalem, Sandy Bridge, Haswell. Tested on i7-5600U     *
 * with Ubuntu 17.10; this code depends on _GNU_SOURCE definitions.           *
 * Expect undocumented instructions to execute differently in VMs/emulators.  *
 *                                                                            *
 * Copyright (C) 2018 Catherine Easdon                                        *
 ******************************************************************************/

#ifndef OPCODE_TESTER_USER_H
#define OPCODE_TESTER_USER_H

#include "perfMonitoringUser.h"
#include <Python.h> //for graph script

//*****************************************************************************
/* Hex helper functions
Taken from https://stackoverflow.com/questions/21869321/treat-input-as-hex-values
(slightly adapted) */

//Lookup hex char
static unsigned hexval(unsigned char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return ~0;
}

//Convert string to hex
int str2bin(unsigned char *data, unsigned char **buf_ptr)
{
        int len= 0;
        unsigned int val;
        unsigned char *buf;

        buf = malloc(strlen(data) + 1);		//ok to use strlen here - fine with text, hex 00 is the problem
        *buf_ptr = buf;

        for (; *data; ) {
                val = (hexval(data[0]) << 4) | hexval(data[1]);
                if (val & ~0xff) return -1;  // If not valid hex, return error
                *buf++ = val;
                data += 2;
        }
        len = buf - *buf_ptr;

        return len;
}

//*****************************************************************************

//TODO: maybe just turn this into lookup from an array of strings, neater
char *getSiCodeString(int sig, int sicode){

  if(sig == OPCODE_SIGILL){
    switch(sicode){
      case 1:
        return "ILL_ILLOPC";  /* illegal opcode */
      case 2:
        return "ILL_ILLOPN";  /* illegal operand */
      case 3:
        return "ILL_ILLADR";  /* illegal addressing mode */
      case 4:
        return "ILL_ILLTRP";  /* illegal trap */
      case 5:
        return "ILL_PRVOPC";  /* privileged opcode */
      case 6:
        return "ILL_PRVREG";  /* privileged register */
      case 7:
        return "ILL_COPROC";  /* coprocessor error */
      case 8:
        return "ILL_BADSTK";  /* internal stack error */
      case 9:
        return "ILL_BADIADDR";  /* unimplemented instruction address */
      case 10:
        return "__ILL_BREAK";   /* illegal break */
      case 11:
        return "__ILL_BNDMOD";  /* bundle-update (modification) in progress */
      }
  }
  else if(sig == OPCODE_SIGTRAP){
    switch(sicode){
      case 1:
        return "TRAP_BRKPT";  /* process breakpoint */
      case 2:
        return "TRAP_TRACE";  /* process trace trap */
      case 3:
        return "TRAP_BRANCH"; /* process taken branch trap */
      case 4:
        return "TRAP_HWBKPT"; /* hardware breakpoint/watchpoint */
    }
  }
  else if(sig == OPCODE_SIGBUS){
    switch(sicode){
      case 1:
        return "BUS_ADRALN";  /* invalid address alignment */
      case 2:
        return "BUS_ADRERR";  /* non-existent physical address */
      case 3:
        return "BUS_OBJERR";  /* object specific hardware error */
      case 4:
        return "BUS_MCEERR_AR"; /* hardware memory error consumed on a machine check: action required */
      case 5:
        return "BUS_MCEERR_AO"; /* hardware memory error detected in process but not consumed: action optional*/
    }
  }
  else if(sig == OPCODE_SIGFPE){
    switch(sicode){
      case 1:
        return "FPE_INTDIV";  /* integer divide by zero */
      case 2:
        return "FPE_INTOVF";  /* integer overflow */
      case 3:
        return "FPE_FLTDIV";  /* floating point divide by zero */
      case 4:
        return "FPE_FLTOVF";  /* floating point overflow */
      case 5:
        return "FPE_FLTUND";  /* floating point underflow */
      case 6:
        return "FPE_FLTRES";  /* floating point inexact result */
      case 7:
        return "FPE_FLTINV";  /* floating point invalid operation */
      case 8:
        return "FPE_FLTSUB";  /* subscript out of range */
      case 9:
        return "__FPE_DECOVF";  /* decimal overflow */
      case 10:
        return "__FPE_DECDIV";  /* decimal division by zero */
      case 11:
        return "__FPE_DECERR";  /* packed decimal error */
      case 12:
        return "__FPE_INVASC";  /* invalid ASCII digit */
      case 13:
        return "__FPE_INVDEC";  /* invalid decimal digit */
      case 14:
        return "FPE_FLTUNK";  /* undiagnosed floating-point exception */
      case 15:
        return "FPE_CONDTRAP";  /* trap on condition */
    }
  }
  else if(sig == OPCODE_SIGSEGV){
    switch(sicode){
      case 1:
        return "SEGV_MAPERR"; /* address not mapped to object */
      case 2:
        return "SEGV_ACCERR"; /* invalid permissions for mapped object */
      case 3:
        return "SEGV_BNDERR"; /* failed address bound checks */
      case 4:
        return "__SEGV_PSTKOVF";  /* paragraph stack overflow */
      case 5:
        return "SEGV_PKUERR"; /* failed protection key checks */
      case 6:
        return "SEGV_ACCADI"; /* ADI not enabled for mapped object */
      case 7:
        return "SEGV_ADIDERR";  /* Disrupting MCD error */
      case 8:
        return "SEGV_ADIPERR";  /* Precise MCD exception */
    }
  }

  return "unknown";
}

#endif
