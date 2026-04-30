#include <iostream>
#include <vector>
#include <string>
#include <memory>
#include <functional>
#include <map>
#include <stack>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <cmath>
#include <complex>
#include <stdexcept>
#include <variant>
#include <optional>
#include <filesystem>
#include <chrono>
#include <random>
#include <numeric>
#include <climits>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#ifdef _WIN32
#include <windows.h>
#define OFX_LIB_EXT ".dll"
#elif defined(__APPLE__)
#include <dlfcn.h>
#define OFX_LIB_EXT ".dylib"
#else
#include <dlfcn.h>
#define OFX_LIB_EXT ".so"
#endif

namespace fs = std::filesystem;

struct Pixel {
    float r, g, b, a;
    Pixel() : r(0), g(0), b(0), a(1.0f) {}
    Pixel(float r, float g, float b, float a = 1.0f) : r(r), g(g), b(b), a(a) {}
};

struct Frame {
    int width, height;
    std::vector<Pixel> pixels;
    double timestamp;
    Frame() : width(0), height(0), timestamp(0.0) {}
    Frame(int w, int h) : width(w), height(h), pixels(w * h), timestamp(0.0) {}
    Pixel& at(int x, int y) { return pixels[y * width + x]; }
    const Pixel& at(int x, int y) const { return pixels[y * width + x]; }
    Pixel sampleBilinear(float x, float y) const {
        if (width == 0 || height == 0) return Pixel();
        float cx = std::clamp(x, 0.0f, (float)(width - 1));
        float cy = std::clamp(y, 0.0f, (float)(height - 1));
        int ix = (int)cx, iy = (int)cy;
        float fx = cx - ix, fy = cy - iy;
        int ix2 = std::min(ix + 1, width - 1), iy2 = std::min(iy + 1, height - 1);
        Pixel p00 = at(ix, iy), p10 = at(ix2, iy), p01 = at(ix, iy2), p11 = at(ix2, iy2);
        return {
            p00.r * (1 - fx) * (1 - fy) + p10.r * fx * (1 - fy) + p01.r * (1 - fx) * fy + p11.r * fx * fy,
            p00.g * (1 - fx) * (1 - fy) + p10.g * fx * (1 - fy) + p01.g * (1 - fx) * fy + p11.g * fx * fy,
            p00.b * (1 - fx) * (1 - fy) + p10.b * fx * (1 - fy) + p01.b * (1 - fx) * fy + p11.b * fx * fy,
            p00.a * (1 - fx) * (1 - fy) + p10.a * fx * (1 - fy) + p01.a * (1 - fx) * fy + p11.a * fx * fy
        };
    }
};

struct EffectParam {
    std::string name;
    std::variant<float, int, bool, std::string> value;
    float minVal, maxVal;
    EffectParam() : minVal(0), maxVal(1) {}
    EffectParam(const std::string& n, float v, float mn = 0.0f, float mx = 1.0f)
        : name(n), value(v), minVal(mn), maxVal(mx) {}
    EffectParam(const std::string& n, int v, int mn = 0, int mx = 100)
        : name(n), value(v), minVal((float)mn), maxVal((float)mx) {}
    EffectParam(const std::string& n, bool v)
        : name(n), value(v), minVal(0), maxVal(1) {}
    float asFloat() const {
        if (auto* f = std::get_if<float>(&value)) return *f;
        if (auto* i = std::get_if<int>(&value)) return (float)*i;
        if (auto* b = std::get_if<bool>(&value)) return *b ? 1.0f : 0.0f;
        return 0.0f;
    }
    int asInt() const { return (int)asFloat(); }
    bool asBool() const { return asFloat() != 0.0f; }
};

class VideoEffect {
public:
    std::string name;
    bool enabled;
    std::map<std::string, EffectParam> params;
    VideoEffect(const std::string& n) : name(n), enabled(true) {}
    virtual ~VideoEffect() = default;
    virtual Frame apply(const Frame& input) const = 0;
    void setParam(const std::string& key, float v) {
        if (params.count(key)) params[key].value = v;
    }
    float getParam(const std::string& key, float def = 0.0f) const {
        auto it = params.find(key);
        if (it != params.end()) return it->second.asFloat();
        return def;
    }
};

class AddNoiseEffect : public VideoEffect {
public:
    AddNoiseEffect() : VideoEffect("Add Noise") {
        params["amount"] = EffectParam("amount", 0.1f, 0.0f, 1.0f);
        params["monochrome"] = EffectParam("monochrome", false);
        params["seed"] = EffectParam("seed", 42, 0, 9999);
    }
    Frame apply(const Frame& input) const override {
        Frame out = input;
        float amount = getParam("amount");
        bool mono = params.at("monochrome").asBool();
        std::mt19937 rng(params.at("seed").asInt());
        std::uniform_real_distribution<float> dist(-amount, amount);
        for (int y = 0; y < out.height; y++) {
            for (int x = 0; x < out.width; x++) {
                Pixel& p = out.at(x, y);
                if (mono) {
                    float n = dist(rng);
                    p.r = std::clamp(p.r + n, 0.0f, 1.0f);
                    p.g = std::clamp(p.g + n, 0.0f, 1.0f);
                    p.b = std::clamp(p.b + n, 0.0f, 1.0f);
                } else {
                    p.r = std::clamp(p.r + dist(rng), 0.0f, 1.0f);
                    p.g = std::clamp(p.g + dist(rng), 0.0f, 1.0f);
                    p.b = std::clamp(p.b + dist(rng), 0.0f, 1.0f);
                }
            }
        }
        return out;
    }
};

class GrayscaleEffect : public VideoEffect {
public:
    GrayscaleEffect() : VideoEffect("Grayscale") {
        params["intensity"] = EffectParam("intensity", 1.0f, 0.0f, 1.0f);
    }
    Frame apply(const Frame& input) const override {
        Frame out = input;
        float intensity = getParam("intensity");
        for (int y = 0; y < out.height; y++) {
            for (int x = 0; x < out.width; x++) {
                Pixel& p = out.at(x, y);
                float lum = 0.299f * p.r + 0.587f * p.g + 0.114f * p.b;
                p.r = p.r + (lum - p.r) * intensity;
                p.g = p.g + (lum - p.g) * intensity;
                p.b = p.b + (lum - p.b) * intensity;
            }
        }
        return out;
    }
};

class ColorCurvesEffect : public VideoEffect {
public:
    ColorCurvesEffect() : VideoEffect("Color Curves") {
        params["red_lift"] = EffectParam("red_lift", 0.0f, -1.0f, 1.0f);
        params["green_lift"] = EffectParam("green_lift", 0.0f, -1.0f, 1.0f);
        params["blue_lift"] = EffectParam("blue_lift", 0.0f, -1.0f, 1.0f);
        params["red_gain"] = EffectParam("red_gain", 1.0f, 0.0f, 4.0f);
        params["green_gain"] = EffectParam("green_gain", 1.0f, 0.0f, 4.0f);
        params["blue_gain"] = EffectParam("blue_gain", 1.0f, 0.0f, 4.0f);
        params["gamma"] = EffectParam("gamma", 1.0f, 0.1f, 5.0f);
    }
    Frame apply(const Frame& input) const override {
        Frame out = input;
        float rl = getParam("red_lift"), gl = getParam("green_lift"), bl = getParam("blue_lift");
        float rg = getParam("red_gain"), gg = getParam("green_gain"), bg = getParam("blue_gain");
        float gm = getParam("gamma");
        for (int y = 0; y < out.height; y++) {
            for (int x = 0; x < out.width; x++) {
                Pixel& p = out.at(x, y);
                p.r = std::clamp(std::pow(std::clamp(p.r * rg + rl, 0.0f, 1.0f), 1.0f / gm), 0.0f, 1.0f);
                p.g = std::clamp(std::pow(std::clamp(p.g * gg + gl, 0.0f, 1.0f), 1.0f / gm), 0.0f, 1.0f);
                p.b = std::clamp(std::pow(std::clamp(p.b * bg + bl, 0.0f, 1.0f), 1.0f / gm), 0.0f, 1.0f);
            }
        }
        return out;
    }
};

class CheckerboardEffect : public VideoEffect {
public:
    CheckerboardEffect() : VideoEffect("Checkerboard") {
        params["tile_size"] = EffectParam("tile_size", 32, 4, 256);
        params["color1_r"] = EffectParam("color1_r", 0.0f, 0.0f, 1.0f);
        params["color1_g"] = EffectParam("color1_g", 0.0f, 0.0f, 1.0f);
        params["color1_b"] = EffectParam("color1_b", 0.0f, 0.0f, 1.0f);
        params["color2_r"] = EffectParam("color2_r", 1.0f, 0.0f, 1.0f);
        params["color2_g"] = EffectParam("color2_g", 1.0f, 0.0f, 1.0f);
        params["color2_b"] = EffectParam("color2_b", 1.0f, 0.0f, 1.0f);
        params["blend"] = EffectParam("blend", 1.0f, 0.0f, 1.0f);
    }
    Frame apply(const Frame& input) const override {
        Frame out = input;
        int ts = params.at("tile_size").asInt();
        float c1r = getParam("color1_r"), c1g = getParam("color1_g"), c1b = getParam("color1_b");
        float c2r = getParam("color2_r"), c2g = getParam("color2_g"), c2b = getParam("color2_b");
        float blend = getParam("blend");
        for (int y = 0; y < out.height; y++) {
            for (int x = 0; x < out.width; x++) {
                Pixel& p = out.at(x, y);
                bool which = ((x / ts) + (y / ts)) % 2 == 0;
                float cr = which ? c1r : c2r;
                float cg = which ? c1g : c2g;
                float cb = which ? c1b : c2b;
                p.r = p.r * (1 - blend) + cr * blend;
                p.g = p.g * (1 - blend) + cg * blend;
                p.b = p.b * (1 - blend) + cb * blend;
            }
        }
        return out;
    }
};

class FourPointColorGradientEffect : public VideoEffect {
public:
    FourPointColorGradientEffect() : VideoEffect("4-Point Color Gradient") {
        params["tl_r"] = EffectParam("tl_r", 1.0f, 0.0f, 1.0f);
        params["tl_g"] = EffectParam("tl_g", 0.0f, 0.0f, 1.0f);
        params["tl_b"] = EffectParam("tl_b", 0.0f, 0.0f, 1.0f);
        params["tr_r"] = EffectParam("tr_r", 0.0f, 0.0f, 1.0f);
        params["tr_g"] = EffectParam("tr_g", 1.0f, 0.0f, 1.0f);
        params["tr_b"] = EffectParam("tr_b", 0.0f, 0.0f, 1.0f);
        params["bl_r"] = EffectParam("bl_r", 0.0f, 0.0f, 1.0f);
        params["bl_g"] = EffectParam("bl_g", 0.0f, 0.0f, 1.0f);
        params["bl_b"] = EffectParam("bl_b", 1.0f, 0.0f, 1.0f);
        params["br_r"] = EffectParam("br_r", 1.0f, 0.0f, 1.0f);
        params["br_g"] = EffectParam("br_g", 1.0f, 0.0f, 1.0f);
        params["br_b"] = EffectParam("br_b", 0.0f, 0.0f, 1.0f);
        params["blend"] = EffectParam("blend", 1.0f, 0.0f, 1.0f);
    }
    Frame apply(const Frame& input) const override {
        Frame out = input;
        float blend = getParam("blend");
        for (int y = 0; y < out.height; y++) {
            float fy = out.height > 1 ? (float)y / (out.height - 1) : 0.0f;
            for (int x = 0; x < out.width; x++) {
                float fx = out.width > 1 ? (float)x / (out.width - 1) : 0.0f;
                float tlr = getParam("tl_r"), tlg = getParam("tl_g"), tlb = getParam("tl_b");
                float trr = getParam("tr_r"), trg = getParam("tr_g"), trb = getParam("tr_b");
                float blr = getParam("bl_r"), blg = getParam("bl_g"), blb = getParam("bl_b");
                float brr = getParam("br_r"), brg = getParam("br_g"), brb = getParam("br_b");
                float cr = (tlr * (1 - fx) + trr * fx) * (1 - fy) + (blr * (1 - fx) + brr * fx) * fy;
                float cg = (tlg * (1 - fx) + trg * fx) * (1 - fy) + (blg * (1 - fx) + brg * fx) * fy;
                float cb = (tlb * (1 - fx) + trb * fx) * (1 - fy) + (blb * (1 - fx) + brb * fx) * fy;
                Pixel& p = out.at(x, y);
                p.r = p.r * (1 - blend) + cr * blend;
                p.g = p.g * (1 - blend) + cg * blend;
                p.b = p.b * (1 - blend) + cb * blend;
            }
        }
        return out;
    }
};

class HSLAdjustEffect : public VideoEffect {
public:
    HSLAdjustEffect() : VideoEffect("HSL Adjust") {
        params["hue_shift"] = EffectParam("hue_shift", 0.0f, -180.0f, 180.0f);
        params["saturation"] = EffectParam("saturation", 1.0f, 0.0f, 3.0f);
        params["lightness"] = EffectParam("lightness", 0.0f, -1.0f, 1.0f);
    }
    static void rgbToHsl(float r, float g, float b, float& h, float& s, float& l) {
        float mx = std::max({r, g, b}), mn = std::min({r, g, b});
        l = (mx + mn) / 2.0f;
        if (mx == mn) { h = s = 0.0f; return; }
        float d = mx - mn;
        s = l > 0.5f ? d / (2.0f - mx - mn) : d / (mx + mn);
        if (mx == r) h = (g - b) / d + (g < b ? 6.0f : 0.0f);
        else if (mx == g) h = (b - r) / d + 2.0f;
        else h = (r - g) / d + 4.0f;
        h /= 6.0f;
    }
    static float hueToRgb(float p, float q, float t) {
        if (t < 0) t += 1; if (t > 1) t -= 1;
        if (t < 1.0f/6) return p + (q - p) * 6 * t;
        if (t < 0.5f) return q;
        if (t < 2.0f/3) return p + (q - p) * (2.0f/3 - t) * 6;
        return p;
    }
    static void hslToRgb(float h, float s, float l, float& r, float& g, float& b) {
        if (s == 0) { r = g = b = l; return; }
        float q = l < 0.5f ? l * (1 + s) : l + s - l * s;
        float p = 2 * l - q;
        r = hueToRgb(p, q, h + 1.0f/3);
        g = hueToRgb(p, q, h);
        b = hueToRgb(p, q, h - 1.0f/3);
    }
    Frame apply(const Frame& input) const override {
        Frame out = input;
        float hs = getParam("hue_shift") / 360.0f;
        float sat = getParam("saturation");
        float lit = getParam("lightness");
        for (int y = 0; y < out.height; y++) {
            for (int x = 0; x < out.width; x++) {
                Pixel& p = out.at(x, y);
                float h, s, l;
                rgbToHsl(p.r, p.g, p.b, h, s, l);
                h = std::fmod(h + hs + 1.0f, 1.0f);
                s = std::clamp(s * sat, 0.0f, 1.0f);
                l = std::clamp(l + lit, 0.0f, 1.0f);
                hslToRgb(h, s, l, p.r, p.g, p.b);
            }
        }
        return out;
    }
};

