// EmoteBlinkController — ctor (0x662968) + step (sub_663BDC) faithful port.

#include "EmoteBlinkController.h"
#include "EmoteBlinkRng.h"

#include <cmath>
#include <cstring> // std::memcpy for the raw-bits trackPow reinterpret

#include "psbfile/PSBValue.h"

namespace motion {

    namespace {

        // Helpers mirroring the binary's Motion_propGet* on a PSB dict/list.
        // The binary calls iTJSDispatch2::PropGet/GetCount on the live TJS
        // dispatch; locally the same data is a parsed PSB object. These extract
        // the same scalar with the same default (0) when the key is absent.

        int psbInt(const PSB::PSBDictionary* d, const char* key, int dflt = 0) {
            if (!d) return dflt;
            const auto v = (*d)[std::string(key)];
            if (const auto n = std::dynamic_pointer_cast<PSB::PSBNumber>(v)) {
                return static_cast<int>(n->getLongValue());
            }
            return dflt;
        }

        double psbDouble(const PSB::PSBDictionary* d, const char* key,
                         double dflt = 0.0) {
            if (!d) return dflt;
            const auto v = (*d)[std::string(key)];
            if (const auto n = std::dynamic_pointer_cast<PSB::PSBNumber>(v)) {
                switch (n->numberType) {
                    case PSB::PSBNumberType::Float:
                        return n->getFloatValue();
                    case PSB::PSBNumberType::Double:
                        return n->getValue<double>();
                    default:
                        return static_cast<double>(n->getLongValue());
                }
            }
            return dflt;
        }

        bool psbBool(const PSB::PSBDictionary* d, const char* key,
                     bool dflt = false) {
            if (!d) return dflt;
            const auto v = (*d)[std::string(key)];
            if (const auto b = std::dynamic_pointer_cast<PSB::PSBBool>(v)) {
                return b->value;
            }
            if (const auto n = std::dynamic_pointer_cast<PSB::PSBNumber>(v)) {
                return n->getLongValue() != 0;
            }
            return dflt;
        }

        // sub_6637BC(arr, idx): reads element `idx` of a PSB array as an int.
        // (The binary's sub_6637BC dispatches on the variant type and coerces to
        //  i32; for PSB the element is a PSBNumber.)
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

