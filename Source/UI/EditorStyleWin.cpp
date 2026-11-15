// The Windows back end of EditorStyle.h: DirectWrite faces and metrics (the
// bundled Inter / Schibsted Grotesk loaded privately from the bundle's
// Resources, nothing installed), the Direct2D Canvas, and the re-tinted
// Music-Prod mark through WIC.
#include "EditorStyleWin.h"

#include <d2d1helper.h>
#include <dwrite_1.h>
#include <dwrite_3.h>
#include <wincodec.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <mutex>
#include <unordered_map>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")

namespace chordengine::ui::style
{
std::wstring widen(const std::string& text)
{
    if (text.empty())
        return {};
    const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(std::max(0, length)), L'\0');
    if (length > 0)
        MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), length);
    return out;
}

std::string narrow(const std::wstring& text)
{
    if (text.empty())
        return {};
    const int length = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0,
                                           nullptr, nullptr);
    std::string out(static_cast<std::size_t>(std::max(0, length)), '\0');
    if (length > 0)
        WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), length, nullptr,
                            nullptr);
    return out;
}

HMODULE thisModule()
{
    HMODULE module = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&thisModule), &module);
    return module;
}

ID2D1Factory* d2dFactory()
{
    static ComPtr<ID2D1Factory> factory = []
    {
        ComPtr<ID2D1Factory> created;
        D2D1_FACTORY_OPTIONS options {};
        D2D1CreateFactory(D2D1_FACTORY_TYPE_MULTI_THREADED, __uuidof(ID2D1Factory), &options,
                          reinterpret_cast<void**>(created.GetAddressOf()));
        return created;
    }();
    return factory.Get();
}

IDWriteFactory* dwriteFactory()
{
    static ComPtr<IDWriteFactory> factory = []
    {
        ComPtr<IDWriteFactory> created;
        DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                            reinterpret_cast<IUnknown**>(created.GetAddressOf()));
        return created;
    }();
    return factory.Get();
}

// ----------------------------------------------------------------- files
std::filesystem::path resourceFile(const std::string& name)
{
    static const std::vector<std::filesystem::path> roots = []
    {
        std::vector<std::filesystem::path> found;
        wchar_t buffer[MAX_PATH * 4] {};
        if (GetModuleFileNameW(thisModule(), buffer, static_cast<DWORD>(std::size(buffer))) != 0)
        {
            // <bundle>.vst3/Contents/x86_64-win/<name>.vst3 -> Contents/Resources
            const std::filesystem::path binary(buffer);
            found.push_back(binary.parent_path().parent_path() / "Resources");
            found.push_back(binary.parent_path() / "Resources");
        }
#ifdef CHORDENGINE_RESOURCE_DIR
        found.emplace_back(CHORDENGINE_RESOURCE_DIR);
#endif
        return found;
    }();
    std::error_code error;
    for (const auto& root : roots)
        for (const auto& candidate : { root / name, root / "Fonts" / name })
            if (std::filesystem::exists(candidate, error))
                return candidate;
    return {};
}