class InvertEffect : public VideoEffect {
public:
    InvertEffect() : VideoEffect("Invert") {
        params["intensity"] = EffectParam("intensity", 1.0f, 0.0f, 1.0f);
        params["invert_alpha"] = EffectParam("invert_alpha", false);
    }
    Frame apply(const Frame& input) const override {
        Frame out = input;
        float intensity = getParam("intensity");
        bool ia = params.at("invert_alpha").asBool();
        for (int y = 0; y < out.height; y++) {
            for (int x = 0; x < out.width; x++) {
                Pixel& p = out.at(x, y);
                p.r = p.r + (1.0f - p.r - p.r) * intensity;
                p.g = p.g + (1.0f - p.g - p.g) * intensity;
                p.b = p.b + (1.0f - p.b - p.b) * intensity;
                if (ia) p.a = 1.0f - p.a;
            }
        }
        return out;
    }
};

class SwirlEffect : public VideoEffect {
public:
    SwirlEffect() : VideoEffect("Swirl") {
        params["angle"] = EffectParam("angle", 1.0f, -10.0f, 10.0f);
        params["radius"] = EffectParam("radius", 0.5f, 0.01f, 2.0f);
        params["center_x"] = EffectParam("center_x", 0.5f, 0.0f, 1.0f);
        params["center_y"] = EffectParam("center_y", 0.5f, 0.0f, 1.0f);
    }
    Frame apply(const Frame& input) const override {
        Frame out = input;
        float angle = getParam("angle");
        float radius = getParam("radius");
        float cx = getParam("center_x") * input.width;
        float cy = getParam("center_y") * input.height;
        float r2 = radius * std::min(input.width, input.height) * 0.5f;
        for (int y = 0; y < out.height; y++) {
            for (int x = 0; x < out.width; x++) {
                float dx = x - cx, dy = y - cy;
                float dist = std::sqrt(dx * dx + dy * dy);
                float a = std::atan2(dy, dx) + angle * std::exp(-dist / r2);
                float sx = cx + dist * std::cos(a);
                float sy = cy + dist * std::sin(a);
                out.at(x, y) = input.sampleBilinear(sx, sy);
            }
        }
        return out;
    }
};

class TwirlEffect : public VideoEffect {
public:
    TwirlEffect() : VideoEffect("Twirl") {
        params["angle"] = EffectParam("angle", (float)M_PI, -(float)M_PI * 4, (float)M_PI * 4);
        params["radius"] = EffectParam("radius", 0.5f, 0.01f, 2.0f);
        params["center_x"] = EffectParam("center_x", 0.5f, 0.0f, 1.0f);
        params["center_y"] = EffectParam("center_y", 0.5f, 0.0f, 1.0f);
    }
    Frame apply(const Frame& input) const override {
        Frame out = input;
        float angle = getParam("angle");
        float radius = getParam("radius") * std::min(input.width, input.height) * 0.5f;
        float cx = getParam("center_x") * input.width;
        float cy = getParam("center_y") * input.height;
        for (int y = 0; y < out.height; y++) {
            for (int x = 0; x < out.width; x++) {
                float dx = x - cx, dy = y - cy;
                float dist = std::sqrt(dx * dx + dy * dy);
                float t = dist < radius ? 1.0f - dist / radius : 0.0f;
                float a = std::atan2(dy, dx) + angle * t * t;
                float sx = cx + dist * std::cos(a);
                float sy = cy + dist * std::sin(a);
                out.at(x, y) = input.sampleBilinear(sx, sy);
            }
        }
        return out;
    }
};

class ZigzagEffect : public VideoEffect {
public:
    ZigzagEffect() : VideoEffect("Zigzag") {
        params["amplitude"] = EffectParam("amplitude", 10.0f, 0.0f, 100.0f);
        params["frequency"] = EffectParam("frequency", 5.0f, 0.1f, 50.0f);
        params["phase"] = EffectParam("phase", 0.0f, 0.0f, (float)(2 * M_PI));
        params["direction"] = EffectParam("direction", 0, 0, 1);
    }
    Frame apply(const Frame& input) const override {
        Frame out = input;
        float amp = getParam("amplitude");
        float freq = getParam("frequency");
        float phase = getParam("phase");
        int dir = params.at("direction").asInt();
        for (int y = 0; y < out.height; y++) {
            for (int x = 0; x < out.width; x++) {
                float offset = amp * std::sin(freq * (dir == 0 ? y : x) * (float)M_PI / 180.0f + phase);
                float sx = x + (dir == 0 ? offset : 0);
                float sy = y + (dir == 1 ? offset : 0);
                out.at(x, y) = input.sampleBilinear(sx, sy);
            }
        }
        return out;
    }
};

class ShearEffect : public VideoEffect {
public:
    ShearEffect() : VideoEffect("Shear") {
        params["shear_x"] = EffectParam("shear_x", 0.0f, -2.0f, 2.0f);
        params["shear_y"] = EffectParam("shear_y", 0.0f, -2.0f, 2.0f);
    }
    Frame apply(const Frame& input) const override {
        Frame out = input;
        float sx = getParam("shear_x");
        float sy = getParam("shear_y");
        float cx = input.width * 0.5f, cy = input.height * 0.5f;
        for (int y = 0; y < out.height; y++) {
            for (int x = 0; x < out.width; x++) {
                float dx = x - cx, dy = y - cy;
                float srcX = cx + dx + sx * dy;
                float srcY = cy + dy + sy * dx;
                out.at(x, y) = input.sampleBilinear(srcX, srcY);
            }
        }
        return out;
    }
};

class DisplaceEffect : public VideoEffect {
public:
    DisplaceEffect() : VideoEffect("Displace") {
        params["amount_x"] = EffectParam("amount_x", 20.0f, -200.0f, 200.0f);
        params["amount_y"] = EffectParam("amount_y", 20.0f, -200.0f, 200.0f);
        params["scale"] = EffectParam("scale", 0.1f, 0.01f, 1.0f);
    }
    Frame apply(const Frame& input) const override {
        Frame out = input;
        float ax = getParam("amount_x");
        float ay = getParam("amount_y");
        float scale = getParam("scale");
        for (int y = 0; y < out.height; y++) {
            for (int x = 0; x < out.width; x++) {
                const Pixel& src = input.at(x, y);
                float lum = 0.299f * src.r + 0.587f * src.g + 0.114f * src.b;
                float dx = std::sin(x * scale) * ax * lum;
                float dy = std::cos(y * scale) * ay * lum;
                out.at(x, y) = input.sampleBilinear(x + dx, y + dy);
            }
        }
        return out;
    }
};

class ThreeColorGradientEffect : public VideoEffect {
public:
    ThreeColorGradientEffect() : VideoEffect("3 Color Gradient") {
        params["c1_r"] = EffectParam("c1_r", 1.0f, 0.0f, 1.0f);
        params["c1_g"] = EffectParam("c1_g", 0.0f, 0.0f, 1.0f);
        params["c1_b"] = EffectParam("c1_b", 0.0f, 0.0f, 1.0f);
        params["c2_r"] = EffectParam("c2_r", 0.0f, 0.0f, 1.0f);
        params["c2_g"] = EffectParam("c2_g", 1.0f, 0.0f, 1.0f);
        params["c2_b"] = EffectParam("c2_b", 0.0f, 0.0f, 1.0f);
        params["c3_r"] = EffectParam("c3_r", 0.0f, 0.0f, 1.0f);
        params["c3_g"] = EffectParam("c3_g", 0.0f, 0.0f, 1.0f);
        params["c3_b"] = EffectParam("c3_b", 1.0f, 0.0f, 1.0f);
        params["mid_point"] = EffectParam("mid_point", 0.5f, 0.0f, 1.0f);
        params["blend"] = EffectParam("blend", 1.0f, 0.0f, 1.0f);
        params["angle"] = EffectParam("angle", 0.0f, 0.0f, 360.0f);
    }
    Frame apply(const Frame& input) const override {
        Frame out = input;
        float blend = getParam("blend");
        float mid = getParam("mid_point");
        float angle = getParam("angle") * (float)M_PI / 180.0f;
        float cosA = std::cos(angle), sinA = std::sin(angle);
        for (int y = 0; y < out.height; y++) {
            for (int x = 0; x < out.width; x++) {
                float nx = (float)x / out.width - 0.5f;
                float ny = (float)y / out.height - 0.5f;
                float t = std::clamp((cosA * nx + sinA * ny) + 0.5f, 0.0f, 1.0f);
                float cr, cg, cb;
                if (t < mid) {
                    float u = mid > 0 ? t / mid : 0;
                    cr = getParam("c1_r") * (1 - u) + getParam("c2_r") * u;
                    cg = getParam("c1_g") * (1 - u) + getParam("c2_g") * u;
                    cb = getParam("c1_b") * (1 - u) + getParam("c2_b") * u;
                } else {
                    float u = mid < 1 ? (t - mid) / (1 - mid) : 1;
                    cr = getParam("c2_r") * (1 - u) + getParam("c3_r") * u;
                    cg = getParam("c2_g") * (1 - u) + getParam("c3_g") * u;
                    cb = getParam("c2_b") * (1 - u) + getParam("c3_b") * u;
                }
                Pixel& p = out.at(x, y);
                p.r = p.r * (1 - blend) + cr * blend;
                p.g = p.g * (1 - blend) + cg * blend;
                p.b = p.b * (1 - blend) + cb * blend;
            }
        }
        return out;
    }
};

class KaleidoEffect : public VideoEffect {
public:
    KaleidoEffect() : VideoEffect("Kaleido") {
        params["segments"] = EffectParam("segments", 6, 2, 32);
        params["rotation"] = EffectParam("rotation", 0.0f, 0.0f, 360.0f);
        params["zoom"] = EffectParam("zoom", 1.0f, 0.1f, 5.0f);
        params["center_x"] = EffectParam("center_x", 0.5f, 0.0f, 1.0f);
        params["center_y"] = EffectParam("center_y", 0.5f, 0.0f, 1.0f);
    }
    Frame apply(const Frame& input) const override {
        Frame out = input;
        int segs = params.at("segments").asInt();
        float rot = getParam("rotation") * (float)M_PI / 180.0f;
        float zoom = getParam("zoom");
        float cx = getParam("center_x") * input.width;
        float cy = getParam("center_y") * input.height;
        float segAngle = (float)(2.0 * M_PI / segs);
        for (int y = 0; y < out.height; y++) {
            for (int x = 0; x < out.width; x++) {
                float dx = (x - cx) / zoom, dy = (y - cy) / zoom;
                float a = std::atan2(dy, dx) + rot;
                float r = std::sqrt(dx * dx + dy * dy);
                a = std::fmod(a, segAngle);
                if (a < 0) a += segAngle;
                if (a > segAngle * 0.5f) a = segAngle - a;
                float sx = cx + r * std::cos(a);
                float sy = cy + r * std::sin(a);
                out.at(x, y) = input.sampleBilinear(sx, sy);
            }
        }
        return out;
    }
};

class GradientMapEffect : public VideoEffect {
public:
    GradientMapEffect() : VideoEffect("Gradient Map") {
        params["dark_r"] = EffectParam("dark_r", 0.0f, 0.0f, 1.0f);
        params["dark_g"] = EffectParam("dark_g", 0.0f, 0.0f, 1.0f);
        params["dark_b"] = EffectParam("dark_b", 0.0f, 0.0f, 1.0f);
        params["light_r"] = EffectParam("light_r", 1.0f, 0.0f, 1.0f);
        params["light_g"] = EffectParam("light_g", 1.0f, 0.0f, 1.0f);
        params["light_b"] = EffectParam("light_b", 1.0f, 0.0f, 1.0f);
        params["blend"] = EffectParam("blend", 1.0f, 0.0f, 1.0f);
    }
    Frame apply(const Frame& input) const override {
        Frame out = input;
        float blend = getParam("blend");
        for (int y = 0; y < out.height; y++) {
            for (int x = 0; x < out.width; x++) {
                Pixel& p = out.at(x, y);
                float lum = 0.299f * p.r + 0.587f * p.g + 0.114f * p.b;
                float cr = getParam("dark_r") * (1 - lum) + getParam("light_r") * lum;
                float cg = getParam("dark_g") * (1 - lum) + getParam("light_g") * lum;
                float cb = getParam("dark_b") * (1 - lum) + getParam("light_b") * lum;
                p.r = p.r * (1 - blend) + cr * blend;
                p.g = p.g * (1 - blend) + cg * blend;
                p.b = p.b * (1 - blend) + cb * blend;
            }
        }
        return out;
    }
};

class TintEffect : public VideoEffect {
public:
    TintEffect() : VideoEffect("Tint") {
        params["r"] = EffectParam("r", 1.0f, 0.0f, 2.0f);
        params["g"] = EffectParam("g", 1.0f, 0.0f, 2.0f);
        params["b"] = EffectParam("b", 1.0f, 0.0f, 2.0f);
        params["intensity"] = EffectParam("intensity", 0.5f, 0.0f, 1.0f);
    }
    Frame apply(const Frame& input) const override {
        Frame out = input;
        float tr = getParam("r"), tg = getParam("g"), tb = getParam("b");
        float intensity = getParam("intensity");
        for (int y = 0; y < out.height; y++) {
            for (int x = 0; x < out.width; x++) {
                Pixel& p = out.at(x, y);
                p.r = std::clamp(p.r + (tr - 1.0f) * intensity, 0.0f, 1.0f);
                p.g = std::clamp(p.g + (tg - 1.0f) * intensity, 0.0f, 1.0f);
                p.b = std::clamp(p.b + (tb - 1.0f) * intensity, 0.0f, 1.0f);
            }
        }
        return out;
    }
};

