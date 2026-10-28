#!/usr/bin/env python3
import sys
import re

def process_objdump(file):
    # Regex for a function header line, e.g.:
    #   00000000004004e0 <_start>:
    header_re = re.compile(r'^([0-9a-fA-F]+)\s+<([^>]+)>:$')
    # Regex for an instruction line, e.g.:
    #      4004e0:  31 ed                   xor    %ebp,%ebp
    instr_re = re.compile(r'^\s*([0-9a-fA-F]+):\s+((?:[0-9a-fA-F]{2}\s)+)(.*)$')

    current_func_start = None

    for line in file:
        # Check if this is a function header.
        header_match = header_re.match(line)
        if header_match:
            current_func_start = int(header_match.group(1), 16)
            sys.stdout.write(line)
            continue

        # Check if this is an instruction line.
        instr_match = instr_re.match(line)
        if instr_match and current_func_start is not None:
            addr_str = instr_match.group(1)
            addr = int(addr_str, 16)
            offset = addr - current_func_start
            # Append offset information (in hex) at the end of the line.
            new_line = line.rstrip('\n') + "  [offset: +0x{:x}]\n".format(offset)
            sys.stdout.write(new_line)
        else:
            # For any line that doesn't match, just print it unchanged.
            sys.stdout.write(line)

def main():
    if len(sys.argv) > 1:
        # Read from the file provided as argument.
        with open(sys.argv[1], 'r') as f:
            process_objdump(f)
    else:
        # Read from standard input.
        process_objdump(sys.stdin)

if __name__ == "__main__":
    main()
