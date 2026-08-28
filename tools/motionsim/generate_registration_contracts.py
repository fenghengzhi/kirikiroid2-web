#!/usr/bin/env python3
"""Generate the final four-binary registration-string/argument contract ledger.

The 316-row NCB equivalence ledger covers the motionplayer.dll registrar tree.
This generator preserves those rows, adds the DrawDeviceD3D.dll seven-class
surface proved by MP-A30/MP-A31, and adds the four module/dependency roots.
Argument contracts are explicit for constructors, factories and every raw
callback; ordinary typed members have no script-side optional defaults.
"""

from __future__ import annotations

import argparse
import csv
from pathlib import Path


FIELDS = (
    "contract_id",
    "module",
    "owner",
    "sequence",
    "kind",
    "script_name",
    "binding",
    "argument_contract",
    "android_arm64",
    "android_armv7",
    "ios_arm64",
    "ios_armv7",
    "registration_status",
    "evidence_report",
)


RAW_CONTRACTS = {
    ("Player", "setVariable"):
        "raw: argc>=2; label,value required; mode argv2 defaults to 0; argv3+ ignored",
    ("Player", "play"):
        "raw: argc>=2; label and flags required; no optional default; argv2+ ignored",
    ("Player", "progress"):
        "raw: argc>=1; milliseconds required; no optional default; argv1+ ignored",
    ("EmotePlayer", "setVariable"):
        "raw: argc>=2; label,value required; transition=0,ease=0; argv4+ ignored",
    ("EmotePlayer", "setCoord"):
        "raw: argc>=2; x,y required; transition=0,ease=0; argv4+ ignored",
    ("EmotePlayer", "setScale"):
        "raw: argc>=1; scale required; transition=0,ease=0; argv3+ ignored",
    ("EmotePlayer", "setRotate"):
        "raw: argc>=1; angle required; transition=0,ease=0; argv3+ ignored",
    ("EmotePlayer", "setColor"):
        "raw: argc>=1; color required; transition=0,ease=0; argv3+ ignored",
    ("EmotePlayer", "setOuterForce"):
        "raw: argc>=3; label,x,y required; transition=0,ease=0; argv5+ ignored",
    ("EmotePlayer", "playTimeline"):
        "raw: argc>=1; label required; flags=0; argv2+ ignored",
    ("EmotePlayer", "stopTimeline"):
        "raw: argc>=0; label defaults to allocated empty string; argv1+ ignored",
    ("EmotePlayer", "getTimelinePlaying"):
        "raw: argc>=0; label defaults to allocated empty string; argv1+ ignored",
    ("EmotePlayer", "setTimelineBlendRatio"):
        "raw: argc>=1; label required; duration=0,ease=1,autoStop=false; argv4+ ignored",
    ("EmotePlayer", "fadeInTimeline"):
        "raw: argc>=1; label required; duration=0,ease=1; argv3+ ignored",
    ("EmotePlayer", "fadeOutTimeline"):
        "raw: argc>=1; label required; duration=0,ease=1; argv3+ ignored",
}


CTOR_CONTRACTS = {
    "SourceCache": "zero-arg typed constructor; any argc>=0 accepted and argv ignored; exact-one-Void empty-adaptor sentinel",
    "ObjSource": "zero-arg typed constructor; any argc>=0 accepted and argv ignored; exact-one-Void empty-adaptor sentinel",
    "Point": "zero-arg typed constructor; any argc>=0 accepted and argv ignored; exact-one-Void empty-adaptor sentinel",
    "Circle": "zero-arg typed constructor; any argc>=0 accepted and argv ignored; exact-one-Void empty-adaptor sentinel",
    "Rect": "zero-arg typed constructor; any argc>=0 accepted and argv ignored; exact-one-Void empty-adaptor sentinel",
    "Quad": "zero-arg typed constructor; any argc>=0 accepted and argv ignored; exact-one-Void empty-adaptor sentinel",
    "LayerGetter": "zero-arg typed constructor; any argc>=0 accepted and argv ignored; exact-one-Void empty-adaptor sentinel",
    "SeparateLayerAdaptor": "typed constructor: argc>=1; one Variant required; surplus ignored; exact-one-Void empty-adaptor sentinel",
    "Player": "typed constructor: argc>=1; one Variant required; surplus ignored; exact-one-Void empty-adaptor sentinel",
    "ResourceManager": "typed constructor: argc>=2; Variant and int32 required; surplus ignored; exact-one-Void empty-adaptor sentinel",
}