// ----------------------------------------------------------------- faces
namespace
{
struct FaceInfo
{
    ComPtr<IDWriteFontCollection> collection; // null = the system collection
    std::wstring family;
    DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL;
    DWRITE_FONT_STYLE style = DWRITE_FONT_STYLE_NORMAL;
    DWRITE_FONT_STRETCH stretch = DWRITE_FONT_STRETCH_NORMAL;
    float heightPerEm = 1.2f;  // (ascent + descent) / em
    float ascentPerEm = 0.95f;
    float descentPerEm = 0.25f;
};

void readMetrics(IDWriteFont* font, FaceInfo& info)
{
    DWRITE_FONT_METRICS metrics {};
    font->GetMetrics(&metrics);
    if (metrics.designUnitsPerEm == 0)
        return;
    const float em = static_cast<float>(metrics.designUnitsPerEm);
    info.ascentPerEm = static_cast<float>(metrics.ascent) / em;
    info.descentPerEm = static_cast<float>(metrics.descent) / em;
    info.heightPerEm = std::max(0.1f, info.ascentPerEm + info.descentPerEm);
    info.weight = font->GetWeight();
    info.style = font->GetStyle();
    info.stretch = font->GetStretch();
}

// One bundled file as its own private collection (whatever its family name).
bool fromFile(const char* fileName, FaceInfo& info)
{
    ComPtr<IDWriteFactory5> factory;
    if (dwriteFactory() == nullptr || FAILED(dwriteFactory()->QueryInterface(IID_PPV_ARGS(&factory))))
        return false; // DirectWrite before Windows 10 1703: system faces only
    const auto path = resourceFile(fileName);
    if (path.empty())
        return false;
    ComPtr<IDWriteFontSetBuilder1> builder;
    ComPtr<IDWriteFontFile> file;
    ComPtr<IDWriteFontSet> set;
    ComPtr<IDWriteFontCollection1> collection;
    if (FAILED(factory->CreateFontSetBuilder(&builder))
        || FAILED(factory->CreateFontFileReference(path.wstring().c_str(), nullptr, &file))
        || FAILED(builder->AddFontFile(file.Get())) || FAILED(builder->CreateFontSet(&set))
        || FAILED(factory->CreateFontCollectionFromFontSet(set.Get(), &collection))
        || collection->GetFontFamilyCount() == 0)
        return false;
    ComPtr<IDWriteFontFamily> family;
    ComPtr<IDWriteLocalizedStrings> names;
    ComPtr<IDWriteFont> font;
    if (FAILED(collection->GetFontFamily(0, &family)) || FAILED(family->GetFamilyNames(&names))
        || FAILED(family->GetFont(0, &font)))
        return false;
    UINT32 length = 0;
    if (FAILED(names->GetStringLength(0, &length)))
        return false;
    std::wstring name(length + 1, L'\0');
    if (FAILED(names->GetString(0, name.data(), length + 1)))
        return false;
    name.resize(length);
    info.collection = collection;
    info.family = name;
    readMetrics(font.Get(), info);
    return true;
}

bool fromSystem(std::initializer_list<const wchar_t*> families, DWRITE_FONT_WEIGHT weight, DWRITE_FONT_STYLE style,
                FaceInfo& info)
{
    ComPtr<IDWriteFontCollection> system;
    if (dwriteFactory() == nullptr || FAILED(dwriteFactory()->GetSystemFontCollection(&system)))
        return false;
    for (const auto* name : families)
    {
        UINT32 index = 0;
        BOOL exists = FALSE;
        if (FAILED(system->FindFamilyName(name, &index, &exists)) || !exists)
            continue;
        ComPtr<IDWriteFontFamily> family;
        ComPtr<IDWriteFont> font;
        if (FAILED(system->GetFontFamily(index, &family))
            || FAILED(family->GetFirstMatchingFont(weight, DWRITE_FONT_STRETCH_NORMAL, style, &font)))
            continue;
        info.collection.Reset();
        info.family = name;
        readMetrics(font.Get(), info);
        return true;
    }
    return false;
}

const FaceInfo& faceInfo(Face face)
{
    static std::array<FaceInfo, 6> faces;
    static std::once_flag once;
    std::call_once(once, []
    {
        const auto load = [](FaceInfo& info, const char* file, std::initializer_list<const wchar_t*> fallback,
                             DWRITE_FONT_WEIGHT weight, DWRITE_FONT_STYLE style)
        {
            if (file != nullptr && fromFile(file, info))
                return;
            if (!fromSystem(fallback, weight, style, info))
            {
                info.family = L"Segoe UI";
                info.weight = weight;
                info.style = style;
            }
        };
        load(faces[0], "Inter-Regular.ttf", { L"Segoe UI", L"Arial" }, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL);
        load(faces[1], "Inter-SemiBold.ttf", { L"Segoe UI", L"Arial" }, DWRITE_FONT_WEIGHT_SEMI_BOLD,
             DWRITE_FONT_STYLE_NORMAL);
        load(faces[2], "SchibstedGrotesk-SemiBold.ttf", { L"Segoe UI", L"Arial" }, DWRITE_FONT_WEIGHT_SEMI_BOLD,
             DWRITE_FONT_STYLE_NORMAL);
        // The display serif and the script wordmark come from the system, as on
        // macOS: Baskerville where installed, else Georgia; Snell Roundhand
        // where installed, else Segoe Script (ships with Windows).
        load(faces[3], nullptr, { L"Baskerville", L"Baskerville Old Face", L"Georgia", L"Times New Roman" },
             DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL);
        load(faces[4], nullptr, { L"Snell Roundhand", L"Segoe Script", L"Georgia" }, DWRITE_FONT_WEIGHT_BOLD,
             DWRITE_FONT_STYLE_NORMAL);
        load(faces[5], nullptr, { L"Consolas", L"Cascadia Mono", L"Courier New" }, DWRITE_FONT_WEIGHT_NORMAL,
             DWRITE_FONT_STYLE_NORMAL);
    });
    return faces[static_cast<std::size_t>(face)];
}

// Text layouts are measured many times per paint (fitting tries widths), so
// they are kept, keyed by face, size, tracking and text.
struct LayoutEntry
{
    ComPtr<IDWriteTextLayout> layout;
    float width = 0.0f;
    float baseline = 0.0f;
    Rect ink {};
};

const LayoutEntry* textLayout(const Font& font, const std::string& value)
{
    static std::unordered_map<std::string, LayoutEntry> cache;
    static std::map<std::pair<int, int>, ComPtr<IDWriteTextFormat>> formats;
    const float em = fontEmSize(font);
    const int emKey = static_cast<int>(std::lround(em * 100.0f));
    std::string key;
    key.reserve(value.size() + 24);
    key += std::to_string(static_cast<int>(font.face));
    key += '|';
    key += std::to_string(emKey);
    key += '|';
    key += std::to_string(static_cast<int>(std::lround(font.kerning * font.height * 1000.0f)));
    key += '|';
    key += value;
    if (auto found = cache.find(key); found != cache.end())
        return &found->second;
    if (dwriteFactory() == nullptr)
        return nullptr;

    auto& format = formats[{ static_cast<int>(font.face), emKey }];
    if (format == nullptr)
    {
        const auto& info = faceInfo(font.face);
        if (FAILED(dwriteFactory()->CreateTextFormat(info.family.c_str(), info.collection.Get(), info.weight, info.style,
                                                     info.stretch, std::max(0.5f, em), L"en-us", &format)))
            return nullptr;
        format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    }
    const auto wide = widen(value);
    LayoutEntry entry;
    if (FAILED(dwriteFactory()->CreateTextLayout(wide.c_str(), static_cast<UINT32>(wide.size()), format.Get(), 100000.0f,
                                                 10000.0f, &entry.layout)))
        return nullptr;
    const float tracking = font.kerning * font.height;
    if (tracking != 0.0f && !wide.empty())
    {
        ComPtr<IDWriteTextLayout1> layout1;
        if (SUCCEEDED(entry.layout.As(&layout1)))
            layout1->SetCharacterSpacing(0.0f, tracking, 0.0f, { 0, static_cast<UINT32>(wide.size()) });
    }
    DWRITE_TEXT_METRICS metrics {};
    entry.layout->GetMetrics(&metrics);
    entry.width = metrics.widthIncludingTrailingWhitespace;
    DWRITE_LINE_METRICS line {};
    UINT32 lines = 0;
    entry.layout->GetLineMetrics(&line, 1, &lines);
    entry.baseline = lines > 0 ? line.baseline : ascent(font);
    DWRITE_OVERHANG_METRICS overhang {};
    if (SUCCEEDED(entry.layout->GetOverhangMetrics(&overhang)))
    {
        // The layout box is 100000 wide: ink right = metrics.width + overhang.right
        // measured from the box's left edge.
        const float inkLeft = -overhang.left;
        const float inkRight = metrics.width + overhang.right;
        const float inkTop = -overhang.top;
        const float inkBottom = metrics.height + overhang.bottom;
        // Core Text's convention: y up from the baseline, origin bottom-left.
        entry.ink = Rect(inkLeft, entry.baseline - inkBottom, inkRight - inkLeft, inkBottom - inkTop);
    }
    if (cache.size() > 4096)
        cache.clear();
    return &cache.emplace(std::move(key), std::move(entry)).first->second;
}
} // namespace

