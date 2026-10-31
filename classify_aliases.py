import json, re, bisect, sys

# Parse dumpbin /DISASM:BYTES output: every decoded instruction (address -> mnemonic),
# plus direct branch edges (jmp/jcc/call with a literal 8-hex-digit target).
insn_re = re.compile(
    r'^\s*([0-9A-Fa-f]{8}):\s+[0-9A-Fa-f ]+\s+([A-Za-z0-9]+)(?:\s+(\S.*))?$'
)
direct_branch = re.compile(
    r'^\s*([0-9A-Fa-f]{8}):\s+[0-9A-Fa-f ]+\s+(j[a-z]+|call)\s+([0-9A-Fa-f]{8})\s*$'
)

refs = {}        # target_addr -> list of source_addr (explicit direct branches)
mnem_at = {}      # addr -> mnemonic, for every decoded instruction
with open('analysis/full_disasm.txt', encoding='utf-8', errors='replace') as f:
    for line in f:
        m = insn_re.match(line)
        if m:
            mnem_at[int(m.group(1), 16)] = m.group(2).lower()
        m = direct_branch.match(line)
        if m:
            src = int(m.group(1), 16)
            tgt = int(m.group(3), 16)
            refs.setdefault(tgt, []).append(src)

print(f'[*] parsed {len(mnem_at)} instructions, '
      f'{sum(len(v) for v in refs.values())} direct branch edges, '
      f'{len(refs)} distinct branch targets', file=sys.stderr)

addrs_sorted = sorted(mnem_at)

def last_insn_before(end):
    """The address of the last decoded instruction strictly below `end`."""
    i = bisect.bisect_left(addrs_sorted, end) - 1
    return addrs_sorted[i] if i >= 0 else None

TERMINATORS = {'ret', 'retn', 'retf', 'iretd', 'iret', 'jmp'}

d = json.load(open('analysis/functions.json'))
by_addr = {f['address']: f for f in d['functions']}
starts = sorted((f['address'], f['end']) for f in d['functions'] if f['entry_kind'] == 'start')
start_addrs = [a for a, _ in starts]

def owning_start(addr):
    i = bisect.bisect_right(start_addrs, addr) - 1
    if i < 0:
        return None
    a, e = starts[i]
    return (a, e) if a <= addr < e else None

aliases = [f for f in d['functions'] if f['entry_kind'] == 'alias']
pure_intra = {}   # addr -> Function, safe to exclude
cross_fn = {}      # addr -> Function, must stay dispatchable

for f in aliases:
    addr = f['address']
    owner = owning_start(addr)
    sources = refs.get(addr, [])
    if not sources or owner is None or not all(owner[0] <= s < owner[1] for s in sources):
        cross_fn[addr] = f
    else:
        pure_intra[addr] = f

# Fixpoint: any KEPT entry (start, or alias already in cross_fn) whose own body
# doesn't end in a terminator falls through past its own 'end'. If that 'end'
# address is one we excluded as pure_intra, it's actually needed as a dispatch
# target for the falling-through entry -- promote it. Promoting can itself
# create a new fallthrough dependency (a chain of shared tail blocks), so repeat
# until nothing changes.
kept = {a: (a, f['end']) for a, f in cross_fn.items()}
kept.update({a: (a, e) for a, e in starts})
changed = True
rounds = 0
while changed:
    changed = False
    rounds += 1
    for addr, (ea, end) in list(kept.items()):
        li = last_insn_before(end)
        if li is None or mnem_at.get(li) in TERMINATORS:
            continue
        if end in pure_intra:
            f = pure_intra.pop(end)
            cross_fn[end] = f
            kept[end] = (end, f['end'])
            changed = True

print(f'[*] fallthrough fixpoint took {rounds} rounds', file=sys.stderr)
print(f'[*] {len(aliases)} total aliases', file=sys.stderr)
print(f'[*] {len(pure_intra)} pure intra-function (safe to exclude -- goto covers them)', file=sys.stderr)
print(f'[*] {len(cross_fn)} cross-function or fallthrough-dependent (must stay dispatchable)', file=sys.stderr)

out = {'pure_intra': sorted(pure_intra), 'cross_fn': sorted(cross_fn)}
json.dump(out, open('analysis/alias_classification.json', 'w'), indent=1)
print('[*] wrote analysis/alias_classification.json', file=sys.stderr)
