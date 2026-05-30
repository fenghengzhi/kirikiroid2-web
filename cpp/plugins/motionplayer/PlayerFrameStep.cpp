// PlayerFrameStep.cpp — M1/P2 binary-aligned parseFrame / mergeFrameContent.
//
// Independent port of:
//   Player_parseFrame        @ libkrkr2.so 0x6926B4
//   Player_mergeFrameContent @ libkrkr2.so 0x692AB0
//
// NOT wired into the live frame-progress path. Exercised only by the
// motionplayer-dll unit test. See PlayerFrameStep.h header for the full scope
// rationale and the slot offset map.
//
// Binary helper -> local mapping (all are iTJSDispatch2 PropGet wrappers in the
// binary; here they read decoded values from PSB::PSBDictionary):
//   sub_662668  double  PropGet(key)          -> psbNumber()
//   sub_6635DC  int     PropGet(key)          -> (int)psbNumber()
//   sub_6636D4  bool    PropGet(key)          -> psbBool()
//   sub_6695BC  double  PropGet([index])      -> psbListNumber(list, index)
//   sub_529524  variant PropGet(key)          -> psbString() (logical view)
//   sub_56C694  int     PropGet("count")      -> list->size()
//   sub_A0FB64  tTJSVariant copy of curve blk -> psbCurveDoubles()

#include "PlayerFrameStep.h"

#include "psbfile/PSBValue.h"

#include <utility>

namespace motion {
    namespace detail {

        namespace {

            // PropGet(key) -> optional double (sub_662668 / sub_6695BC value path)
            std::optional<double>
            psbNumber(const std::shared_ptr<PSB::PSBDictionary> &dic,
                      const char *key) {
                if(!dic) return std::nullopt;
                auto v = (*dic)[key];
                if(auto n = std::dynamic_pointer_cast<PSB::PSBNumber>(v)) {
                    switch(n->numberType) {
                        case PSB::PSBNumberType::Float:
                            return n->getValue<float>();
                        case PSB::PSBNumberType::Double:
                            return n->getValue<double>();
                        case PSB::PSBNumberType::Int:
                            return static_cast<double>(n->getValue<int>());
                        case PSB::PSBNumberType::Long:
                        default:
                            return static_cast<double>(
                                n->getValue<tjs_int64>());
                    }
                }
                if(auto b = std::dynamic_pointer_cast<PSB::PSBBool>(v)) {
                    return b->value ? 1.0 : 0.0;
                }
                return std::nullopt;
            }

            // sub_6636D4: bool := (value != 0)
            bool psbBool(const std::shared_ptr<PSB::PSBDictionary> &dic,
                         const char *key) {
                if(auto v = psbNumber(dic, key)) return *v != 0.0;
                return false;
            }

            // sub_529524 variant ref, decoded as a string (logical "src"/"act"/
            // "dtgt"/"target" handle).
            std::string psbString(const std::shared_ptr<PSB::PSBDictionary> &dic,
                                  const char *key) {
                if(!dic) return {};
                if(auto s =
                       std::dynamic_pointer_cast<PSB::PSBString>((*dic)[key])) {
                    return s->value;
                }
                return {};
            }

            std::shared_ptr<PSB::PSBList>
            psbList(const std::shared_ptr<PSB::PSBDictionary> &dic,
                    const char *key) {
                if(!dic) return nullptr;
                return std::dynamic_pointer_cast<PSB::PSBList>((*dic)[key]);
            }

            std::shared_ptr<PSB::PSBDictionary>
            psbDict(const std::shared_ptr<PSB::PSBDictionary> &dic,
                    const char *key) {
                if(!dic) return nullptr;
                return std::dynamic_pointer_cast<PSB::PSBDictionary>((*dic)[key]);
            }

            // sub_6695BC: list[index] as double.
            std::optional<double>
            psbListNumber(const std::shared_ptr<PSB::PSBList> &list,
                          std::size_t index) {
                if(!list || index >= list->size()) return std::nullopt;
                auto v = (*list)[index];
                if(auto n = std::dynamic_pointer_cast<PSB::PSBNumber>(v)) {
                    switch(n->numberType) {
                        case PSB::PSBNumberType::Float:
                            return n->getValue<float>();
                        case PSB::PSBNumberType::Double:
                            return n->getValue<double>();
                        case PSB::PSBNumberType::Int:
                            return static_cast<double>(n->getValue<int>());
                        case PSB::PSBNumberType::Long:
                        default:
                            return static_cast<double>(
                                n->getValue<tjs_int64>());
                    }
                }
                if(auto b = std::dynamic_pointer_cast<PSB::PSBBool>(v)) {
                    return b->value ? 1.0 : 0.0;
                }
                return std::nullopt;
            }