class DuochromeEffect : public VideoEffect {
public:
    DuochromeEffect() : VideoEffect("Duochrome") {
        params["c1_r"] = EffectParam("c1_r", 0.0f, 0.0f, 1.0f);
        params["c1_g"] = EffectParam("c1_g", 0.0f, 0.0f, 1.0f);
        params["c1_b"] = EffectParam("c1_b", 0.5f, 0.0f, 1.0f);
        params["c2_r"] = EffectParam("c2_r", 1.0f, 0.0f, 1.0f);
        params["c2_g"] = EffectParam("c2_g", 0.5f, 0.0f, 1.0f);
        params["c2_b"] = EffectParam("c2_b", 0.0f, 0.0f, 1.0f);
        params["blend"] = EffectParam("blend", 1.0f, 0.0f, 1.0f);
    }
    Frame apply(const Frame& input) const override {
        Frame out = input;
        float blend = getParam("blend");
        for (int y = 0; y < out.height; y++) {
            for (int x = 0; x < out.width; x++) {
                Pixel& p = out.at(x, y);
                float lum = 0.299f * p.r + 0.587f * p.g + 0.114f * p.b;
                float cr = getParam("c1_r") * (1 - lum) + getParam("c2_r") * lum;
                float cg = getParam("c1_g") * (1 - lum) + getParam("c2_g") * lum;
                float cb = getParam("c1_b") * (1 - lum) + getParam("c2_b") * lum;
                p.r = p.r * (1 - blend) + cr * blend;
                p.g = p.g * (1 - blend) + cg * blend;
                p.b = p.b * (1 - blend) + cb * blend;
            }
        }
        return out;
    }
};

class FourierTransformEffect : public VideoEffect {
public:
    FourierTransformEffect() : VideoEffect("Fourier Transform") {
        params["low_pass"] = EffectParam("low_pass", 0.3f, 0.0f, 1.0f);
        params["high_pass"] = EffectParam("high_pass", 0.0f, 0.0f, 1.0f);
        params["visualize"] = EffectParam("visualize", false);
    }
    Frame apply(const Frame& input) const override {
        Frame out = input;
        float lp = getParam("low_pass");
        float hp = getParam("high_pass");
        bool vis = params.at("visualize").asBool();
        int W = input.width, H = input.height;
        std::vector<std::complex<float>> freq(W * H);
        for (int y = 0; y < H; y++) {
            for (int x = 0; x < W; x++) {
                const Pixel& p = input.at(x, y);
                float lum = 0.299f * p.r + 0.587f * p.g + 0.114f * p.b;
                float re = 0, im = 0;
                for (int ky = 0; ky < H; ky++) {
                    for (int kx = 0; kx < W; kx++) {
                        const Pixel& sp = input.at(kx, ky);
                        float sl = 0.299f * sp.r + 0.587f * sp.g + 0.114f * sp.b;
                        float angle = -2.0f * (float)M_PI * ((float)(x * kx) / W + (float)(y * ky) / H);
                        re += sl * std::cos(angle);
                        im += sl * std::sin(angle);
                    }
                }
                freq[y * W + x] = {re / (W * H), im / (W * H)};
            }
        }
        float cx = W * 0.5f, cy = H * 0.5f;
        for (int y = 0; y < H; y++) {
            for (int x = 0; x < W; x++) {
                float dx = (x - cx) / cx, dy = (y - cy) / cy;
                float r = std::sqrt(dx * dx + dy * dy);
                std::complex<float>& f = freq[y * W + x];
                float mag = std::abs(f);
                if (r > lp) f *= 0;
                if (r < hp) f *= 0;
                Pixel& p = out.at(x, y);
                if (vis) {
                    float v = std::log(1 + mag) * 3.0f;
                    p.r = p.g = p.b = std::clamp(v, 0.0f, 1.0f);
                } else {
                    float re2 = 0;
                    for (int ky = 0; ky < H; ky++) {
                        for (int kx = 0; kx < W; kx++) {
                            float angle = 2.0f * (float)M_PI * ((float)(x * kx) / W + (float)(y * ky) / H);
                            re2 += freq[ky * W + kx].real() * std::cos(angle) - freq[ky * W + kx].imag() * std::sin(angle);
                        }
                    }
                    float v = std::clamp(re2, 0.0f, 1.0f);
                    p.r = v; p.g = v; p.b = v;
                }
            }
        }
        return out;
    }
};

class ChannelMixerEffect : public VideoEffect {
public:
    ChannelMixerEffect() : VideoEffect("Channel Mixer") {
        params["rr"] = EffectParam("rr", 1.0f, -2.0f, 2.0f);
        params["rg"] = EffectParam("rg", 0.0f, -2.0f, 2.0f);
        params["rb"] = EffectParam("rb", 0.0f, -2.0f, 2.0f);
        params["gr"] = EffectParam("gr", 0.0f, -2.0f, 2.0f);
        params["gg"] = EffectParam("gg", 1.0f, -2.0f, 2.0f);
        params["gb"] = EffectParam("gb", 0.0f, -2.0f, 2.0f);
        params["br"] = EffectParam("br", 0.0f, -2.0f, 2.0f);
        params["bg"] = EffectParam("bg", 0.0f, -2.0f, 2.0f);
        params["bb"] = EffectParam("bb", 1.0f, -2.0f, 2.0f);
    }
    Frame apply(const Frame& input) const override {
        Frame out = input;
        float rr = getParam("rr"), rg = getParam("rg"), rb = getParam("rb");
        float gr = getParam("gr"), gg = getParam("gg"), gb = getParam("gb");
        float br = getParam("br"), bg = getParam("bg"), bb = getParam("bb");
        for (int y = 0; y < out.height; y++) {
            for (int x = 0; x < out.width; x++) {
                Pixel& p = out.at(x, y);
                float nr = std::clamp(p.r * rr + p.g * rg + p.b * rb, 0.0f, 1.0f);
                float ng = std::clamp(p.r * gr + p.g * gg + p.b * gb, 0.0f, 1.0f);
                float nb = std::clamp(p.r * br + p.g * bg + p.b * bb, 0.0f, 1.0f);
                p.r = nr; p.g = ng; p.b = nb;
            }
        }
        return out;
    }
};

class RippleEffect : public VideoEffect {
public:
    RippleEffect() : VideoEffect("Ripple") {
        params["amplitude"] = EffectParam("amplitude", 10.0f, 0.0f, 100.0f);
        params["wavelength"] = EffectParam("wavelength", 30.0f, 1.0f, 200.0f);
        params["phase"] = EffectParam("phase", 0.0f, 0.0f, (float)(2 * M_PI));
        params["center_x"] = EffectParam("center_x", 0.5f, 0.0f, 1.0f);
        params["center_y"] = EffectParam("center_y", 0.5f, 0.0f, 1.0f);
    }
    Frame apply(const Frame& input) const override {
        Frame out = input;
        float amp = getParam("amplitude");
        float wl = getParam("wavelength");
        float phase = getParam("phase");
        float cx = getParam("center_x") * input.width;
        float cy = getParam("center_y") * input.height;
        for (int y = 0; y < out.height; y++) {
            for (int x = 0; x < out.width; x++) {
                float dx = x - cx, dy = y - cy;
                float dist = std::sqrt(dx * dx + dy * dy);
                float offset = amp * std::sin(2.0f * (float)M_PI * dist / wl + phase);
                float sx = dist > 0 ? x + offset * dx / dist : x;
                float sy = dist > 0 ? y + offset * dy / dist : y;
                out.at(x, y) = input.sampleBilinear(sx, sy);
            }
        }
        return out;
    }
};

class AngularWarpEffect : public VideoEffect {
public:
    AngularWarpEffect() : VideoEffect("Angular Warp") {
        params["strength"] = EffectParam("strength", 1.0f, -10.0f, 10.0f);
        params["frequency"] = EffectParam("frequency", 3.0f, 0.1f, 20.0f);
        params["center_x"] = EffectParam("center_x", 0.5f, 0.0f, 1.0f);
        params["center_y"] = EffectParam("center_y", 0.5f, 0.0f, 1.0f);
    }
    Frame apply(const Frame& input) const override {
        Frame out = input;
        float str = getParam("strength");
        float freq = getParam("frequency");
        float cx = getParam("center_x") * input.width;
        float cy = getParam("center_y") * input.height;
        for (int y = 0; y < out.height; y++) {
            for (int x = 0; x < out.width; x++) {
                float dx = x - cx, dy = y - cy;
                float r = std::sqrt(dx * dx + dy * dy);
                float a = std::atan2(dy, dx);
                a += str * std::sin(freq * r / std::max(input.width, input.height));
                float sx = cx + r * std::cos(a);
                float sy = cy + r * std::sin(a);
                out.at(x, y) = input.sampleBilinear(sx, sy);
            }
        }
        return out;
    }
};

class LogarithmWarpEffect : public VideoEffect {
public:
    LogarithmWarpEffect() : VideoEffect("Logarithm Warp") {
        params["strength"] = EffectParam("strength", 0.5f, -5.0f, 5.0f);
        params["base"] = EffectParam("base", 2.718f, 1.01f, 20.0f);
        params["center_x"] = EffectParam("center_x", 0.5f, 0.0f, 1.0f);
        params["center_y"] = EffectParam("center_y", 0.5f, 0.0f, 1.0f);
    }
    Frame apply(const Frame& input) const override {
        Frame out = input;
        float str = getParam("strength");
        float base = getParam("base");
        float cx = getParam("center_x") * input.width;
        float cy = getParam("center_y") * input.height;
        float logBase = std::log(base);
        for (int y = 0; y < out.height; y++) {
            for (int x = 0; x < out.width; x++) {
                float dx = x - cx, dy = y - cy;
                float r = std::sqrt(dx * dx + dy * dy);
                float a = std::atan2(dy, dx);
                float nr = r > 0 ? std::log(1 + r * std::abs(str)) / logBase * (str > 0 ? 1 : -1) : 0;
                float sx = cx + nr * std::cos(a);
                float sy = cy + nr * std::sin(a);
                out.at(x, y) = input.sampleBilinear(sx, sy);
            }
        }
        return out;
    }
};

class SinWaveEffect : public VideoEffect {
public:
    SinWaveEffect() : VideoEffect("Sin Wave") {
        params["amplitude_x"] = EffectParam("amplitude_x", 15.0f, 0.0f, 100.0f);
        params["amplitude_y"] = EffectParam("amplitude_y", 0.0f, 0.0f, 100.0f);
        params["frequency_x"] = EffectParam("frequency_x", 5.0f, 0.0f, 50.0f);
        params["frequency_y"] = EffectParam("frequency_y", 5.0f, 0.0f, 50.0f);
        params["phase_x"] = EffectParam("phase_x", 0.0f, 0.0f, (float)(2 * M_PI));
        params["phase_y"] = EffectParam("phase_y", 0.0f, 0.0f, (float)(2 * M_PI));
    }
    Frame apply(const Frame& input) const override {
        Frame out = input;
        float ax = getParam("amplitude_x"), ay = getParam("amplitude_y");
        float fx = getParam("frequency_x"), fy = getParam("frequency_y");
        float px = getParam("phase_x"), py = getParam("phase_y");
        for (int y = 0; y < out.height; y++) {
            for (int x = 0; x < out.width; x++) {
                float sx = x + ax * std::sin(fy * y / input.height * 2 * (float)M_PI + py);
                float sy = y + ay * std::sin(fx * x / input.width * 2 * (float)M_PI + px);
                out.at(x, y) = input.sampleBilinear(sx, sy);
            }
        }
        return out;
    }
};

class JuliaDistortEffect : public VideoEffect {
public:
    JuliaDistortEffect() : VideoEffect("Julia Distort") {
        params["cx"] = EffectParam("cx", -0.7f, -2.0f, 2.0f);
        params["cy"] = EffectParam("cy", 0.27f, -2.0f, 2.0f);
        params["scale"] = EffectParam("scale", 2.5f, 0.1f, 10.0f);
        params["strength"] = EffectParam("strength", 0.3f, 0.0f, 1.0f);
        params["iterations"] = EffectParam("iterations", 8, 1, 64);
    }
    Frame apply(const Frame& input) const override {
        Frame out = input;
        float cx = getParam("cx"), cy = getParam("cy");
        float scale = getParam("scale");
        float str = getParam("strength");
        int iters = params.at("iterations").asInt();
        for (int y = 0; y < out.height; y++) {
            for (int x = 0; x < out.width; x++) {
                float zx = ((float)x / input.width - 0.5f) * scale;
                float zy = ((float)y / input.height - 0.5f) * scale;
                float ox = zx, oy = zy;
                for (int i = 0; i < iters; i++) {
                    float nx = zx * zx - zy * zy + cx;
                    zy = 2 * zx * zy + cy;
                    zx = nx;
                    if (zx * zx + zy * zy > 4) break;
                }
                float dx = (zx - ox) * str * input.width / scale;
                float dy = (zy - oy) * str * input.height / scale;
                out.at(x, y) = input.sampleBilinear(x + dx, y + dy);
            }
        }
        return out;
    }
};

class MandelbrotGeneratorEffect : public VideoEffect {
public:
    MandelbrotGeneratorEffect() : VideoEffect("Mandelbrot Generator") {
        params["zoom"] = EffectParam("zoom", 1.0f, 0.01f, 1000.0f);
        params["center_x"] = EffectParam("center_x", -0.5f, -3.0f, 3.0f);
        params["center_y"] = EffectParam("center_y", 0.0f, -3.0f, 3.0f);
        params["iterations"] = EffectParam("iterations", 64, 8, 512);
        params["color_scale"] = EffectParam("color_scale", 1.0f, 0.1f, 10.0f);
        params["blend"] = EffectParam("blend", 1.0f, 0.0f, 1.0f);
    }
    Frame apply(const Frame& input) const override {
        Frame out = input;
        float zoom = getParam("zoom");
        float ocx = getParam("center_x"), ocy = getParam("center_y");
        int maxIter = params.at("iterations").asInt();
        float cs = getParam("color_scale");
        float blend = getParam("blend");
        for (int y = 0; y < out.height; y++) {
            for (int x = 0; x < out.width; x++) {
                float cr = ((float)x / out.width - 0.5f) * 3.5f / zoom + ocx;
                float ci = ((float)y / out.height - 0.5f) * 2.0f / zoom + ocy;
                float zr = 0, zi = 0;
                int iter = 0;
                while (zr * zr + zi * zi < 4 && iter < maxIter) {
                    float nr = zr * zr - zi * zi + cr;
                    zi = 2 * zr * zi + ci;
                    zr = nr;
                    iter++;
                }
                Pixel& p = out.at(x, y);
                float t = (float)iter / maxIter;
                float fr = 0.5f + 0.5f * std::sin(t * cs * 10.0f + 0.0f);
                float fg = 0.5f + 0.5f * std::sin(t * cs * 10.0f + 2.094f);
                float fb = 0.5f + 0.5f * std::sin(t * cs * 10.0f + 4.189f);
                p.r = p.r * (1 - blend) + fr * blend;
                p.g = p.g * (1 - blend) + fg * blend;
                p.b = p.b * (1 - blend) + fb * blend;
            }
        }
        return out;
    }
};

