#include "platform/CCPlatformConfig.h"
#if CC_TARGET_PLATFORM == CC_PLATFORM_EMSCRIPTEN

#include "platform/CCDevice.h"
#include "platform/CCFileUtils.h"
#include <emscripten.h>
#include <cstring>
#include <algorithm>
#include <vector>
#include <string>
#include <map>
#include <cwctype>

#include "ft2build.h"
#include FT_FREETYPE_H

EM_JS(int, _cc_canvas_render_text, (const char* textPtr, int fontSize,
    int fillR, int fillG, int fillB, int fillA,
    int dimW, int dimH, int alignVal), {
    var text = UTF8ToString(textPtr);
    if (!text || text.length === 0) return 0;

    var canvas = document.createElement('canvas');
    var ctx = canvas.getContext('2d');
    var fontStr = fontSize + 'px sans-serif';
    ctx.font = fontStr;

    var lines = text.split('\n');
    var lineHeight = Math.ceil(fontSize * 1.3);
    var maxW = 0;
    for (var i = 0; i < lines.length; i++) {
        var m = ctx.measureText(lines[i]);
        if (m.width > maxW) maxW = m.width;
    }
    var w = Math.ceil(maxW) || 1;
    var h = lineHeight * lines.length || 1;
    if (dimW > 0 && dimW > w) w = dimW;
    if (dimH > 0 && dimH > h) h = dimH;

    canvas.width = w;
    canvas.height = h;
    ctx.font = fontStr;
    ctx.clearRect(0, 0, w, h);
    ctx.fillStyle = 'rgba(' + fillR + ',' + fillG + ',' + fillB + ',' + (fillA / 255.0) + ')';
    ctx.textBaseline = 'top';

    for (var i = 0; i < lines.length; i++) {
        var x = 0;
        if (alignVal === 1) x = (w - ctx.measureText(lines[i]).width) / 2;
        else if (alignVal === 2) x = w - ctx.measureText(lines[i]).width;
        ctx.fillText(lines[i], x, i * lineHeight);
    }

    var imageData = ctx.getImageData(0, 0, w, h);
    var totalBytes = 8 + w * h * 4;
    var buf = _malloc(totalBytes);
    HEAP32[buf >> 2] = w;
    HEAP32[(buf + 4) >> 2] = h;
    HEAPU8.set(imageData.data, buf + 8);
    return buf;
});

NS_CC_BEGIN

int Device::getDPI()
{
    return 96;
}

void Device::setAccelerometerEnabled(bool isEnabled) {}
void Device::setAccelerometerInterval(float interval) {}

struct LineBreakGlyph {
    FT_UInt glyphIndex;
    int paintPosition;
    int glyphWidth;
    int bearingX;
    int kerning;
    int horizAdvance;
};

struct LineBreakLine {
    std::vector<LineBreakGlyph> glyphs;
    int lineWidth = 0;

    void reset() { glyphs.clear(); lineWidth = 0; }

    void calculateWidth() {
        lineWidth = 0;
        if (!glyphs.empty()) {
            auto& g = glyphs.back();
            lineWidth = g.paintPosition + std::max(g.glyphWidth, g.horizAdvance - g.bearingX);
        }
    }
};

class BitmapDC {
public:
    BitmapDC() {
        libError = FT_Init_FreeType(&library);
        _data = nullptr;
    }

    ~BitmapDC() {
        FT_Done_FreeType(library);
        reset();
    }

    void reset() {
        iMaxLineWidth = 0;
        iMaxLineHeight = 0;
        textLines.clear();
    }