    // Aligned with libkrkr2.so EmoteBlinkController_ctor @ 0x662968.
    // Decompiled pseudocode (this conversation):
    //   memset(self,0,0x50); EmoteAngleController_ctor_12Bdeque(self,0); // +0 track
    //   memset(self+80,0,0x50); sub_6827A8(self+80,0);                   // +80 track
    //   memset(self+160,0,0x68); sub_6828FC(self+184,0);                 // edge/node
    //   beginFrame = propGetInt("beginFrame");   // +328  (idx 82)
    //   endFrame   = propGetInt("endFrame");      // +332  (idx 83)
    //   blinkIntervalMin = (float)propGetDouble("blinkIntervalMin"); // +340 (idx85)
    //   blinkIntervalMax = (float)propGetDouble("blinkIntervalMax"); // +344 (idx86)
    //   blinkFrameCount  = (float)propGetDouble("blinkFrameCount");  // +348 (idx87)
    //   blinkEnabled = propGetBool("blinkEnabled") & 1;              // +360
    //   trackValue = blinkPos = (float)beginFrame;        // +300 (idx75) / +356 (idx89)
    //   blinkTimer = min + (max-min)*rand();              // +352 (idx88)  rand=sub_9F17D0(sub_9F1A08())
    //   for each "edge" elem: edgeTable.push({(float)elem[0], (float)elem[1]});
    //   for each "node" elem: nodeRows.push(vector<float>{ (float)elem[i] ... });
    void EmoteBlinkController_ctor(EmoteBlinkController* self,
                                   const PSB::PSBDictionary* dict) {
        // Embedded value-track deques default-construct empty (the binary's
        //   memset + EmoteAngleController_ctor_12Bdeque / sub_6827A8 / sub_6828FC
        //   leave them empty; std::deque/std::vector default ctors replicate this
        //   under the PLATFORM_BOUNDARY ABI note). valueTrack12B is constructed
        //   by its in-class member; mirror the ctor's explicit init:
        EmoteAngleController_ctor(&self->valueTrack12B, 0); // 0x6629b8

        // Blink scalar fields.
        self->beginFrame = psbInt(dict, "beginFrame");          // +328
        self->endFrame   = psbInt(dict, "endFrame");            // +332
        self->blinkIntervalMin =
            static_cast<float>(psbDouble(dict, "blinkIntervalMin")); // +340
        self->blinkIntervalMax =
            static_cast<float>(psbDouble(dict, "blinkIntervalMax")); // +344
        self->blinkFrameCount =
            static_cast<float>(psbDouble(dict, "blinkFrameCount"));  // +348
        self->blinkEnabled =
            psbBool(dict, "blinkEnabled") ? 1u : 0u;            // +360 (v9 & 1)

        const float v10 = self->blinkIntervalMin; // *((float*)v3+85)
        const float v11 = self->blinkIntervalMax; // *((float*)v3+86)
        const float v12 = static_cast<float>(self->beginFrame); // (float)*((int*)v3+82)
        self->trackValue = v12;  // *((float*)v3+75)  (+300)
        self->blinkPos   = v12;  // *((float*)v3+89)  (+356)

        // nextBlink countdown = min + (max-min)*rand, rand in [0,1).
        const float rnd = static_cast<float>(
            EmoteBlinkRng_next(EmoteBlinkRng_get())); // sub_9F17D0(sub_9F1A08())
        self->blinkTimer = v10 + (v11 - v10) * rnd;   // *((float*)v3+88)  (+352)

        // "edge" array -> edgeTable of {x,y} pairs (each elem a 2-int sub-array).
        const PSB::PSBList* edge = nullptr;
        if (dict) {
            edge = dynamic_cast<const PSB::PSBList*>(
                (*dict)[std::string("edge")].get());
        }
        if (edge) {
            const int count = static_cast<int>(edge->size()); // Motion_propGetCount
            self->mesh.edgeTable.reserve(static_cast<size_t>(count));
            for (int i = 0; i < count; ++i) {
                const auto sub = dynamic_cast<const PSB::PSBList*>(
                    (*edge)[i].get());
                const int x = psbArrayInt(sub, 0); // sub_6637BC(elem,0)
                const int y = psbArrayInt(sub, 1); // sub_6637BC(elem,1)
                self->mesh.edgeTable.emplace_back(static_cast<float>(x),
                                                  static_cast<float>(y));
            }
        }

        // "node" array -> nodeRows: each elem is a sub-array; push a row of its
        //   int->float values (the binary builds a vector<float> per node into
        //   the 504-block deque @+184).
        const PSB::PSBList* node = nullptr;
        if (dict) {
            node = dynamic_cast<const PSB::PSBList*>(
                (*dict)[std::string("node")].get());
        }
        if (node) {
            const int nodeCount = static_cast<int>(node->size());
            for (int i = 0; i < nodeCount; ++i) {
                const auto sub = dynamic_cast<const PSB::PSBList*>(
                    (*node)[i].get());
                std::vector<float> row;
                const int rowCount = sub ? static_cast<int>(sub->size()) : 0;
                row.reserve(static_cast<size_t>(rowCount));
                for (int j = 0; j < rowCount; ++j) {
                    row.push_back(static_cast<float>(psbArrayInt(sub, j)));
                }
                self->mesh.nodeRows.push_back(std::move(row));
            }
        }
    }

