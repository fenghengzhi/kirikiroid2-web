// EmoteMeshResolver — faithful port of sub_661F7C (@0x661F7C) and its engine
//   sub_660028 (@0x660028). See EmoteMeshResolver.h for the high-level contract.
//
// This is a literal, branch-for-branch transcription of the libkrkr2.so engine.
// The binary uses libstdc++ std::deque<float> / std::vector<float> with byte-
// level cursor arithmetic; per CLAUDE.md byte-layout methodology those map
// directly to std::deque<float> / std::vector<float> here (push_back / pop_back
// / front / back / clear), and the libstdc++ deque size()/index magic reduces to
// plain .size() / operator[]. Float element values are the only cross-platform
// data contract; ABI offsets are never reproduced.
//
// The four engine states (v22 & 3 in the binary) are kept as separate code
// paths exactly as decompiled — they are NOT merged, because each selects a
// different driving value (a2 / v19 / a3) and matches a different edge endpoint
// (edge.lo vs edge.hi), with distinct emit conditions (LABEL_268 / LABEL_317 /
// LABEL_183 / LABEL_199 / LABEL_447). Decompile line refs are in comments.

#include "EmoteMeshResolver.h"

#include <cmath>

namespace motion {

    namespace {

        // edgeFind: first edge index i with edge[i].lo <= X && edge[i].hi >= X,
        //   else -1. The binary scans (edge+4)=hi first then edge+0=lo; condition
        //   is `lo <= X && hi >= X` (decompile 0x660ca0/0x660de4/0x660f30/...).
        int edgeFind(const std::vector<std::pair<float, float>>& edge, float X) {
            const int count = static_cast<int>(edge.size());
            for (int i = 0; i < count; ++i) {
                if (edge[i].first <= X && edge[i].second >= X) {
                    return i;
                }
            }
            return -1;
        }

        // visitedContains: is `val` already in the visited-value stack? The binary
        //   linear-scans v16..v350 (the manually-grown vector<float>).
        bool visitedContains(const std::vector<float>& visited, float val) {
            for (float f : visited) {
                if (f == val) {
                    return true;
                }
            }
            return false;
        }

        // visitedErase: remove the first occurrence of `val` (binary memmove
        //   compaction at LABEL_295/LABEL_304, 0x66112c/0x661190).
        void visitedErase(std::vector<float>& visited, float val) {
            for (std::size_t k = 0; k < visited.size(); ++k) {
                if (visited[k] == val) {
                    visited.erase(visited.begin() + static_cast<long>(k));
                    return;
                }
            }
        }

        // Engine scratch state for one search (the binary's stack-locals: two
        //   deques p / v351, the manual vector v16, dist `i`).
        struct Engine {
            EmoteMeshResolverState* self;
            std::deque<std::pair<float, float>> pathSeg; // p   (segments {from,to})
            std::deque<float> valStack;                  // v351 (value backtrack stack)
            std::vector<float> visited;                  // v16 (values already on path)
            float dist = 0.0f;                           // i   (accumulated distance)

            // Emit the current pathSeg as one 88-byte output row with distance d
            //   (sub_686FEC: copy pathSeg's flattened floats into row.path, set
            //   row.dist = d). pathSeg holds {from,to} pairs; the binary stores the
            //   deque<float> flattened, so we flatten here.
            void emitRow(float d) {
                MeshPathRow row;
                row.path = pathSeg; // copy the {from,to} pair-deque (sub_687234)
                row.dist = d;
                self->outputRows.push_back(std::move(row));
            }

            // Does an output row with distance == d already exist? (dedup loop
            //   0x6612a0: v222 |= (i == row[+80])).
            bool rowWithDist(float d) const {
                for (const MeshPathRow& r : self->outputRows) {
                    if (r.dist == d) {
                        return true;
                    }
                }
                return false;
            }
        };

    } // namespace

