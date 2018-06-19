/******************************************************************************
 * opcodeTesterKernel.c                                                       *
 * Automated Intel x86_64 CPU undocumented instruction testing/analysis       *
 * (when used in tandem with Sandsifter)                                      *
 *                                                                            *
 * This driver is designed to test undocumented Intel x86_64 instructions     *
 * in the kernel in tandem with the user mode opcodeTester program. This      *
 * driver implements error handling and UD exception handling, but please     *
 * be aware that running undocumented instructions in the kernel is           *
 * inherently risky; an instruction which successfully executes may have an   *
 * effect which crashes the system, modifies data or changes system           *
 * behaviour. Use at your own risk.                                           *
 *                                                                            *
 * NOTE: compatible ONLY with Intel x86_64. Currently supported               *
 * microarchitectures: Sandy/Ivy Bridge, Haswell/Broadwell, Skylake.          *
 * Tested on i7-5600U with Ubuntu 17.10; this code depends on _GNU_SOURCE     *
 * definitions. Expect undocumented instructions to execute differently in    *
 * VMs/emulators.  *                                                          *
 *                                                                            *
 * By Catherine Easdon, 2018                                                  *
 ******************************************************************************/

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/preempt.h>
#include <linux/hardirq.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/types.h>
#include <linux/sched/signal.h>
#include <linux/kdebug.h> //for die_notifier
#include "../../include/opcodeTester.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Catherine Easdon");
MODULE_DESCRIPTION("A module to enable opcodeTester to run opcodes in ring 0.");
MODULE_VERSION("1.0");
MODULE_PARM_DESC(name, "OpcodeTesterKernel");  /// parameter description: name to display in /var/log/kern.log

unsigned static cyclesStartLow, cyclesStartHigh, cyclesEndLow, cyclesEndHigh;
static struct class *opcodeTesterKernel_class = NULL;
static struct device *opcodeTesterKernel_device = NULL;
static bool instrLenSet = false;
static unsigned long instrLen = 100;
static unsigned char   message[256] = {0};
static uint64_t dataForUser[SIZE_OF_DATA_FOR_USER / sizeof(uint64_t)] = {0, 0, 0, 0};  //NOTE: if you change this you must change SIZE_OF_DATA_FOR_USER
static uint64_t lastSignal = 0;   //must be included in SIZE_OF_DATA_FOR_USER
#define BOUND_OF_LOOP 1000 // was 100000
#define UINT64_MAX (18446744073709551615ULL)
static int opcodeExecutedSuccessfully = 1;
static unsigned char *opcode;
static int prevExceptionsHandled = 0;
static bool opcodeIsExecuting = false;
static int opcodeByteCount = 0;
static uint64_t measurement_overhead = 0;
static bool firstTest = true;
static long beforeState[SIZE_STATE] = {0};
static long afterState[SIZE_STATE] = {0};

/***************************************************************************************************************
Note: this driver uses some code from the following guides:
http://derekmolloy.ie/writing-a-linux-kernel-module-part-2-a-character-device/ (driver init and exit)
https://www.intel.com/content/dam/www/public/us/en/documents/white-papers/ia-32-ia-64-benchmark-code-execution-paper.pdf (clock cycles alg adapted from here)

Caveats:
* unseen instrs in SMM will also increase time stamp counter - not much we can do about that unfortunately :(
* Intel paper method assumed it was running on a BIOS tuned to reduce "indeterminism" i.e. power optimisation, Intel Hyper-Threading,
frequency scaling, turbo mode turned off.

TODO:
* detecting speculative uops for illegal instructions
* use perf counters described in appendix B.4.2 of intel optimisation manual (bad speculation uops, as illegal one won't retire)
* UOPS_ISSUED.ANY – UOPS_RETIRED.RETIRE_SLOTS + 4 *INT_MISC.RECOVERY_CYCLES

****************************************************************************************************************/

static int    majorNumber;
#define DEVICE_NAME "opcodeTesterKernel_dev"
#define CLASS_NAME "opcodeTesterKernel_class"

