//---------------------------------------------------------------------------
/*
        TVP2 ( T Visual Presenter 2 )  A script authoring tool
        Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

        See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// KAG Parser Utility Class
//---------------------------------------------------------------------------

#ifndef KAGParserH
#define KAGParserH
//---------------------------------------------------------------------------

#define TVP_KAGPARSER_EX_CLASSNAME TJS_W("KAGParser")

#include "tjs.h"
#include "tjsNative.h"
#include "tjsArray.h"
#include "StorageImpl.h"
#include "MsgIntf.h"
#include "CharacterSet.h"
#include "TransIntf.h"
#include "tjsHashSearch.h"
#include <vector>

using namespace TJS;
/*[*/
//---------------------------------------------------------------------------
// KAG Parser debug level
//---------------------------------------------------------------------------
enum tTVPKAGDebugLevel {
    tkdlNone, // none is reported
    tkdlSimple, // simple report
    tkdlVerbose // complete report ( verbose )
};
/*]*/

//---------------------------------------------------------------------------
// tTVPCharHolder
//---------------------------------------------------------------------------
class tTVPCharHolder {
    tjs_char *Buffer;
    size_t BufferSize;

public:
    tTVPCharHolder() : Buffer(nullptr), BufferSize(0) {}

    ~tTVPCharHolder() { Clear(); }

    tTVPCharHolder(const tTVPCharHolder &ref) : Buffer(nullptr), BufferSize(0) {
        operator=(ref);
    }

    void Clear() {
        if(Buffer)
            delete[] Buffer, Buffer = nullptr;
        BufferSize = 0;
    }

    void operator=(const tTVPCharHolder &ref) {
        Clear();
        BufferSize = ref.BufferSize;
        Buffer = new tjs_char[BufferSize];
        memcpy(Buffer, ref.Buffer, BufferSize * sizeof(tjs_char));
    }

    void operator=(const tjs_char *ref) {
        Clear();
        if(ref) {
            BufferSize = TJS_strlen(ref) + 1;
            Buffer = new tjs_char[BufferSize];
            memcpy(Buffer, ref, BufferSize * sizeof(tjs_char));
        }
    }

    operator const tjs_char *() const { return Buffer; }

    operator tjs_char *() { return Buffer; }
};
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// tTVPScenarioCacheItem : Scenario Cache Item
//---------------------------------------------------------------------------
class tTVPScenarioCacheItem {
public:
    struct tLine {
        const tjs_char *Start;
        tjs_int Length;
    };

private:
    tTVPCharHolder Buffer;
    tLine *Lines;
    tjs_int LineCount;

public:
    struct tLabelCacheData {
        tjs_int Line;
        tjs_int Count;

        tLabelCacheData(tjs_int line, tjs_int count) {
            Line = line;
            Count = count;
        }
    };

public:
    typedef tTJSHashTable<ttstr, tLabelCacheData> tLabelCacheHash;

private:
    tLabelCacheHash LabelCache; // Label cache
    std::vector<ttstr> LabelAliases;
    bool LabelCached; // whether the label is cached

    tjs_int RefCount;

public:
    tTVPScenarioCacheItem(const ttstr &name, bool istring);

protected:
    ~tTVPScenarioCacheItem();

public:
    void AddRef();

    void Release();

private:
    void LoadScenario(const ttstr &name, bool isstring);
    // load file or string to buffer
public:
    const ttstr &GetLabelAliasFromLine(tjs_int line) const {
        return LabelAliases[line];
    }

    void EnsureLabelCache();

    tLine *GetLines() const { return Lines; }

    tjs_int GetLineCount() const { return LineCount; }

    const tLabelCacheHash &GetLabelCache() const { return LabelCache; }
};
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// tTJSNI_KAGParser
//---------------------------------------------------------------------------
class tTJSNI_KAGParser : public tTJSNativeInstance {
    typedef tTJSNativeInstance inherited;

public:
    tTJSNI_KAGParser();

    tjs_error Construct(tjs_int numparams, tTJSVariant **param,
                        iTJSDispatch2 *tjs_obj) override;

    void Invalidate() override;

private:
    iTJSDispatch2 *Owner; // owner object

    iTJSDispatch2 *DicClear; // Dictionary.Clear method pointer
    iTJSDispatch2 *DicAssign; // Dictionary
    iTJSDispatch2 *DicObj; // DictionaryObject