    // Aligned with libkrkr2.so sub_663BDC EmoteVarController4_step @ 0x663BDC.
    // Decompiled pseudocode (this conversation):
    //   v5 = trackState(+296);
    //   if (v5 != 2) {                                  // not animating
    //     LABEL_24: while (v5 == 1) { ... pop 8B-track @+96; set trackValue@+300;
    //         if (val==target) goto BLINK; v5=2; trackDir = sign(target-val); }
    //     if (v5) goto BLINK;
    //     if (12B-track @+16 empty) goto BLINK;          // (a1+16)==(a1+48)
    //     pop 12B-track elem {endVal,dur,pow}; advance/free block;
    //     sub_661F7C(self+160, self+80, trackValue, endVal);   // <-- mesh resolver (SCOPE BOUNDARY)
    //     trackAccum(+316)=0; trackSpan(+312)=trackTarget(+288??)...
    //     trackInvDur(+320)=1/dur; trackPow(+324)=pow; v5 = trackState+1;
    //   }
    //   LABEL_19 (v5==2 animating):
    //     v23 = pow(trackAccum/trackSpan, 1/pow) + invDur*dt; v24 = pow(v23,pow);
    //     v27 = v24*trackSpan - trackAccum;
    //     trackValue += trackDir * v27;
    //     if (overshoot) { v5=1; trackValue=trackTarget; goto LABEL_24; }
    //     else trackAccum += v27;
    //   BLINK (LABEL_28): switch(blinkPhase@+336):
    //     case 0:  if (blinkEnabled && beginFrame==(int)blinkPos)
    //                 { blinkTimer-=dt; if(blinkTimer<=0) blinkPhase=10; }
    //     case 10: blinkPos += (dt*2.5/blinkFrameCount)*(endFrame-beginFrame);
    //              if (blinkPos>=endFrame){blinkPos=endFrame; blinkPhase=11;
    //                                       blinkTimer=blinkFrameCount/5;}
    //     case 11: blinkTimer-=dt; if(blinkTimer<=0){ blinkPhase=12;
    //                  blinkTimer = min + (max-min)*rand(); }
    //     case 12: blinkPos += (dt*-2.5/blinkFrameCount)*(endFrame-beginFrame);
    //              if (blinkPos<=beginFrame){blinkPos=beginFrame; blinkPhase=0;}
    //   // final remap:
    //   v43 = trackValue;
    //   if (v43>=beginFrame && v43<=endFrame)
    //       v43 = ((endFrame-v43)*(blinkPos-beginFrame))/(endFrame-beginFrame)+v43;
    //   *out = v43;
    void EmoteBlinkController_step(EmoteBlinkController* self, float* out,
                                   float dt) {
        const float a3 = dt;
        int v5 = self->trackState; // *(a1+296)

        if (v5 != 2) {
            // LABEL_24: drain the 8B value track while in pop-pending state.
            while (v5 == 1) {
                if (self->valueTrack8B.empty()) { // *(a1+128)==*(a1+96)
                    v5 = 0;
                    self->trackState = 0;
                } else {
                    const std::pair<float, float> elem =
                        self->valueTrack8B.front(); // v14 = *v13; v15 = v13[1]
                    self->valueTrack8B.pop_front();  // advance +96 / free block
                    const float v14 = elem.first;
                    const float v15 = elem.second;
                    self->trackValue = v14; // *(a1+300) = v14
                    if (v14 == v15) {
                        // val == target: nothing to animate, go to blink phase.
                        goto blink;
                    }
                    v5 = 2;
                    self->trackTarget = v15; // *(a1+304) = v15
                    self->trackDir = ((v15 - v14) < 0.0f) ? -1.0f : 1.0f; // +308
                    self->trackState = v5;   // *(a1+296) = v5
                    // v5==2 -> fall to animating block.
                    goto animate;
                }
            }

            if (v5) {
                goto blink; // v5 != 0 (and != 1 handled) -> skip track setup
            }

            // trackState == 0: pop a 12B-track keyframe (if any).
            if (self->valueTrack12B.queue.empty()) { // *(a1+48)==*(a1+16)
                goto blink;
            }
            {
                const EmoteAngleKeyValue12B kf =
                    self->valueTrack12B.queue.front(); // {endRad,duration,powCount}
                self->valueTrack12B.queue.pop_front();  // advance +16 / free block

                // Mesh resolver (sub_661F7C @0x661F7C -> sub_660028). The binary
                //   calls sub_661F7C(self+160, self+80, trackValue, kf.endRad) to
                //   rebuild the 8B value track (valueTrack8B) from the resolved
                //   eye mesh rows (mesh.edgeTable + mesh.nodeRows) and to write the
                //   resolved span to mesh.trackResolvedSpan(+288). Faithful port.
                EmoteMeshResolver_resolve(&self->mesh, &self->valueTrack8B,
                                          self->trackValue, kf.endRad);

                // trackAccum=0; trackSpan=trackResolvedSpan(+288); invDur=1/dur;
                //   pow=powCount. +288 was written by sub_661F7C just above.
                self->trackAccum  = 0.0f;                   // *(a1+316)=0
                self->trackSpan   = self->mesh.trackResolvedSpan;// *(a1+312)=*(a1+288)
                self->trackInvDur = 1.0f / kf.duration;     // *(a1+320)=1/v11
                // trackPow(+324) = keyframe[+8] RAW BITS. The binary copies the
                //   keyframe's powCount dword (v12 = *(_DWORD*)(v10+8)) into
                //   *(_DWORD*)(a1+324) and later reads it as *(float*)(a1+324)
                //   (0x663cf0 store / 0x663d50,0x663d90 LDR S, no SCVTF). It is a
                //   raw float-bit reinterpret, NOT an int->float conversion.
                std::memcpy(&self->trackPow, &kf.powCount, sizeof(float)); // *(a1+324)
                v5 = self->trackState + 1;                  // v19+1
                self->trackState = v5;                      // *(a1+296)=v5
                if (v5 != 2) {
                    goto blink;
                }
            }

        animate:
            // LABEL_19: v5 == 2, advance the power-curve ramp.
            {
                const float span = self->trackSpan;
                const float invDur = self->trackInvDur;
                const float pw = self->trackPow; // raw float bits, read as-is
                const float v23 =
                    std::pow(self->trackAccum / span, 1.0f / pw) + invDur * a3;
                const float v24 = std::pow(v23, pw);
                const float v25 = self->trackAccum;          // *(a1+316)
                const float v26 = self->trackDir;            // *(a1+308)
                const float v27 = v24 * span - v25;
                const float v28 = self->trackValue + (v26 * v27);
                self->trackValue = v28;                      // *(a1+300)=v28
                const float v29 = self->trackTarget;         // *(a1+304)
                const bool overshoot =
                    (v26 > 0.0f && v29 <= v28) || (v26 < 0.0f && v29 >= v28);
                if (overshoot) {
                    v5 = 1;
                    self->trackState = 1;                    // *(a1+296)=1
                    self->trackValue = v29;                  // *(a1+300)=v29
                    // goto LABEL_24 (re-drive the 8B track next).
                    while (v5 == 1) {
                        if (self->valueTrack8B.empty()) {
                            v5 = 0;
                            self->trackState = 0;
                        } else {
                            const std::pair<float, float> elem =
                                self->valueTrack8B.front();
                            self->valueTrack8B.pop_front();
                            const float a = elem.first;
                            const float b = elem.second;
                            self->trackValue = a;
                            if (a == b) { goto blink; }
                            v5 = 2;
                            self->trackTarget = b;
                            self->trackDir = ((b - a) < 0.0f) ? -1.0f : 1.0f;
                            self->trackState = v5;
                            goto animate;
                        }
                    }
                    goto blink;
                }
                self->trackAccum = v25 + v27;                // *(a1+316)=v25+v27
            }
        }

    blink:
        // LABEL_28: blink state machine on +336.
        switch (self->blinkPhase) {
            case 0: { // wait for blink trigger
                if (self->blinkEnabled) {
                    if (self->beginFrame ==
                        static_cast<int>(self->blinkPos)) { // (int)*(a1+356)
                        const float v30 = self->blinkTimer - a3;
                        self->blinkTimer = v30;
                        if (v30 <= 0.0f) {
                            self->blinkPhase = 10;
                        }
                    }
                }
                break;
            }
            case 10: { // closing
                const int v31 = self->endFrame;             // *(a1+332)
                const float v32 = self->blinkFrameCount;    // *(a1+348)
                const float v33 = self->blinkPos +
                    (((a3 * 2.5f) / v32) *
                     static_cast<float>(v31 - self->beginFrame)); // - *(a1+328)
                self->blinkPos = v33;
                if (v33 >= static_cast<float>(v31)) {
                    self->blinkPos = static_cast<float>(v31);
                    self->blinkPhase = 11;
                    self->blinkTimer = v32 / 5.0f;          // *(a1+352)=v32/5
                }
                break;
            }
            case 11: { // hold (eyes closed)
                const float v34 = self->blinkTimer - a3;
                self->blinkTimer = v34;
                if (v34 <= 0.0f) {
                    const float v35 = self->blinkIntervalMin; // *(a1+340)
                    const float v36 = self->blinkIntervalMax; // *(a1+344)
                    self->blinkPhase = 12;
                    const float v37 = v36 - v35;
                    const float v39 = static_cast<float>(
                        EmoteBlinkRng_next(EmoteBlinkRng_get())); // sub_9F17D0(sub_9F1A08())
                    self->blinkTimer = v35 + (v37 * v39);   // *(a1+352)
                }
                break;
            }
            case 12: { // opening
                const int v40 = self->beginFrame;           // *(a1+328)
                const float v41 = self->blinkPos +
                    (((a3 * -2.5f) / self->blinkFrameCount) *
                     static_cast<float>(self->endFrame - v40)); // *(a1+332)-v40
                self->blinkPos = v41;
                if (v41 <= static_cast<float>(v40)) {
                    self->blinkPos = static_cast<float>(v40);
                    self->blinkPhase = 0;
                }
                break;
            }
            default:
                break;
        }

        // Final remap: blend the track value by the blink position when inside
        //   the [beginFrame, endFrame] window.
        const int v42 = self->beginFrame;   // *(a1+328)
        float v43 = self->trackValue;       // *(a1+300)
        if (v43 >= static_cast<float>(v42)) {
            const int v44 = self->endFrame; // *(a1+332)
            if (v43 <= static_cast<float>(v44)) {
                v43 = (static_cast<float>(
                           (static_cast<float>(v44) - v43) *
                           (self->blinkPos - static_cast<float>(v42))) /
                       static_cast<double>(v44 - v42)) +
                      v43;
            }
        }
        *out = v43; // *a2 = v43
    }

} // namespace motion