// The prototype functions for the character driver -- must come before the struct definition
static int     opcodeTesterKernel_open(struct inode *, struct file *);
static int     opcodeTesterKernel_release(struct inode *, struct file *);
static ssize_t opcodeTesterKernel_read(struct file *, char *, size_t, loff_t *);
static ssize_t opcodeTesterKernel_write(struct file *, const char *, size_t, loff_t *);

static struct file_operations fops =
{
   .open = opcodeTesterKernel_open,
   .read = opcodeTesterKernel_read,
   .write = opcodeTesterKernel_write,
   .release = opcodeTesterKernel_release,
};


/* by returning NOTIFY_STOP we skip the call to die() in do_error_trap()
but this means we miss important things like re-enabling interrupts!
so we must do all these things here, otherwise the system freezes */
static int opcodeTesterKernel_die_event_handler (struct notifier_block *self, unsigned long event, void *data){

    struct die_args* args = (struct die_args *)data;

    if (prevExceptionsHandled < 4 && opcodeIsExecuting){

    	if(args->trapnr == 6){
    		opcodeExecutedSuccessfully = 0;
    		prevExceptionsHandled++;	//4 exceptions expected - any more is a problem

    		if(opcodeByteCount > 4) args->regs->ip += (opcodeByteCount - 2);
    		else if(opcodeByteCount > 1) args->regs->ip += (opcodeByteCount);
        //this works for 1-3 bytes as long as the instr doesn't page fault (shorter ones more likely to - I assume missing expected addressing modes/operands)
    		else return 0;

    		switch(args->signr){
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

    	  //equiv of unexported cond_local_irq_enable(args->regs);
    	  if(args->regs->flags & X86_EFLAGS_IF){
    		    local_irq_enable();
    	  }

    	  return NOTIFY_STOP;

    	}

    	else if(args->trapnr == 14){
    		//TODO: page fault specific handling, GP too. Note GP does not use do_error_trap - slightly different code
    		return 0;
    	}

    	else return 0;

    }

    else {
    	//if too many exceptions or an event we cannot handle, play it safe and let the kernel handle the event and kill the program
    	//NOTE: we also always get a 'spurious' SIG_ILL before we execute a SIG_ILL opcode in the kernel - our kernel handler is triggered by testing it in ring 3
    	//printk(KERN_INFO "OpcodeTesterKernel: event was %ld, trapnr was %d, signr was %d, msg is %s\n", event, args->trapnr, args->signr, args->str);
    	return 0;
    }
}

static __read_mostly struct notifier_block opcodeTesterKernel_die_notifier = {
  .notifier_call = opcodeTesterKernel_die_event_handler,
  .next = NULL,
  .priority = 0
};

static int opcodeTesterKernel_open(struct inode *inodep, struct file *filep){
   printk(KERN_INFO "OpcodeTesterKernel: device opened\n");
   return 0;
}

static ssize_t opcodeTesterKernel_read(struct file *filep, char *buffer, size_t len, loff_t *offset){

   int error_count = 0;
   error_count = copy_to_user(buffer, &dataForUser, SIZE_OF_DATA_FOR_USER);

   if (error_count==0){
      return 0;
   }
   else {
      printk(KERN_INFO "OpcodeTesterKernel: Failed to send opcode to the user.\n");
      return -EFAULT;
   }
}

static void opcodeTesterKernel_calculateOverhead(void){

      uint64_t min_time = UINT64_MAX, temp_time = 0, total_time = 0, startTime = 0, endTime = 0;
      unsigned long flags;
      int i = 0;
      volatile int variable = 0;

      //warm instruction cache
      GET_START_TIME();
      GET_END_TIME();

      //calculate overhead of measurement instrs
      for(i=0; i<BOUND_OF_LOOP; i++){
        variable = 0;
        preempt_disable();
        raw_local_irq_save(flags);
        GET_START_TIME();
        //((void(*)(void))overheadCode)();  //TODO: weirdly this makes total time higher than when we have the opcode as well?
        GET_END_TIME();
        raw_local_irq_restore(flags);
        preempt_enable();
        startTime = ( ((uint64_t)cyclesStartHigh << 32) | cyclesStartLow );
        endTime = ( ((uint64_t)cyclesEndHigh << 32) | cyclesEndLow );
        if ((endTime - startTime) < 0){
          printk(KERN_INFO "OpcodeTesterKernel: Error measuring measurement overhead cycle count\n");
        }
        else {
            temp_time = endTime - startTime;
            total_time += temp_time;
        }
      }
      measurement_overhead = total_time / BOUND_OF_LOOP; //int division as no fpu in kernel, so only rough estimate of mean
      printk(KERN_INFO "OpcodeTesterKernel: Debug: total overhead time %lld, measurement overhead is %lld\n", total_time, measurement_overhead);
      firstTest = false;
}

static ssize_t opcodeTesterKernel_write(struct file *filep, const char *buffer, size_t len, loff_t *offset){
  if(!instrLenSet){   //first of two writes from user program
    int err = 0;
    sprintf(message, "%ld", buffer);
    err = kstrtoul(message, 10, &instrLen);
    if(err) printk(KERN_INFO "OpcodeTesterKernel: Opcode instruction length error is %d.\n", err);
    instrLenSet = true;
  }
  else{   //second of two writes from user program
    int i = 0;
    uint64_t min_time = UINT64_MAX, total_min_time = 0, temp_time = 0, num_min_times = 0, total_time = 0, startTime = 0, endTime = 0;
    unsigned long flags;
    volatile int variable = 0;
    instrLenSet = false;

    if(instrLen < MAX_INSTR_LENGTH && instrLen > 0){     //catch mem corruption in user program (has happened...)
      copy_from_user(opcode, buffer, instrLen);
      opcodeByteCount = 0;
      for(i=1; i<instrLen-2; i++){
					opcodeByteCount++;
      }

      if(firstTest) opcodeTesterKernel_calculateOverhead();

      min_time = UINT64_MAX;
      total_min_time = 0;
      num_min_times = 0;

      //warm instruction cache
      opcodeIsExecuting = true;
      GET_START_TIME();
      //((void(*)(void))opcode)();	//this is still too problematic
      GET_END_TIME();
      opcodeIsExecuting = false;

      //disable preemption and interrupts ('get ownership of CPU'), then time opcode
      for (i=0; i<BOUND_OF_LOOP; i++) {
        temp_time = 0;
        opcodeExecutedSuccessfully = 1;
        opcodeIsExecuting = true;
        prevExceptionsHandled = 0;
        variable = 0; //stops compiler re-ordering?
        preempt_disable();
        raw_local_irq_save(flags);
        GET_START_TIME();
				//ideally this would be a dependency chain but I see no way of doing that for all possible opcodes...
        ((void(*)(void))opcode)();
        ((void(*)(void))opcode)();
        ((void(*)(void))opcode)();
        ((void(*)(void))opcode)();
        GET_END_TIME();
        raw_local_irq_restore(flags);
        preempt_enable();
        opcodeIsExecuting = false;

        if(opcodeExecutedSuccessfully){
          dataForUser[0] = 1; //execution success
        	startTime = ( ((uint64_t)cyclesStartHigh << 32) | cyclesStartLow );
        	endTime = ( ((uint64_t)cyclesEndHigh << 32) | cyclesEndLow );
        	if ((endTime - startTime) < 0){
        	  printk(KERN_INFO "OpcodeTesterKernel: Error measuring opcode cycle count\n");
        	  dataForUser[1] = 0;
  	      }
  	      else {
  	       //temp_time = (endTime - startTime) / 4;  //divide AFTER all tests - reduce loss of precision due to repeated int divisions
            total_time += temp_time;  //no overflow detection here but unlikely given num of reps
            if(temp_time < min_time){
              min_time = temp_time;
              if(i != 0){
                total_min_time += temp_time;
                num_min_times++;
              }
            }
  	      }

        }
        else {
          dataForUser[0] = 0;   //execution failed
          dataForUser[1] = lastSignal;
          break;  //stop testing opcode
        }
      }

      if(opcodeExecutedSuccessfully){
	      if(num_min_times == 0) num_min_times = 1; //just in case i=0 was fastest time
	      //return mean timings to user, check for overflows (uints should never become negative but I saw it in testing? may have been printk doing that)
        if(min_time != 0) min_time = min_time / 4;
  			if(total_min_time != 0) total_min_time = total_min_time / 4;
  			if(total_time != 0) total_time = total_time / 4;
	      temp_time = min_time - measurement_overhead; //min time minus overhead
	      dataForUser[1] = (temp_time >= 0) && (temp_time <= min_time) ? temp_time : 0;
	      temp_time = (total_min_time / num_min_times) - measurement_overhead; //mean min time minus overhead
	      dataForUser[2] = (temp_time >= 0) && (temp_time <= total_min_time) ? temp_time : 0;
	      temp_time = (total_time / BOUND_OF_LOOP) - measurement_overhead;  //full mean in case it's useful
	      dataForUser[3] = (temp_time >= 0) && (temp_time <= total_time) ? temp_time : 0;
     }

    }
   else{
     printk(KERN_INFO "OpcodeTesterKernel: Did not test opcode because instrLen was wrong, got len %ld.\n", instrLen);
   }
 }
return len;
}

static int opcodeTesterKernel_release(struct inode *inodep, struct file *filep){
   printk(KERN_INFO "OpcodeTesterKernel: Device closed\n");
   return 0;
}


static int __init opcodeTesterKernel_init(void) {

  printk(KERN_INFO "OpcodeTesterKernel: Module starting.\n");

  // Try to dynamically allocate a major number for the device
  majorNumber = register_chrdev(0, DEVICE_NAME, &fops);
  if (majorNumber<0){
    printk(KERN_ALERT "OpcodeTesterKernel: Failed to register a major number\n");
    return majorNumber;
  }
  printk(KERN_INFO "OpcodeTesterKernel: Registered correctly with major number %d\n", majorNumber);

  // Register the device class
  opcodeTesterKernel_class = class_create(THIS_MODULE, CLASS_NAME);
  if (IS_ERR(opcodeTesterKernel_class)){
    unregister_chrdev(majorNumber, DEVICE_NAME);
    printk(KERN_ALERT "Failed to register device class\n");
    return PTR_ERR(opcodeTesterKernel_class);
  }

  // Register the device driver
  opcodeTesterKernel_device = device_create(opcodeTesterKernel_class, NULL, MKDEV(majorNumber, 0), NULL, DEVICE_NAME);
  if (IS_ERR(opcodeTesterKernel_device)){
    class_destroy(opcodeTesterKernel_class);
    unregister_chrdev(majorNumber, DEVICE_NAME);
    printk(KERN_ALERT "OpcodeTesterKernel: Failed to create the device\n");
    return PTR_ERR(opcodeTesterKernel_device);
  }

  register_die_notifier (&opcodeTesterKernel_die_notifier);
  opcode = __vmalloc(15, GFP_KERNEL, PAGE_KERNEL_EXEC);
  memset(opcode, 0, 15);


  printk(KERN_INFO "OpcodeTesterKernel: Device class created correctly\n");

 return 0;
}


static void __exit opcodeTesterKernel_exit(void) {

  unregister_die_notifier(&opcodeTesterKernel_die_notifier);  //this is crucial to avoid crash when reloading driver
  vfree(opcode);

  device_destroy(opcodeTesterKernel_class, MKDEV(majorNumber, 0));
  class_unregister(opcodeTesterKernel_class);
  class_destroy(opcodeTesterKernel_class);
  unregister_chrdev(majorNumber, DEVICE_NAME);
  printk(KERN_INFO "OpcodeTesterKernel: Module exited.\n");
}

module_init(opcodeTesterKernel_init);
module_exit(opcodeTesterKernel_exit);
