---
name: playTimeline vs play dispatch separation
description: libkrkr2.so Player_playTimeline does NOT build node tree; it assumes play()/setMotion() already initialized labelMap. Two independent NCB entry points.
type: project
---

Player_playTimeline @ 0x672F70: pure timeline-state op. Looks up label in Player+117*8 hash bucket array (size Player+118), pushes node to active list [130..132], calls sub_670840 (init) + sub_671A50 (start). Throws "timeline label not found" if miss. No buildNodeTree, no initNonEmoteMotion, no motion loading.

NCB bindings in EmotePlayer_ncb_registerMembers @ 0x67FAC8:
- "play" → Player_play_NCBWrapper (0x67F40C) → Player_play (0x6B21E8) → Player_playImpl (0x6B2284) → Player_initNonEmoteMotion (0x6B365C) → Player_buildNodeTree_recursive (eager full chain)
- "playTimeline" → sub_672E44 (0x672E44) → Player_playTimeline (0x672F70) (NO build)
- "setMotion" → Player_setMotion_NCBWrapper (0x681CAC) → Player_play (same eager path)

**Why:** labelMap (Player+936 hash buckets) is only populated during buildNodeTree. playTimeline uses it as pre-existing state.

**How to apply:** Code or tests calling playTimeline must first invoke play()/setMotion() on the same Player. Web port's playCompat eager vs playTimeline lazy split is architecturally correct; callers (e.g. motion_playback_port.cpp) must order play()→playTimeline() like libkrkr2 expects. Do NOT add eager build to playTimeline to "fix" a caller that skipped play().

All xrefs to 0x672F70 confirm post-init assumption: D3DEmotePlayer_playTimeline (shell), sub_672E44 (NCB), sub_6736EC/sub_673944 (blend helpers on running Player), sub_678454 (state restore from Dict with label/flags/curTime already populated).