float fontEmSize(const Font& font)
{
    return std::max(1.0f, font.height / faceInfo(font.face).heightPerEm);
}

std::wstring gdiFaceName(Face face)
{
    return faceInfo(face).family;
}

float ascent(const Font& font)
{
    return fontEmSize(font) * faceInfo(font.face).ascentPerEm;
}

float descent(const Font& font)
{
    return fontEmSize(font) * faceInfo(font.face).descentPerEm;
}

float stringWidth(const Font& font, const std::string& value)
{
    if (value.empty())
        return 0.0f;
    const auto* entry = textLayout(font, value);
    return entry != nullptr ? entry->width : 0.0f;
}

Rect inkBounds(const Font& font, const std::string& value)
{
    if (value.empty())
        return {};
    const auto* entry = textLayout(font, value);
    return entry != nullptr ? entry->ink : Rect {};
}

// ----------------------------------------------------------------- the mark
namespace
{
struct MarkPixels
{
    UINT width = 0, height = 0;
    std::vector<std::uint8_t> bgra; // premultiplied
};

ComPtr<IWICImagingFactory> wicFactory()
{
    ComPtr<IWICImagingFactory> factory;
    HRESULT result = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (result == CO_E_NOTINITIALIZED)
    {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    }
    return factory;
}

// The artwork as straight-alpha BGRA (empty when missing).
const MarkPixels& markArtwork()
{
    static MarkPixels artwork;
    static std::once_flag once;
    std::call_once(once, []
    {
        const auto path = resourceFile("MusicProdLogo.png");
        auto factory = wicFactory();
        if (path.empty() || factory == nullptr)
            return;
        ComPtr<IWICBitmapDecoder> decoder;
        ComPtr<IWICBitmapFrameDecode> frame;
        ComPtr<IWICFormatConverter> converter;
        if (FAILED(factory->CreateDecoderFromFilename(path.wstring().c_str(), nullptr, GENERIC_READ,
                                                      WICDecodeMetadataCacheOnDemand, &decoder))
            || FAILED(decoder->GetFrame(0, &frame)) || FAILED(factory->CreateFormatConverter(&converter))
            || FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr,
                                            0.0, WICBitmapPaletteTypeCustom)))
            return;
        UINT width = 0, height = 0;
        converter->GetSize(&width, &height);
        std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 4);
        if (FAILED(converter->CopyPixels(nullptr, width * 4, static_cast<UINT>(pixels.size()), pixels.data())))
            return;
        artwork.width = width;
        artwork.height = height;
        artwork.bgra = std::move(pixels);
    });
    return artwork;
}