FACTORY_CONTRACTS = {
    "D3DAdaptor": "raw factory: argc>=5; Window,int32,int32,int32,int32 required; surplus ignored; exact-one-Void empty-adaptor sentinel",
    "EmotePlayer": "typed factory: argc>=1; one Variant required; surplus ignored; exact-one-Void empty-adaptor sentinel",
}


def argument_contract(row: dict[str, str]) -> str:
    kind = row["local_kind"]
    owner = row["owner"]
    name = row["script_name"]
    if kind == "method_raw":
        return RAW_CONTRACTS[(owner, name)]
    if kind == "constructor":
        return CTOR_CONTRACTS[owner]
    if kind == "factory":
        return FACTORY_CONTRACTS[owner]
    if kind in {"method", "method_detail"}:
        if (owner, name) == ("EmotePlayer", "play"):
            return (
                "typed: label and flags both required; C++ flags=0 default is not "
                "a script default; surplus ignored"
            )
        return (
            "typed fixed signature: every bound parameter required; no script-side "
            "optional default; surplus ignored"
        )
    return "not applicable"


COMMON_ROOT_MEMBERS = (
    ("property_ro", "children", "getChildren"),
    ("property", "clearColor", "getClearColor/setClearColor"),
    ("property", "transState", "getTransState/setTransState"),
    ("method", "add", "add"),
    ("method", "remove", "remove"),
    ("method", "startTransition", "startTransition"),
    ("method", "stopTransition", "stopTransition"),
    ("method", "update", "update"),
    ("method", "checkEnable", "checkEnable"),
    ("method", "getModule", "getModule"),
    ("method", "capture", "capture"),
    ("property", "offsetX", "getOffsetX/setOffsetX"),
    ("property", "offsetY", "getOffsetY/setOffsetY"),
    ("method", "setOffset", "setOffset"),
    ("property", "stretchType", "getStretchType/setStretchType"),
    ("property", "bicubicParam", "getBicubicParam/setBicubicParam"),
    ("property", "forceRenderTexture", "getForceRenderTexture/setForceRenderTexture"),
    ("property_ro", "interface", "getInterface"),
    ("method", "setPrimarySize", "setPrimarySize"),
    ("property", "primaryWidth", "getPrimaryWidth/setPrimaryWidth"),
    ("property", "primaryHeight", "getPrimaryHeight/setPrimaryHeight"),
    ("method", "setScreenRect", "setScreenRect"),
    ("property", "screenLeft", "getScreenLeft/setScreenLeft"),
    ("property", "screenTop", "getScreenTop/setScreenTop"),
    ("property", "screenWidth", "getScreenWidth/setScreenWidth"),
    ("property", "screenHeight", "getScreenHeight/setScreenHeight"),
    ("property_ro", "primaryLayers", "getPrimaryLayers"),
    ("property", "layerManagerIndex", "getLayerManagerIndex/setLayerManagerIndex"),
    ("method", "getPrimaryLayerBitmap", "getPrimaryLayerBitmap"),
    ("property_ro", "destLeft", "getDestLeft"),
    ("property_ro", "destTop", "getDestTop"),
    ("property_ro", "destWidth", "getDestWidth"),
    ("property_ro", "destHeight", "getDestHeight"),
)


