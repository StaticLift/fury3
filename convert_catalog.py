import json
d = json.load(open('analysis/functions.json'))
# "start" entries are real, independently-callable function boundaries -- always kept.
#
# "alias" entries are jump/call targets that land inside another function's
# catalogued extent. disasm32 lumps two different situations under this one
# label, and they need opposite treatment:
#
#  - Pure intra-function labels (e.g. sub_0040B8EC): every branch that reaches
#    them originates from within their own enclosing "start" function's body
#    (an ordinary `jmp`/`jne` merge point). The enclosing function's own lift
#    already covers this address range and the lifter correctly turns the
#    branch into a plain `goto` -- so also cataloguing it as an independent
#    top-level function is actively harmful: the standalone copy starts
#    mid-function with none of the register/stack state the real control flow
#    established (e.g. ebp == 0), and if anything ever dispatches to it
#    directly the result is a crash (confirmed: ebp-relative read off ebp==0).
#
#  - True cross-function aliases (e.g. a shared DefWindowProcA-style tail
#    reached by an indirect tail-call from a *different* function): these are
#    genuinely shared code the lifter is designed to keep dispatchable
#    (disasm32.py's close_dispatch_targets docstring: "the lifter needs a
#    dispatchable body at that address or the branch is unresolved at
#    runtime"). Excluding these breaks every caller that jumps in from
#    outside -- confirmed via `RECOMP_ITAIL: unresolved VA` at runtime for
#    0x0044E8AC and 0x0040B86F once they were dropped.
#
# classify_aliases.py tells the two apart by cross-referencing every direct
# branch in the binary's disassembly against each alias's enclosing function.
cls = json.load(open('analysis/alias_classification.json'))
pure_intra = set(cls['pure_intra'])

starts = [f for f in d['functions'] if f.get('entry_kind', 'start') == 'start']
aliases_kept = [f for f in d['functions']
                if f.get('entry_kind') == 'alias' and f['address'] not in pure_intra]
excluded = [f for f in d['functions']
            if f.get('entry_kind') == 'alias' and f['address'] in pure_intra]

out = {'functions': [{'ea': f['address'], 'end': f['end'], 'entry_kind': f['entry_kind']}
                      for f in starts + aliases_kept]}
json.dump(out, open('analysis/ida_funcs.json', 'w'))
print(f"Wrote analysis/ida_funcs.json ({len(out['functions'])} functions: "
      f"{len(starts)} starts + {len(aliases_kept)} cross-function aliases kept dispatchable, "
      f"excluded {len(excluded)} pure intra-function labels)")