            // sub_A0FB64 copies a curve tTJSVariant. The binary keeps the raw
            // variant; here we decode the curve dict's "x" array into doubles as
            // the logical view (occ/ccc/acc/zcc/scc/cp blocks).
            std::vector<double>
            psbCurveDoubles(const std::shared_ptr<PSB::PSBDictionary> &content,
                            const char *key) {
                std::vector<double> out;
                auto curve = psbDict(content, key);
                if(!curve) return out;
                if(auto xs = std::dynamic_pointer_cast<PSB::PSBList>(
                       (*curve)["x"])) {
                    for(std::size_t i = 0; i < xs->size(); ++i) {
                        if(auto v = psbListNumber(xs, i)) out.push_back(*v);
                    }
                }
                return out;
            }

        } // namespace

        // =================================================================
        // Player_mergeFrameContent @ 0x692AB0
        // =================================================================
        void mergeFrameContentLike_0x692AB0(
            ParsedFrameSlotLike_0x6926B4 &slot,
            int nodeType,
            const std::shared_ptr<PSB::PSBDictionary> &content) {

            // 0x692AEC: if (slot.typeZeroFlag) early-out, after setting merged.
            // 0x692AF0: slot.mergedFlag = 1.
            slot.mergedFlag = 1;
            if(slot.typeZeroFlag) {
                return;
            }
            if(!content) {
                return;
            }

            // 0x692C70..0x692C90: reset the transform/colour block to defaults
            // BEFORE applying mask bits (this is why opacity/blend/colours have
            // their defaults here even though parseFrame already reset).
            slot.packedColors = {0xFF808080u, 0xFF808080u, 0xFF808080u,
                                 0xFF808080u};
            slot.opacity = 255;       // v3[22] = 255
            slot.blendMode = 16;      // v3[11] = 16

            const std::uint32_t mask = slot.mask;  // v3[5]

            // ---- src/icon: gated by ((1<<nodeType) & 0x1849) (0x692C94) ----
            // 0x1849 = nodeTypes {0,3,6,11,12}. The binary reads "src" variant
            // into v3+9, then "icon" into v3+7 (icon index lookup). We keep the
            // "src" string (logical) only; icon-handle resolution is a live
            // dispatch concern (DEFERRED below).
            if(nodeType >= 0 && nodeType < 32 &&
               ((1u << static_cast<unsigned>(nodeType)) & 0x1849u) != 0) {
                slot.src.clear();
                slot.srcList.clear();
                if(auto s = psbString(content, "src"); !s.empty()) {
                    slot.src = s;
                } else if(auto sl = psbList(content, "src")) {
                    for(std::size_t i = 0; i < sl->size(); ++i) {
                        if(auto e = std::dynamic_pointer_cast<PSB::PSBString>(
                               (*sl)[i])) {
                            slot.srcList.push_back(e->value);
                        }
                    }
                    if(!slot.srcList.empty()) slot.src = slot.srcList[0];
                }
                // DEFERRED: "icon" (v3+7) PropGet [1024]"icon" + sub_A13878
                //           icon-index handle (0x692CFC..0x692DBC). Requires the
                //           live iTJSDispatch2 icon table; not reproducible from
                //           the PSB dict alone.
            }

            // ---- mask 0x1: ox/oy (0x692DC4) ----
            if((mask & 0x1) != 0) {
                if(auto v = psbNumber(content, "ox")) slot.ox = *v; // (double*)v3+7
                if(auto v = psbNumber(content, "oy")) slot.oy = *v; // (double*)v3+8
            }

            // ---- mask 0x2: coord[0..2] (0x692E14) ----
            if((mask & 0x2) != 0) {
                auto coord = psbList(content, "coord");
                if(coord) {
                    if(auto v = psbListNumber(coord, 0)) slot.coordX = *v; // v3+12
                    if(auto v = psbListNumber(coord, 1)) slot.coordY = *v; // v3+13
                    if(auto v = psbListNumber(coord, 2)) slot.coordZ = *v; // v3+14
                }
            }

            // ---- mask 0x20600 group: bm / color / opa (0x692F1C) ----
            if((mask & 0x20600) != 0) {
                // mask 0x20000: bm (0x692F20)
                if((mask & 0x20000) != 0) {
                    if(auto v = psbNumber(content, "bm"))
                        slot.blendMode = static_cast<std::uint32_t>(*v); // v3[11]
                }
                // mask 0x200: color (0x692F4C). Binary decodes 4 packed RGBA
                // DWORDs from a content["color"] array/int (sub_6637BC, multiple
                // variant tags). We approximate: a 4-element list -> 4 channels.
                if((mask & 0x200) != 0) {
                    if(auto col = psbList(content, "color")) {
                        for(int i = 0; i < 4 && i < (int)col->size(); ++i) {
                            if(auto v = psbListNumber(col, (std::size_t)i))
                                slot.packedColors[i] =
                                    static_cast<std::uint32_t>(*v);
                        }
                    } else if(auto v = psbNumber(content, "color")) {
                        // scalar int -> broadcast (binary vdupq_n_s32, case 2/4)
                        const auto c = static_cast<std::uint32_t>(*v);
                        slot.packedColors = {c, c, c, c};
                    }
                    // else: default 0xFF808080 (already set).
                } else if((slot.blendMode & 0xF0) == 0) {
                    // 0x6933D0: when no color bit AND blendMode has no 0xF0
                    // nibble, write -1 to all four packed colours (the binary
                    // STP X9,X9,[X24] stores 16 bytes = 0xFFFFFFFF x4).
                    slot.packedColors = {0xFFFFFFFFu, 0xFFFFFFFFu,
                                         0xFFFFFFFFu, 0xFFFFFFFFu};
                }
                // else (blendMode & 0xF0 != 0, no color bit): leave default
                // 0xFF808080 (binary falls through 692F5C straight to opa test).
                // mask 0x400: opa (0x693440)
                if((mask & 0x400) != 0) {
                    if(auto v = psbNumber(content, "opa"))
                        slot.opacity = static_cast<std::uint32_t>(*v); // v3[22]
                }
            }

            // ---- mask 0x1FC group: fx/fy, angle, zx/zy, sx/sy (0x692F64) ----
            if((mask & 0x1FC) != 0) {
                // mask 0xC: fx/fy (0x692F6C)
                if((mask & 0xC) != 0) {
                    slot.flipX = psbBool(content, "fx") ? 1 : 0; // (byte)v3+120
                    slot.flipY = psbBool(content, "fy") ? 1 : 0; // (byte)v3+121
                }
                // mask 0x10: angle (0x692FC4)
                if((mask & 0x10) != 0) {
                    if(auto v = psbNumber(content, "angle"))
                        slot.angle = *v;                          // (double*)v3+16
                }
                // mask 0x60: zx/zy (0x692FF4)
                if((mask & 0x60) != 0) {
                    if(auto v = psbNumber(content, "zx")) slot.zx = *v; // v3+17
                    if(auto v = psbNumber(content, "zy")) slot.zy = *v; // v3+18
                }
                // mask 0x180: sx/sy (0x693048)
                if((mask & 0x180) != 0) {
                    if(auto v = psbNumber(content, "sx")) slot.sx = *v; // v3+19
                    if(auto v = psbNumber(content, "sy")) slot.sy = *v; // v3+20
                }
            }

            // The remaining ti / colour-curve / cp blocks are ALL reached only
            // when interpFlag (slot+25) is set: 0x693474 LABEL_95 jumps straight
            // to LABEL_96 (mesh) when !interpFlag.
            if(slot.interpFlag != 0) {
                // ---- "ti": gated by (mask byte v3+23 & 4) (0x6930A0) ----
                // byte (v3+23) is mask bits 24-31; (&4) == mask & 0x04000000.
                if((mask & 0x04000000u) != 0) {
                    if(auto v = psbNumber(content, "ti"))
                        slot.ti = static_cast<std::uint32_t>(*v);     // v3[4]
                }

                // ---- colour-curve group mask 0xF800 (0x6930D8) ----
                if((mask & 0xF800) != 0) {
                    if((mask & 0x800) != 0)   // ccc (v3+42)
                        slot.ccc = psbCurveDoubles(content, "ccc");
                    if((mask & 0x8000) != 0)  // occ (v3+47)
                        slot.occ = psbCurveDoubles(content, "occ");
                    if((mask & 0x1000) != 0)  // acc (v3+52)
                        slot.acc = psbCurveDoubles(content, "acc");
                    if((mask & 0x2000) != 0)  // zcc (v3+57)
                        slot.zcc = psbCurveDoubles(content, "zcc");
                    if((mask & 0x4000) != 0)  // scc (v3+62)
                        slot.scc = psbCurveDoubles(content, "scc");
                }

                // ---- "cp": also requires (mask byte v3+22 & 1) ----
                // 0x6932C4: skip unless (*(byte)(v3+22) & 1). byte (v3+22) is
                // mask bits 16-23; (&1) == mask & 0x10000. Reads "cp" (v3+67).
                if((mask & 0x10000u) != 0) {
                    slot.cp = psbCurveDoubles(content, "cp");
                }
            }

            // ---- mask 0x2000000: mesh / bezierPatch (0x693480) ----
            // DEFERRED: 0x693484..0x69389C decodes content["mesh"] (or "obj"
            // child) then iterates 32 bezier-patch points via sub_6695BC into a
            // raw growing float-pair vector (v3+80/82/84), validating count==32
            // (sub_56C694). The raw float-vector growth path (operator new /
            // memmove) is a live render-buffer concern; the scalar slot we expose
            // here does not need it for P2 unit coverage. Marked DEFERRED.

            // ---- mask 0x80000: motion sub-object (0x6938CC) ----
            if((mask & 0x80000) != 0) {
                auto motion = psbDict(content, "motion");
                const std::uint32_t mm = static_cast<std::uint32_t>(
                    psbNumber(motion, "mask").value_or(0.0));
                slot.motionDocmpl = false;     // (byte)v3+360 = 0
                slot.motionDtgt.clear();        // v3+91 variant released
                slot.motionDt = 0;              // (qword)v3+43 = 0x100000000 low
                slot.motionDofst = 0.0;
                if((mm & 0x1) != 0)
                    slot.motionFlags = (int)psbNumber(motion, "flags").value_or(0); // v3[86]
                if((mm & 0x2) != 0)
                    slot.motionDt = (int)psbNumber(motion, "dt").value_or(0);       // v3[87]
                if((mm & 0x4) != 0)
                    slot.motionDocmpl = psbBool(motion, "docmpl");                  // v3+360
                if((mm & 0x8) != 0)
                    slot.motionDofst = psbNumber(motion, "dofst").value_or(0.0);    // v3+44
                if((mm & 0x10) != 0)
                    slot.motionDtgt = psbString(motion, "dtgt");                    // v3+91
                slot.motionTimeOffset =
                    psbNumber(motion, "timeOffset").value_or(0.0);                  // v3+47
            }

            // ---- mask 0x1000000: model sub-object (0x693AE8) ----
            if((mask & 0x1000000) != 0) {
                auto model = psbDict(content, "model");
                slot.modelTimeOffset =
                    psbNumber(model, "timeOffset").value_or(0.0);  // (double*)v3+51
                slot.modelLoop = psbBool(model, "loop");           // (byte)v3+384
                slot.modelDt = (int)psbNumber(model, "dt").value_or(0); // v3[97]
                slot.modelDtgt = psbString(model, "dtgt");         // (qword)v3+49
            }

            // ---- mask 0x100000: prt (particle) sub-object (0x693C64) ----
            if((mask & 0x100000) != 0) {
                auto prt = psbDict(content, "prt");
                const std::uint32_t pm = static_cast<std::uint32_t>(
                    psbNumber(prt, "mask").value_or(0.0));
                // 0x693D20..0x693D4C: reset prt block to defaults (10.0/0.0).
                slot.prtFmin = 10.0; slot.prtFmax = 10.0;
                slot.prtVmin = 0.0;  slot.prtVmax = 0.0;
                slot.prtAmin = 0.0;  slot.prtAmax = 0.0;
                slot.prtZmin = 0.0;  slot.prtZmax = 0.0;
                slot.prtRange = 0.0; slot.prtTrigger = 0;
                if((pm & 0x1) != 0)
                    slot.prtTrigger = (int)psbNumber(prt, "trigger").value_or(0); // v3[104]
                if((pm & 0x2) != 0) {
                    slot.prtFmin = psbNumber(prt, "fmin").value_or(0.0); // v3+53
                    slot.prtFmax = psbNumber(prt, "fmax").value_or(0.0); // v3+54
                }
                if((pm & 0x4) != 0) {
                    slot.prtVmin = psbNumber(prt, "vmin").value_or(0.0); // v3+55
                    slot.prtVmax = psbNumber(prt, "vmax").value_or(0.0); // v3+56
                }
                if((pm & 0x8) != 0) {
                    slot.prtAmin = psbNumber(prt, "amin").value_or(0.0); // v3+57
                    slot.prtAmax = psbNumber(prt, "amax").value_or(0.0); // v3+58
                }
                if((pm & 0x10) != 0) {
                    slot.prtZmin = psbNumber(prt, "zmin").value_or(0.0); // v3+59
                    slot.prtZmax = psbNumber(prt, "zmax").value_or(0.0); // v3+60
                }
                if((pm & 0x20) != 0)
                    slot.prtRange = psbNumber(prt, "range").value_or(0.0); // v3+61
            }

            // ---- mask 0x200000: camera sub-object (0x693EF0) ----
            if((mask & 0x200000) != 0) {
                auto cam = psbDict(content, "camera");
                slot.cameraFov = psbNumber(cam, "fov").value_or(0.0);  // (double*)v3+62
                slot.cameraTarget = psbString(cam, "target");           // (qword)v3+63
            }

            // ---- mask 0x800000: anchor sub-object (0x694020) ----
            if((mask & 0x800000) != 0) {
                auto anchor = psbDict(content, "anchor");
                slot.anchorTarget = psbString(anchor, "target");        // (qword)v3+129
            }

            // ---- mask 0x8000000: feedback sub-object (0x694130) ----
            if((mask & 0x8000000u) != 0) {
                auto fb = psbDict(content, "feedback");
                slot.feedbackTimespan =
                    psbNumber(fb, "timespan").value_or(0.0);            // (double*)v3+66
            }
        }