    static int utf8(const char** p) {
        const unsigned char* s = (const unsigned char*)*p;
        if ((*s & 0x80) == 0x00) { *p += 1; return s[0]; }
        if ((*s & 0xE0) == 0xC0) { *p += 2; return ((s[0] & 0x1F) << 6) | (s[1] & 0x3F); }
        if ((*s & 0xF0) == 0xE0) { *p += 3; return ((s[0] & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F); }
        if ((*s & 0xF8) == 0xF0) { *p += 4; return ((s[0] & 0x07) << 18) | ((s[1] & 0x3F) << 12) | ((s[2] & 0x3F) << 6) | (s[3] & 0x3F); }
        *p += 1;
        return 0;
    }

    bool divideString(FT_Face face, const char* sText, int iMaxWidth, int iMaxHeight) {
        const char* pText = sText;
        textLines.clear();
        iMaxLineWidth = 0;

        FT_UInt prevCharacter = 0;
        FT_UInt prevGlyphIndex = 0;
        FT_Vector delta;
        LineBreakLine currentLine;
        int currentPaintPosition = 0;
        int lastBreakIndex = -1;
        bool hasKerning = FT_HAS_KERNING(face);

        int unicode;
        while ((unicode = utf8(&pText))) {
            if (unicode == '\n') {
                currentLine.calculateWidth();
                iMaxLineWidth = std::max(iMaxLineWidth, currentLine.lineWidth);
                textLines.push_back(currentLine);
                currentLine.reset();
                prevGlyphIndex = 0;
                prevCharacter = 0;
                lastBreakIndex = -1;
                currentPaintPosition = 0;
                continue;
            }

            FT_UInt glyphIndex = FT_Get_Char_Index(face, unicode);
            if (FT_Load_Glyph(face, glyphIndex, FT_LOAD_DEFAULT))
                continue;

            LineBreakGlyph glyph;
            glyph.glyphIndex = glyphIndex;
            glyph.glyphWidth = face->glyph->metrics.width >> 6;
            glyph.bearingX = face->glyph->metrics.horiBearingX >> 6;
            glyph.horizAdvance = face->glyph->metrics.horiAdvance >> 6;
            glyph.kerning = 0;

            if (prevGlyphIndex != 0 && hasKerning) {
                FT_Get_Kerning(face, prevGlyphIndex, glyphIndex, FT_KERNING_DEFAULT, &delta);
                glyph.kerning = delta.x >> 6;
            }

            if (std::iswspace(unicode)) {
                lastBreakIndex = (int)currentLine.glyphs.size();
            } else if (std::iswspace(prevCharacter)) {
                lastBreakIndex = (int)currentLine.glyphs.size();
            } else if (prevCharacter == '-' || prevCharacter == '/' || prevCharacter == '\\') {
                lastBreakIndex = (int)currentLine.glyphs.size() - 1;
            }

            if (iMaxWidth > 0 && !std::iswspace(unicode) &&
                currentPaintPosition + glyph.bearingX + glyph.kerning + glyph.glyphWidth > iMaxWidth) {
                if (lastBreakIndex >= 0 && lastBreakIndex < (int)currentLine.glyphs.size()) {
                    std::vector<LineBreakGlyph> overflow(
                        currentLine.glyphs.begin() + lastBreakIndex, currentLine.glyphs.end());
                    currentLine.glyphs.erase(
                        currentLine.glyphs.begin() + lastBreakIndex, currentLine.glyphs.end());
                    currentLine.calculateWidth();
                    iMaxLineWidth = std::max(iMaxLineWidth, currentLine.lineWidth);
                    textLines.push_back(currentLine);
                    currentLine.reset();
                    currentPaintPosition = 0;
                    for (auto& og : overflow) {
                        if (currentLine.glyphs.empty()) {
                            currentPaintPosition = -og.bearingX;
                            og.kerning = 0;
                        }
                        og.paintPosition = currentPaintPosition + og.bearingX + og.kerning;
                        currentLine.glyphs.push_back(og);
                        currentPaintPosition += og.kerning + og.horizAdvance;
                    }
                } else {
                    currentLine.calculateWidth();
                    iMaxLineWidth = std::max(iMaxLineWidth, currentLine.lineWidth);
                    textLines.push_back(currentLine);
                    currentLine.reset();
                    currentPaintPosition = 0;
                    glyph.kerning = 0;
                }
                prevGlyphIndex = 0;
                prevCharacter = 0;
                lastBreakIndex = -1;
            } else {
                prevGlyphIndex = glyphIndex;
                prevCharacter = unicode;
            }

            if (currentLine.glyphs.empty())
                currentPaintPosition = -glyph.bearingX;
            glyph.paintPosition = currentPaintPosition + glyph.bearingX + glyph.kerning;
            currentLine.glyphs.push_back(glyph);
            currentPaintPosition += glyph.kerning + glyph.horizAdvance;
        }

        if (!currentLine.glyphs.empty()) {
            currentLine.calculateWidth();
            iMaxLineWidth = std::max(iMaxLineWidth, currentLine.lineWidth);
            textLines.push_back(currentLine);
        }
        return true;
    }

    int computeLineStartX(Device::TextAlign align, int line) {
        int lineWidth = textLines.at(line).lineWidth;
        if (align == Device::TextAlign::CENTER || align == Device::TextAlign::TOP || align == Device::TextAlign::BOTTOM)
            return (iMaxLineWidth - lineWidth) / 2;
        if (align == Device::TextAlign::RIGHT || align == Device::TextAlign::TOP_RIGHT || align == Device::TextAlign::BOTTOM_RIGHT)
            return iMaxLineWidth - lineWidth;
        return 0;
    }

    int computeLineStartY(FT_Face face, Device::TextAlign align, int txtHeight, int borderHeight) {
        int baseline = (int)std::ceil(face->size->metrics.ascender / 64.0f);
        if (align == Device::TextAlign::CENTER || align == Device::TextAlign::LEFT || align == Device::TextAlign::RIGHT)
            return (borderHeight - txtHeight) / 2 + baseline;
        if (align == Device::TextAlign::BOTTOM_RIGHT || align == Device::TextAlign::BOTTOM || align == Device::TextAlign::BOTTOM_LEFT)
            return borderHeight - txtHeight + baseline;
        return baseline;
    }

    std::string getFontFile(const char* fontName) {
        static std::map<std::string, std::string> cache;
        auto it = cache.find(fontName);
        if (it != cache.end()) return it->second;

        std::string path = fontName;
        std::string lower = path;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

        if (lower.find(".ttf") != std::string::npos || lower.find(".ttc") != std::string::npos || lower.find(".otf") != std::string::npos) {
            std::string fullPath = FileUtils::getInstance()->fullPathForFilename(path);
            if (!fullPath.empty() && FileUtils::getInstance()->isFileExist(fullPath)) {
                cache[fontName] = fullPath;
                return fullPath;
            }
        }

        auto tryFont = [&](const char* name) -> std::string {
            std::string fp = FileUtils::getInstance()->fullPathForFilename(name);
            if (!fp.empty() && FileUtils::getInstance()->isFileExist(fp)) return fp;
            return "";
        };
        std::string result;
        if (!(result = tryFont("NotoSansCJK-Regular.ttc")).empty()) { cache[fontName] = result; return result; }
        if (!(result = tryFont("DroidSansFallback.ttf")).empty()) { cache[fontName] = result; return result; }

        cache[fontName] = path;
        return path;
    }

    bool getBitmapCanvas(const char* text, const FontDefinition& textDef, Device::TextAlign align) {
        int fontSize = static_cast<int>(textDef._fontSize);
        if (fontSize <= 0) fontSize = 24;

        int alignVal = 0;
        if (align == Device::TextAlign::CENTER || align == Device::TextAlign::TOP || align == Device::TextAlign::BOTTOM)
            alignVal = 1;
        else if (align == Device::TextAlign::RIGHT || align == Device::TextAlign::TOP_RIGHT || align == Device::TextAlign::BOTTOM_RIGHT)
            alignVal = 2;

        int resultPtr = _cc_canvas_render_text(text, fontSize,
            textDef._fontFillColor.r, textDef._fontFillColor.g,
            textDef._fontFillColor.b, textDef._fontAlpha,
            (int)textDef._dimensions.width, (int)textDef._dimensions.height,
            alignVal);
        if (!resultPtr) return false;

        int* header = reinterpret_cast<int*>(resultPtr);
        iMaxLineWidth = header[0];
        iMaxLineHeight = header[1];
        if (iMaxLineWidth <= 0 || iMaxLineHeight <= 0) {
            free(header);
            return false;
        }
        int dataSize = iMaxLineWidth * iMaxLineHeight * 4;
        _data = static_cast<unsigned char*>(malloc(dataSize));
        memcpy(_data, reinterpret_cast<unsigned char*>(resultPtr) + 8, dataSize);
        free(header);
        return true;
    }

    bool getBitmap(const char* text, const FontDefinition& textDef, Device::TextAlign align) {
        if (libError) return getBitmapCanvas(text, textDef, align);

        std::string fontFile = getFontFile(textDef._fontName.c_str());

        FT_Face face;
        if (FT_New_Face(library, fontFile.c_str(), 0, &face)) {
            return getBitmapCanvas(text, textDef, align);
        }

        if (FT_Select_Charmap(face, FT_ENCODING_UNICODE)) {
            FT_Done_Face(face);
            return false;
        }

        int fontSize = static_cast<int>(textDef._fontSize);
        if (FT_Set_Pixel_Sizes(face, fontSize, fontSize)) {
            FT_Done_Face(face);
            return false;
        }

        if (!divideString(face, text, (int)textDef._dimensions.width, (int)textDef._dimensions.height)) {
            FT_Done_Face(face);
            return false;
        }

        iMaxLineWidth = std::max(iMaxLineWidth, (int)textDef._dimensions.width);
        if (iMaxLineWidth <= 0) iMaxLineWidth = 1;

        int lineHeight = face->size->metrics.height >> 6;
        int txtHeight = lineHeight * (int)textLines.size();
        iMaxLineHeight = std::max(txtHeight, (int)textDef._dimensions.height);
        if (iMaxLineHeight <= 0) iMaxLineHeight = 1;

        int dataSize = iMaxLineWidth * iMaxLineHeight * 4;
        _data = (unsigned char*)malloc(dataSize);
        memset(_data, 0, dataSize);

        unsigned char r = textDef._fontFillColor.r;
        unsigned char g = textDef._fontFillColor.g;
        unsigned char b = textDef._fontFillColor.b;
        unsigned char a = textDef._fontAlpha;

        int curY = computeLineStartY(face, align, txtHeight, iMaxLineHeight);

        for (int line = 0; line < (int)textLines.size(); line++) {
            int curX = computeLineStartX(align, line);

            for (int i = 0; i < (int)textLines[line].glyphs.size(); i++) {
                auto& glyph = textLines[line].glyphs[i];

                if (FT_Load_Glyph(face, glyph.glyphIndex, FT_LOAD_RENDER))
                    continue;

                FT_Bitmap& bitmap = face->glyph->bitmap;
                int yoff = curY - (face->glyph->metrics.horiBearingY >> 6);
                int xoff = curX + glyph.paintPosition;

                for (int y = 0; y < (int)bitmap.rows; ++y) {
                    int iY = yoff + y;
                    if (iY < 0) continue;
                    if (iY >= iMaxLineHeight) break;

                    for (int x = 0; x < (int)bitmap.width; ++x) {
                        int iX = xoff + x;
                        if (iX < 0) continue;
                        if (iX >= iMaxLineWidth) break;

                        unsigned char alpha = bitmap.buffer[y * bitmap.width + x];
                        if (alpha == 0) continue;

                        unsigned char ga = (unsigned char)((alpha * a) / 255);
                        int idx = (iY * iMaxLineWidth + iX) * 4;
                        _data[idx + 0] = (r * ga) / 255;
                        _data[idx + 1] = (g * ga) / 255;
                        _data[idx + 2] = (b * ga) / 255;
                        _data[idx + 3] = ga;
                    }
                }
            }
            curY += lineHeight;
        }

        FT_Done_Face(face);
        return true;
    }

    FT_Library library;
    unsigned char* _data;
    int libError;
    std::vector<LineBreakLine> textLines;
    int iMaxLineWidth = 0;
    int iMaxLineHeight = 0;
};

static BitmapDC& sharedBitmapDC() {
    static BitmapDC s_dc;
    return s_dc;
}

Data Device::getTextureDataForText(const char* text,
                                   const FontDefinition& textDefinition,
                                   TextAlign align,
                                   int& width, int& height,
                                   bool& hasPremultipliedAlpha)
{
    Data ret;
    do {
        BitmapDC& dc = sharedBitmapDC();
        if (!dc.getBitmap(text, textDefinition, align)) break;
        if (!dc._data) break;
        width = dc.iMaxLineWidth;
        height = dc.iMaxLineHeight;
        dc.reset();
        ret.fastSet(dc._data, width * height * 4);
        hasPremultipliedAlpha = true;
    } while (0);
    return ret;
}

void Device::setKeepScreenOn(bool) {}
void Device::vibrate(float) {}

NS_CC_END

#endif // CC_TARGET_PLATFORM == CC_PLATFORM_EMSCRIPTEN