const MarkPixels* tintedMark(Colour tone)
{
    static std::map<std::uint32_t, MarkPixels> cache;
    if (auto found = cache.find(tone.argb); found != cache.end())
        return found->second.bgra.empty() ? nullptr : &found->second;
    const auto& artwork = markArtwork();
    MarkPixels tinted;
    tinted.width = artwork.width;
    tinted.height = artwork.height;
    tinted.bgra.resize(artwork.bgra.size());
    const auto toByte = [](float value)
    { return static_cast<std::uint8_t>(std::clamp(static_cast<int>(std::lround(value)), 0, 255)); };
    // Alpha from luminance keeps the exact letterforms and drops the artwork's
    // near-black ground (as on macOS).
    for (std::size_t index = 0; index < artwork.bgra.size() / 4; ++index)
    {
        const auto* p = &artwork.bgra[index * 4];
        const float a = p[3] / 255.0f;
        const float red = p[2] / 255.0f, green = p[1] / 255.0f, blue = p[0] / 255.0f;
        const float luminance = 0.2126f * red + 0.7152f * green + 0.0722f * blue;
        const float alpha = std::clamp((luminance - 0.08f) / 0.84f, 0.0f, 1.0f) * a * tone.alpha();
        auto* q = &tinted.bgra[index * 4];
        q[0] = toByte(tone.b() * alpha);
        q[1] = toByte(tone.g() * alpha);
        q[2] = toByte(tone.r() * alpha);
        q[3] = toByte(alpha * 255.0f);
    }
    auto& stored = cache.emplace(tone.argb, std::move(tinted)).first->second;
    return stored.bgra.empty() ? nullptr : &stored;
}

D2D1_COLOR_F d2dColour(Colour colour)
{
    return D2D1::ColorF(colour.r() / 255.0f, colour.g() / 255.0f, colour.b() / 255.0f, colour.alpha());
}

D2D1_RECT_F d2dRect(Rect rect)
{
    return D2D1::RectF(rect.x, rect.y, rect.x + rect.width, rect.y + rect.height);
}
} // namespace

float musicProdMarkAspect()
{
    const auto& artwork = markArtwork();
    return artwork.height == 0 ? 0.0f : static_cast<float>(artwork.width) / static_cast<float>(artwork.height);
}

// ----------------------------------------------------------------- canvas
D2DCanvas::D2DCanvas(ID2D1RenderTarget& target, D2DResources& resources) : target_(target), resources_(resources)
{
    if (resources_.target != &target_)
    {
        resources_.reset();
        resources_.target = &target_;
    }
    target_.SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    target_.SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
}

D2DCanvas::~D2DCanvas()
{
    while (!layers_.empty())
        endLayer();
    while (!states_.empty())
        restore();
    for (; clips_ > 0; --clips_)
        target_.PopAxisAlignedClip();
}

