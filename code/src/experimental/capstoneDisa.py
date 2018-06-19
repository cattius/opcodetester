from capstone import *

CODE = b"\x0f\xae\xf7"

md = Cs(CS_ARCH_X86, CS_MODE_64)
for (address, size, mnemonic, op_str) in md.disasm_lite(CODE, 0x1000):
    print("0x%x:\t%s\t%s" %(address, mnemonic, op_str))