LAYER_MEMBERS = (
    ("variant", "DrawPlaneFront", "integer constant 1"),
    ("variant", "DrawPlaneBack", "integer constant 2"),
    ("variant", "DrawPlaneBoth", "integer constant 3"),
    ("property", "visible", "IsVisible/setVisible"),
    ("property", "frontIndex", "getFrontIndex/setFrontIndex"),
    ("property", "backIndex", "getBackIndex/setBackIndex"),
    ("property", "drawPlane", "getDrawPlane/setDrawPlane"),
    ("method", "setMatrix", "setMatrix"),
    ("method", "setMatrixGL", "setMatrixGL"),
    ("method", "setClip", "setClip"),
)


IMAGE_MEMBERS = (
    ("property_ro", "width", "getWidth"),
    ("property_ro", "height", "getHeight"),
    ("method", "load", "load"),
)


PICTURE_MEMBERS = (
    ("property", "opacity", "getOpacity/setOpacity"),
    ("property", "blendMode", "getBlendMode/setBlendMode"),
    ("property", "stretchType", "getStretchType/setStretchType"),
    ("property", "bicubicParam", "getBicubicParam/setBicubicParam"),
    ("method", "assignImageRange", "assignImageRange"),
    ("method", "clearImageRange", "clearImageRange"),
    ("method", "setCoord", "setCoord"),
    ("method", "setScale", "setScale"),
    ("method", "getScale", "getScale"),
)


MODULE_MEMBERS = (
    ("property", "maskMode", "getMaskMode/setMaskMode"),
    ("property", "maskRegionClipping", "getMaskRegionClipping/setMaskRegionClipping"),
    ("property", "mipMapEnabled", "getMipMapEnabled/setMipMapEnabled"),
    ("property", "alphaOp", "getAlphaOp/setAlphaOp"),
    ("property", "protectTranslucentTextureColor", "getProtectTranslucentTextureColor/setProtectTranslucentTextureColor"),
    ("property", "pixelateDivision", "getPixelateDivision/setPixelateDivision"),
    ("method", "setMaxTextureSize", "setMaxTextureSize"),
)


PLAYER_CONSTANTS = (
    ("variant", "MaskModeStencil", "integer constant 0"),
    ("variant", "MaskModeAlpha", "integer constant 1"),
    ("variant", "TimelinePlayFlagParallel", "integer constant 1"),
    ("variant", "TimelinePlayFlagDifference", "integer constant 2"),
)


