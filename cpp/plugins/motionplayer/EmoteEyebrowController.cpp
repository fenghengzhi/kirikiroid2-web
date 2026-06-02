// EmoteEyebrowController — ctor (0x66480C) + step (sub_665600) faithful port.
//
// The "slim" sibling of EmoteBlinkController. See EmoteEyebrowController.h for
// the full structural-difference analysis (no blink machine, no RNG, no remap;
// value-track offsets swapped relative to the eye controller but semantically
// identical).

#include "EmoteEyebrowController.h"

#include <cmath>

#include "psbfile/PSBValue.h"

namespace motion {

    namespace {

        // Helpers mirroring the binary's Motion_propGet* on a PSB dict/list, with
        // the same default (0) when the key is absent. (Identical to the eye
        // slice's helpers.)

        int psbInt(const PSB::PSBDictionary* d, const char* key, int dflt = 0) {
            if (!d) return dflt;
            const auto v = (*d)[std::string(key)];
            if (const auto n = std::dynamic_pointer_cast<PSB::PSBNumber>(v)) {
                return static_cast<int>(n->getLongValue());
            }
            return dflt;
        }

        // sub_6637BC(arr, idx): reads element `idx` of a PSB array as an int.
        int psbArrayInt(const PSB::PSBList* arr, int idx) {
            if (!arr || idx < 0 || idx >= static_cast<int>(arr->size())) {
                return 0;
            }
            const auto v = (*arr)[idx];
            if (const auto n = std::dynamic_pointer_cast<PSB::PSBNumber>(v)) {
                return static_cast<int>(n->getLongValue());
            }
            return 0;
        }

    } // namespace

    // Aligned with libkrkr2.so EmoteBlinkController_ctor_slim_guess @ 0x66480C.
    // Decompiled pseudocode (this conversation):
    //   memset(self,0,0x50); EmoteAngleController_ctor_12Bdeque(self,0);  // +0
    //   memset(self+80,0,0x50); sub_6827A8(self+80,0);                    // +80
    //   memset(self+160,0,0x68); sub_6828FC(self+184,0);                  // edge/node
    //   self+264=0; self+272=0; self+280=0; self+296=0; self+304=0;       // cursors/state
    //   beginFrame = sub_6635DC("beginFrame");        // +328  (idx 82)
    //   for each "edge" elem: edgeTable.push({(float)elem[0], (float)elem[1]});
    //   for each "node" elem: nodeRows.push(vector<float>{ (float)elem[i] ... });
    //   trackValue = (float)beginFrame;               // +300 (idx 75)
    // NOTE: NO endFrame / blinkInterval* / blinkFrameCount / blinkEnabled reads,
    //   and NO RNG call. The slim controller has no blink state at all.
    void EmoteEyebrowController_ctor(EmoteEyebrowController* self,
                                     const PSB::PSBDictionary* dict) {
        // Embedded value-track deques default-construct empty (the binary's
        //   memset + EmoteAngleController_ctor_12Bdeque / sub_6827A8 / sub_6828FC
        //   leave them empty; std::deque/std::vector default ctors replicate this
        //   under the PLATFORM_BOUNDARY ABI note).
        EmoteAngleController_ctor(&self->valueTrack12B, 0); // 0x664858

        // beginFrame (the ONLY scalar field read; +328, idx 82).        /*0x664938*/
        self->beginFrame = psbInt(dict, "beginFrame");

        // "edge" array -> edgeTable of {x,y} pairs (each elem a 2-int sub-array).
        //   sub_56C694(edge) = count; loop sub_6637BC(elem,0)/(elem,1).  /*0x6649d4*/
        const PSB::PSBList* edge = nullptr;
        if (dict) {
            edge = dynamic_cast<const PSB::PSBList*>(
                (*dict)[std::string("edge")].get());
        }
        if (edge) {
            const int count = static_cast<int>(edge->size()); // sub_56C694
            self->edgeTable.reserve(static_cast<size_t>(count));
            for (int i = 0; i < count; ++i) {
                const auto sub = dynamic_cast<const PSB::PSBList*>(
                    (*edge)[i].get());
                const int x = psbArrayInt(sub, 0); // sub_6637BC(elem,0)
                const int y = psbArrayInt(sub, 1); // sub_6637BC(elem,1)
                self->edgeTable.emplace_back(static_cast<float>(x),
                                             static_cast<float>(y));
            }
        }

        // "node" array -> nodeRows: each elem is a sub-array; push a row of its
        //   int->float values (the binary builds a vector<float> per node into
        //   the 504-block deque @+184).                                  /*0x664c5c*/
        const PSB::PSBList* node = nullptr;
        if (dict) {
            node = dynamic_cast<const PSB::PSBList*>(
                (*dict)[std::string("node")].get());
        }
        if (node) {
            const int nodeCount = static_cast<int>(node->size()); // sub_56C694
            for (int i = 0; i < nodeCount; ++i) {
                const auto sub = dynamic_cast<const PSB::PSBList*>(
                    (*node)[i].get());
                std::vector<float> row;
                const int rowCount = sub ? static_cast<int>(sub->size()) : 0;
                row.reserve(static_cast<size_t>(rowCount));
                for (int j = 0; j < rowCount; ++j) {
                    row.push_back(static_cast<float>(psbArrayInt(sub, j)));
                }
                self->nodeRows.push_back(std::move(row));
            }
        }

        // trackValue = (float)beginFrame.   *((float*)v3+75) = (float)*((int*)v3+82)
        self->trackValue = static_cast<float>(self->beginFrame); // +300 /*0x664f68*/
    }