        // =================================================================
        // Player_parseFrame @ 0x6926B4
        // =================================================================
        void parseFrameLike_0x6926B4(
            ParsedFrameSlotLike_0x6926B4 &slot,
            const std::shared_ptr<PSB::PSBDictionary> &frame,
            std::uint32_t frameIndex,
            int nodeType) {

            // 0x6926E0: Frame_resetSlot.
            resetSlotLike_0x69260C(slot);
            // 0x6926EC: slot.frameIndex = frameIndex.
            slot.frameIndex = frameIndex;
            if(!frame) {
                // Binary dereferences the frame variant unconditionally; with a
                // null PSB frame we mirror the type==0 (invisible) default.
                slot.typeZeroFlag = 1;
                return;
            }

            // 0x6927E0: slot.time = frame["time"].
            slot.time = psbNumber(frame, "time").value_or(0.0);

            // 0x692800: type := frame["type"].
            const int type = (int)psbNumber(frame, "type").value_or(0.0);
            if(type == 0) {
                // 0x692828: invisible frame.
                slot.typeZeroFlag = 1;
                return;
            }
            slot.typeZeroFlag = 0;          // 0x69280C
            if(type == 2) {
                slot.interpFlag = 0;        // 0x692830
            } else if(type == 3) {
                slot.interpFlag = 1;        // 0x69281C
            }

            // 0x692864: content := frame["content"].
            auto content = psbDict(frame, "content");

            // 0x6928E4: slot.mask = content["mask"].
            const std::uint32_t mask = static_cast<std::uint32_t>(
                psbNumber(content, "mask").value_or(0.0));
            slot.mask = mask;

            // 0x6928EC: if (mask & 0x40000) slot.act = content["act"].
            if((mask & 0x40000) != 0) {
                slot.act = psbString(content, "act");
            }

            // NOTE: the binary's parseFrame does NOT itself call
            // mergeFrameContent — the caller (reseekTimelineCursors path) invokes
            // mergeFrameContentLike_0x692AB0 separately on the parsed slot. For
            // unit convenience we leave merging to the explicit call so the two
            // routines stay independently testable, matching the binary's split.
            (void)nodeType;
        }

    } // namespace detail
} // namespace motion