    // Aligned with libkrkr2.so sub_660028 @0x660028 — bounded DFS path search
    //   from startValue (a2) to endValue (a3) through the edge/node value graph.
    //   Fills self->outputRows with one MeshPathRow per discovered path.
    static void EmoteMeshResolver_search(EmoteMeshResolverState* self,
                                         float a2, float a3) {
        Engine eng;
        eng.self = self;

        const std::vector<std::pair<float, float>>& edge = self->edgeTable;
        const std::deque<std::vector<float>>& node = self->nodeRows;

        float v19 = a2;     // current value
        int v17 = 0;        // v17 — "previous state" carry
        int v18 = 0;        // outer-iteration counter (cap: v18++ <= 8 => 9 passes)
        bool v20 = true;    // keepGoing (LABEL_328 gate)
        int v148 = 0;       // function-scope "current pass state" carry (binary v148)

        // LABEL_328 — restart a fresh DFS pass with state 0.
        while (v20) { // 0x6613d8: if(!v20) -> cleanup
            int v22 = 0; // state (0x660164)

            for (;;) { // LABEL_215 / while(1) @0x660c68
                const int s = v22 & 3;       // v147
                v148 = v17;                  // 0x660c70 (function-scope carry)

                if (s == 1) {
                    // ---- Block A (v147==1): is v19 in the SAME edge as a3? ----
                    //   0x660f10. v188 = edgeFind(v19); v23 = edgeFind(a3).
                    const int v188 = edgeFind(edge, v19);
                    const int v23 = edgeFind(edge, a3);
                    if (v188 != v23) {
                        // Different edges: descend. v17==1 -> state 2 else state 3.
                        v22 = (v148 == 1) ? 2 : 3; // 0x660170
                        v17 = v148;                // 0x660174
                        continue;                  // 0x660178
                    }
                    // Same edge: close the path with the final {v19,a3} segment.
                    const int v205 = edgeFind(edge, v19); // 0x6610dc
                    if (v205 >= 0) {
                        // Remove edge[v205].lo and .hi from visited
                        //   (LABEL_295 / LABEL_304).
                        visitedErase(eng.visited, edge[v205].first);
                        visitedErase(eng.visited, edge[v205].second);
                    }
                    // dist += |a3 - v19|; push {v19,a3} segment (LABEL_304/0x6611d4).
                    float v216 = a3 - v19;
                    if (v216 < 0.0f) {
                        v216 = -v216;
                    }
                    eng.dist = v216 + eng.dist;
                    eng.pathSeg.emplace_back(v19, a3);

                    // Emit unless a row with this exact distance already exists
                    //   (0x661288 dedup); LABEL_317 emits.
                    if (!eng.rowWithDist(eng.dist)) {
                        eng.emitRow(eng.dist);
                    }
                    // v20 = valStack not empty (0x6613d0).
                    v20 = !eng.valStack.empty();
                    // LABEL_278: bump iteration counter, restart pass.
                    const bool cont = (v18++ <= 8); // 0x6610b4
                    v17 = v148;                     // 0x6610bc
                    if (!cont) {
                        goto cleanup;               // 0x6610c4
                    }
                    goto next_pass;                 // 0x6610c4 -> LABEL_328
                }

                if (s == 2) {
                    // ---- Block B (v147==2): explore neighbours of v19 ----
                    //   0x660dc8. v168 = edgeFind(v19); match edge[v168].hi in nodes.
                    const int v168 = edgeFind(edge, v19);
                    const int nodeCount = static_cast<int>(node.size());
                    if (nodeCount == 0) {
                        goto backtrack; // LABEL_183 (0x660e48 empty-deque guard)
                    }
                    const float v175 = (v168 >= 0) ? edge[v168].second : 0.0f; // 0x660e58 edge.hi
                    bool advanced = false;
                    for (int v176 = 0; v176 < nodeCount; ++v176) { // 0x660e6c
                        // Find the node row that CONTAINS v175 (LABEL_251).
                        const std::vector<float>& row = node[v176];
                        bool found = false;
                        for (float f : row) { // 0x660eec
                            if (f == v175) { found = true; break; }
                        }
                        if (!found) {
                            continue; // LABEL_254 (0x660f08)
                        }
                        // node[v176] is the graph node for v175; walk its neighbours
                        //   (LABEL_68, re-reading node[v176] as the neighbour list).
                        const std::vector<float>& neigh = node[v176]; // 0x660434
                        for (float v59 : neigh) { // 0x660450
                            // Skip neighbours already visited.
                            if (visitedContains(eng.visited, v59)) {
                                continue; // 0x660484 (v60 set -> skip)
                            }
                            // First unvisited neighbour drives the step. (The binary
                            //   breaks the inner loop on the first unvisited.)
                            if (v59 != -1.0f) { // 0x66049c
                                // dist += |v175 - v19|; push {v19,v175}; push v175
                                //   to visited & valStack; recurse with v19=v59.
                                float v64 = v175 - v19;
                                if (v64 < 0.0f) { v64 = -v64; }
                                eng.dist = v64 + eng.dist;            // 0x6604c0
                                eng.pathSeg.emplace_back(v19, v175);  // 0x660520
                                eng.visited.push_back(v59);           // 0x660554 (v59 onto visited)
                                eng.valStack.push_back(v19);          // 0x660668 (v19 onto stack)
                                v22 = 1; // 0x660688
                                v17 = v148;
                                v19 = v59; // 0x660690
                                a2 = v59;  // 0x660694
                                advanced = true;
                            }
                            break; // inner neighbour loop steps once
                        }
                        if (advanced) {
                            break;
                        }
                        // neighbour list exhausted without an unvisited target.
                        goto backtrack; // LABEL_183 (0x660490 / fallthrough)
                    }
                    if (advanced) {
                        continue; // LABEL_215
                    }
                    goto backtrack; // LABEL_183 (no node matched)
                }

                if (s == 3) {
                    break; // 0x660c84 -> tail (from-a2 restart)
                }

                // ---- State 0 (init): clear scratch, seed from v19 ----
                eng.pathSeg.clear(); // 0x66018c-0x6601dc
                eng.dist = 0.0f;     // i = 0.0 (0x66019c)
                eng.valStack.clear();// 0x6601e8-0x66022c
                {
                    const int idxV19 = edgeFind(edge, v19); // 0x660248
                    if (idxV19 >= 0) {
                        const int idxA3 = edgeFind(edge, a3); // 0x660294 (search vs a3)
                        if (idxA3 >= 0) {
                            v22 = 1; // 0x6602c0
                            v17 = 1; // 0x6602c8
                            continue;// 0x6602cc -> LABEL_215
                        }
                    }
                }
                goto emit_label_268;
            } // for(;;) LABEL_215

            // ===== tail (state 3): restart search from a2 =====
            // Clear scratch (the tail re-derives from a2). 0x660c88 onward.
            {
                eng.pathSeg.clear();
                eng.valStack.clear();
                // v151 = edgeFind(a2). 0x660ca0.
                const int v151 = edgeFind(edge, a2);
                const int nodeCount = static_cast<int>(node.size());
                if (nodeCount == 0 || v151 < 0) {
                    goto emit_label_199; // 0x660d04 empty / no edge
                }
                const float v156 = edge[v151].first; // 0x660d10 edge.lo
                // Find node containing v156 (LABEL_232).
                int v157 = -1;
                for (int k = 0; k < nodeCount; ++k) { // 0x660d24
                    const std::vector<float>& row = node[k];
                    bool found = false;
                    for (float f : row) { if (f == v156) { found = true; break; } } // 0x660da4
                    if (found) { v157 = k; break; }
                }
                if (v157 < 0) {
                    goto emit_label_199; // 0x6606a4 (-1)
                }
                // v156 onto visited (0x66072c). Then walk node[v157] neighbours.
                eng.visited.push_back(v156);
                {
                    const std::vector<float>& neigh = node[v157]; // 0x660800
                    bool advanced = false;
                    for (float v107 : neigh) { // 0x66081c
                        if (visitedContains(eng.visited, v107)) {
                            continue; // 0x660850
                        }
                        if (v107 != -1.0f) { // 0x660868
                            float v111 = v156 - a2;
                            if (v111 < 0.0f) { v111 = -v111; }
                            eng.dist = v111 + eng.dist;           // 0x66088c
                            eng.pathSeg.emplace_back(a2, v156);   // 0x6608ec
                            eng.visited.push_back(v107);          // 0x660920
                            eng.valStack.push_back(a2);           // 0x660a34
                            v22 = 1; // 0x660a54
                            v17 = v148;
                            v19 = v107; // 0x660a5c
                            a2 = v107;   // 0x660a60
                            advanced = true;
                        }
                        break; // step once
                    }
                    if (advanced) {
                        continue; // LABEL_215
                    }
                }
                goto emit_label_199;
            }

        emit_label_268:
            // LABEL_268 (0x660f98): emit current path with dist = -1 sentinel,
            //   keepGoing=0, v17=1, then LABEL_278.
            eng.dist = -1.0f;
            eng.emitRow(eng.dist);
            v20 = false;       // 0x6610ac
            v17 = 1;           // 0x6610b0
            {
                const bool cont = (v18++ <= 8);
                if (!cont) { goto cleanup; }
                goto next_pass;
            }

        emit_label_199:
            // LABEL_199 (0x660b70): if valStack non-empty, backtrack one step and
            //   set state 3; else fall through to LABEL_447 (final emit).
            if (!eng.valStack.empty()) {
                // v19 = valStack.back(); pop. (0x660ba4)
                v19 = eng.valStack.back();
                eng.valStack.pop_back();
                if (!eng.pathSeg.empty()) {
                    // dist -= |last.to - last.from|; pop the segment. (0x660bd4)
                    const std::pair<float, float> last = eng.pathSeg.back();
                    float v142 = last.second - last.first;
                    if (v142 < 0.0f) { v142 = -v142; }
                    eng.dist = eng.dist - v142;
                    eng.pathSeg.pop_back();
                }
                a2 = v19;     // 0x660c2c
                // pop valStack once more (0x660c34/0x660c58 second pop).
                if (!eng.valStack.empty()) {
                    eng.valStack.pop_back();
                }
                v22 = 3; // 0x660c60
                v17 = v148;
                continue; // LABEL_215
            }
            // valStack empty: run the final "from-a3" close block (0x6613e0)
            //   before LABEL_447. Driven by a3 and edge.hi, mirroring the tail's
            //   from-a2 block.
            {
                const int v239 = edgeFind(edge, a3); // 0x6613f8 edge containing a3
                const int nodeCount2 = static_cast<int>(node.size());
                if (nodeCount2 == 0 || v239 < 0) {
                    goto emit_label_447; // 0x661464 (v242==21 empty) / no edge
                }
                const float v244 = edge[v239].second; // 0x661478 edge.hi
                // Find node containing v244 (LABEL_344).
                int v245 = -1;
                for (int k = 0; k < nodeCount2; ++k) { // 0x6614a4
                    const std::vector<float>& row = node[k];
                    bool found = false;
                    for (float f : row) { if (f == v244) { found = true; break; } } // 0x661500
                    if (found) { v245 = k; break; }
                }
                if (v245 < 0) {
                    goto emit_label_447; // 0x661528 (-1)
                }
                // Scan node[v245] for the first value not already visited; that
                //   becomes v21 (the close value). (LABEL_356, 0x6615d0.)
                float v21 = -1.0f; // default sentinel (0x6617d0 LODWORD(v15)=-1)
                {
                    const std::vector<float>& row = node[v245];
                    for (float f : row) { // 0x6615d0
                        if (visitedContains(eng.visited, f)) {
                            continue; // 0x661604
                        }
                        v21 = f; // 0x661618
                        break;
                    }
                }
                // v244 onto visited (0x6616a0). (LABEL_368.)
                eng.visited.push_back(v244);
                // Walk node[v245] neighbours (LABEL_394, 0x661788) for the first
                //   unvisited; emit a segment if it is not -1.
                {
                    const std::vector<float>& neigh = node[v245]; // 0x66176c
                    float v290 = -1.0f;
                    bool sawNeighbour = false;
                    for (float f : neigh) { // 0x661788
                        if (visitedContains(eng.visited, f)) {
                            continue; // 0x6617bc
                        }
                        v290 = f;
                        sawNeighbour = true;
                        break;
                    }
                    if (sawNeighbour && v290 != -1.0f) { // 0x6617d8 (else-branch)
                        // dist += |v21 - v19|; push {v19,v21}; push v19 onto
                        //   valStack (a2 onto value-stack). (0x6617e8.)
                        float v294 = v21 - v19;
                        if (v294 < 0.0f) { v294 = -v294; }
                        eng.dist = v294 + eng.dist;          // 0x661800
                        eng.pathSeg.emplace_back(v19, v21);  // 0x661970
                        eng.valStack.push_back(a2);          // 0x661aac (a2 pushed)
                        eng.visited.push_back(v290);         // 0x6619a4
                    }
                    // Either way fall to LABEL_409 / LABEL_447 (final emit).
                }
            }
            goto emit_label_447;

        backtrack:
            // LABEL_183 (0x660a6c): same backtrack shape as LABEL_199 but sets
            //   state 3 (if stack non-empty) or 2 fall-through.
            v17 = -1; // 0x660a74
            v22 = 3;  // 0x660a7c
            if (!eng.valStack.empty()) { // 0x660a80
                v19 = eng.valStack.back();
                eng.valStack.pop_back();
                if (!eng.pathSeg.empty()) {
                    const std::pair<float, float> last = eng.pathSeg.back();
                    float v134 = last.second - last.first;
                    if (v134 < 0.0f) { v134 = -v134; }
                    eng.dist = eng.dist - v134;
                    eng.pathSeg.pop_back();
                }
                a2 = v19; // 0x660b30
                if (!eng.valStack.empty()) {
                    eng.valStack.pop_back();
                }
                v22 = 2;  // 0x660b64
                v17 = v148;
            }
            continue; // LABEL_215 (0x660b6c)

        emit_label_447:
            // LABEL_447 (0x661acc): final emit of the current path with dist=-1,
            //   then cleanup.
            eng.dist = -1.0f;
            eng.emitRow(eng.dist);
            goto cleanup;

        next_pass:
            continue; // outer while -> LABEL_328
        } // while(v20)

    cleanup:
        // Scratch deques/vector free at function exit — handled by destructors.
        (void)a2;
        (void)v18;
        (void)v17;
        (void)v148;
    }