class JuliaGeneratorEffect : public VideoEffect {
public:
    JuliaGeneratorEffect() : VideoEffect("Julia Generator") {
        params["cx"] = EffectParam("cx", -0.7f, -2.0f, 2.0f);
        params["cy"] = EffectParam("cy", 0.27f, -2.0f, 2.0f);
        params["zoom"] = EffectParam("zoom", 1.0f, 0.01f, 100.0f);
        params["iterations"] = EffectParam("iterations", 64, 8, 512);
        params["color_scale"] = EffectParam("color_scale", 1.0f, 0.1f, 10.0f);
        params["blend"] = EffectParam("blend", 1.0f, 0.0f, 1.0f);
    }
    Frame apply(const Frame& input) const override {
        Frame out = input;
        float cx = getParam("cx"), cy = getParam("cy");
        float zoom = getParam("zoom");
        int maxIter = params.at("iterations").asInt();
        float cs = getParam("color_scale");
        float blend = getParam("blend");
        for (int y = 0; y < out.height; y++) {
            for (int x = 0; x < out.width; x++) {
                float zr = ((float)x / out.width - 0.5f) * 3.5f / zoom;
                float zi = ((float)y / out.height - 0.5f) * 2.0f / zoom;
                int iter = 0;
                while (zr * zr + zi * zi < 4 && iter < maxIter) {
                    float nr = zr * zr - zi * zi + cx;
                    zi = 2 * zr * zi + cy;
                    zr = nr;
                    iter++;
                }
                Pixel& p = out.at(x, y);
                float t = (float)iter / maxIter;
                float fr = 0.5f + 0.5f * std::sin(t * cs * 10.0f + 1.0f);
                float fg = 0.5f + 0.5f * std::sin(t * cs * 10.0f + 3.0f);
                float fb = 0.5f + 0.5f * std::sin(t * cs * 10.0f + 5.0f);
                p.r = p.r * (1 - blend) + fr * blend;
                p.g = p.g * (1 - blend) + fg * blend;
                p.b = p.b * (1 - blend) + fb * blend;
            }
        }
        return out;
    }
};

class WarholEffect : public VideoEffect {
public:
    WarholEffect() : VideoEffect("Warhol") {
        params["grid_x"] = EffectParam("grid_x", 2, 1, 6);
        params["grid_y"] = EffectParam("grid_y", 2, 1, 6);
        params["saturation"] = EffectParam("saturation", 2.0f, 0.0f, 5.0f);
    }
    Frame apply(const Frame& input) const override {
        Frame out(input.width, input.height);
        out.timestamp = input.timestamp;
        int gx = params.at("grid_x").asInt(), gy = params.at("grid_y").asInt();
        float sat = getParam("saturation");
        int cellW = input.width / gx, cellH = input.height / gy;
        std::vector<std::tuple<float, float, float>> tints = {
            {1.0f, 0.3f, 0.3f}, {0.3f, 1.0f, 0.3f}, {0.3f, 0.3f, 1.0f},
            {1.0f, 1.0f, 0.3f}, {1.0f, 0.3f, 1.0f}, {0.3f, 1.0f, 1.0f}
        };
        for (int cy = 0; cy < gy; cy++) {
            for (int cx2 = 0; cx2 < gx; cx2++) {
                int ti = (cy * gx + cx2) % tints.size();
                float tr = std::get<0>(tints[ti]);
                float tg = std::get<1>(tints[ti]);
                float tb = std::get<2>(tints[ti]);
                for (int y = 0; y < cellH; y++) {
                    for (int x = 0; x < cellW; x++) {
                        float sx = (float)x / cellW * input.width;
                        float sy = (float)y / cellH * input.height;
                        Pixel p = input.sampleBilinear(sx, sy);
                        float lum = 0.299f * p.r + 0.587f * p.g + 0.114f * p.b;
                        p.r = std::clamp(lum + (p.r - lum) * sat, 0.0f, 1.0f) * tr;
                        p.g = std::clamp(lum + (p.g - lum) * sat, 0.0f, 1.0f) * tg;
                        p.b = std::clamp(lum + (p.b - lum) * sat, 0.0f, 1.0f) * tb;
                        int ox = cx2 * cellW + x, oy = cy * cellH + y;
                        if (ox < input.width && oy < input.height) out.at(ox, oy) = p;
                    }
                }
            }
        }
        return out;
    }
};

class ChromaticAberrationEffect : public VideoEffect {
public:
    ChromaticAberrationEffect() : VideoEffect("Chromatic Aberration") {
        params["r_offset_x"] = EffectParam("r_offset_x", 3.0f, -50.0f, 50.0f);
        params["r_offset_y"] = EffectParam("r_offset_y", 0.0f, -50.0f, 50.0f);
        params["b_offset_x"] = EffectParam("b_offset_x", -3.0f, -50.0f, 50.0f);
        params["b_offset_y"] = EffectParam("b_offset_y", 0.0f, -50.0f, 50.0f);
        params["radial"] = EffectParam("radial", false);
        params["strength"] = EffectParam("strength", 1.0f, 0.0f, 5.0f);
    }
    Frame apply(const Frame& input) const override {
        Frame out = input;
        float rx = getParam("r_offset_x"), ry = getParam("r_offset_y");
        float bx = getParam("b_offset_x"), by = getParam("b_offset_y");
        bool radial = params.at("radial").asBool();
        float str = getParam("strength");
        float cx = input.width * 0.5f, cy = input.height * 0.5f;
        for (int y = 0; y < out.height; y++) {
            for (int x = 0; x < out.width; x++) {
                float drx = rx, dry = ry, dbx = bx, dby = by;
                if (radial) {
                    float dx = (x - cx) / cx, dy2 = (y - cy) / cy;
                    drx = dx * rx * str; dry = dy2 * ry * str;
                    dbx = dx * bx * str; dby = dy2 * by * str;
                }
                Pixel r = input.sampleBilinear(x + drx, y + dry);
                Pixel g = input.at(std::clamp(x, 0, input.width - 1), std::clamp(y, 0, input.height - 1));
                Pixel b = input.sampleBilinear(x + dbx, y + dby);
                out.at(x, y) = {r.r, g.g, b.b, g.a};
            }
        }
        return out;
    }
};

class EdgeDetectEffect : public VideoEffect {
public:
    EdgeDetectEffect() : VideoEffect("Edge Detect") {
        params["threshold"] = EffectParam("threshold", 0.1f, 0.0f, 1.0f);
        params["strength"] = EffectParam("strength", 1.0f, 0.0f, 5.0f);
        params["invert"] = EffectParam("invert", false);
        params["blend"] = EffectParam("blend", 1.0f, 0.0f, 1.0f);
    }
    Frame apply(const Frame& input) const override {
        Frame out = input;
        float thr = getParam("threshold");
        float str = getParam("strength");
        bool inv = params.at("invert").asBool();
        float blend = getParam("blend");
        int gx[3][3] = {{-1, 0, 1}, {-2, 0, 2}, {-1, 0, 1}};
        int gy[3][3] = {{-1, -2, -1}, {0, 0, 0}, {1, 2, 1}};
        for (int y = 0; y < input.height; y++) {
            for (int x = 0; x < input.width; x++) {
                float sx = 0, sy2 = 0;
                for (int ky = -1; ky <= 1; ky++) {
                    for (int kx = -1; kx <= 1; kx++) {
                        int nx = std::clamp(x + kx, 0, input.width - 1);
                        int ny = std::clamp(y + ky, 0, input.height - 1);
                        const Pixel& p = input.at(nx, ny);
                        float lum = 0.299f * p.r + 0.587f * p.g + 0.114f * p.b;
                        sx += lum * gx[ky + 1][kx + 1];
                        sy2 += lum * gy[ky + 1][kx + 1];
                    }
                }
                float mag = std::clamp(std::sqrt(sx * sx + sy2 * sy2) * str, 0.0f, 1.0f);
                if (mag < thr) mag = 0;
                if (inv) mag = 1 - mag;
                Pixel& p = out.at(x, y);
                p.r = p.r * (1 - blend) + mag * blend;
                p.g = p.g * (1 - blend) + mag * blend;
                p.b = p.b * (1 - blend) + mag * blend;
            }
        }
        return out;
    }
};

class LensCorrectEffect : public VideoEffect {
public:
    LensCorrectEffect() : VideoEffect("Lens Correct") {
        params["k1"] = EffectParam("k1", 0.0f, -1.0f, 1.0f);
        params["k2"] = EffectParam("k2", 0.0f, -1.0f, 1.0f);
        params["k3"] = EffectParam("k3", 0.0f, -1.0f, 1.0f);
        params["scale"] = EffectParam("scale", 1.0f, 0.5f, 2.0f);
    }
    Frame apply(const Frame& input) const override {
        Frame out = input;
        float k1 = getParam("k1"), k2 = getParam("k2"), k3 = getParam("k3");
        float scale = getParam("scale");
        float cx = input.width * 0.5f, cy = input.height * 0.5f;
        float norm = std::max(cx, cy);
        for (int y = 0; y < out.height; y++) {
            for (int x = 0; x < out.width; x++) {
                float nx = (x - cx) / (norm * scale);
                float ny = (y - cy) / (norm * scale);
                float r2 = nx * nx + ny * ny;
                float r4 = r2 * r2, r6 = r4 * r2;
                float factor = 1 + k1 * r2 + k2 * r4 + k3 * r6;
                float sx = cx + nx * factor * norm;
                float sy = cy + ny * factor * norm;
                out.at(x, y) = input.sampleBilinear(sx, sy);
            }
        }
        return out;
    }
};

class VignetteEffect : public VideoEffect {
public:
    VignetteEffect() : VideoEffect("Vignette") {
        params["strength"] = EffectParam("strength", 0.5f, 0.0f, 1.0f);
        params["radius"] = EffectParam("radius", 0.7f, 0.1f, 2.0f);
        params["softness"] = EffectParam("softness", 0.5f, 0.01f, 2.0f);
        params["center_x"] = EffectParam("center_x", 0.5f, 0.0f, 1.0f);
        params["center_y"] = EffectParam("center_y", 0.5f, 0.0f, 1.0f);
    }
    Frame apply(const Frame& input) const override {
        Frame out = input;
        float str = getParam("strength");
        float rad = getParam("radius");
        float soft = getParam("softness");
        float cx = getParam("center_x"), cy = getParam("center_y");
        for (int y = 0; y < out.height; y++) {
            for (int x = 0; x < out.width; x++) {
                float dx = (float)x / out.width - cx;
                float dy = (float)y / out.height - cy;
                float dist = std::sqrt(dx * dx + dy * dy) / rad;
                float v = std::clamp((dist - 1.0f + soft) / soft, 0.0f, 1.0f);
                float factor = 1.0f - str * v * v;
                Pixel& p = out.at(x, y);
                p.r *= factor; p.g *= factor; p.b *= factor;
            }
        }
        return out;
    }
};

class ElseveLensEffect : public VideoEffect {
public:
    ElseveLensEffect() : VideoEffect("Elseve Lens") {
        params["focal_length"] = EffectParam("focal_length", 50.0f, 10.0f, 500.0f);
        params["aberration"] = EffectParam("aberration", 0.02f, 0.0f, 0.5f);
        params["blur_strength"] = EffectParam("blur_strength", 0.5f, 0.0f, 2.0f);
        params["bloom"] = EffectParam("bloom", 0.1f, 0.0f, 1.0f);
    }
    Frame apply(const Frame& input) const override {
        Frame out = input;
        float fl = getParam("focal_length");
        float ab = getParam("aberration");
        float blur = getParam("blur_strength");
        float bloom = getParam("bloom");
        float cx = input.width * 0.5f, cy = input.height * 0.5f;
        for (int y = 0; y < out.height; y++) {
            for (int x = 0; x < out.width; x++) {
                float dx = (x - cx) / cx, dy = (y - cy) / cy;
                float r2 = dx * dx + dy * dy;
                float distort = 1.0f + r2 * ab * (fl / 50.0f);
                float sx = cx + (x - cx) / distort;
                float sy = cy + (y - cy) / distort;
                Pixel p = input.sampleBilinear(sx, sy);
                if (blur > 0) {
                    float blurR = r2 * blur * 3;
                    float sum_r = 0, sum_g = 0, sum_b = 0, sw = 0;
                    int br = (int)blurR;
                    for (int ky = -br; ky <= br; ky++) {
                        for (int kx = -br; kx <= br; kx++) {
                            float w = std::exp(-(kx*kx + ky*ky) / (2.0f * blurR * blurR + 0.001f));
                            Pixel sp = input.sampleBilinear(sx + kx, sy + ky);
                            sum_r += sp.r * w; sum_g += sp.g * w; sum_b += sp.b * w; sw += w;
                        }
                    }
                    if (sw > 0) { p.r = sum_r / sw; p.g = sum_g / sw; p.b = sum_b / sw; }
                }
                float lum = 0.299f * p.r + 0.587f * p.g + 0.114f * p.b;
                if (lum > 0.7f) { p.r = std::clamp(p.r + bloom, 0.0f, 1.0f); p.g = std::clamp(p.g + bloom * 0.8f, 0.0f, 1.0f); p.b = std::clamp(p.b + bloom * 0.6f, 0.0f, 1.0f); }
                out.at(x, y) = p;
            }
        }
        return out;
    }
};

class ExtensionistLensVignetteEffect : public VideoEffect {
public:
    ExtensionistLensVignetteEffect() : VideoEffect("Extensionist Lens Vignette") {
        params["strength"] = EffectParam("strength", 0.7f, 0.0f, 1.0f);
        params["outer_radius"] = EffectParam("outer_radius", 0.8f, 0.0f, 2.0f);
        params["inner_radius"] = EffectParam("inner_radius", 0.3f, 0.0f, 1.0f);
        params["color_r"] = EffectParam("color_r", 0.0f, 0.0f, 1.0f);
        params["color_g"] = EffectParam("color_g", 0.0f, 0.0f, 1.0f);
        params["color_b"] = EffectParam("color_b", 0.0f, 0.0f, 1.0f);
        params["lens_distort"] = EffectParam("lens_distort", 0.1f, 0.0f, 1.0f);
    }
    Frame apply(const Frame& input) const override {
        Frame out = input;
        float str = getParam("strength");
        float outr = getParam("outer_radius");
        float inr = getParam("inner_radius");
        float cr = getParam("color_r"), cg = getParam("color_g"), cb = getParam("color_b");
        float ld = getParam("lens_distort");
        float cx = input.width * 0.5f, cy = input.height * 0.5f;
        for (int y = 0; y < out.height; y++) {
            for (int x = 0; x < out.width; x++) {
                float dx = (float)(x - cx) / cx, dy = (float)(y - cy) / cy;
                float r = std::sqrt(dx * dx + dy * dy);
                float ldist = 1.0f + r * r * ld;
                float sx = cx + (x - cx) / ldist;
                float sy = cy + (y - cy) / ldist;
                Pixel p = input.sampleBilinear(sx, sy);
                float vigFactor = 0;
                if (r > inr) vigFactor = std::clamp((r - inr) / (outr - inr + 0.001f), 0.0f, 1.0f);
                vigFactor = vigFactor * vigFactor * str;
                p.r = p.r * (1 - vigFactor) + cr * vigFactor;
                p.g = p.g * (1 - vigFactor) + cg * vigFactor;
                p.b = p.b * (1 - vigFactor) + cb * vigFactor;
                out.at(x, y) = p;
            }
        }
        return out;
    }
};