ID2D1SolidColorBrush* D2DCanvas::brush(Colour colour)
{
    if (resources_.brush == nullptr)
        target_.CreateSolidColorBrush(d2dColour(colour), &resources_.brush);
    else
        resources_.brush->SetColor(d2dColour(colour));
    return resources_.brush.Get();
}

void D2DCanvas::fillRect(Rect rect, Colour colour)
{
    if (colour.isTransparent())
        return;
    if (auto* b = brush(colour))
        target_.FillRectangle(d2dRect(rect), b);
}

void D2DCanvas::fillRoundedRect(Rect rect, float radius, Colour colour)
{
    if (colour.isTransparent() || rect.width <= 0.0f || rect.height <= 0.0f)
        return;
    radius = std::min({ radius, rect.width * 0.5f, rect.height * 0.5f });
    if (auto* b = brush(colour))
        target_.FillRoundedRectangle(D2D1::RoundedRect(d2dRect(rect), radius, radius), b);
}

void D2DCanvas::strokeRoundedRect(Rect rect, float radius, float lineWidth, Colour colour)
{
    if (colour.isTransparent() || rect.width <= 0.0f || rect.height <= 0.0f)
        return;
    radius = std::min({ radius, rect.width * 0.5f, rect.height * 0.5f });
    if (auto* b = brush(colour))
        target_.DrawRoundedRectangle(D2D1::RoundedRect(d2dRect(rect), radius, radius), b, lineWidth);
}

void D2DCanvas::fillEllipse(Rect rect, Colour colour)
{
    if (colour.isTransparent())
        return;
    if (auto* b = brush(colour))
        target_.FillEllipse(D2D1::Ellipse(D2D1::Point2F(rect.midX(), rect.midY()), rect.width * 0.5f, rect.height * 0.5f),
                            b);
}

void D2DCanvas::strokeEllipse(Rect rect, float lineWidth, Colour colour)
{
    if (colour.isTransparent())
        return;
    if (auto* b = brush(colour))
        target_.DrawEllipse(D2D1::Ellipse(D2D1::Point2F(rect.midX(), rect.midY()), rect.width * 0.5f, rect.height * 0.5f),
                            b, lineWidth);
}

void D2DCanvas::fillBottomRoundedGradient(Rect rect, float radius, Colour top, Colour bottom)
{
    auto* factory = d2dFactory();
    if (factory == nullptr || rect.width <= 0.0f || rect.height <= 0.0f)
        return;
    const float x0 = rect.minX(), x1 = rect.maxX(), y0 = rect.minY(), y1 = rect.maxY();
    const float r = std::min({ radius, rect.width * 0.5f, rect.height * 0.5f });
    ComPtr<ID2D1PathGeometry> path;
    ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(factory->CreatePathGeometry(&path)) || FAILED(path->Open(&sink)))
        return;
    sink->BeginFigure(D2D1::Point2F(x0, y0), D2D1_FIGURE_BEGIN_FILLED);
    sink->AddLine(D2D1::Point2F(x1, y0));
    sink->AddLine(D2D1::Point2F(x1, y1 - r));
    if (r > 0.0f)
        sink->AddArc(D2D1::ArcSegment(D2D1::Point2F(x1 - r, y1), D2D1::SizeF(r, r), 0.0f, D2D1_SWEEP_DIRECTION_CLOCKWISE,
                                      D2D1_ARC_SIZE_SMALL));
    sink->AddLine(D2D1::Point2F(x0 + r, y1));
    if (r > 0.0f)
        sink->AddArc(D2D1::ArcSegment(D2D1::Point2F(x0, y1 - r), D2D1::SizeF(r, r), 0.0f, D2D1_SWEEP_DIRECTION_CLOCKWISE,
                                      D2D1_ARC_SIZE_SMALL));
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    sink->Close();

    if (top.argb == bottom.argb)
    {
        if (auto* b = brush(top))
            target_.FillGeometry(path.Get(), b);
        return;
    }
    const D2D1_GRADIENT_STOP stops[] = { { 0.0f, d2dColour(top) }, { 1.0f, d2dColour(bottom) } };
    ComPtr<ID2D1GradientStopCollection> collection;
    ComPtr<ID2D1LinearGradientBrush> gradient;
    if (FAILED(target_.CreateGradientStopCollection(stops, 2, &collection))
        || FAILED(target_.CreateLinearGradientBrush(
            D2D1::LinearGradientBrushProperties(D2D1::Point2F(x0, y0), D2D1::Point2F(x0, y1)), collection.Get(),
            &gradient)))
        return;
    target_.FillGeometry(path.Get(), gradient.Get());
}