    iTJSDispatch2 *TagListClear; // Array.clear method pointer (for TagList)
    iTJSDispatch2 *TagListAdd; // Array.add method pointer (for TagList)
    iTJSDispatch2 *TagList; // ordered list of the current tag's attribute
                            // names (Array); attached as the "taglist" member
                            // of DicObj. libkrkr2.so sub_561F3C @0x561F3C keeps
                            // this array at parser+24, clears it together with
                            // DicObj and Array.add's each stored member name

    iTJSDispatch2 *ArrayAssign; // Array.assign method pointer
                                // libkrkr2.so qword_1AB3C10 (取自 Array class
                                // "assign")；用于把 names-array 深拷进 MacroArgs
                                // 元素的 second 字段（sub_569A18/sub_5698BC）

    iTJSDispatch2 *ArrayPush; // Array.push method pointer
                              // libkrkr2.so qword_1AB27E0 (取自 Array class
                              // "push")；Store 序列化每个 MacroArgs 槽时一次
                              // push(key,value) 两元素到扁平 [k,v,k,v...] 数组
                              // (serializeMacroArg @0x54B1C8)

    iTJSDispatch2 *Macros; // Macro Dictionary Object

    // Macro arguments: libkrkr2.so 把每个宏参数槽存为 {values-dict, names-array}
    // 配对的 16B 元素 vector（parser+56..+80, 元素 +0=first=values-dict,
    // +8=second=names-array）。first 是宏实参字典，second 是按源码顺序记录的属性
    // 名 Array（与 DicObj/TagList 并行）。sub_569A18/sub_5698BC 构造，
    // GetMacroTopNoAddRef 返回 .first，'*' 转发分支按 .second 的 PropGetByNum
    // 有序枚举。详见 analysis 与 sub_561F3C @0x5666c0/@0x564080。
    std::vector<std::pair<iTJSDispatch2 *, iTJSDispatch2 *>> MacroArgs;
    tjs_uint MacroArgStackDepth;
    tjs_uint MacroArgStackBase;

    struct tCallStackData {
        ttstr Storage; // caller storage
        ttstr Label; // caller nearest label
        tjs_int Offset; // line offset from the label
        ttstr OrgLineStr; // original line string
        ttstr LineBuffer; // line string (if alive)
        tjs_int Pos;
        bool LineBufferUsing; // whether LineBuffer is used or not
        tjs_uint MacroArgStackBase;
        tjs_uint MacroArgStackDepth;
        std::vector<tjs_int> ExcludeLevelStack;
        std::vector<bool> IfLevelExecutedStack;
        tjs_int ExcludeLevel;
        tjs_int IfLevel;

        tCallStackData(const ttstr &storage, const ttstr &label, tjs_int offset,
                       const ttstr &orglinestr, const ttstr &linebuffer,
                       tjs_int pos, bool linebufferusing,
                       tjs_uint macroargstackbase, tjs_uint macroargstackdepth,
                       const std::vector<tjs_int> &excludelevelstack,
                       tjs_int excludelevel,
                       const std::vector<bool> &iflevelexecutedstack,
                       tjs_int iflevel) :
            Storage(storage), Label(label), Offset(offset),
            OrgLineStr(orglinestr), LineBuffer(linebuffer), Pos(pos),
            LineBufferUsing(linebufferusing),
            MacroArgStackBase(macroargstackbase),
            MacroArgStackDepth(macroargstackdepth),
            ExcludeLevelStack(excludelevelstack), ExcludeLevel(excludelevel),
            IfLevelExecutedStack(iflevelexecutedstack), IfLevel(iflevel) {}
    };

    std::vector<tCallStackData> CallStack;

    tTVPScenarioCacheItem *Scenario;
    tTVPScenarioCacheItem::tLine *Lines; // is copied from Scenario
    tjs_int LineCount; // is copied from Scenario

    ttstr StorageName;
    ttstr StorageShortName;

    tjs_int CurLine; // current processing line
    tjs_int CurPos; // current processing position ( column )
    const tjs_char *CurLineStr; // current line string
    ttstr LineBuffer; // line buffer ( if any macro/emb was expanded )
    bool LineBufferUsing;
    ttstr CurLabel; // Current Label
    ttstr CurPage; // Current Page Name
    tjs_int TagLine; // line number of previous tag