class AnamorphicEffect : public VideoEffect {
public:
    AnamorphicEffect() : VideoEffect("Anamorphic") {
        params["squeeze_x"] = EffectParam("squeeze_x", 1.33f, 0.5f, 3.0f);
        params["squeeze_y"] = EffectParam("squeeze_y", 1.0f, 0.5f, 3.0f);
        params["bokeh_x"] = EffectParam("bokeh_x", 2.0f, 1.0f, 5.0f);
        params["flare_intensity"] = EffectParam("flare_intensity", 0.3f, 0.0f, 1.0f);
    }
    Frame apply(const Frame& input) const override {
        Frame out = input;
        float sqx = getParam("squeeze_x"), sqy = getParam("squeeze_y");
        float flare = getParam("flare_intensity");
        float cx = input.width * 0.5f, cy = input.height * 0.5f;
        for (int y = 0; y < out.height; y++) {
            for (int x = 0; x < out.width; x++) {
                float sx = cx + (x - cx) / sqx;
                float sy = cy + (y - cy) / sqy;
                out.at(x, y) = input.sampleBilinear(sx, sy);
            }
        }
        if (flare > 0) {
            int midY = out.height / 2;
            for (int y = midY - 2; y <= midY + 2; y++) {
                if (y < 0 || y >= out.height) continue;
                for (int x = 0; x < out.width; x++) {
                    float t = (float)x / out.width;
                    float falloff = std::exp(-std::abs(y - midY) * 2.0f);
                    Pixel& p = out.at(x, y);
                    p.r = std::clamp(p.r + flare * falloff * 0.8f, 0.0f, 1.0f);
                    p.g = std::clamp(p.g + flare * falloff * 0.9f, 0.0f, 1.0f);
                    p.b = std::clamp(p.b + flare * falloff, 0.0f, 1.0f);
                }
            }
        }
        return out;
    }
};

class LensFlareEffect : public VideoEffect {
public:
    LensFlareEffect() : VideoEffect("Lens Flare") {
        params["flare_x"] = EffectParam("flare_x", 0.3f, 0.0f, 1.0f);
        params["flare_y"] = EffectParam("flare_y", 0.3f, 0.0f, 1.0f);
        params["intensity"] = EffectParam("intensity", 0.8f, 0.0f, 2.0f);
        params["radius"] = EffectParam("radius", 0.1f, 0.01f, 0.5f);
        params["halo_radius"] = EffectParam("halo_radius", 0.25f, 0.0f, 0.5f);
        params["streak_length"] = EffectParam("streak_length", 0.4f, 0.0f, 1.0f);
        params["color_r"] = EffectParam("color_r", 1.0f, 0.0f, 1.0f);
        params["color_g"] = EffectParam("color_g", 0.9f, 0.0f, 1.0f);
        params["color_b"] = EffectParam("color_b", 0.7f, 0.0f, 1.0f);
    }
    Frame apply(const Frame& input) const override {
        Frame out = input;
        float fx = getParam("flare_x") * input.width;
        float fy = getParam("flare_y") * input.height;
        float intensity = getParam("intensity");
        float rad = getParam("radius") * std::min(input.width, input.height);
        float hrad = getParam("halo_radius") * std::min(input.width, input.height);
        float streak = getParam("streak_length") * input.width * 0.5f;
        float cr = getParam("color_r"), cg = getParam("color_g"), cb = getParam("color_b");
        for (int y = 0; y < out.height; y++) {
            for (int x = 0; x < out.width; x++) {
                float dx = x - fx, dy = y - fy;
                float dist = std::sqrt(dx * dx + dy * dy);
                float flareVal = 0;
                flareVal += intensity * std::exp(-dist * dist / (2 * rad * rad));
                if (hrad > 0) {
                    float halo = std::exp(-std::pow(dist - hrad, 2) / (2 * hrad * 0.1f * hrad * 0.1f)) * intensity * 0.3f;
                    flareVal += halo;
                }
                if (streak > 0 && std::abs(dy) < 2) {
                    float streakVal = intensity * 0.2f * std::exp(-std::abs(dx) / streak) * std::exp(-dy * dy / 2.0f);
                    flareVal += streakVal;
                }
                Pixel& p = out.at(x, y);
                p.r = std::clamp(p.r + flareVal * cr, 0.0f, 1.0f);
                p.g = std::clamp(p.g + flareVal * cg, 0.0f, 1.0f);
                p.b = std::clamp(p.b + flareVal * cb, 0.0f, 1.0f);
            }
        }
        return out;
    }
};

class CornerPinEffect : public VideoEffect {
public:
    CornerPinEffect() : VideoEffect("Corner Pin") {
        params["tl_x"] = EffectParam("tl_x", 0.0f, -1.0f, 2.0f);
        params["tl_y"] = EffectParam("tl_y", 0.0f, -1.0f, 2.0f);
        params["tr_x"] = EffectParam("tr_x", 1.0f, -1.0f, 2.0f);
        params["tr_y"] = EffectParam("tr_y", 0.0f, -1.0f, 2.0f);
        params["bl_x"] = EffectParam("bl_x", 0.0f, -1.0f, 2.0f);
        params["bl_y"] = EffectParam("bl_y", 1.0f, -1.0f, 2.0f);
        params["br_x"] = EffectParam("br_x", 1.0f, -1.0f, 2.0f);
        params["br_y"] = EffectParam("br_y", 1.0f, -1.0f, 2.0f);
    }
    Frame apply(const Frame& input) const override {
        Frame out = input;
        float tlx = getParam("tl_x"), tly = getParam("tl_y");
        float trx = getParam("tr_x"), try_ = getParam("tr_y");
        float blx = getParam("bl_x"), bly = getParam("bl_y");
        float brx = getParam("br_x"), bry = getParam("br_y");
        int W = input.width, H = input.height;
        for (int y = 0; y < H; y++) {
            for (int x = 0; x < W; x++) {
                float u = (float)x / W, v = (float)y / H;
                float sx = (tlx * (1 - u) + trx * u) * (1 - v) + (blx * (1 - u) + brx * u) * v;
                float sy = (tly * (1 - u) + try_ * u) * (1 - v) + (bly * (1 - u) + bry * u) * v;
                out.at(x, y) = input.sampleBilinear(sx * W, sy * H);
            }
        }
        return out;
    }
};

class LumiLayerEffect : public VideoEffect {
public:
    LumiLayerEffect() : VideoEffect("LumiLayer") {
        params["luminance_boost"] = EffectParam("luminance_boost", 0.3f, -1.0f, 1.0f);
        params["shadow_lift"] = EffectParam("shadow_lift", 0.0f, 0.0f, 0.5f);
        params["highlight_roll"] = EffectParam("highlight_roll", 1.0f, 0.5f, 2.0f);
        params["layers"] = EffectParam("layers", 3, 1, 8);
    }
    Frame apply(const Frame& input) const override {
        Frame out = input;
        float boost = getParam("luminance_boost");
        float shadow = getParam("shadow_lift");
        float highlight = getParam("highlight_roll");
        int layers = params.at("layers").asInt();
        for (int y = 0; y < out.height; y++) {
            for (int x = 0; x < out.width; x++) {
                Pixel& p = out.at(x, y);
                float lum = 0.299f * p.r + 0.587f * p.g + 0.114f * p.b;
                float newLum = lum;
                for (int l = 0; l < layers; l++) {
                    float t = (float)l / layers;
                    float layerBoost = boost * std::sin(newLum * (float)M_PI * (l + 1));
                    newLum = std::clamp(newLum + layerBoost / layers, 0.0f, 1.0f);
                }
                newLum = newLum + shadow * (1 - lum);
                newLum = std::pow(newLum, 1.0f / highlight);
                float ratio = lum > 0 ? newLum / lum : 1.0f;
                p.r = std::clamp(p.r * ratio, 0.0f, 1.0f);
                p.g = std::clamp(p.g * ratio, 0.0f, 1.0f);
                p.b = std::clamp(p.b * ratio, 0.0f, 1.0f);
            }
        }
        return out;
    }
};

class LumiShakeEffect : public VideoEffect {
public:
    LumiShakeEffect() : VideoEffect("LumiShake") {
        params["shake_x"] = EffectParam("shake_x", 5.0f, 0.0f, 50.0f);
        params["shake_y"] = EffectParam("shake_y", 5.0f, 0.0f, 50.0f);
        params["frequency"] = EffectParam("frequency", 1.0f, 0.0f, 10.0f);
        params["time"] = EffectParam("time", 0.0f, 0.0f, 100.0f);
        params["luminance_trigger"] = EffectParam("luminance_trigger", 0.5f, 0.0f, 1.0f);
    }
    Frame apply(const Frame& input) const override {
        Frame out = input;
        float sx = getParam("shake_x"), sy = getParam("shake_y");
        float freq = getParam("frequency");
        float time = getParam("time");
        float lumTrigger = getParam("luminance_trigger");
        float avgLum = 0;
        for (int y = 0; y < input.height; y++)
            for (int x = 0; x < input.width; x++) {
                const Pixel& p = input.at(x, y);
                avgLum += 0.299f * p.r + 0.587f * p.g + 0.114f * p.b;
            }
        avgLum /= (input.width * input.height);
        float trigger = std::clamp(avgLum / (lumTrigger + 0.001f), 0.0f, 1.0f);
        float offX = sx * trigger * std::sin(2.0f * (float)M_PI * freq * time);
        float offY = sy * trigger * std::cos(2.0f * (float)M_PI * freq * time * 0.7f);
        for (int y = 0; y < out.height; y++) {
            for (int x = 0; x < out.width; x++) {
                out.at(x, y) = input.sampleBilinear(x + offX, y + offY);
            }
        }
        return out;
    }
};

class Lumi3DShapeEffect : public VideoEffect {
public:
    Lumi3DShapeEffect() : VideoEffect("Lumi3DShape") {
        params["shape_type"] = EffectParam("shape_type", 0, 0, 3);
        params["rotation_x"] = EffectParam("rotation_x", 0.0f, -180.0f, 180.0f);
        params["rotation_y"] = EffectParam("rotation_y", 0.0f, -180.0f, 180.0f);
        params["perspective"] = EffectParam("perspective", 0.3f, 0.0f, 1.0f);
        params["depth"] = EffectParam("depth", 0.5f, 0.0f, 2.0f);
    }
    Frame apply(const Frame& input) const override {
        Frame out = input;
        float rx = getParam("rotation_x") * (float)M_PI / 180.0f;
        float ry = getParam("rotation_y") * (float)M_PI / 180.0f;
        float persp = getParam("perspective");
        float depth = getParam("depth");
        float cx = input.width * 0.5f, cy = input.height * 0.5f;
        float cosX = std::cos(rx), sinX = std::sin(rx);
        float cosY = std::cos(ry), sinY = std::sin(ry);
        for (int y = 0; y < out.height; y++) {
            for (int x = 0; x < out.width; x++) {
                float nx = ((float)x / input.width - 0.5f) * 2;
                float ny = ((float)y / input.height - 0.5f) * 2;
                float nz = depth * (1 - std::sqrt(nx*nx + ny*ny));
                float ry2 = nx * cosY - nz * sinY;
                float rz = nx * sinY + nz * cosY;
                float rx2 = ny * cosX + rz * sinX;
                float rz2 = -ny * sinX + rz * cosX;
                float w = 1.0f + persp * rz2;
                float sx = (ry2 / w + 0.5f) * input.width;
                float sy = (rx2 / w + 0.5f) * input.height;
                out.at(x, y) = input.sampleBilinear(sx, sy);
            }
        }
        return out;
    }
};

class MirrorEffect : public VideoEffect {
public:
    MirrorEffect() : VideoEffect("Mirror") {
        params["axis"] = EffectParam("axis", 0, 0, 3);
        params["position"] = EffectParam("position", 0.5f, 0.0f, 1.0f);
        params["flip_source"] = EffectParam("flip_source", false);
    }
    Frame apply(const Frame& input) const override {
        Frame out = input;
        int axis = params.at("axis").asInt();
        float pos = getParam("position");
        bool flip = params.at("flip_source").asBool();
        for (int y = 0; y < out.height; y++) {
            for (int x = 0; x < out.width; x++) {
                int sx = x, sy = y;
                if (axis == 0 && x > (int)(pos * input.width)) sx = (int)(pos * input.width) * 2 - x;
                else if (axis == 1 && x < (int)(pos * input.width)) sx = (int)(pos * input.width) * 2 - x;
                else if (axis == 2 && y > (int)(pos * input.height)) sy = (int)(pos * input.height) * 2 - y;
                else if (axis == 3 && y < (int)(pos * input.height)) sy = (int)(pos * input.height) * 2 - y;
                sx = std::clamp(sx, 0, input.width - 1);
                sy = std::clamp(sy, 0, input.height - 1);
                out.at(x, y) = input.at(sx, sy);
            }
        }
        return out;
    }
};

class GenericEquationEffect : public VideoEffect {
public:
    GenericEquationEffect() : VideoEffect("Generic Equation") {
        params["equation_r"] = EffectParam("equation_r", 0.0f, -10.0f, 10.0f);
        params["equation_g"] = EffectParam("equation_g", 0.0f, -10.0f, 10.0f);
        params["equation_b"] = EffectParam("equation_b", 0.0f, -10.0f, 10.0f);
        params["freq"] = EffectParam("freq", 1.0f, 0.0f, 20.0f);
        params["blend"] = EffectParam("blend", 1.0f, 0.0f, 1.0f);
    }
    Frame apply(const Frame& input) const override {
        Frame out = input;
        float er = getParam("equation_r"), eg = getParam("equation_g"), eb = getParam("equation_b");
        float freq = getParam("freq");
        float blend = getParam("blend");
        for (int y = 0; y < out.height; y++) {
            for (int x = 0; x < out.width; x++) {
                float u = (float)x / out.width;
                float v = (float)y / out.height;
                Pixel& p = out.at(x, y);
                float fr = std::clamp(p.r + er * std::sin(freq * u * 2 * (float)M_PI) * std::cos(freq * v * 2 * (float)M_PI), 0.0f, 1.0f);
                float fg = std::clamp(p.g + eg * std::cos(freq * u * 2 * (float)M_PI) * std::sin(freq * v * 2 * (float)M_PI), 0.0f, 1.0f);
                float fb = std::clamp(p.b + eb * std::sin(freq * (u + v) * 2 * (float)M_PI), 0.0f, 1.0f);
                p.r = p.r * (1 - blend) + fr * blend;
                p.g = p.g * (1 - blend) + fg * blend;
                p.b = p.b * (1 - blend) + fb * blend;
            }
        }
        return out;
    }
};

