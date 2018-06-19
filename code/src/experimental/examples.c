/* Minimal kernel code snippet, no headers */

static unsigned char *code;

opcode = __vmalloc(15, GFP_KERNEL, PAGE_KERNEL_EXEC);
memset(opcode, 0, 15);
code[0] = 0x55; code[1] = 0x90;
code[2] = 0x55; code[3] = 0xc3;
((void(*)(void))code)();
printk(KERN_INFO "Success!\n");
vfree(opcode);

//Note: copy_from_user(opcode, buffer, instrLen);


/* Minimal user mode code snippet, no headers */

unsigned char code[4];

//pointer passed to mprotect must be aligned to page boundary
size_t pagesize = sysconf(_SC_PAGESIZE);
uintptr_t pagestart = ((uintptr_t) &code) & -pagesize;
if(mprotect((void*) pagestart, pagesize, PROT_READ|PROT_WRITE|PROT_EXEC)){
  perror("mprotect");
  return 1;
}
code[0] = 0x55; //push rbp (frame pointer)
code[1] = 0x90; //our instruction - NOP
code[2] = 0x55; //pop rbp
code[3] = 0xc3; //retq
((void(*)())code)();
printf("Success!\n");
