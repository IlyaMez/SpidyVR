"""Locate airborne-state velocity accesses in the supported executable, offline."""
import re
from research_game import Research

r = Research()
for start, end, _ in r.functions:
    if not 0xa60000 <= start < 0xa80000:
        continue
    code = r.disassemble(start)
    found = set()
    for i, (_, _, _, operands) in enumerate(code):
        if re.search(r'\[(?:rcx|rbx|rdi|rsi|rbp|r1[2345]) \+ 0x(?:3cc|3f8|400)\]', operands):
            found.update(range(max(0, i-4), min(len(code), i+5)))
    if found:
        print(f'\nFragment {start:#x}..{end:#x}')
        for i in sorted(found):
            address, size, op, args = code[i]
            print(f'{address:08x}  {op:8} {args}')