class ThreeDRippleEffect : public VideoEffect {
public:
    ThreeDRippleEffect() : VideoEffect("3D Ripple") {
        params["amplitude"] = EffectParam("amplitude", 20.0f, 0.0f, 100.0f);
        params["frequency_x"] = EffectParam("frequency_x", 5.0f, 0.1f, 30.0f);
        params["frequency_y"] = EffectParam("frequency_y", 5.0f, 0.1f, 30.0f);
        params["phase"] = EffectParam("phase", 0.0f, 0.0f, (float)(2 * M_PI));
        params["perspective"] = EffectParam("perspective", 0.2f, 0.0f, 1.0f);
        params["depth_scale"] = EffectParam("depth_scale", 0.5f, 0.0f, 2.0f);
    }
    Frame apply(const Frame& input) const override {
        Frame out = input;
        float amp = getParam("amplitude");
        float fx = getParam("frequency_x"), fy = getParam("frequency_y");
        float phase = getParam("phase");
        float persp = getParam("perspective");
        float depthScale = getParam("depth_scale");
        float cx = input.width * 0.5f, cy = input.height * 0.5f;
        for (int y = 0; y < out.height; y++) {
            for (int x = 0; x < out.width; x++) {
                float u = (float)x / input.width, v = (float)y / input.height;
                float depth = amp * std::sin(fx * u * 2 * (float)M_PI + phase) * std::cos(fy * v * 2 * (float)M_PI + phase);
                float perspFactor = 1.0f / (1.0f + persp * depth * depthScale / input.height);
                float sx = cx + (x - cx) * perspFactor;
                float sy = cy + (y - cy) * perspFactor + depth;
                out.at(x, y) = input.sampleBilinear(sx, sy);
            }
        }
        return out;
    }
};

class MediaItem {
public:
    std::string path;
    std::string name;
    std::string type;
    double duration;
    int width, height;
    double fps;
    std::vector<Frame> frames;
    MediaItem() : duration(0), width(0), height(0), fps(24.0) {}
    MediaItem(const std::string& p) : path(p), duration(0), width(1920), height(1080), fps(24.0) {
        name = fs::path(p).filename().string();
        std::string ext = fs::path(p).extension().string();
        if (ext == ".mp4" || ext == ".mov" || ext == ".avi" || ext == ".mkv") type = "video";
        else if (ext == ".mp3" || ext == ".wav" || ext == ".aac") type = "audio";
        else if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp") type = "image";
        else type = "unknown";
    }
};

class Clip {
public:
    std::string id;
    std::shared_ptr<MediaItem> media;
    double startTime;
    double endTime;
    double trackPosition;
    double speed;
    bool muted;
    bool locked;
    std::vector<std::shared_ptr<VideoEffect>> effects;
    Clip() : startTime(0), endTime(5), trackPosition(0), speed(1.0), muted(false), locked(false) {
        static int counter = 0;
        id = "clip_" + std::to_string(counter++);
    }
    double duration() const { return (endTime - startTime) / speed; }
    Frame getFrameAt(double t) const {
        if (!media || media->frames.empty()) {
            Frame f(media ? media->width : 1920, media ? media->height : 1080);
            return f;
        }
        double mediaT = startTime + t * speed;
        int frameIdx = (int)(mediaT * media->fps) % std::max((int)media->frames.size(), 1);
        frameIdx = std::clamp(frameIdx, 0, (int)media->frames.size() - 1);
        Frame f = media->frames[frameIdx];
        for (auto& effect : effects) {
            if (effect->enabled) f = effect->apply(f);
        }
        return f;
    }
    void addEffect(std::shared_ptr<VideoEffect> effect) {
        effects.push_back(effect);
    }
    bool removeEffect(const std::string& effectName) {
        auto it = std::find_if(effects.begin(), effects.end(),
            [&](const std::shared_ptr<VideoEffect>& e) { return e->name == effectName; });
        if (it != effects.end()) { effects.erase(it); return true; }
        return false;
    }
};

class Track {
public:
    std::string id;
    std::string name;
    std::string type;
    bool muted;
    bool solo;
    bool locked;
    float volume;
    float opacity;
    std::vector<std::shared_ptr<Clip>> clips;
    Track() : muted(false), solo(false), locked(false), volume(1.0f), opacity(1.0f) {
        static int counter = 0;
        id = "track_" + std::to_string(counter++);
        name = "Track " + std::to_string(counter);
        type = "video";
    }
    void addClip(std::shared_ptr<Clip> clip) {
        clips.push_back(clip);
        std::sort(clips.begin(), clips.end(),
            [](const std::shared_ptr<Clip>& a, const std::shared_ptr<Clip>& b) {
                return a->trackPosition < b->trackPosition;
            });
    }
    bool removeClip(const std::string& clipId) {
        auto it = std::find_if(clips.begin(), clips.end(),
            [&](const std::shared_ptr<Clip>& c) { return c->id == clipId; });
        if (it != clips.end()) { clips.erase(it); return true; }
        return false;
    }
    std::shared_ptr<Clip> getClipAt(double time) const {
        for (auto& clip : clips) {
            if (time >= clip->trackPosition && time < clip->trackPosition + clip->duration()) return clip;
        }
        return nullptr;
    }
};

class Layer {
public:
    std::string id;
    std::string name;
    bool visible;
    bool locked;
    float opacity;
    int zOrder;
    std::string blendMode;
    std::vector<std::shared_ptr<Track>> tracks;
    Layer() : visible(true), locked(false), opacity(1.0f), zOrder(0), blendMode("normal") {
        static int counter = 0;
        id = "layer_" + std::to_string(counter++);
        name = "Layer " + std::to_string(counter);
    }
    void addTrack(std::shared_ptr<Track> track) {
        tracks.push_back(track);
    }
    bool removeTrack(const std::string& trackId) {
        auto it = std::find_if(tracks.begin(), tracks.end(),
            [&](const std::shared_ptr<Track>& t) { return t->id == trackId; });
        if (it != tracks.end()) { tracks.erase(it); return true; }
        return false;
    }
};

struct TimelineState {
    std::vector<std::shared_ptr<Layer>> layers;
    std::vector<std::shared_ptr<MediaItem>> mediaPool;
    double duration;
    double fps;
    int width, height;
    double playhead;
    std::string projectName;
    std::string projectPath;
    TimelineState() : duration(300), fps(24), width(1920), height(1080), playhead(0) {}
};

class CommandBase {
public:
    virtual ~CommandBase() = default;
    virtual void execute() = 0;
    virtual void undo() = 0;
    virtual std::string description() const = 0;
};

class AddClipCommand : public CommandBase {
    std::shared_ptr<Track> track;
    std::shared_ptr<Clip> clip;
public:
    AddClipCommand(std::shared_ptr<Track> t, std::shared_ptr<Clip> c) : track(t), clip(c) {}
    void execute() override { track->addClip(clip); }
    void undo() override { track->removeClip(clip->id); }
    std::string description() const override { return "Add Clip"; }
};

class RemoveClipCommand : public CommandBase {
    std::shared_ptr<Track> track;
    std::shared_ptr<Clip> clip;
    size_t index;
public:
    RemoveClipCommand(std::shared_ptr<Track> t, std::shared_ptr<Clip> c, size_t i) : track(t), clip(c), index(i) {}
    void execute() override { track->removeClip(clip->id); }
    void undo() override { track->addClip(clip); }
    std::string description() const override { return "Remove Clip"; }
};

class MoveClipCommand : public CommandBase {
    std::shared_ptr<Clip> clip;
    double oldPos, newPos;
public:
    MoveClipCommand(std::shared_ptr<Clip> c, double oldP, double newP) : clip(c), oldPos(oldP), newPos(newP) {}
    void execute() override { clip->trackPosition = newPos; }
    void undo() override { clip->trackPosition = oldPos; }
    std::string description() const override { return "Move Clip"; }
};

class AddEffectCommand : public CommandBase {
    std::shared_ptr<Clip> clip;
    std::shared_ptr<VideoEffect> effect;
public:
    AddEffectCommand(std::shared_ptr<Clip> c, std::shared_ptr<VideoEffect> e) : clip(c), effect(e) {}
    void execute() override { clip->addEffect(effect); }
    void undo() override { clip->removeEffect(effect->name); }
    std::string description() const override { return "Add Effect: " + effect->name; }
};

class RemoveEffectCommand : public CommandBase {
    std::shared_ptr<Clip> clip;
    std::shared_ptr<VideoEffect> effect;
    size_t index;
public:
    RemoveEffectCommand(std::shared_ptr<Clip> c, std::shared_ptr<VideoEffect> e, size_t i) : clip(c), effect(e), index(i) {}
    void execute() override { clip->removeEffect(effect->name); }
    void undo() override { clip->effects.insert(clip->effects.begin() + index, effect); }
    std::string description() const override { return "Remove Effect: " + effect->name; }
};

class SplitClipCommand : public CommandBase {
    std::shared_ptr<Track> track;
    std::shared_ptr<Clip> original;
    std::shared_ptr<Clip> splitA;
    std::shared_ptr<Clip> splitB;
    double splitTime;
public:
    SplitClipCommand(std::shared_ptr<Track> t, std::shared_ptr<Clip> c, double st)
        : track(t), original(c), splitTime(st) {}
    void execute() override {
        double relativeTime = splitTime - original->trackPosition;
        splitA = std::make_shared<Clip>(*original);
        splitB = std::make_shared<Clip>(*original);
        splitA->endTime = original->startTime + relativeTime * original->speed;
        splitB->startTime = original->startTime + relativeTime * original->speed;
        splitB->trackPosition = splitTime;
        track->removeClip(original->id);
        track->addClip(splitA);
        track->addClip(splitB);
    }
    void undo() override {
        if (splitA) track->removeClip(splitA->id);
        if (splitB) track->removeClip(splitB->id);
        track->addClip(original);
    }
    std::string description() const override { return "Split Clip"; }
};

class EditHistory {
    std::stack<std::unique_ptr<CommandBase>> undoStack;
    std::stack<std::unique_ptr<CommandBase>> redoStack;
    static const size_t MAX_HISTORY = 100;
public:
    void execute(std::unique_ptr<CommandBase> cmd) {
        cmd->execute();
        undoStack.push(std::move(cmd));
        while (!redoStack.empty()) redoStack.pop();
        while (undoStack.size() > MAX_HISTORY) {
            std::stack<std::unique_ptr<CommandBase>> tmp;
            while (undoStack.size() > 1) { tmp.push(std::move(const_cast<std::unique_ptr<CommandBase>&>(undoStack.top()))); undoStack.pop(); }
            undoStack.pop();
            while (!tmp.empty()) { undoStack.push(std::move(const_cast<std::unique_ptr<CommandBase>&>(tmp.top()))); tmp.pop(); }
        }
    }
    bool undo() {
        if (undoStack.empty()) return false;
        undoStack.top()->undo();
        redoStack.push(std::move(const_cast<std::unique_ptr<CommandBase>&>(undoStack.top())));
        undoStack.pop();
        return true;
    }
    bool redo() {
        if (redoStack.empty()) return false;
        redoStack.top()->execute();
        undoStack.push(std::move(const_cast<std::unique_ptr<CommandBase>&>(redoStack.top())));
        redoStack.pop();
        return true;
    }
    bool canUndo() const { return !undoStack.empty(); }
    bool canRedo() const { return !redoStack.empty(); }
    std::string undoDescription() const { return undoStack.empty() ? "" : undoStack.top()->description(); }
    std::string redoDescription() const { return redoStack.empty() ? "" : redoStack.top()->description(); }
};

class Clipboard {
public:
    std::vector<std::shared_ptr<Clip>> clips;
    bool isCut;
    Clipboard() : isCut(false) {}
    void copy(const std::vector<std::shared_ptr<Clip>>& cs) {
        clips.clear();
        for (auto& c : cs) clips.push_back(std::make_shared<Clip>(*c));
        isCut = false;
    }
    void cut(const std::vector<std::shared_ptr<Clip>>& cs) {
        copy(cs);
        isCut = true;
    }
    std::vector<std::shared_ptr<Clip>> paste() {
        std::vector<std::shared_ptr<Clip>> result;
        for (auto& c : clips) {
            auto newClip = std::make_shared<Clip>(*c);
            static int counter = 0;
            newClip->id = "clip_paste_" + std::to_string(counter++);
            result.push_back(newClip);
        }
        return result;
    }
    bool hasContent() const { return !clips.empty(); }
};

struct OFXPlugin {
    std::string name;
    std::string identifier;
    std::string path;
    void* handle;
    OFXPlugin() : handle(nullptr) {}
};

void loadOpenFXForWin(std::vector<OFXPlugin>& plugins) {
#ifdef _WIN32
    std::vector<std::string> searchPaths = {
        "C:\\Program Files\\Common Files\\OFX\\Plugins",
        "C:\\Program Files (x86)\\Common Files\\OFX\\Plugins"
    };
    for (const auto& searchPath : searchPaths) {
        if (!fs::exists(searchPath)) continue;
        for (const auto& entry : fs::recursive_directory_iterator(searchPath)) {
            if (entry.path().extension() == ".ofx") {
                OFXPlugin plugin;
                plugin.path = entry.path().string();
                plugin.name = entry.path().stem().string();
                plugin.handle = LoadLibraryA(plugin.path.c_str());
                if (plugin.handle) {
                    plugins.push_back(plugin);
                }
            }
        }
    }
#endif
}

void loadOpenFXForMacOS(std::vector<OFXPlugin>& plugins) {
#ifdef __APPLE__
    std::vector<std::string> searchPaths = {
        "/Library/OFX/Plugins",
        std::string(getenv("HOME") ? getenv("HOME") : "") + "/Library/OFX/Plugins"
    };
    for (const auto& searchPath : searchPaths) {
        if (!fs::exists(searchPath)) continue;
        for (const auto& entry : fs::recursive_directory_iterator(searchPath)) {
            if (entry.path().extension() == ".ofx") {
                OFXPlugin plugin;
                plugin.path = entry.path().string();
                plugin.name = entry.path().stem().string();
                plugin.handle = dlopen(plugin.path.c_str(), RTLD_LAZY);
                if (plugin.handle) {
                    plugins.push_back(plugin);
                }
            }
        }
    }
#endif
}