PLAYER_MEMBERS = (
    ("property_ro", "module", "getModule"),
    ("method", "clear", "clear"),
    ("method_raw", "load", "loadCompat"),
    ("method", "clone", "clone"),
    ("method", "show", "show"),
    ("method", "hide", "hide"),
    ("property", "visible", "getVisible/setVisible"),
    ("property", "smoothing", "getSmoothing/setSmoothing"),
    ("property", "meshDivisionRatio", "getMeshDivisionRatio/setMeshDivisionRatio"),
    ("property", "queing", "getQueuing/setQueuing"),
    ("property", "hairScale", "getHairScale/setHairScale"),
    ("property", "partsScale", "getPartsScale/setPartsScale"),
    ("property", "bustScale", "getBustScale/setBustScale"),
    ("method", "assignState", "assignState"),
    ("method", "setCoord", "setCoord"),
    ("method", "setScale", "setScale"),
    ("method", "getScale", "getScale"),
    ("method", "setRot", "setRot"),
    ("method", "getRot", "getRot"),
    ("method", "setColor", "setColor"),
    ("method", "getColor", "getColor"),
    ("method", "countVariables", "countVariables"),
    ("method", "getVariableLabelAt", "getVariableLabelAt"),
    ("method", "countVariableFrameAt", "countVariableFrameAt"),
    ("method", "getVariableFrameLabelAt", "getVariableFrameLabelAt"),
    ("method", "getVariableFrameValueAt", "getVariableFrameValueAt"),
    ("method", "setVariable", "setVariable"),
    ("method", "getVariable", "getVariable"),
    ("method", "startWind", "startWind"),
    ("method", "stopWind", "stopWind"),
    ("method", "countMainTimelines", "countMainTimelines"),
    ("method", "getMainTimelineLabelAt", "getMainTimelineLabelAt"),
    ("method", "countDiffTimelines", "countDiffTimelines"),
    ("method", "getDiffTimelineLabelAt", "getDiffTimelineLabelAt"),
    ("method", "countPlayingTimelines", "countPlayingTimelines"),
    ("method", "getPlayingTimelineLabelAt", "getPlayingTimelineLabelAt"),
    ("method", "getPlayingTimelineFlagsAt", "getPlayingTimelineFlagsAt"),
    ("method", "isLoopTimeline", "isLoopTimeline"),
    ("method", "getTimelineTotalFrameCount", "getTimelineTotalFrameCount"),
    ("method", "playTimeline", "playTimeline"),
    ("method", "isTimelinePlaying", "isTimelinePlaying"),
    ("method", "stopTimeline", "stopTimeline"),
    ("method_detail", "setTimelineBlendRatio", "setTimeline(ttstr,float,float,float,bool)"),
    ("method", "getTimelineBlendRatio", "getTimelineBlendRatio"),
    ("method", "fadeInTimeline", "fadeInTimeline"),
    ("method", "fadeOutTimeline", "fadeOutTimeline"),
    ("property_ro", "animating", "getAnimating"),
    ("method", "skip", "skip"),
    ("method_detail", "pass", "passTimelines_guess()"),
    ("method", "progress", "progress"),
    ("property_ro", "modified", "getModified"),
    ("method", "setOuterForce", "setOuterForce"),
    ("method", "getOuterForce", "getOuterForce"),
    ("method", "contains", "contains"),
)


D3D_MAP = {
    "DrawDeviceD3D": {
        "wrapper": ("0x534684", "0x497E78", "0x100236664", "0x235360"),
        "registrar": ("0x52A618", "0x492790", "0x10023070C", "0x22F622"),
        "factory": ("0x52B654", "0x492BFC", "0x100230C88", "0x22FB28"),
    },
    "D3D": {
        "wrapper": ("0x538A80", "0x49BD14", "0x10023B1C0", "0x23AC4C"),
        "registrar": ("0x52BC18", "0x492F10", "0x100230FF0", "0x22FDFA"),
        "factory": ("0x52CC54", "0x49337C", "0x10023156C", "0x230300"),
    },
    "D3DLayer": {
        "wrapper": ("0x53B9AC", "0x49E890", "0x10023F0A8", "0x23E83C"),
        "registrar": ("0x52CE8C", "0x49345C", "0x100231618", "0x230408"),
        "factory": ("0x52D308", "0x49361C", "0x1002317E8", "0x230594"),
    },
    "D3DImage": {
        "wrapper": ("0x53DA14", "0x49FE6C", "0x100240BC8", "0x2406A0"),
        "registrar": ("0x52D768", "0x493950", "0x100231AFC", "0x230932"),
        "factory": ("0x52D98C", "0x4939F8", "0x100231BE8", "0x2309DC"),
    },
    "D3DPicture": {
        "wrapper": ("0x53E774", "0x4A0A64", "0x100241AF0", "0x241810"),
        "registrar": ("0x52DCE0", "0x493BBC", "0x100231DD0", "0x230B86"),
        "factory": ("0x53F140", "0x4A1014", "0x10024227C", "0x2420A0"),
    },
    "D3DEmoteModule": {
        "wrapper": ("0x540EC4", "0x4A2B40", "0x100244320", "0x2446D4"),
        "registrar": ("0x52E388", "0x493E54", "0x100232078", "0x230DB0"),
        "factory": ("0x5416A8", "0x4A3060", "0x100244A08", "0x244EF8"),
    },
    "D3DEmotePlayer": {
        "wrapper": ("0x542178", "0x4A3AD0", "0x100245634", "0x245D28"),
        "registrar": ("0x52E8E4", "0x494078", "0x100232278", "0x230F46"),
        "factory": ("0x542B44", "0x4A4080", "0x100245DC0", "0x2465B8"),
    },
}


