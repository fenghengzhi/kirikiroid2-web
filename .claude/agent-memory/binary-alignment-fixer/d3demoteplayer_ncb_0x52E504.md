---
name: d3demoteplayer-ncb-0x52E504
description: D3DEmotePlayer NCB table @0x52E504 has 4 deliberate NAME/callback mismatches; real names are NOT registered
metadata:
  type: project
---

D3DEmotePlayer_ncb_registerMembers @0x52E504 registers 4 callbacks under
MISMATCHED member names. The "real" names are ABSENT from the table — any
port binding under the real name is a port invention and must be removed.

**Why:** Binary is ground truth for member names (CLAUDE.md IDA symbol rule).
Enumerated every ncb_addMember/wrapper NAME string in @0x52E504.

**How to apply:** these 4 keep ONLY the mismatched-name binding in main.cpp:
- `bustScale` (@0x52eb08) → set/getBodyScale cb  (real name `bodyScale` ABSENT)
- `modified` (@0x52f824) → getPlayCallback RO prop (real `playCallback` ABSENT)
- `setTimelineBlendRatio` (@0x52f53c) → setTimeline cb (real `setTimeline` ABSENT)
- `pass` (@0x52f730) → addPlayCallback cb (real `addPlayCallback` ABSENT)
Also `queing`(@0x52e9a0)→set/getBustScale, `clear`(@0x52e680)→create cb — same
mismatch pattern, already aligned.

Other absent-from-table port inventions already removed earlier (M11 D-01):
useD3D, drawvisible/drawOpacity/opengl, setMirror, initPhysics, play, draw.
progress IS a real method member (@0x52f7b0), not a property.