    // Aligned with libkrkr2.so sub_661F7C @0x661F7C.
    void EmoteMeshResolver_resolve(EmoteMeshResolverState* self,
                                   std::deque<std::pair<float, float>>* valueTrack8B,
                                   float startValue, float endValue) {
        // Clear the 88-byte output-row vector (0x661f9c loop frees each row's
        //   deque; here destructors do it). a1 = self.
        self->outputRows.clear();

        // Reset the 8B value-track deque to empty (0x662000-0x662038 collapses
        //   a2's finish back onto its start, freeing extra nodes).
        valueTrack8B->clear();

        // Run the path search (sub_660028(a1, a3=startValue? — see note)).
        //   NOTE on argument order: sub_661F7C(a1, a2, a3=trackValue, a4=endVal)
        //   calls sub_660028(a1, a3, a4) i.e. (self, trackValue, endVal). So the
        //   engine's a2 = startValue (current trackValue), a3 = endValue.
        EmoteMeshResolver_search(self, startValue, endValue);

        // Selector: pick the output row with minimum dist != -1 (0x662058 loop:
        //   v26 = 99999.0; scan row+80, skip -1, track min). v30 = best index.
        float bestDist = 99999.0f; // v26
        int best = -1;             // v30
        const int rowCount = static_cast<int>(self->outputRows.size());
        for (int i = 0; i < rowCount; ++i) { // 0x66207c, stride 88 (=22 floats)
            const float d = self->outputRows[i].dist; // *(row+80)
            const bool notSentinel = (d != -1.0f);     // v33
            const bool less = (d < bestDist);          // v34
            if (notSentinel && less) {
                best = i;       // v30 = v27
                bestDist = d;   // v26 = v32
            }
        }

        if (best == -1) {
            // LABEL_37 (0x66220c): no valid row -> push a single {endVal,endVal}
            //   pair into valueTrack8B and trackResolvedSpan = 0.
            valueTrack8B->emplace_back(endValue, endValue);
            self->trackResolvedSpan = 0.0f; // *(a1+128) = 0
            return;
        }

        // Valid best row (0x6620b4): trackResolvedSpan = bestDist; load the row's
        //   flattened float path into valueTrack8B as {from,to} pairs.
        self->trackResolvedSpan = bestDist; // *(float*)(a1+128) = v26
        const MeshPathRow& row = self->outputRows[best];
        // The binary resizes a2's deque (sub_6622AC) then copies the row's
        //   pair-deque element-for-element into it (else-branch loop @0x662148,
        //   index-aligned a2[k] = row[k]). Plain push of the pairs in order.
        for (const std::pair<float, float>& seg : row.path) {
            valueTrack8B->push_back(seg);
        }
    }

} // namespace motion