ROOT_ROWS = (
    (
        "motionplayer.dll", "motionplayer.dll", "static module root",
        ("0x42F1F8", "0x3016E8", "0x10014FC74", "0x151C98"),
        "analysis/motionplayer_reconstruction_scope_and_roots_four_binary_2026-08-26.md",
    ),
    (
        "emoteplayer.dll", "emoteplayer.dll", "pre-registration module root",
        ("0x42EEE0", "0x3013BC", "0x1001CAE20", "0x1C8EB2"),
        "analysis/motionplayer_emoteplayer_module_decrypt_root_four_binary_2026-08-27.md",
    ),
    (
        "DrawDeviceD3D.dll", "DrawDeviceD3D.dll", "static seven-class module root",
        ("0x42CBD8", "0x2FF094", "0x10024CB00", "0x24E6D8"),
        "analysis/motionplayer_drawdevice_d3d_ncb_surfaces_four_binary_2026-08-27.md",
    ),
    (
        "DrawDeviceD3DZ.dll", "DrawDeviceD3DZ.dll", "dependency-shim pre-registration root",
        ("0x5310F0", "0x495228", "0x100233668", "0x2324C0"),
        "analysis/motionplayer_drawdevice_d3d_ncb_surfaces_four_binary_2026-08-27.md",
    ),
)