void loadOpenFXForLinux(std::vector<OFXPlugin>& plugins) {
#if defined(__linux__)
    std::vector<std::string> searchPaths = {
        "/usr/OFX/Plugins",
        "/usr/local/OFX/Plugins",
        std::string(getenv("HOME") ? getenv("HOME") : "") + "/.local/OFX/Plugins"
    };
    for (const auto& searchPath : searchPaths) {
        if (!fs::exists(searchPath)) continue;
        for (const auto& entry : fs::recursive_directory_iterator(searchPath)) {
            if (entry.path().extension() == ".ofx") {
                OFXPlugin plugin;
                plugin.path = entry.path().string();
                plugin.name = entry.path().stem().string();
                plugin.handle = dlopen(plugin.path.c_str(), RTLD_LAZY);
                if (plugin.handle) {
                    plugins.push_back(plugin);
                }
            }
        }
    }
#endif
}

class MediaPool {
    std::vector<std::shared_ptr<MediaItem>> items;
public:
    std::shared_ptr<MediaItem> uploadMedia(const std::string& filePath) {
        if (!fs::exists(filePath)) {
            throw std::runtime_error("File not found: " + filePath);
        }
        auto item = std::make_shared<MediaItem>(filePath);
        item->width = 1920;
        item->height = 1080;
        item->fps = 24.0;
        item->duration = 10.0;
        Frame f(item->width, item->height);
        for (int y = 0; y < f.height; y++)
            for (int x = 0; x < f.width; x++)
                f.at(x, y) = {(float)x / f.width, (float)y / f.height, 0.5f, 1.0f};
        item->frames.push_back(f);
        items.push_back(item);
        return item;
    }
    bool removeItem(const std::string& name) {
        auto it = std::find_if(items.begin(), items.end(),
            [&](const std::shared_ptr<MediaItem>& m) { return m->name == name; });
        if (it != items.end()) { items.erase(it); return true; }
        return false;
    }
    const std::vector<std::shared_ptr<MediaItem>>& getItems() const { return items; }
};

class Renderer {
public:
    Frame composite(const std::vector<std::shared_ptr<Layer>>& layers, double time, int width, int height) {
        Frame result(width, height);
        std::vector<std::shared_ptr<Layer>> sortedLayers = layers;
        std::sort(sortedLayers.begin(), sortedLayers.end(),
            [](const std::shared_ptr<Layer>& a, const std::shared_ptr<Layer>& b) { return a->zOrder < b->zOrder; });
        for (auto& layer : sortedLayers) {
            if (!layer->visible) continue;
            for (auto& track : layer->tracks) {
                if (track->muted) continue;
                auto clip = track->getClipAt(time);
                if (!clip) continue;
                double clipLocalTime = time - clip->trackPosition;
                Frame clipFrame = clip->getFrameAt(clipLocalTime);
                for (int y = 0; y < height; y++) {
                    for (int x = 0; x < width; x++) {
                        Pixel& dst = result.at(x, y);
                        Pixel src = clipFrame.sampleBilinear((float)x * clipFrame.width / width, (float)y * clipFrame.height / height);
                        float a = src.a * layer->opacity * track->opacity;
                        if (layer->blendMode == "normal") {
                            dst.r = dst.r * (1 - a) + src.r * a;
                            dst.g = dst.g * (1 - a) + src.g * a;
                            dst.b = dst.b * (1 - a) + src.b * a;
                        } else if (layer->blendMode == "add") {
                            dst.r = std::clamp(dst.r + src.r * a, 0.0f, 1.0f);
                            dst.g = std::clamp(dst.g + src.g * a, 0.0f, 1.0f);
                            dst.b = std::clamp(dst.b + src.b * a, 0.0f, 1.0f);
                        } else if (layer->blendMode == "multiply") {
                            dst.r = dst.r * (1 - a) + dst.r * src.r * a;
                            dst.g = dst.g * (1 - a) + dst.g * src.g * a;
                            dst.b = dst.b * (1 - a) + dst.b * src.b * a;
                        } else if (layer->blendMode == "screen") {
                            dst.r = 1 - (1 - dst.r) * (1 - src.r * a);
                            dst.g = 1 - (1 - dst.g) * (1 - src.g * a);
                            dst.b = 1 - (1 - dst.b) * (1 - src.b * a);
                        }
                        dst.a = std::clamp(dst.a + a * (1 - dst.a), 0.0f, 1.0f);
                    }
                }
            }
        }
        return result;
    }
};

class ExportSettings {
public:
    std::string outputPath;
    std::string format;
    std::string codec;
    int width, height;
    double fps;
    int bitrate;
    double startTime, endTime;
    ExportSettings() : format("mp4"), codec("h264"), width(1920), height(1080), fps(24.0), bitrate(8000000), startTime(0), endTime(60) {}
};

class ProjectSerializer {
public:
    static void saveProject(const TimelineState& state, const std::string& path) {
        std::ofstream file(path);
        if (!file.is_open()) throw std::runtime_error("Cannot open file for writing: " + path);
        file << "NLVE_PROJECT_V1\n";
        file << "name=" << state.projectName << "\n";
        file << "width=" << state.width << "\n";
        file << "height=" << state.height << "\n";
        file << "fps=" << state.fps << "\n";
        file << "duration=" << state.duration << "\n";
        file << "playhead=" << state.playhead << "\n";
        file << "media_pool_count=" << state.mediaPool.size() << "\n";
        for (auto& media : state.mediaPool) {
            file << "media_path=" << media->path << "\n";
            file << "media_name=" << media->name << "\n";
            file << "media_type=" << media->type << "\n";
            file << "media_duration=" << media->duration << "\n";
        }
        file << "layer_count=" << state.layers.size() << "\n";
        for (auto& layer : state.layers) {
            file << "layer_id=" << layer->id << "\n";
            file << "layer_name=" << layer->name << "\n";
            file << "layer_visible=" << layer->visible << "\n";
            file << "layer_locked=" << layer->locked << "\n";
            file << "layer_opacity=" << layer->opacity << "\n";
            file << "layer_zorder=" << layer->zOrder << "\n";
            file << "layer_blend=" << layer->blendMode << "\n";
            file << "track_count=" << layer->tracks.size() << "\n";
            for (auto& track : layer->tracks) {
                file << "track_id=" << track->id << "\n";
                file << "track_name=" << track->name << "\n";
                file << "track_type=" << track->type << "\n";
                file << "track_muted=" << track->muted << "\n";
                file << "track_locked=" << track->locked << "\n";
                file << "track_volume=" << track->volume << "\n";
                file << "track_opacity=" << track->opacity << "\n";
                file << "clip_count=" << track->clips.size() << "\n";
                for (auto& clip : track->clips) {
                    file << "clip_id=" << clip->id << "\n";
                    file << "clip_media=" << (clip->media ? clip->media->name : "") << "\n";
                    file << "clip_start=" << clip->startTime << "\n";
                    file << "clip_end=" << clip->endTime << "\n";
                    file << "clip_pos=" << clip->trackPosition << "\n";
                    file << "clip_speed=" << clip->speed << "\n";
                    file << "clip_muted=" << clip->muted << "\n";
                    file << "clip_locked=" << clip->locked << "\n";
                    file << "effect_count=" << clip->effects.size() << "\n";
                    for (auto& effect : clip->effects) {
                        file << "effect_name=" << effect->name << "\n";
                        file << "effect_enabled=" << effect->enabled << "\n";
                        file << "param_count=" << effect->params.size() << "\n";
                        for (auto& [key, param] : effect->params) {
                            file << "param_key=" << key << "\n";
                            file << "param_val=" << param.asFloat() << "\n";
                        }
                    }
                }
            }
        }
        file.close();
    }

    static TimelineState loadProject(const std::string& path, MediaPool& pool) {
        std::ifstream file(path);
        if (!file.is_open()) throw std::runtime_error("Cannot open file for reading: " + path);
        TimelineState state;
        state.projectPath = path;
        std::string line;
        std::getline(file, line);
        if (line != "NLVE_PROJECT_V1") throw std::runtime_error("Invalid project file format");
        auto readVal = [&](const std::string& key) -> std::string {
            std::getline(file, line);
            auto pos = line.find('=');
            if (pos == std::string::npos) return "";
            return line.substr(pos + 1);
        };
        state.projectName = readVal("name");
        state.width = std::stoi(readVal("width"));
        state.height = std::stoi(readVal("height"));
        state.fps = std::stod(readVal("fps"));
        state.duration = std::stod(readVal("duration"));
        state.playhead = std::stod(readVal("playhead"));
        int mediaCount = std::stoi(readVal("media_pool_count"));
        for (int i = 0; i < mediaCount; i++) {
            std::string mpath = readVal("media_path");
            std::string mname = readVal("media_name");
            std::string mtype = readVal("media_type");
            double mdur = std::stod(readVal("media_duration"));
            try {
                auto item = pool.uploadMedia(mpath);
                item->duration = mdur;
                state.mediaPool.push_back(item);
            } catch (...) {
                auto item = std::make_shared<MediaItem>();
                item->path = mpath; item->name = mname; item->type = mtype; item->duration = mdur;
                state.mediaPool.push_back(item);
            }
        }
        int layerCount = std::stoi(readVal("layer_count"));
        for (int li = 0; li < layerCount; li++) {
            auto layer = std::make_shared<Layer>();
            layer->id = readVal("layer_id");
            layer->name = readVal("layer_name");
            layer->visible = readVal("layer_visible") == "1";
            layer->locked = readVal("layer_locked") == "1";
            layer->opacity = std::stof(readVal("layer_opacity"));
            layer->zOrder = std::stoi(readVal("layer_zorder"));
            layer->blendMode = readVal("layer_blend");
            int trackCount = std::stoi(readVal("track_count"));
            for (int ti = 0; ti < trackCount; ti++) {
                auto track = std::make_shared<Track>();
                track->id = readVal("track_id");
                track->name = readVal("track_name");
                track->type = readVal("track_type");
                track->muted = readVal("track_muted") == "1";
                track->locked = readVal("track_locked") == "1";
                track->volume = std::stof(readVal("track_volume"));
                track->opacity = std::stof(readVal("track_opacity"));
                int clipCount = std::stoi(readVal("clip_count"));
                for (int ci = 0; ci < clipCount; ci++) {
                    auto clip = std::make_shared<Clip>();
                    clip->id = readVal("clip_id");
                    std::string mediaName = readVal("clip_media");
                    clip->startTime = std::stod(readVal("clip_start"));
                    clip->endTime = std::stod(readVal("clip_end"));
                    clip->trackPosition = std::stod(readVal("clip_pos"));
                    clip->speed = std::stod(readVal("clip_speed"));
                    clip->muted = readVal("clip_muted") == "1";
                    clip->locked = readVal("clip_locked") == "1";
                    for (auto& m : state.mediaPool) {
                        if (m->name == mediaName) { clip->media = m; break; }
                    }
                    int effectCount = std::stoi(readVal("effect_count"));
                    for (int ei = 0; ei < effectCount; ei++) {
                        readVal("effect_name");
                        readVal("effect_enabled");
                        int paramCount = std::stoi(readVal("param_count"));
                        for (int pi = 0; pi < paramCount; pi++) {
                            readVal("param_key");
                            readVal("param_val");
                        }
                    }
                    track->clips.push_back(clip);
                }
                layer->tracks.push_back(track);
            }
            state.layers.push_back(layer);
        }
        return state;
    }
};

class ExportManager {
public:
    static void exportMedia(const TimelineState& state, const ExportSettings& settings, Renderer& renderer) {
        if (settings.outputPath.empty()) throw std::runtime_error("Output path is empty");
        fs::create_directories(fs::path(settings.outputPath).parent_path());
        std::string rawPath = settings.outputPath + ".raw";
        std::ofstream rawFile(rawPath, std::ios::binary);
        if (!rawFile.is_open()) throw std::runtime_error("Cannot create output file: " + rawPath);
        int totalFrames = (int)((settings.endTime - settings.startTime) * settings.fps);
        int frameW = settings.width, frameH = settings.height;
        rawFile.write(reinterpret_cast<const char*>(&frameW), sizeof(int));
        rawFile.write(reinterpret_cast<const char*>(&frameH), sizeof(int));
        rawFile.write(reinterpret_cast<const char*>(&totalFrames), sizeof(int));
        double fpsD = settings.fps;
        rawFile.write(reinterpret_cast<const char*>(&fpsD), sizeof(double));
        for (int f = 0; f < totalFrames; f++) {
            double t = settings.startTime + (double)f / settings.fps;
            Frame composited = renderer.composite(state.layers, t, frameW, frameH);
            for (int y = 0; y < frameH; y++) {
                for (int x = 0; x < frameW; x++) {
                    const Pixel& p = composited.at(x, y);
                    uint8_t r = (uint8_t)(std::clamp(p.r, 0.0f, 1.0f) * 255);
                    uint8_t g = (uint8_t)(std::clamp(p.g, 0.0f, 1.0f) * 255);
                    uint8_t b = (uint8_t)(std::clamp(p.b, 0.0f, 1.0f) * 255);
                    uint8_t a = (uint8_t)(std::clamp(p.a, 0.0f, 1.0f) * 255);
                    rawFile.write(reinterpret_cast<const char*>(&r), 1);
                    rawFile.write(reinterpret_cast<const char*>(&g), 1);
                    rawFile.write(reinterpret_cast<const char*>(&b), 1);
                    rawFile.write(reinterpret_cast<const char*>(&a), 1);
                }
            }
        }
        rawFile.close();
    }
};

