"""Offline native-body flag references, decoded from unwind function boundaries."""
from research_game import Research
r = Research()
for start, end, _ in r.functions:
    if not 0x2e35000 <= start < 0x2ea8000:
        continue
    instructions = r.disassemble(start)
    matches = [i for i, (_, _, op, args) in enumerate(instructions)
               if ('+ 0x40]' in args or '+ 0x44]' in args) and
               not ('[rsp' in args or '[rbp' in args) and op in ('test', 'cmp', 'and')]
    for i in matches:
        print(f'FUNCTION {start:08x}')
        for address, size, op, args in instructions[max(0, i-4):i+7]:
            print(f'{address:08x} {op:8} {args}')
