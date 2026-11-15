#pragma once

// The DirectWrite / Direct2D back end of EditorStyle.h (Windows only).
#ifndef NOMINMAX
 #define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
 #define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include "EditorStyle.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace chordengine::ui::style
{
using Microsoft::WRL::ComPtr;

std::wstring widen(const std::string& text);
std::string narrow(const std::wstring& text);

// Process-wide factories (created on first use; null when unavailable).
ID2D1Factory* d2dFactory();
IDWriteFactory* dwriteFactory();

// The DirectWrite font size (DIPs) that gives `font` its ascent + descent.
float fontEmSize(const Font& font);
// The GDI face name for a face (native controls: the licence key field).
std::wstring gdiFaceName(Face face);

// The module (DLL or EXE) this code is linked into.
HMODULE thisModule();

// The device-dependent objects of one render target.
struct D2DResources
{
    ID2D1RenderTarget* target = nullptr; // the target they were made for
    ComPtr<ID2D1SolidColorBrush> brush;
    ComPtr<ID2D1StrokeStyle> roundStroke;
    std::map<std::uint32_t, ComPtr<ID2D1Bitmap>> marks;
    void reset()
    {
        target = nullptr;
        brush.Reset();
        roundStroke.Reset();
        marks.clear();
    }
};

// A Canvas on a Direct2D render target whose unit is the point (DIP).
class D2DCanvas final : public Canvas
{
public:
    D2DCanvas(ID2D1RenderTarget& target, D2DResources& resources);
    ~D2DCanvas() override;

    void fillRect(Rect rect, Colour colour) override;
    void fillRoundedRect(Rect rect, float radius, Colour colour) override;
    void strokeRoundedRect(Rect rect, float radius, float lineWidth, Colour colour) override;
    void fillEllipse(Rect rect, Colour colour) override;
    void strokeEllipse(Rect rect, float lineWidth, Colour colour) override;
    void fillBottomRoundedGradient(Rect rect, float radius, Colour top, Colour bottom) override;
    void strokePolyline(const std::vector<Point>& points, float lineWidth, Colour colour) override;
    void drawText(const std::string& text, const Font& font, Colour colour, float x, float baseline,
                  float horizontalScale) override;
    void drawMusicProdMark(Rect target, Colour tone, float opacity) override;
    void save() override;
    void restore() override;
    void translate(float dx, float dy) override;
    void clipTo(Rect rect) override;
    void beginLayer(float opacity) override;
    void endLayer() override;

private:
    ID2D1SolidColorBrush* brush(Colour colour);
    struct State
    {
        D2D1_MATRIX_3X2_F transform;
        int clips = 0;
    };
    ID2D1RenderTarget& target_;
    D2DResources& resources_;
    std::vector<State> states_;
    int clips_ = 0;
    std::vector<bool> layers_; // true = a D2D layer was pushed
};
} // namespace chordengine::ui::style
