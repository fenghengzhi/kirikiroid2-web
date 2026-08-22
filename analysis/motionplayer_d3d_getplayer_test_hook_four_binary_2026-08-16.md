# D3DEmotePlayer `getPlayer` test-hook provenance audit (2026-08-16)

## Finding

The portable public method named `D3DEmotePlayer::getPlayer()` was not an NCB
member and was not used by any production C++ path. All D3D implementation
methods already traverse the private unchecked
shell -> EmoteObject -> Engine -> Player chain through `player()`.

Fresh four-reference checks found:

- no UTF-16LE `getPlayer` literal in any binary;
- no function name matching `D3DEmotePlayer.*getPlayer` in any recovery IDB;
- no `getPlayer` entry in the complete 54-member D3DEmotePlayer registrar;
- exactly nine local calls, all in the motionplayer unit-test translation unit,
  plus the mutable/const inline declarations themselves.

| target | D3DEmotePlayer registrar | UTF-16LE `getPlayer` | recovered function-name hit |
|---|---:|---:|---:|
| Android arm64 | `0x52E8E4` | 0 | 0 |
| Android armv7 | `0x494078` | 0 | 0 |
| iOS arm64 | `0x100232278` | 0 | 0 |
| iOS armv7 | `0x230F46` | 0 | 0 |

Because an unused inline C++ accessor can be stripped completely, this evidence
does not justify claiming that no similarly shaped helper ever existed in the
unavailable source. It does prove that the current `getPlayer` spelling and its
public-API interpretation have no recovered provenance and that the local
method serves tests only.

The accessor was therefore renamed to
`playerForDifferentialTest_guess()`. The `_guess` suffix preserves the unknown
original-source boundary and the explicit test role prevents future code from
treating it as a recovered D3D API. Production methods and the 54-member script
surface are unchanged.

