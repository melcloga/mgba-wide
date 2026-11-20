# Tiny Thumb assembler for the handful of instruction forms we need.
import re

R = {f'r{i}': i for i in range(16)}
R.update(sp=13, lr=14, pc=15)

def reg(s): return R[s.strip().lower()]
def imm(s): s = s.strip().lstrip('#'); return int(s, 0)

def assemble(lines, base):
    # two passes for labels
    labels = {}
    def size(ins):
        return 4 if ins.split()[0] == 'bl' else 2
    pc = base
    items = []
    for raw in lines:
        line = raw.split(';')[0].strip()
        if not line: continue
        if line.endswith(':'):
            labels[line[:-1]] = pc; continue
        items.append((pc, line)); pc += size(line)
    out = []
    for pc, line in items:
        out.extend(enc(line, pc, labels))
    return out, labels

def target(s, labels):
    s = s.strip()
    return labels[s] if s in labels else int(s, 0)

def enc(line, pc, labels):
    m = line.split(None, 1)
    op = m[0].lower(); args = m[1] if len(m) > 1 else ''
    a = [x.strip() for x in re.split(r',(?![^\[]*\]|[^{]*})', args)] if args else []
    def mem(s):
        s = s.strip()[1:-1]; p = [x.strip() for x in s.split(',')]
        return p
    if op == 'push':
        regs = a[0].strip('{}')
        mask = 0; lr = 0
        for part in regs.split(','):
            part = part.strip()
            if part == 'lr': lr = 1; continue
            if '-' in part:
                x, y = part.split('-');
                for i in range(reg(x), reg(y) + 1): mask |= 1 << i
            else: mask |= 1 << reg(part)
        return [0xB400 | (lr << 8) | mask]
    if op == 'pop':
        regs = a[0].strip('{}')
        mask = 0; pcb = 0
        for part in regs.split(','):
            part = part.strip()
            if part == 'pc': pcb = 1; continue
            if '-' in part:
                x, y = part.split('-')
                for i in range(reg(x), reg(y) + 1): mask |= 1 << i
            else: mask |= 1 << reg(part)
        return [0xBC00 | (pcb << 8) | mask]
    if op == 'bx': return [0x4700 | (reg(a[0]) << 3)]
    if op == 'b':
        off = (target(a[0], labels) - (pc + 4)) >> 1
        assert -1024 <= off < 1024
        return [0xE000 | (off & 0x7FF)]
    conds = dict(beq=0, bne=1, bcs=2, bcc=3, bmi=4, bpl=5, bvs=6, bvc=7, bhi=8, bls=9, bge=10, blt=11, bgt=12, ble=13)
    if op in conds:
        off = (target(a[0], labels) - (pc + 4)) >> 1
        assert -128 <= off < 128, (line, off)
        return [0xD000 | (conds[op] << 8) | (off & 0xFF)]
    if op == 'bl':
        off = (target(a[0], labels) - (pc + 4)) >> 1
        assert -(1 << 21) <= off < (1 << 21)
        return [0xF000 | ((off >> 11) & 0x7FF), 0xF800 | (off & 0x7FF)]
    if op == 'mov':
        if a[1].startswith('#'):
            return [0x2000 | (reg(a[0]) << 8) | imm(a[1])]
        d, s = reg(a[0]), reg(a[1])
        if d < 8 and s < 8: return [0x1C00 | (s << 3) | d]  # add rd, rs, #0
        return [0x4600 | ((d & 8) << 4) | (s << 3) | (d & 7)]
    if op in ('add', 'sub'):
        if len(a) == 2 and a[1].startswith('#'):
            if reg(a[0]) == 13:
                v = imm(a[1]) >> 2
                return [0xB000 | (0x80 if op == 'sub' else 0) | v]
            return [(0x3000 if op == 'add' else 0x3800) | (reg(a[0]) << 8) | imm(a[1])]
        if len(a) == 3 and a[1] == 'sp':
            assert op == 'add'
            return [0xA800 | (reg(a[0]) << 8) | (imm(a[2]) >> 2)]
        if len(a) == 3 and a[2].startswith('#'):
            return [(0x1C00 if op == 'add' else 0x1E00) | (imm(a[2]) << 6) | (reg(a[1]) << 3) | reg(a[0])]
        if len(a) == 3:
            return [(0x1800 if op == 'add' else 0x1A00) | (reg(a[2]) << 6) | (reg(a[1]) << 3) | reg(a[0])]
    alu = dict(and_=0, eor=1, lsl_r=2, lsr_r=3, asr_r=4, adc=5, sbc=6, ror=7, tst=8, neg=9, cmp_r=10, cmn=11, orr=12, mul=13, bic=14, mvn=15)
    if op == 'cmp':
        if a[1].startswith('#'): return [0x2800 | (reg(a[0]) << 8) | imm(a[1])]
        return [0x4280 | (reg(a[1]) << 3) | reg(a[0])]
    if op in ('and', 'orr', 'eor', 'bic', 'mvn', 'neg', 'mul', 'tst'):
        k = alu['and_' if op == 'and' else op]
        return [0x4000 | (k << 6) | (reg(a[1]) << 3) | reg(a[0])]
    if op in ('lsl', 'lsr', 'asr') and len(a) == 3:
        k = dict(lsl=0, lsr=1, asr=2)[op]
        return [(k << 11) | (imm(a[2]) << 6) | (reg(a[1]) << 3) | reg(a[0])]
    if op in ('ldrh', 'strh', 'ldr', 'str', 'ldrb', 'strb', 'ldrsh'):
        p = mem(a[1]); rd = reg(a[0]); rb = reg(p[0])
        if op == 'ldrsh':
            return [0x5E00 | (reg(p[1]) << 6) | (rb << 3) | rd]
        off = imm(p[1]) if len(p) > 1 else 0
        if rb == 13:
            return [(0x9800 if op == 'ldr' else 0x9000) | (rd << 8) | (off >> 2)]
        if op in ('ldrh', 'strh'):
            return [(0x8800 if op == 'ldrh' else 0x8000) | ((off >> 1) << 6) | (rb << 3) | rd]
        if op in ('ldr', 'str'):
            return [(0x6800 if op == 'ldr' else 0x6000) | ((off >> 2) << 6) | (rb << 3) | rd]
        if op in ('ldrb', 'strb'):
            return [(0x7800 if op == 'ldrb' else 0x7000) | (off << 6) | (rb << 3) | rd]
    raise ValueError(line)
