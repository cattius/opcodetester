//Important: this code has only been tested on Intel x86-64!!
//Tested on Ubuntu 17.10, Intel i7-5600U (Broadwell)

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
static uint64_t dataForUser[SIZE_OF_DATA_FOR_USER / sizeof(uint64_t)] = {0, 0, 0, 0};  //TODO: if you change this you must change SIZE_OF_DATA_FOR_USER
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
        * I *think* I can get the num of bad speculated uops with (UOPS_ISSUED.ANY – UOPS_RETIRED.RETIRE_SLOTS + 4 *INT_MISC.RECOVERY_CYCLES)
* graphs
      	*compare overhead once vs. every time
      	*with whichever of those won - compare different N values
      	*then compare consistency of values and match to known instrs
      	*consistency of values w undoc instrs too?

****************************************************************************************************************/

static int    majorNumber;
#define DEVICE_NAME "opcodeTesterKernel_dev"
#define CLASS_NAME "opcodeTesterKernel_class"

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
    if (prevExceptionsHandled < 4 && opcodeIsExecuting && (args->trapnr == 6) ) {	//6 is invalid opcode, don't catch 14 page fault or 13 general protection fault as currently have no strategy for handling them
      opcodeExecutedSuccessfully = 0;
      prevExceptionsHandled++;	//4 exceptions expected - any more is a problem

			if(opcodeByteCount > 2) args->regs->ip += (opcodeByteCount - 2);	//NOTE: this has only been tested for byte counts 4-8!!
			else return 0;
			//TODO: what do we do if opcode byte count is 2 or less?????

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

static ssize_t opcodeTesterKernel_write(struct file *filep, const char *buffer, size_t len, loff_t *offset){
      volatile int variable;
      unsigned long flags;
      copy_from_user(opcode, buffer, instrLen);          //NOTE: *MUST* use this, not memcpy!!!!!!!!
      variable = 0; //to stop compiler re-ordering I think (volatile)
      opcodeIsExecuting = true;
      //disable preemption and hard interrupts ('get ownership of CPU'), then time opcode
      preempt_disable();
      raw_local_irq_save(flags);
      GET_START_TIME();
      ((void(*)(void))opcode)();
      ((void(*)(void))opcode)();
      ((void(*)(void))opcode)();
      ((void(*)(void))opcode)();
      GET_END_TIME();
      raw_local_irq_restore(flags);
      preempt_enable();
      opcodeIsExecuting = false;
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