SPECIAL_ROWS = (
    {
        "contract_id": "MOTIONPLAYER-TOPLEVEL-MOTION-CLASS",
        "module": "motionplayer.dll",
        "owner": "Motion",
        "sequence": "class",
        "kind": "class",
        "script_name": "Motion",
        "binding": "NCB_REGISTER_CLASS(Motion)",
        "argument_contract": "not applicable",
        "android_arm64": "registrar=0x6D6EE8",
        "android_armv7": "registrar=0x5991D0",
        "ios_arm64": "registrar=0x100125974",
        "ios_armv7": "registrar=0x124B7C",
        "registration_status": "EVIDENCED_4_4",
        "evidence_report": "analysis/motionplayer_motion_class_registration_surface_four_binary_2026-08-26.md",
    },
    {
        "contract_id": "MOTIONPLAYER-ATTACHED-BEZIERPATCH-CLASS",
        "module": "motionplayer.dll",
        "owner": "BezierPatch",
        "sequence": "class",
        "kind": "attached_class",
        "script_name": "BezierPatch",
        "binding": "NCB_ATTACH_CLASS(BezierPatch, Layer)",
        "argument_contract": "not applicable",
        "android_arm64": "attach=0x6E627C; setup=0x6E6428",
        "android_armv7": "attach=0x5A51C0; setup=0x5A52C0",
        "ios_arm64": "attach=0x100136FA4; setup=0x100137068",
        "ios_armv7": "attach=0x136B9C; setup=0x136D00",
        "registration_status": "EVIDENCED_4_4",
        "evidence_report": "analysis/motionplayer_bezier_layer_extensions_ncb_surface_four_binary_2026-08-27.md",
    },
    {
        "contract_id": "EMOTEPLAYER-DYNAMIC-EMOTEPLAYER-CLASS",
        "module": "emoteplayer.dll",
        "owner": "EmotePlayer",
        "sequence": "class",
        "kind": "class",
        "script_name": "EmotePlayer",
        "binding": "pre-registration publishes Motion.EmotePlayer",
        "argument_contract": "not applicable",
        "android_arm64": "wrapper=0x682FA0; registrar=0x67CEA8",
        "android_armv7": "wrapper=0x564E2C; registrar=0x5612E8",
        "ios_arm64": "wrapper=0x1001B8CD0; registrar=0x1001B5130",
        "ios_armv7": "wrapper=0x1B82B8; registrar=0x1B4DE0",
        "registration_status": "EVIDENCED_4_4",
        "evidence_report": "analysis/motionplayer_emoteplayer_ncb_surface_four_binary_2026-08-27.md",
    },
    {
        "contract_id": "EMOTEPLAYER-INJECTED-SETEMOTEPSBDECRYPTSEED",
        "module": "emoteplayer.dll",
        "owner": "ResourceManager",
        "sequence": "dynamic-1",
        "kind": "method_raw",
        "script_name": "setEmotePSBDecryptSeed",
        "binding": "emoteplayer pre-registration injected static seed setter",
        "argument_contract": "raw: argc>=1; Integer seed required; argv1+ ignored",
        "android_arm64": "callback=0x683110",
        "android_armv7": "callback=0x564EC0",
        "ios_arm64": "callback=0x1001B8D68",
        "ios_armv7": "callback=0x1B83AC",
        "registration_status": "EVIDENCED_4_4",
        "evidence_report": "analysis/motionplayer_emoteplayer_module_decrypt_root_four_binary_2026-08-27.md",
    },
    {
        "contract_id": "EMOTEPLAYER-INJECTED-SETEMOTEPSBDECRYPTFUNC",
        "module": "emoteplayer.dll",
        "owner": "ResourceManager",
        "sequence": "dynamic-2",
        "kind": "method_raw",
        "script_name": "setEmotePSBDecryptFunc",
        "binding": "emoteplayer pre-registration injected static callable setter",
        "argument_contract": "raw: argc>=1; strict ObjectClosure required; argv1+ ignored",
        "android_arm64": "callback=0x683240 internal entry",
        "android_armv7": "callback=0x564F58",
        "ios_arm64": "callback=0x1001B8E50",
        "ios_armv7": "callback=0x1B84D0",
        "registration_status": "EVIDENCED_4_4",
        "evidence_report": "analysis/motionplayer_emoteplayer_module_decrypt_root_four_binary_2026-08-27.md",
    },
    {
        "contract_id": "DRAWDEVICED3D-INTERNAL-D3DLAYERBASE",
        "module": "DrawDeviceD3D.dll",
        "owner": "<native-identity>",
        "sequence": "internal-1",
        "kind": "native_identity",
        "script_name": "D3DLayerBase",
        "binding": "TJSRegisterNativeClass plus sticky root-view ClassInfo",
        "argument_contract": "not applicable",
        "android_arm64": "publisher=0x53101C",
        "android_armv7": "publisher=0x49516C",
        "ios_arm64": "publisher=0x1002335C8",
        "ios_armv7": "publisher=0x2323C0",
        "registration_status": "EVIDENCED_4_4",
        "evidence_report": "analysis/motionplayer_drawdevice_d3d_dependency_root_four_binary_2026-08-27.md",
    },
    {
        "contract_id": "DRAWDEVICED3D-INTERNAL-D3DLAYEROBJECTNATIVEINSTANCE",
        "module": "DrawDeviceD3D.dll",
        "owner": "<native-identity>",
        "sequence": "internal-2",
        "kind": "native_identity",
        "script_name": "D3DLayerObjectNativeInstance",
        "binding": "TJSRegisterNativeClass plus borrowed-view class-id word",
        "argument_contract": "not applicable",
        "android_arm64": "publisher=0x53101C",
        "android_armv7": "publisher=0x49516C",
        "ios_arm64": "publisher=0x1002335C8",
        "ios_armv7": "publisher=0x2323C0",
        "registration_status": "EVIDENCED_4_4",
        "evidence_report": "analysis/motionplayer_drawdevice_d3d_dependency_root_four_binary_2026-08-27.md",
    },
)


def platform_fields(values: tuple[str, str, str, str], prefix: str) -> dict[str, str]:
    return {
        "android_arm64": f"{prefix}={values[0]}",
        "android_armv7": f"{prefix}={values[1]}",
        "ios_arm64": f"{prefix}={values[2]}",
        "ios_armv7": f"{prefix}={values[3]}",
    }