    tTVPKAGDebugLevel DebugLevel; // debugging log level
    bool ProcessSpecialTags; // whether to process special tags
    bool IgnoreCR; // CR is not interpreted as [r] tag when this is
                   // true
    bool RecordingMacro; // recording a macro
    ttstr RecordingMacroStr; // recording macro content
    ttstr RecordingMacroName; // recording macro's name

    tTJSVariant ValueVariant;

    tjs_int ExcludeLevel;
    tjs_int IfLevel;

    std::vector<tjs_int> ExcludeLevelStack;
    std::vector<bool> IfLevelExecutedStack;

    bool Interrupted;

public:
    void operator=(const tTJSNI_KAGParser &ref);

    iTJSDispatch2 *Store();

    void Restore(iTJSDispatch2 *dic);

    void Clear(); // clear all states

private:
    void ClearBuffer(); // clear internal buffer

    void Rewind(); // set current position to first

    void BreakConditionAndMacro(); // break condition state and macro
                                   // expansion

public:
    void LoadScenario(const ttstr &name);

    const ttstr &GetStorageName() const { return StorageName; }

    void GoToLabel(const ttstr &name); // search label and set current position
    void GoToStorageAndLabel(const ttstr &storage, const ttstr &label);

    void CallLabel(const ttstr &name);

private:
    bool SkipCommentOrLabel(); // skip comment or label and go to next line

    // libkrkr2.so 的 push 源是 {DicObj@+16, TagList@+24}：values=DicObj、
    // names=TagList。深拷 values→新元素 .first、names→.second。
    void PushMacroArgs(iTJSDispatch2 *values, iTJSDispatch2 *names);

public:
    void PopMacroArgs();

private:
    void ClearMacroArgs();

    void PopMacroArgsTo(tjs_uint base);

    void FindNearestLabel(tjs_int start, tjs_int &labelline, ttstr &labelname);

    void PushCallStack();

    void PopCallStack(const ttstr &storage, const ttstr &label);

    void StoreIntStackToDic(iTJSDispatch2 *dic, std::vector<tjs_int> &stack,
                            const tjs_char *membername);

    void StoreBoolStackToDic(iTJSDispatch2 *dic, std::vector<bool> &stack,
                             const tjs_char *membername);

    void RestoreIntStackFromStr(std::vector<tjs_int> &stack, const ttstr &str);

    void RestoreBoolStackFromStr(std::vector<bool> &stack, const ttstr &str);

public:
    void ClearCallStack();

    void Interrupt() { Interrupted = true; };

    void ResetInterrupt() { Interrupted = false; };

private:
    iTJSDispatch2 *_GetNextTag();
    // taglist accumulator helpers (libkrkr2.so sub_561F3C @0x561F3C)
    void TagListClearItems(); // clear TagList (qword_1AB3C08 clear)
    void TagListAddName(const ttstr &name); // Array.add (qword_1AB3C18)
    void AttachTagList(); // DicObj.taglist = TagList (sub_568F88 @0x568F88)

public:
    iTJSDispatch2 *GetNextTag();

    const ttstr &GetCurLabel() const { return CurLabel; }

    tjs_int GetCurLine() const { return CurLine; }

    tjs_int GetCurPos() const { return CurPos; }

    const tjs_char *GetCurLineStr() const { return CurLineStr; }

    void SetProcessSpecialTags(bool b) { ProcessSpecialTags = b; }

    bool GetProcessSpecialTags() const { return ProcessSpecialTags; }

    void SetIgnoreCR(bool b) { IgnoreCR = b; }

    bool GetIgnoreCR() const { return IgnoreCR; }

    void SetDebugLevel(tTVPKAGDebugLevel level) { DebugLevel = level; }

    tTVPKAGDebugLevel GetDebugLevel() const { return DebugLevel; }

    iTJSDispatch2 *GetMacrosNoAddRef() const { return Macros; }

    iTJSDispatch2 *GetMacroTopNoAddRef() const;
    // get current macro argument (parameters)

    tjs_int GetCallStackDepth() const { return CallStack.size(); }

    void Assign(const tTJSNI_KAGParser &ref) { operator=(ref); }
};

extern iTJSDispatch2 *TVPCreateNativeClass_KAGParser();


#endif