    // Aligned with libkrkr2.so sub_665600 EmoteVarController5_step @ 0x665600.
    // Decompiled pseudocode (this conversation):
    //   v5 = trackState(+296);
    //   if (v5) {
    //     if (v5 == 2) {                                  // animating
    //       v14 = pow(accum(+312)/span(+316), 1/pow(+320)) + invDur(+324)*dt;
    //       v15 = pow(v14, pow);  v18 = v15*span - accum;
    //       trackValue(+300) += dir(+308) * v18;
    //       if (overshoot) { v18 = (target-trackValue)*dir;
    //                        trackValue = target; trackState = 1; }
    //       accum(+312) = span(+316)... wait: *(a1+312) = span(+316) + v18;
    //     } else if (v5 == 1) {                           // pop-pending (single)
    //       if (8B-track @+96 empty) trackState = 0;
    //       else { v9=track[0]; v8=track[1];
    //              if (v9==v8) trackValue=v8;
    //              else { target=v8; trackValue=v9;
    //                     dir = (v8-v9<0)?-1:1; trackState=2; }
    //              pop_front 8B-track; }
    //     }
    //   } else {                                          // state 0: setup
    //     if (12B-track @+16 non-empty) {
    //       endVal = *(elem); dur = elem[1]; pow = elem[2];
    //       sub_661F7C(self+160,self+80,trackValue,endVal);   // mesh resolver (SCOPE BOUNDARY)
    //       accum(+312)=0; span(+316)=resolvedSpan(+288); invDur(+324)=1/dur;
    //       pow(+320)=powCount; pop_front 12B-track; ++trackState;
    //     }
    //   }
    //   *out = trackValue(+300);   // NO blink machine, NO remap
    void EmoteEyebrowController_step(EmoteEyebrowController* self, float* out,
                                     float dt) {
        const float a3 = dt;
        const int v5 = self->trackState; // *(a1+296)  /*0x665618*/

        if (v5) {                                          // /*0x665624*/
            if (v5 == 2) {                                 // /*0x66562c*/ animating
                // v14 = pow(accum(+312)/span(+316), 1/pow(+320)) + invDur(+324)*dt
                //   (the binary's 1.0/*(a1+320) is 1/pow, *(a1+324)*a3 is invDur*dt).
                //                                                      /*0x6656fc*/
                const float v14 =
                    std::pow(self->trackAccum / self->trackSpan,
                             1.0f / static_cast<float>(self->trackPow)) +
                    (self->trackInvDur * a3);
                const double v15 = std::pow(v14,
                    static_cast<float>(self->trackPow)); // double /*0x665708*/
                const float v16 = self->trackAccum;      // *(a1+312)  /*0x665710*/
                const float v17 = self->trackDir;        // *(a1+308)  /*0x665714*/
                // v18 = v15*span(+316) - accum(+312).               /*0x66572c*/
                float v18 = static_cast<float>(
                    (v15 * self->trackSpan) - v16);
                const float v19 = self->trackValue + (v17 * v18); // /*0x665734*/
                self->trackValue = v19;                  // *(a1+300)=v19 /*0x66573c*/

                bool overshoot = false;
                float v20 = 0.0f;
                if (v17 > 0.0f) {                          // /*0x665760*/
                    v20 = self->trackTarget;               // *(a1+304)
                    overshoot = (v20 <= v19);
                } else if (v17 < 0.0f) {
                    v20 = self->trackTarget;               // *(a1+304)
                    overshoot = (v20 >= v19);
                }
                if (overshoot) {                           // /*0x665760*/
                    v18 = (v20 - v19) * v17;               // /*0x66576c*/
                    self->trackValue = v20;                // *(a1+300)=v20 /*0x665770*/
                    self->trackState = 1;                  // *(a1+296)=1 /*0x665774*/
                }
                // *(a1+312) = accum(+312) + v18 (v16 + v18; write back into the
                //   accum field +312).                                /*0x66577c*/
                self->trackAccum = v16 + v18;
            } else if (v5 == 1) {                          // /*0x665634*/ pop-pending
                // v7 = *(a1+96) (8B-track cursor).                    /*0x66563c*/
                if (self->valueTrack8B.empty()) {          // *(a1+128)==*(a1+96) /*0x665644*/
                    self->trackState = 0;                  // *(a1+296)=0 /*0x665784*/
                } else {
                    const std::pair<float, float> elem =
                        self->valueTrack8B.front();         // v9=*v7; v8=v7[1] /*0x665648*/
                    const float v9 = elem.first;            // first
                    const float v8 = elem.second;           // second
                    if (v9 == v8) {                         // *v7 == v8 /*0x665650*/
                        self->trackValue = v8;              // *(a1+300)=v8 /*0x665654*/
                    } else {
                        self->trackTarget = v8;             // *(a1+304)=v8 /*0x66578c*/
                        self->trackValue  = v9;             // *(a1+300)=v9 /*0x665794*/
                        self->trackDir =
                            ((v8 - v9) < 0.0f) ? -1.0f : 1.0f; // *(a1+308) /*0x6657a8*/
                        self->trackState = 2;               // *(a1+296)=2 /*0x6657b0*/
                    }
                    self->valueTrack8B.pop_front();         // advance +96 / free block /*0x6657c0*/
                }
            }
        } else {                                            // /*0x665660*/ state 0: setup
            if (!self->valueTrack12B.queue.empty()) {       // *(a1+48)!=*(a1+16) /*0x665668*/
                const EmoteAngleKeyValue12B kf =
                    self->valueTrack12B.queue.front();      // {endRad,dur,pow} /*0x665670*/

                // SCOPE BOUNDARY (sub_661F7C @0x661F7C -> sub_660028 mesh
                //   resolver) — identical boundary to the eye slice. The binary
                //   calls sub_661F7C(self+160, self+80, trackValue, kf.endRad) to
                //   rebuild the 8B value track (valueTrack8B) from the resolved
                //   eyebrow mesh rows (edgeTable + nodeRows). NOT ported here; the
                //   call site is kept as a documented anchor. Consequence: the 8B
                //   track is not repopulated, so on the NEXT step the v5==1 branch
                //   sees an empty track and returns to trackState 0 — i.e. the
                //   track interpolation is inert until the resolver lands, while
                //   trackValue holds the popped position.       /*0x66567c*/
                // resolveMeshTrack_0x661F7C(self, self->trackValue, kf.endRad);

                // accum(+312)=0; span(+316)=resolvedSpan(+288);
                //   invDur(+324)=1/dur; pow(+320)=powCount.       /*0x665680..*/
                self->trackAccum  = 0.0f;                    // *(a1+312)=0  /*0x665684*/
                self->trackSpan   = self->trackResolvedSpan; // *(a1+316)=*(a1+288) /*0x665690*/
                self->trackInvDur = 1.0f / kf.duration;      // *(a1+324)=1/*(v10+4) /*0x6656a4*/
                self->trackPow    = static_cast<int32_t>(kf.powCount); // *(a1+320)=*(v10+8) /*0x6656b0*/

                self->valueTrack12B.queue.pop_front();       // advance +16 / free block /*0x6656b4*/
                self->trackState = self->trackState + 1;     // ++*(a1+296) /*0x6657fc*/
            }
        }

        // *out = trackValue. No blink machine, no remap (unlike the eye step).
        *out = self->trackValue;                            // *(a1+300) /*0x66582c*/
    }

} // namespace motion