def d3d_contract(kind: str, owner: str, name: str) -> str:
    if kind == "factory":
        return {
            "DrawDeviceD3D": "raw factory: argc>=2; width,height required; surplus ignored; exact-one-Void empty-adaptor sentinel",
            "D3D": "raw factory: argc>=2; width,height required; surplus ignored; exact-one-Void empty-adaptor sentinel",
            "D3DLayer": "raw factory: argc>=1; D3DLayerBase Object required; surplus ignored; exact-one-Void empty-adaptor sentinel",
            "D3DImage": "raw factory: argc>=1; D3DLayerBase Object required; surplus ignored; exact-one-Void empty-adaptor sentinel",
            "D3DPicture": "typed factory: argc>=2; D3DLayer,D3DImage required; surplus ignored; exact-one-Void empty-adaptor sentinel",
            "D3DEmotePlayer": "typed factory: argc>=1; D3DLayer required; surplus ignored; exact-one-Void empty-adaptor sentinel",
        }[owner]
    if kind == "constructor":
        return "zero-arg typed constructor; any argc>=0 accepted and argv ignored; exact-one-Void empty-adaptor sentinel"
    if kind == "method_raw" and (owner, name) == ("D3DEmotePlayer", "load"):
        return "raw varargs: argc>=0; every supplied arg converted to ttstr in order; no defaults; no surplus because all argv consumed"
    if kind in {"method", "method_detail"}:
        return "typed fixed signature: every bound parameter required; no script-side optional default; surplus ignored"
    return "not applicable"