class EffectFactory {
public:
    static std::shared_ptr<VideoEffect> create(const std::string& name) {
        if (name == "Add Noise") return std::make_shared<AddNoiseEffect>();
        if (name == "Grayscale") return std::make_shared<GrayscaleEffect>();
        if (name == "Color Curves") return std::make_shared<ColorCurvesEffect>();
        if (name == "Checkerboard") return std::make_shared<CheckerboardEffect>();
        if (name == "4-Point Color Gradient") return std::make_shared<FourPointColorGradientEffect>();
        if (name == "HSL Adjust") return std::make_shared<HSLAdjustEffect>();
        if (name == "Invert") return std::make_shared<InvertEffect>();
        if (name == "Swirl") return std::make_shared<SwirlEffect>();
        if (name == "Twirl") return std::make_shared<TwirlEffect>();
        if (name == "Zigzag") return std::make_shared<ZigzagEffect>();
        if (name == "Shear") return std::make_shared<ShearEffect>();
        if (name == "Displace") return std::make_shared<DisplaceEffect>();
        if (name == "3 Color Gradient") return std::make_shared<ThreeColorGradientEffect>();
        if (name == "Kaleido") return std::make_shared<KaleidoEffect>();
        if (name == "Gradient Map") return std::make_shared<GradientMapEffect>();
        if (name == "Tint") return std::make_shared<TintEffect>();
        if (name == "Duochrome") return std::make_shared<DuochromeEffect>();
        if (name == "Fourier Transform") return std::make_shared<FourierTransformEffect>();
        if (name == "Channel Mixer") return std::make_shared<ChannelMixerEffect>();
        if (name == "Ripple") return std::make_shared<RippleEffect>();
        if (name == "Angular Warp") return std::make_shared<AngularWarpEffect>();
        if (name == "Logarithm Warp") return std::make_shared<LogarithmWarpEffect>();
        if (name == "Sin Wave") return std::make_shared<SinWaveEffect>();
        if (name == "Julia Distort") return std::make_shared<JuliaDistortEffect>();
        if (name == "Mandelbrot Generator") return std::make_shared<MandelbrotGeneratorEffect>();
        if (name == "Julia Generator") return std::make_shared<JuliaGeneratorEffect>();
        if (name == "Warhol") return std::make_shared<WarholEffect>();
        if (name == "Chromatic Aberration") return std::make_shared<ChromaticAberrationEffect>();
        if (name == "Edge Detect") return std::make_shared<EdgeDetectEffect>();
        if (name == "Lens Correct") return std::make_shared<LensCorrectEffect>();
        if (name == "Vignette") return std::make_shared<VignetteEffect>();
        if (name == "Elseve Lens") return std::make_shared<ElseveLensEffect>();
        if (name == "Extensionist Lens Vignette") return std::make_shared<ExtensionistLensVignetteEffect>();
        if (name == "Anamorphic") return std::make_shared<AnamorphicEffect>();
        if (name == "Lens Flare") return std::make_shared<LensFlareEffect>();
        if (name == "Corner Pin") return std::make_shared<CornerPinEffect>();
        if (name == "LumiLayer") return std::make_shared<LumiLayerEffect>();
        if (name == "LumiShake") return std::make_shared<LumiShakeEffect>();
        if (name == "Lumi3DShape") return std::make_shared<Lumi3DShapeEffect>();
        if (name == "Mirror") return std::make_shared<MirrorEffect>();
        if (name == "Generic Equation") return std::make_shared<GenericEquationEffect>();
        if (name == "3D Ripple") return std::make_shared<ThreeDRippleEffect>();
        throw std::runtime_error("Unknown effect: " + name);
    }
    static std::vector<std::string> listEffects() {
        return {
            "Add Noise", "Grayscale", "Color Curves", "Checkerboard", "4-Point Color Gradient",
            "HSL Adjust", "Invert", "Swirl", "Twirl", "Zigzag", "Shear", "Displace",
            "3 Color Gradient", "Kaleido", "Gradient Map", "Tint", "Duochrome",
            "Fourier Transform", "Channel Mixer", "Ripple", "Angular Warp", "Logarithm Warp",
            "Sin Wave", "Julia Distort", "Mandelbrot Generator", "Julia Generator", "Warhol",
            "Chromatic Aberration", "Edge Detect", "Lens Correct", "Vignette", "Elseve Lens",
            "Extensionist Lens Vignette", "Anamorphic", "Lens Flare", "Corner Pin",
            "LumiLayer", "LumiShake", "Lumi3DShape", "Mirror", "Generic Equation", "3D Ripple"
        };
    }
};

class NLVE {
    TimelineState state;
    MediaPool mediaPool;
    EditHistory history;
    Clipboard clipboard;
    Renderer renderer;
    std::vector<OFXPlugin> ofxPlugins;
    std::vector<std::shared_ptr<Clip>> selectedClips;
    bool modified;

    std::shared_ptr<Track> findTrack(const std::string& trackId) {
        for (auto& layer : state.layers)
            for (auto& track : layer->tracks)
                if (track->id == trackId) return track;
        return nullptr;
    }
    std::shared_ptr<Clip> findClip(const std::string& clipId) {
        for (auto& layer : state.layers)
            for (auto& track : layer->tracks)
                for (auto& clip : track->clips)
                    if (clip->id == clipId) return clip;
        return nullptr;
    }
    std::shared_ptr<Track> findTrackForClip(const std::string& clipId) {
        for (auto& layer : state.layers)
            for (auto& track : layer->tracks)
                for (auto& clip : track->clips)
                    if (clip->id == clipId) return track;
        return nullptr;
    }

public:
    NLVE() : modified(false) {
        loadOFXPlugins();
        newProject();
    }

    void loadOFXPlugins() {
#ifdef _WIN32
        loadOpenFXForWin(ofxPlugins);
#elif defined(__APPLE__)
        loadOpenFXForMacOS(ofxPlugins);
#else
        loadOpenFXForLinux(ofxPlugins);
#endif
    }

    void newProject() {
        state = TimelineState();
        state.projectName = "Untitled Project";
        auto defaultLayer = std::make_shared<Layer>();
        defaultLayer->name = "Layer 1";
        defaultLayer->zOrder = 0;
        auto defaultTrack = std::make_shared<Track>();
        defaultTrack->name = "Video 1";
        defaultTrack->type = "video";
        defaultLayer->addTrack(defaultTrack);
        state.layers.push_back(defaultLayer);
        while (!history.canUndo()) {}
        selectedClips.clear();
        modified = false;
    }

    void saveProject(const std::string& path) {
        std::string savePath = path.empty() ? state.projectPath : path;
        if (savePath.empty()) savePath = state.projectName + ".nlve";
        state.projectPath = savePath;
        state.mediaPool.clear();
        for (auto& item : mediaPool.getItems()) state.mediaPool.push_back(item);
        ProjectSerializer::saveProject(state, savePath);
        modified = false;
    }

    void openProject(const std::string& path) {
        state = ProjectSerializer::loadProject(path, mediaPool);
        selectedClips.clear();
        modified = false;
    }

    std::shared_ptr<MediaItem> uploadMedia(const std::string& filePath) {
        auto item = mediaPool.uploadMedia(filePath);
        state.mediaPool.push_back(item);
        modified = true;
        return item;
    }

    void exportMedia(const ExportSettings& settings) {
        ExportManager::exportMedia(state, settings, renderer);
    }

    bool undoAction() {
        bool result = history.undo();
        if (result) modified = true;
        return result;
    }
    bool redoAction() {
        bool result = history.redo();
        if (result) modified = true;
        return result;
    }

    void copyClips(const std::vector<std::string>& clipIds) {
        std::vector<std::shared_ptr<Clip>> clips;
        for (auto& id : clipIds) {
            auto c = findClip(id);
            if (c) clips.push_back(c);
        }
        clipboard.copy(clips);
    }

    void cutClips(const std::vector<std::string>& clipIds, const std::string& trackId) {
        std::vector<std::shared_ptr<Clip>> clips;
        for (auto& id : clipIds) {
            auto c = findClip(id);
            if (c) clips.push_back(c);
        }
        clipboard.cut(clips);
        auto track = findTrack(trackId);
        if (track) {
            for (auto& clip : clips) {
                auto it = std::find_if(track->clips.begin(), track->clips.end(),
                    [&](const std::shared_ptr<Clip>& c) { return c->id == clip->id; });
                if (it != track->clips.end()) {
                    size_t idx = it - track->clips.begin();
                    history.execute(std::make_unique<RemoveClipCommand>(track, clip, idx));
                }
            }
        }
        modified = true;
    }

    std::vector<std::string> pasteClips(const std::string& trackId, double atTime) {
        if (!clipboard.hasContent()) return {};
        auto track = findTrack(trackId);
        if (!track) return {};
        auto newClips = clipboard.paste();
        std::vector<std::string> ids;
        double offset = atTime;
        for (auto& clip : newClips) {
            clip->trackPosition = offset;
            offset += clip->duration();
            history.execute(std::make_unique<AddClipCommand>(track, clip));
            ids.push_back(clip->id);
        }
        modified = true;
        return ids;
    }

    std::string addClipToTrack(const std::string& trackId, std::shared_ptr<MediaItem> media, double startTime, double endTime, double position) {
        auto track = findTrack(trackId);
        if (!track) throw std::runtime_error("Track not found: " + trackId);
        auto clip = std::make_shared<Clip>();
        clip->media = media;
        clip->startTime = startTime;
        clip->endTime = endTime;
        clip->trackPosition = position;
        history.execute(std::make_unique<AddClipCommand>(track, clip));
        modified = true;
        return clip->id;
    }

    bool removeClipFromTrack(const std::string& trackId, const std::string& clipId) {
        auto track = findTrack(trackId);
        if (!track) return false;
        auto clip = findClip(clipId);
        if (!clip) return false;
        auto it = std::find_if(track->clips.begin(), track->clips.end(),
            [&](const std::shared_ptr<Clip>& c) { return c->id == clipId; });
        if (it == track->clips.end()) return false;
        size_t idx = it - track->clips.begin();
        history.execute(std::make_unique<RemoveClipCommand>(track, clip, idx));
        modified = true;
        return true;
    }

    bool splitClip(const std::string& clipId, double atTime) {
        auto track = findTrackForClip(clipId);
        auto clip = findClip(clipId);
        if (!track || !clip) return false;
        if (atTime <= clip->trackPosition || atTime >= clip->trackPosition + clip->duration()) return false;
        history.execute(std::make_unique<SplitClipCommand>(track, clip, atTime));
        modified = true;
        return true;
    }

    bool addVideoEffect(const std::string& clipId, const std::string& effectName) {
        auto clip = findClip(clipId);
        if (!clip) return false;
        auto effect = EffectFactory::create(effectName);
        history.execute(std::make_unique<AddEffectCommand>(clip, effect));
        modified = true;
        return true;
    }

    bool addVideoEffect(const std::string& clipId, const std::string& effectName, const std::map<std::string, float>& params) {
        auto clip = findClip(clipId);
        if (!clip) return false;
        auto effect = EffectFactory::create(effectName);
        for (auto& [key, val] : params) effect->setParam(key, val);
        history.execute(std::make_unique<AddEffectCommand>(clip, effect));
        modified = true;
        return true;
    }

    bool removeVideoEffect(const std::string& clipId, const std::string& effectName) {
        auto clip = findClip(clipId);
        if (!clip) return false;
        auto it = std::find_if(clip->effects.begin(), clip->effects.end(),
            [&](const std::shared_ptr<VideoEffect>& e) { return e->name == effectName; });
        if (it == clip->effects.end()) return false;
        size_t idx = it - clip->effects.begin();
        history.execute(std::make_unique<RemoveEffectCommand>(clip, *it, idx));
        modified = true;
        return true;
    }

    bool setEffectParam(const std::string& clipId, const std::string& effectName, const std::string& paramName, float value) {
        auto clip = findClip(clipId);
        if (!clip) return false;
        for (auto& effect : clip->effects) {
            if (effect->name == effectName) {
                effect->setParam(paramName, value);
                modified = true;
                return true;
            }
        }
        return false;
    }

    std::string addTrack(const std::string& layerId, const std::string& name, const std::string& type) {
        for (auto& layer : state.layers) {
            if (layer->id == layerId) {
                auto track = std::make_shared<Track>();
                track->name = name;
                track->type = type;
                layer->addTrack(track);
                modified = true;
                return track->id;
            }
        }
        throw std::runtime_error("Layer not found: " + layerId);
    }

    bool removeTrack(const std::string& layerId, const std::string& trackId) {
        for (auto& layer : state.layers) {
            if (layer->id == layerId) {
                bool result = layer->removeTrack(trackId);
                if (result) modified = true;
                return result;
            }
        }
        return false;
    }

    std::string addLayer(const std::string& name, int zOrder) {
        auto layer = std::make_shared<Layer>();
        layer->name = name;
        layer->zOrder = zOrder;
        state.layers.push_back(layer);
        std::sort(state.layers.begin(), state.layers.end(),
            [](const std::shared_ptr<Layer>& a, const std::shared_ptr<Layer>& b) { return a->zOrder < b->zOrder; });
        modified = true;
        return layer->id;
    }

    bool removeLayer(const std::string& layerId) {
        auto it = std::find_if(state.layers.begin(), state.layers.end(),
            [&](const std::shared_ptr<Layer>& l) { return l->id == layerId; });
        if (it == state.layers.end()) return false;
        state.layers.erase(it);
        modified = true;
        return true;
    }

    Frame renderFrame(double time) {
        return renderer.composite(state.layers, time, state.width, state.height);
    }

    const TimelineState& getState() const { return state; }
    bool isModified() const { return modified; }
    const std::vector<OFXPlugin>& getOFXPlugins() const { return ofxPlugins; }
    const std::vector<std::string> listAvailableEffects() { return EffectFactory::listEffects(); }
};

int main() {
    NLVE editor;

    editor.newProject();

    auto& state = editor.getState();

    std::string trackId;
    if (!state.layers.empty() && !state.layers[0]->tracks.empty())
        trackId = state.layers[0]->tracks[0]->id;

    std::string mediaPath = "test_video.mp4";
    std::shared_ptr<MediaItem> media;
    try {
        media = editor.uploadMedia(mediaPath);
    } catch (...) {
        media = std::make_shared<MediaItem>("dummy.mp4");
        media->width = 1920; media->height = 1080; media->fps = 24; media->duration = 10;
        Frame f(1920, 1080);
        for (int y = 0; y < f.height; y++)
            for (int x = 0; x < f.width; x++)
                f.at(x, y) = {(float)x / f.width, (float)y / f.height, 0.3f, 1.0f};
        media->frames.push_back(f);
    }

    std::string clipId = editor.addClipToTrack(trackId, media, 0.0, 10.0, 0.0);

    editor.addVideoEffect(clipId, "Vignette", {{"strength", 0.6f}, {"radius", 0.7f}});
    editor.addVideoEffect(clipId, "Color Curves", {{"red_gain", 1.1f}, {"gamma", 0.95f}});
    editor.addVideoEffect(clipId, "Lens Flare", {{"flare_x", 0.2f}, {"flare_y", 0.2f}, {"intensity", 0.5f}});
    editor.addVideoEffect(clipId, "Chromatic Aberration", {{"r_offset_x", 2.0f}, {"b_offset_x", -2.0f}});

    editor.undoAction();
    editor.redoAction();

    editor.splitClip(clipId, 5.0);

    std::string layerId2 = editor.addLayer("Layer 2", 1);
    std::string trackId2 = editor.addTrack(layerId2, "Video 2", "video");

    editor.saveProject("my_project.nlve");

    editor.newProject();
    try {
        editor.openProject("my_project.nlve");
    } catch (...) {}

    ExportSettings exportSettings;
    exportSettings.outputPath = "output/render";
    exportSettings.format = "mp4";
    exportSettings.codec = "h264";
    exportSettings.width = 1920;
    exportSettings.height = 1080;
    exportSettings.fps = 24.0;
    exportSettings.startTime = 0.0;
    exportSettings.endTime = 1.0;
    try {
        editor.exportMedia(exportSettings);
    } catch (...) {}

    Frame rendered = editor.renderFrame(0.5);

    return 0;
}