void D2DCanvas::strokePolyline(const std::vector<Point>& points, float lineWidth, Colour colour)
{
    auto* factory = d2dFactory();
    if (points.size() < 2 || colour.isTransparent() || factory == nullptr)
        return;
    if (resources_.roundStroke == nullptr)
        factory->CreateStrokeStyle(D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
                                                               D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND),
                                   nullptr, 0, &resources_.roundStroke);
    ComPtr<ID2D1PathGeometry> path;
    ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(factory->CreatePathGeometry(&path)) || FAILED(path->Open(&sink)))
        return;
    sink->BeginFigure(D2D1::Point2F(points[0].x, points[0].y), D2D1_FIGURE_BEGIN_HOLLOW);
    for (std::size_t index = 1; index < points.size(); ++index)
        sink->AddLine(D2D1::Point2F(points[index].x, points[index].y));
    sink->EndFigure(D2D1_FIGURE_END_OPEN);
    sink->Close();
    if (auto* b = brush(colour))
        target_.DrawGeometry(path.Get(), b, lineWidth, resources_.roundStroke.Get());
}

void D2DCanvas::drawText(const std::string& value, const Font& font, Colour colour, float x, float baseline,
                         float horizontalScale)
{
    if (value.empty() || colour.isTransparent())
        return;
    const auto* entry = textLayout(font, value);
    if (entry == nullptr || entry->layout == nullptr)
        return;
    D2D1_MATRIX_3X2_F current {};
    target_.GetTransform(&current);
    target_.SetTransform(D2D1::Matrix3x2F::Scale(horizontalScale, 1.0f) * D2D1::Matrix3x2F::Translation(x, baseline)
                         * *D2D1::Matrix3x2F::ReinterpretBaseType(&current));
    if (auto* b = brush(colour))
        target_.DrawTextLayout(D2D1::Point2F(0.0f, -entry->baseline), entry->layout.Get(), b,
                               D2D1_DRAW_TEXT_OPTIONS_NONE);
    target_.SetTransform(current);
}

void D2DCanvas::drawMusicProdMark(Rect area, Colour tone, float opacity)
{
    auto& bitmap = resources_.marks[tone.argb];
    if (bitmap == nullptr)
    {
        const auto* pixels = tintedMark(tone);
        if (pixels == nullptr)
            return;
        const auto properties = D2D1::BitmapProperties(
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
        if (FAILED(target_.CreateBitmap(D2D1::SizeU(pixels->width, pixels->height), pixels->bgra.data(),
                                        pixels->width * 4, properties, &bitmap)))
            return;
    }
    target_.DrawBitmap(bitmap.Get(), d2dRect(area), opacity, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
}

void D2DCanvas::save()
{
    State state {};
    target_.GetTransform(&state.transform);
    state.clips = clips_;
    states_.push_back(state);
}

void D2DCanvas::restore()
{
    if (states_.empty())
        return;
    const State state = states_.back();
    states_.pop_back();
    for (; clips_ > state.clips; --clips_)
        target_.PopAxisAlignedClip();
    target_.SetTransform(state.transform);
}

void D2DCanvas::translate(float dx, float dy)
{
    D2D1_MATRIX_3X2_F current {};
    target_.GetTransform(&current);
    target_.SetTransform(D2D1::Matrix3x2F::Translation(dx, dy) * *D2D1::Matrix3x2F::ReinterpretBaseType(&current));
}

void D2DCanvas::clipTo(Rect rect)
{
    target_.PushAxisAlignedClip(d2dRect(rect), D2D1_ANTIALIAS_MODE_ALIASED);
    ++clips_;
}

void D2DCanvas::beginLayer(float opacity)
{
    // A fully opaque layer composites exactly like drawing directly.
    if (opacity >= 1.0f)
    {
        layers_.push_back(false);
        return;
    }
    ComPtr<ID2D1Layer> layer;
    if (FAILED(target_.CreateLayer(&layer)))
    {
        layers_.push_back(false);
        return;
    }
    target_.PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                                            D2D1::IdentityMatrix(), std::max(0.0f, opacity)),
                      layer.Get());
    layers_.push_back(true);
}

void D2DCanvas::endLayer()
{
    if (layers_.empty())
        return;
    if (layers_.back())
        target_.PopLayer();
    layers_.pop_back();
}
} // namespace chordengine::ui::style