def add_d3d_rows(rows: list[dict[str, str]]) -> None:
    report = "analysis/motionplayer_drawdevice_d3d_ncb_surfaces_four_binary_2026-08-27.md"
    player_report = "analysis/motionplayer_d3demoteplayer_surface_factory_clone_todo_four_binary_2026-08-27.md"
    surfaces = {
        "DrawDeviceD3D": COMMON_ROOT_MEMBERS,
        "D3D": COMMON_ROOT_MEMBERS,
        "D3DLayer": LAYER_MEMBERS,
        "D3DImage": IMAGE_MEMBERS,
        "D3DPicture": PICTURE_MEMBERS,
        "D3DEmoteModule": MODULE_MEMBERS,
        "D3DEmotePlayer": PLAYER_CONSTANTS + PLAYER_MEMBERS,
    }
    factory_kinds = {
        "D3DEmoteModule": ("constructor", "NCB_CONSTRUCTOR(())"),
    }
    for owner, members in surfaces.items():
        mapping = D3D_MAP[owner]
        class_fields = platform_fields(mapping["wrapper"], "wrapper")
        rows.append({
            "contract_id": f"DRAWDEVICED3D-{owner.upper()}-CLASS",
            "module": "DrawDeviceD3D.dll",
            "owner": owner,
            "sequence": "1",
            "kind": "class",
            "script_name": owner,
            "binding": f"NCB_REGISTER_CLASS({owner})",
            "argument_contract": "not applicable",
            **class_fields,
            "registration_status": "EVIDENCED_4_4",
            "evidence_report": player_report if owner == "D3DEmotePlayer" else report,
        })
        kind, binding = factory_kinds.get(
            owner, ("factory", f"Factory(&{owner}::factory)")
        )
        factory_fields = platform_fields(mapping["factory"], "entry")
        rows.append({
            "contract_id": f"DRAWDEVICED3D-{owner.upper()}-{kind.upper()}",
            "module": "DrawDeviceD3D.dll",
            "owner": owner,
            "sequence": "2",
            "kind": kind,
            "script_name": f"<{kind}>",
            "binding": binding,
            "argument_contract": d3d_contract(kind, owner, f"<{kind}>") ,
            **factory_fields,
            "registration_status": "EVIDENCED_4_4",
            "evidence_report": player_report if owner == "D3DEmotePlayer" else report,
        })
        registrar_fields = platform_fields(mapping["registrar"], "registrar")
        for sequence, (member_kind, name, member_binding) in enumerate(members, 3):
            rows.append({
                "contract_id": f"DRAWDEVICED3D-{owner.upper()}-{sequence:03d}-{name.upper()}",
                "module": "DrawDeviceD3D.dll",
                "owner": owner,
                "sequence": str(sequence),
                "kind": member_kind,
                "script_name": name,
                "binding": member_binding,
                "argument_contract": d3d_contract(member_kind, owner, name),
                **registrar_fields,
                "registration_status": "EVIDENCED_4_4",
                "evidence_report": player_report if owner == "D3DEmotePlayer" else report,
            })


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--input",
        type=Path,
        default=Path("analysis/motionplayer_ncb_equivalence.tsv"),
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("analysis/motionplayer_registration_contracts.tsv"),
    )
    args = parser.parse_args()

    with args.input.open(encoding="utf-8", newline="") as handle:
        source = list(csv.DictReader(handle, delimiter="\t"))
    if len(source) != 316:
        raise ValueError(f"expected 316 NCB rows, found {len(source)}")
    if any(row["registration_status"] != "EVIDENCED_4_4" for row in source):
        raise ValueError("all 316 NCB rows must be EVIDENCED_4_4")
    raw_keys = {
        (row["owner"], row["script_name"])
        for row in source
        if row["local_kind"] == "method_raw"
    }
    if raw_keys != set(RAW_CONTRACTS):
        raise ValueError(f"raw callback contracts differ: {raw_keys!r}")
    ctor_owners = {
        row["owner"] for row in source if row["local_kind"] == "constructor"
    }
    if ctor_owners != set(CTOR_CONTRACTS):
        raise ValueError(f"constructor contracts differ: {ctor_owners!r}")
    factory_owners = {
        row["owner"] for row in source if row["local_kind"] == "factory"
    }
    if factory_owners != set(FACTORY_CONTRACTS):
        raise ValueError(f"factory contracts differ: {factory_owners!r}")

    rows: list[dict[str, str]] = []
    for row in source:
        rows.append({
            "contract_id": row["candidate_id"],
            "module": row["module"],
            "owner": row["owner"],
            "sequence": row["sequence"],
            "kind": row["local_kind"],
            "script_name": row["script_name"],
            "binding": row["binding"],
            "argument_contract": argument_contract(row),
            "android_arm64": row["android_arm64"],
            "android_armv7": row["android_armv7"],
            "ios_arm64": row["ios_arm64"],
            "ios_armv7": row["ios_armv7"],
            "registration_status": row["registration_status"],
            "evidence_report": row["evidence_report"],
        })

    add_d3d_rows(rows)
    for name, module, binding, targets, report in ROOT_ROWS:
        rows.append({
            "contract_id": f"MODULE-ROOT-{name.upper()}",
            "module": module,
            "owner": "<module-root>",
            "sequence": "0",
            "kind": "module_root",
            "script_name": name,
            "binding": binding,
            "argument_contract": "not applicable",
            **platform_fields(targets, "root"),
            "registration_status": "EVIDENCED_4_4",
            "evidence_report": report,
        })
    rows.extend(dict(row) for row in SPECIAL_ROWS)

    if len(rows) != 494:
        raise ValueError(f"expected 494 registration contracts, found {len(rows)}")
    ids = [row["contract_id"] for row in rows]
    if len(ids) != len(set(ids)):
        raise ValueError("duplicate registration contract ID")
    for row in rows:
        for field in FIELDS:
            if field not in row or row[field] == "":
                raise ValueError(f"{row['contract_id']} has empty {field}")
            if any(ch in row[field] for ch in "\t\r\n"):
                raise ValueError(f"{row['contract_id']} has embedded control text")

    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=FIELDS, delimiter="\t")
        writer.writeheader()
        writer.writerows(rows)
    print(f"wrote {len(rows)} registration contract rows to {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
