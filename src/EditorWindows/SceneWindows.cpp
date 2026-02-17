#include "Engine.h"
#include "ModelLoader.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <cstdlib>
#include <cfloat>
#include <cmath>
#include <cctype>
#include <functional>
#include <sstream>
#include <fstream>
#include <regex>
#include <unordered_set>
#include <unordered_map>
#include <optional>
#include <future>
#include <chrono>
#include <future>

#ifdef _WIN32
#include <shlobj.h>
#endif

#pragma region Hierarchy Helpers
namespace {
    std::optional<std::string> InferManagedTypeFromSource(const std::string& source,
                                                          const std::string& fallbackClass) {
        std::string nameSpace;
        std::string className;
        try {
            std::smatch match;
            std::regex namespacePattern(R"(namespace\s+([A-Za-z_][A-Za-z0-9_\.]*)\s*(\{|;))");
            if (std::regex_search(source, match, namespacePattern) && match.size() > 1) {
                nameSpace = match[1].str();
            }

            if (!fallbackClass.empty()) {
                auto escapeRegex = [](const std::string& value) {
                    std::string escaped;
                    escaped.reserve(value.size() * 2);
                    for (char c : value) {
                        if (c == '\\' || c == '.' || c == '+' || c == '*' || c == '?' || c == '^' || c == '$' ||
                            c == '(' || c == ')' || c == '[' || c == ']' || c == '{' || c == '}' || c == '|') {
                            escaped.push_back('\\');
                        }
                        escaped.push_back(c);
                    }
                    return escaped;
                };
                std::regex classMatch("\\bclass\\s+" + escapeRegex(fallbackClass) + "\\b");
                if (std::regex_search(source, classMatch)) {
                    className = fallbackClass;
                }
            }

            if (className.empty()) {
                std::regex classPattern(R"(\bclass\s+([A-Za-z_][A-Za-z0-9_]*))");
                if (std::regex_search(source, match, classPattern) && match.size() > 1) {
                    className = match[1].str();
                }
            }
        } catch (...) {
            return std::nullopt;
        }

        if (className.empty()) return std::nullopt;
        if (!nameSpace.empty()) return nameSpace + "." + className;
        return className;
    }

    std::optional<std::string> InferManagedTypeFromFile(const fs::path& path) {
        std::ifstream file(path);
        if (!file.is_open()) return std::nullopt;
        std::ostringstream ss;
        ss << file.rdbuf();
        std::string source = ss.str();
        std::string fallback = path.stem().string();
        return InferManagedTypeFromSource(source, fallback);
    }

    ImU32 GetHierarchyTypeColor(const SceneObject& obj) {
        if (!obj.scripts.empty()) return IM_COL32(255, 175, 90, 235);
        if (obj.hasCamera) return IM_COL32(110, 175, 235, 220);
        if (obj.hasLight) return IM_COL32(255, 200, 90, 220);
        if (obj.hasPostFX) return IM_COL32(200, 140, 230, 220);
        if (obj.hasUI) return IM_COL32(160, 210, 255, 220);
        if (obj.hasRenderer) {
            switch (obj.renderType) {
                case RenderType::OBJMesh:
                case RenderType::Model:
                case RenderType::Sprite:
                    return IM_COL32(120, 200, 150, 220);
                case RenderType::Mirror:
                    return IM_COL32(180, 200, 210, 220);
                case RenderType::Plane:
                    return IM_COL32(170, 180, 190, 220);
                case RenderType::Torus:
                    return IM_COL32(155, 215, 180, 220);
                case RenderType::Cube:
                case RenderType::Sphere:
                case RenderType::Capsule:
                case RenderType::None:
                default:
                    break;
            }
        }
        return IM_COL32(130, 150, 170, 220);
    }

    void UpdateLegacyTypeFromComponents(SceneObject& target) {
        if (target.hasRenderer) {
            switch (target.renderType) {
                case RenderType::Cube: target.type = ObjectType::Cube; break;
                case RenderType::Sphere: target.type = ObjectType::Sphere; break;
                case RenderType::Capsule: target.type = ObjectType::Capsule; break;
                case RenderType::OBJMesh: target.type = ObjectType::OBJMesh; break;
                case RenderType::Model: target.type = ObjectType::Model; break;
                case RenderType::Mirror: target.type = ObjectType::Mirror; break;
                case RenderType::Plane: target.type = ObjectType::Plane; break;
                case RenderType::Torus: target.type = ObjectType::Torus; break;
                case RenderType::Sprite: target.type = ObjectType::Sprite; break;
                case RenderType::None: break;
            }
            return;
        }
        if (target.hasUI) {
            switch (target.ui.type) {
                case UIElementType::Canvas: target.type = ObjectType::Canvas; break;
                case UIElementType::Image: target.type = ObjectType::UIImage; break;
                case UIElementType::Slider: target.type = ObjectType::UISlider; break;
                case UIElementType::Button: target.type = ObjectType::UIButton; break;
                case UIElementType::Text: target.type = ObjectType::UIText; break;
                case UIElementType::Sprite2D: target.type = ObjectType::Sprite2D; break;
                case UIElementType::None: break;
            }
            return;
        }
        if (target.hasLight) {
            switch (target.light.type) {
                case LightType::Directional: target.type = ObjectType::DirectionalLight; break;
                case LightType::Point: target.type = ObjectType::PointLight; break;
                case LightType::Spot: target.type = ObjectType::SpotLight; break;
                case LightType::Area: target.type = ObjectType::AreaLight; break;
            }
            return;
        }
        if (target.hasCamera) {
            target.type = ObjectType::Camera;
            return;
        }
        if (target.hasPostFX) {
            target.type = ObjectType::PostFXNode;
            return;
        }
        target.type = ObjectType::Empty;
    }

    struct SvgPathSpec {
        const char* d;
        bool stroke;
    };

    struct SvgIconSpec {
        float viewW;
        float viewH;
        const SvgPathSpec* paths;
        int pathCount;
    };

    struct SvgSubpath {
        std::vector<ImVec2> points;
        bool closed = false;
        bool stroke = false;
    };

    struct SvgIconCache {
        bool built = false;
        std::vector<SvgSubpath> subpaths;
    };

    static bool IsCommandChar(char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
    }

    static void SkipSvgSeparators(const char*& s) {
        while (*s) {
            if (*s == ' ' || *s == '\n' || *s == '\t' || *s == '\r' || *s == ',') {
                ++s;
                continue;
            }
            break;
        }
    }

    static bool ParseSvgNumber(const char*& s, float& out) {
        SkipSvgSeparators(s);
        if (!*s) return false;
        char* end = nullptr;
        out = strtof(s, &end);
        if (end == s) return false;
        s = end;
        return true;
    }

    static ImVec2 SvgLerp(const ImVec2& a, const ImVec2& b, float t) {
        return ImVec2(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t);
    }

    static ImVec2 SvgCubic(const ImVec2& p0, const ImVec2& p1, const ImVec2& p2, const ImVec2& p3, float t) {
        ImVec2 a = SvgLerp(p0, p1, t);
        ImVec2 b = SvgLerp(p1, p2, t);
        ImVec2 c = SvgLerp(p2, p3, t);
        ImVec2 d = SvgLerp(a, b, t);
        ImVec2 e = SvgLerp(b, c, t);
        return SvgLerp(d, e, t);
    }

    static void AppendSvgCubic(std::vector<ImVec2>& pts, const ImVec2& p0, const ImVec2& p1,
                               const ImVec2& p2, const ImVec2& p3, int segments) {
        for (int i = 1; i <= segments; ++i) {
            float t = static_cast<float>(i) / static_cast<float>(segments);
            pts.push_back(SvgCubic(p0, p1, p2, p3, t));
        }
    }

    static float SvgVectorAngle(const ImVec2& u, const ImVec2& v) {
        float dot = u.x * v.x + u.y * v.y;
        float det = u.x * v.y - u.y * v.x;
        return std::atan2(det, dot);
    }

    static void AppendSvgArc(std::vector<ImVec2>& pts, const ImVec2& start, const ImVec2& end,
                             float rx, float ry, float xAxisRotationDeg, bool largeArc, bool sweep) {
        if (rx == 0.0f || ry == 0.0f) {
            pts.push_back(end);
            return;
        }

        float phi = xAxisRotationDeg * (IM_PI / 180.0f);
        float cosPhi = std::cos(phi);
        float sinPhi = std::sin(phi);

        float dx2 = (start.x - end.x) * 0.5f;
        float dy2 = (start.y - end.y) * 0.5f;
        float x1p = cosPhi * dx2 + sinPhi * dy2;
        float y1p = -sinPhi * dx2 + cosPhi * dy2;

        rx = std::fabs(rx);
        ry = std::fabs(ry);

        float lambda = (x1p * x1p) / (rx * rx) + (y1p * y1p) / (ry * ry);
        if (lambda > 1.0f) {
            float s = std::sqrt(lambda);
            rx *= s;
            ry *= s;
        }

        float rx2 = rx * rx;
        float ry2 = ry * ry;
        float x1p2 = x1p * x1p;
        float y1p2 = y1p * y1p;

        float denom = (rx2 * y1p2 + ry2 * x1p2);
        float num = rx2 * ry2 - rx2 * y1p2 - ry2 * x1p2;
        float coef = 0.0f;
        if (denom > 0.0f) {
            float sign = (largeArc == sweep) ? -1.0f : 1.0f;
            coef = sign * std::sqrt(std::max(0.0f, num / denom));
        }

        float cxp = coef * (rx * y1p / ry);
        float cyp = coef * (-ry * x1p / rx);

        float cx = cosPhi * cxp - sinPhi * cyp + (start.x + end.x) * 0.5f;
        float cy = sinPhi * cxp + cosPhi * cyp + (start.y + end.y) * 0.5f;

        ImVec2 v1((x1p - cxp) / rx, (y1p - cyp) / ry);
        ImVec2 v2((-x1p - cxp) / rx, (-y1p - cyp) / ry);

        float startAngle = std::atan2(v1.y, v1.x);
        float deltaAngle = SvgVectorAngle(v1, v2);
        if (!sweep && deltaAngle > 0.0f) deltaAngle -= 2.0f * IM_PI;
        if (sweep && deltaAngle < 0.0f) deltaAngle += 2.0f * IM_PI;

        float absDelta = std::fabs(deltaAngle);
        int segments = std::max(4, static_cast<int>(std::ceil(absDelta / (IM_PI / 8.0f))));
        for (int i = 1; i <= segments; ++i) {
            float t = static_cast<float>(i) / static_cast<float>(segments);
            float angle = startAngle + deltaAngle * t;
            float cosA = std::cos(angle);
            float sinA = std::sin(angle);
            float x = cx + cosPhi * rx * cosA - sinPhi * ry * sinA;
            float y = cy + sinPhi * rx * cosA + cosPhi * ry * sinA;
            pts.push_back(ImVec2(x, y));
        }
    }

    static void FinalizeSvgSubpath(std::vector<SvgSubpath>& out, std::vector<ImVec2>& current, bool closed, bool stroke) {
        if (current.size() < 2) {
            current.clear();
            return;
        }
        if (closed && current.size() > 2) {
            if (current.front().x != current.back().x || current.front().y != current.back().y) {
                current.push_back(current.front());
            }
        }
        SvgSubpath sub;
        sub.points = std::move(current);
        sub.closed = closed;
        sub.stroke = stroke;
        out.push_back(std::move(sub));
        current.clear();
    }

    static void ParseSvgPathData(const char* d, std::vector<SvgSubpath>& out, bool stroke) {
        const char* s = d;
        char cmd = 0;
        ImVec2 cur(0, 0);
        ImVec2 start(0, 0);
        ImVec2 lastCtrl(0, 0);
        bool hasCtrl = false;
        std::vector<ImVec2> current;

        while (*s) {
            SkipSvgSeparators(s);
            if (!*s) break;
            if (IsCommandChar(*s)) {
                cmd = *s++;
            } else if (!cmd) {
                break;
            }

            bool relative = (cmd >= 'a' && cmd <= 'z');
            char upper = static_cast<char>(std::toupper(static_cast<unsigned char>(cmd)));

            if (upper == 'M') {
                float x, y;
                if (!ParseSvgNumber(s, x) || !ParseSvgNumber(s, y)) break;
                if (relative) {
                    cur.x += x;
                    cur.y += y;
                } else {
                    cur = ImVec2(x, y);
                }
                FinalizeSvgSubpath(out, current, false, stroke);
                current.push_back(cur);
                start = cur;
                hasCtrl = false;

                while (ParseSvgNumber(s, x) && ParseSvgNumber(s, y)) {
                    ImVec2 p = relative ? ImVec2(cur.x + x, cur.y + y) : ImVec2(x, y);
                    cur = p;
                    current.push_back(cur);
                }
                continue;
            }

            if (upper == 'Z') {
                FinalizeSvgSubpath(out, current, true, stroke);
                cur = start;
                hasCtrl = false;
                continue;
            }

            if (upper == 'L') {
                float x, y;
                while (ParseSvgNumber(s, x) && ParseSvgNumber(s, y)) {
                    cur = relative ? ImVec2(cur.x + x, cur.y + y) : ImVec2(x, y);
                    current.push_back(cur);
                }
                hasCtrl = false;
                continue;
            }

            if (upper == 'H') {
                float x;
                while (ParseSvgNumber(s, x)) {
                    cur.x = relative ? cur.x + x : x;
                    current.push_back(cur);
                }
                hasCtrl = false;
                continue;
            }

            if (upper == 'V') {
                float y;
                while (ParseSvgNumber(s, y)) {
                    cur.y = relative ? cur.y + y : y;
                    current.push_back(cur);
                }
                hasCtrl = false;
                continue;
            }

            if (upper == 'C') {
                float x1, y1, x2, y2, x, y;
                while (ParseSvgNumber(s, x1) && ParseSvgNumber(s, y1) &&
                       ParseSvgNumber(s, x2) && ParseSvgNumber(s, y2) &&
                       ParseSvgNumber(s, x) && ParseSvgNumber(s, y)) {
                    ImVec2 p1 = relative ? ImVec2(cur.x + x1, cur.y + y1) : ImVec2(x1, y1);
                    ImVec2 p2 = relative ? ImVec2(cur.x + x2, cur.y + y2) : ImVec2(x2, y2);
                    ImVec2 p3 = relative ? ImVec2(cur.x + x, cur.y + y) : ImVec2(x, y);
                    AppendSvgCubic(current, cur, p1, p2, p3, 12);
                    cur = p3;
                    lastCtrl = p2;
                    hasCtrl = true;
                }
                continue;
            }

            if (upper == 'S') {
                float x2, y2, x, y;
                while (ParseSvgNumber(s, x2) && ParseSvgNumber(s, y2) &&
                       ParseSvgNumber(s, x) && ParseSvgNumber(s, y)) {
                    ImVec2 p1 = hasCtrl ? ImVec2(cur.x * 2.0f - lastCtrl.x, cur.y * 2.0f - lastCtrl.y) : cur;
                    ImVec2 p2 = relative ? ImVec2(cur.x + x2, cur.y + y2) : ImVec2(x2, y2);
                    ImVec2 p3 = relative ? ImVec2(cur.x + x, cur.y + y) : ImVec2(x, y);
                    AppendSvgCubic(current, cur, p1, p2, p3, 12);
                    cur = p3;
                    lastCtrl = p2;
                    hasCtrl = true;
                }
                continue;
            }

            if (upper == 'A') {
                float rx, ry, xRot, largeFlag, sweepFlag, x, y;
                while (ParseSvgNumber(s, rx) && ParseSvgNumber(s, ry) &&
                       ParseSvgNumber(s, xRot) && ParseSvgNumber(s, largeFlag) &&
                       ParseSvgNumber(s, sweepFlag) && ParseSvgNumber(s, x) &&
                       ParseSvgNumber(s, y)) {
                    ImVec2 end = relative ? ImVec2(cur.x + x, cur.y + y) : ImVec2(x, y);
                    AppendSvgArc(current, cur, end, rx, ry, xRot, largeFlag != 0.0f, sweepFlag != 0.0f);
                    cur = end;
                    hasCtrl = false;
                }
                continue;
            }

            hasCtrl = false;
        }

        FinalizeSvgSubpath(out, current, false, stroke);
    }

    static void BuildSvgIconCache(const SvgIconSpec& spec, SvgIconCache& cache) {
        if (cache.built) return;
        for (int i = 0; i < spec.pathCount; ++i) {
            ParseSvgPathData(spec.paths[i].d, cache.subpaths, spec.paths[i].stroke);
        }
        cache.built = true;
    }

    static ImVec2 SvgTransformPoint(const ImVec2& p, const ImVec2& min, const ImVec2& max,
                                    float viewW, float viewH, float scaleFactor) {
        float size = std::min(max.x - min.x, max.y - min.y) * scaleFactor;
        ImVec2 center = ImVec2((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
        float scale = size / std::max(viewW, viewH);
        ImVec2 offset = ImVec2(center.x - (viewW * scale) * 0.5f, center.y - (viewH * scale) * 0.5f);
        return ImVec2(offset.x + p.x * scale, offset.y + p.y * scale);
    }

    static void DrawSvgIcon(ImDrawList* drawList, const SvgIconSpec& spec, SvgIconCache& cache,
                            const ImVec2& min, const ImVec2& max, ImU32 color, float strokeScale, float scaleFactor) {
        BuildSvgIconCache(spec, cache);
        for (const SvgSubpath& sub : cache.subpaths) {
            if (sub.points.size() < 2) continue;
            drawList->PathClear();
            for (const ImVec2& p : sub.points) {
                drawList->PathLineTo(SvgTransformPoint(p, min, max, spec.viewW, spec.viewH, scaleFactor));
            }
            ImDrawFlags flags = sub.closed ? ImDrawFlags_Closed : 0;
            drawList->PathStroke(color, flags, strokeScale);
        }
    }

    static const SvgPathSpec kEmptyObjectSvgPaths[] = {
        { "M 10.005859 0.5 A 0.50083746 0.50083746 0 0 0 9.7539062 0.56445312 L 1.7539062 5.0644531 A 0.50083746 0.50083746 0 0 0 1.5 5.5 L 1.5 14.5 A 0.50083746 0.50083746 0 0 0 1.7539062 14.935547 L 9.7539062 19.435547 A 0.50083746 0.50083746 0 0 0 10.246094 19.435547 L 18.246094 14.935547 A 0.50083746 0.50083746 0 0 0 18.5 14.5 L 18.5 5.5 A 0.50083746 0.50083746 0 0 0 18.246094 5.0644531 L 10.246094 0.56445312 A 0.50083746 0.50083746 0 0 0 10.005859 0.5 z M 10 1.5742188 L 16.978516 5.5 L 10 9.4257812 L 3.0214844 5.5 L 10 1.5742188 z M 2.5 6.3554688 L 9.5 10.292969 L 9.5 18.144531 L 2.5 14.207031 L 2.5 6.3554688 z M 17.5 6.3554688 L 17.5 14.207031 L 10.5 18.144531 L 10.5 10.292969 L 17.5 6.3554688 z", true }
    };

    static const SvgIconSpec kEmptyObjectSvg = { 20.0f, 20.0f, kEmptyObjectSvgPaths, 1 };
    static SvgIconCache gEmptyObjectSvgCache;

    void DrawEmptyObjectIcon(ImDrawList* drawList, ImVec2 pos, float size, ImU32 color) {
        ImVec2 min(pos.x, pos.y);
        ImVec2 max(pos.x + size, pos.y + size);
        float stroke = std::max(1.0f, size * 0.055f);
        DrawSvgIcon(drawList, kEmptyObjectSvg, gEmptyObjectSvgCache, min, max, color, stroke, 0.9f);
    }

    void DrawHierarchyLines(ImDrawList* drawList, const ImVec2& itemMin, const ImVec2& itemMax,
                            const std::vector<bool>& ancestorHasNext, int depth, bool isLast) {
        if (depth <= 0) {
            return;
        }
        ImGuiStyle& style = ImGui::GetStyle();
        ImVec4 base = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
        ImU32 lineColor = ImGui::ColorConvertFloat4ToU32(ImVec4(base.x, base.y, base.z, 0.6f));
        float indent = style.IndentSpacing;
        float rowTop = itemMin.y;
        float rowBottom = itemMax.y;
        float rowMid = (rowTop + rowBottom) * 0.5f;
        float baseX = itemMin.x - indent * depth;

        for (int i = 0; i < depth && i < static_cast<int>(ancestorHasNext.size()); ++i) {
            if (ancestorHasNext[i]) {
                float x = baseX + indent * (i + 0.5f);
                drawList->AddLine(ImVec2(x, rowTop), ImVec2(x, rowBottom), lineColor, 1.0f);
            }
        }

        float connectorX = baseX + indent * (depth - 0.5f);
        float vertEnd = isLast ? rowMid : rowBottom;
        drawList->AddLine(ImVec2(connectorX, rowTop), ImVec2(connectorX, vertEnd), lineColor, 1.0f);
        drawList->AddLine(ImVec2(connectorX, rowMid), ImVec2(itemMin.x + 6.0f, rowMid), lineColor, 1.0f);
    }

    void ApplyReverbPreset(ReverbZoneComponent& zone, ReverbPreset preset) {
        zone.preset = preset;
        switch (preset) {
            case ReverbPreset::Room:
                zone.room = -1000.0f;
                zone.roomHF = -500.0f;
                zone.roomLF = 0.0f;
                zone.decayTime = 1.2f;
                zone.decayHFRatio = 0.8f;
                zone.reflections = -2600.0f;
                zone.reflectionsDelay = 0.01f;
                zone.reverb = 100.0f;
                zone.reverbDelay = 0.012f;
                zone.hfReference = 5000.0f;
                zone.lfReference = 250.0f;
                zone.roomRolloffFactor = 0.0f;
                zone.diffusion = 85.0f;
                zone.density = 90.0f;
                break;
            case ReverbPreset::LivingRoom:
                zone.room = -1200.0f;
                zone.roomHF = -800.0f;
                zone.roomLF = 0.0f;
                zone.decayTime = 1.5f;
                zone.decayHFRatio = 0.7f;
                zone.reflections = -2400.0f;
                zone.reflectionsDelay = 0.02f;
                zone.reverb = 150.0f;
                zone.reverbDelay = 0.015f;
                zone.hfReference = 5000.0f;
                zone.lfReference = 250.0f;
                zone.roomRolloffFactor = 0.0f;
                zone.diffusion = 90.0f;
                zone.density = 95.0f;
                break;
            case ReverbPreset::Hall:
                zone.room = -1000.0f;
                zone.roomHF = -200.0f;
                zone.roomLF = 0.0f;
                zone.decayTime = 3.2f;
                zone.decayHFRatio = 0.7f;
                zone.reflections = -1500.0f;
                zone.reflectionsDelay = 0.03f;
                zone.reverb = 500.0f;
                zone.reverbDelay = 0.02f;
                zone.hfReference = 5000.0f;
                zone.lfReference = 250.0f;
                zone.roomRolloffFactor = 0.0f;
                zone.diffusion = 95.0f;
                zone.density = 100.0f;
                break;
            case ReverbPreset::Forest:
                zone.room = -1500.0f;
                zone.roomHF = -1800.0f;
                zone.roomLF = 0.0f;
                zone.decayTime = 1.1f;
                zone.decayHFRatio = 0.3f;
                zone.reflections = -3000.0f;
                zone.reflectionsDelay = 0.02f;
                zone.reverb = -100.0f;
                zone.reverbDelay = 0.01f;
                zone.hfReference = 2500.0f;
                zone.lfReference = 150.0f;
                zone.roomRolloffFactor = 0.0f;
                zone.diffusion = 50.0f;
                zone.density = 60.0f;
                break;
            case ReverbPreset::Custom:
            default:
                break;
        }
    }
}
#pragma endregion

#pragma region Hierarchy Panel
void Engine::renderHierarchyPanel() {
    ImGui::Begin("Hierarchy", &showHierarchy);

    static char searchBuffer[128] = "";
    float animSpeed = 0.0f;
    if (uiAnimationMode == UIAnimationMode::Fluid) {
        animSpeed = 8.0f;
    } else if (uiAnimationMode == UIAnimationMode::Snappy) {
        animSpeed = 18.0f;
    }
    float animStep = (uiAnimationMode == UIAnimationMode::Off) ? 1.0f
        : (1.0f - std::exp(-animSpeed * ImGui::GetIO().DeltaTime));

    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4 headerBg = style.Colors[ImGuiCol_MenuBarBg];
    headerBg.x = std::min(headerBg.x + 0.02f, 1.0f);
    headerBg.y = std::min(headerBg.y + 0.02f, 1.0f);
    headerBg.z = std::min(headerBg.z + 0.02f, 1.0f);
    ImVec4 listBg = style.Colors[ImGuiCol_WindowBg];
    listBg.x = std::min(listBg.x + 0.01f, 1.0f);
    listBg.y = std::min(listBg.y + 0.01f, 1.0f);
    listBg.z = std::min(listBg.z + 0.01f, 1.0f);

    ImGui::PushStyleColor(ImGuiCol_ChildBg, headerBg);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 4.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 4.0f));
    ImGui::BeginChild("HierarchyHeader", ImVec2(0, 74), true, ImGuiWindowFlags_NoScrollbar);
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##Search", "Search...", searchBuffer, sizeof(searchBuffer));
    ImGui::Spacing();
    ImGui::Checkbox("Texture Preview", &hierarchyShowTexturePreview);
    ImGui::SameLine();
    ImGui::BeginDisabled(!hierarchyShowTexturePreview);
    ImGui::TextDisabled("Filter");
    ImGui::SameLine();
    const char* filterOptions[] = { "Bilinear", "Nearest" };
    int filterIndex = hierarchyPreviewNearest ? 1 : 0;
    ImGui::SetNextItemWidth(120.0f);
    ImGui::SetNextWindowBgAlpha(0.85f);
    if (ImGui::BeginCombo("##HierarchyTexFilter", filterOptions[filterIndex])) {
        for (int i = 0; i < IM_ARRAYSIZE(filterOptions); ++i) {
            bool selected = (i == filterIndex);
            if (ImGui::Selectable(filterOptions[i], selected)) {
                filterIndex = i;
                hierarchyPreviewNearest = (filterIndex == 1);
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();

    std::string filter = searchBuffer;
    std::transform(filter.begin(), filter.end(), filter.begin(), ::tolower);

    auto importDroppedModel = [&](const fs::path& path, int parentId) {
        std::error_code ec;
        fs::directory_entry entry(path, ec);
        if (ec) {
            return;
        }
        if (!fileBrowser.isModelFile(entry)) {
            return;
        }
        size_t beforeCount = sceneObjects.size();
        if (fileBrowser.isOBJFile(entry)) {
            importOBJToScene(path.string(), "");
        } else {
            importModelToScene(path.string(), "");
        }
        if (sceneObjects.size() > beforeCount && parentId >= 0) {
            int newId = sceneObjects.back().id;
            setParent(newId, parentId);
        }
    };

    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCENE_OBJECT")) {
            int draggedId = *(const int*)payload->Data;
            setParent(draggedId, -1);
        }
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("FILE_PATH")) {
            const char* path = static_cast<const char*>(payload->Data);
            importDroppedModel(fs::path(path), -1);
        }
        ImGui::EndDragDropTarget();
    }

    ImGui::PushStyleColor(ImGuiCol_ChildBg, listBg);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 2.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 2.0f));
    ImGui::BeginChild("HierarchyList", ImVec2(0, 0), true);

    std::vector<size_t> rootIndices;
    rootIndices.reserve(sceneObjects.size());
    std::unordered_set<int> knownIds;
    knownIds.reserve(sceneObjects.size());
    for (const auto& obj : sceneObjects) {
        knownIds.insert(obj.id);
    }
    for (size_t i = 0; i < sceneObjects.size(); i++) {
        int parentId = sceneObjects[i].parentId;
        if (parentId == -1 || knownIds.find(parentId) == knownIds.end()) {
            rootIndices.push_back(i);
        }
    }

    std::vector<bool> ancestorHasNext;
    for (size_t i = 0; i < rootIndices.size(); ++i) {
        bool isLastRoot = (i + 1 == rootIndices.size());
        renderObjectNode(sceneObjects[rootIndices[i]], filter, ancestorHasNext, isLastRoot, 0, animStep);
    }

    if (ImGui::BeginPopupContextWindow("HierarchyBackground",
            ImGuiPopupFlags_MouseButtonRight |
            ImGuiPopupFlags_NoOpenOverItems))
    {
        auto createUIWithCanvas = [&](ObjectType type, const std::string& baseName) {
            int canvasId = -1;
            for (const auto& obj : sceneObjects) {
                if (obj.hasUI && obj.ui.type == UIElementType::Canvas) {
                    canvasId = obj.id;
                    break;
                }
            }
            if (canvasId < 0) {
                addObject(ObjectType::Canvas, "Canvas");
                if (!sceneObjects.empty()) {
                    canvasId = sceneObjects.back().id;
                }
            }
            addObject(type, baseName);
            if (!sceneObjects.empty() && canvasId >= 0) {
                setParent(sceneObjects.back().id, canvasId);
            }
        };
        auto createReverbZoneObject = [&]() {
            addObject(ObjectType::Empty, "Reverb Zone");
            if (!sceneObjects.empty()) {
                sceneObjects.back().hasReverbZone = true;
                sceneObjects.back().reverbZone = ReverbZoneComponent{};
                sceneObjects.back().reverbZone.boxSize = glm::max(sceneObjects.back().scale, glm::vec3(1.0f));
            }
        };
        if (ImGui::BeginMenu("Create"))
        {
            if (ImGui::MenuItem("Empty")) addObject(ObjectType::Empty, "Empty");
            // ── Primitives ─────────────────────────────
            if (ImGui::BeginMenu("Primitives"))
            {
                if (ImGui::MenuItem("Cube"))    addObject(ObjectType::Cube, "Cube");
                if (ImGui::MenuItem("Sphere"))  addObject(ObjectType::Sphere, "Sphere");
                if (ImGui::MenuItem("Capsule")) addObject(ObjectType::Capsule, "Capsule");
                if (ImGui::MenuItem("Plane"))   addObject(ObjectType::Plane, "Plane");
                if (ImGui::MenuItem("Torus"))   addObject(ObjectType::Torus, "Torus");
                if (ImGui::MenuItem("Sprite (Quad)")) addObject(ObjectType::Sprite, "Sprite");
                if (ImGui::MenuItem("Mirror"))  addObject(ObjectType::Mirror, "Mirror");
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("RMesh"))
            {
                if (ImGui::BeginMenu("Primitives"))
                {
                    if (ImGui::MenuItem("Cube"))   createRMeshPrimitive("Cube");
                    if (ImGui::MenuItem("Sphere")) createRMeshPrimitive("Sphere");
                    if (ImGui::MenuItem("Plane"))  createRMeshPrimitive("Plane");
                    ImGui::EndMenu();
                }
                ImGui::EndMenu();
            }

            // ── Lights ────────────────────────────────
            if (ImGui::BeginMenu("Lights"))
            {
                if (ImGui::MenuItem("Directional Light")) addObject(ObjectType::DirectionalLight, "Directional Light");
                if (ImGui::MenuItem("Point Light"))       addObject(ObjectType::PointLight, "Point Light");
                if (ImGui::MenuItem("Spot Light"))        addObject(ObjectType::SpotLight, "Spot Light");
                if (ImGui::MenuItem("Area Light"))        addObject(ObjectType::AreaLight, "Area Light");
                ImGui::EndMenu();
            }

            // ── Other / Effects ───────────────────────
            if (ImGui::BeginMenu("Effects"))
            {
                if (ImGui::MenuItem("Post FX Node")) addObject(ObjectType::PostFXNode, "Post FX");
                if (ImGui::MenuItem("Audio Reverb Zone")) createReverbZoneObject();
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("2D/UI"))
            {
                if (ImGui::MenuItem("Canvas")) addObject(ObjectType::Canvas, "Canvas");
                if (ImGui::MenuItem("UI Image")) createUIWithCanvas(ObjectType::UIImage, "UI Image");
                if (ImGui::MenuItem("UI Slider")) createUIWithCanvas(ObjectType::UISlider, "UI Slider");
                if (ImGui::MenuItem("UI Button")) createUIWithCanvas(ObjectType::UIButton, "UI Button");
                if (ImGui::MenuItem("UI Text")) createUIWithCanvas(ObjectType::UIText, "UI Text");
                if (ImGui::MenuItem("Sprite2D")) createUIWithCanvas(ObjectType::Sprite2D, "Sprite2D");
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Camera")) addObject(ObjectType::Camera, "Camera");

            ImGui::EndMenu();
        }
        ImGui::EndPopup();
    }

    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();

    ImGui::End();
}

void Engine::renderObjectNode(SceneObject& obj, const std::string& filter,
                              std::vector<bool>& ancestorHasNext, bool isLast, int depth, float animStep) {
    std::string nameLower = obj.name;
    std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);

    if (!filter.empty() && nameLower.find(filter) == std::string::npos) {
        return;
    }

    bool hasChildren = !obj.childIds.empty();
    bool isSelected = std::find(selectedObjectIds.begin(), selectedObjectIds.end(), obj.id) != selectedObjectIds.end();

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (isSelected) flags |= ImGuiTreeNodeFlags_Selected;
    if (!hasChildren) flags |= ImGuiTreeNodeFlags_Leaf;

    ImGuiID nodeId = ImGui::GetID((void*)(intptr_t)obj.id);
    bool nodeOpen = ImGui::TreeNodeEx((void*)(intptr_t)obj.id, flags, "");

    ImVec2 itemMin = ImGui::GetItemRectMin();
    ImVec2 itemMax = ImGui::GetItemRectMax();
    UIAnimationState& animState = editorUiAnimationStates[nodeId];
    bool hovered = ImGui::IsItemHovered();
    bool active = ImGui::IsItemActive();
    float openTarget = nodeOpen ? 1.0f : 0.0f;
    if (uiAnimationMode == UIAnimationMode::Off) {
        animState.hover = hovered ? 1.0f : 0.0f;
        animState.active = active ? 1.0f : 0.0f;
        animState.sliderValue = openTarget;
    } else {
        float hoverTarget = hovered ? 1.0f : 0.0f;
        float activeTarget = active ? 1.0f : 0.0f;
        animState.hover += (hoverTarget - animState.hover) * animStep;
        animState.active += (activeTarget - animState.active) * animStep;
        animState.sliderValue += (openTarget - animState.sliderValue) * animStep;
    }
    float hoverT = std::clamp(animState.hover, 0.0f, 1.0f);
    float activeT = std::clamp(animState.active, 0.0f, 1.0f);
    float openT = std::clamp(animState.sliderValue, 0.0f, 1.0f);
    float glow = std::min(1.0f, hoverT * 0.7f + activeT * 1.0f);
    if (glow > 0.001f) {
        ImVec2 pad(4.0f + 10.0f * hoverT + 14.0f * activeT, 2.0f + 6.0f * hoverT);
        ImVec2 glowMin(itemMin.x - pad.x, itemMin.y - pad.y);
        ImVec2 glowMax(itemMax.x + pad.x, itemMax.y + pad.y);
        ImVec4 glowCol = ImGui::GetStyleColorVec4(ImGuiCol_HeaderHovered);
        glowCol.w *= 0.35f * glow;
        ImGui::GetWindowDrawList()->AddRectFilled(glowMin, glowMax, ImGui::GetColorU32(glowCol), 6.0f);
    }
    DrawHierarchyLines(ImGui::GetWindowDrawList(), itemMin, itemMax, ancestorHasNext, depth, isLast);

    float lineHeight = itemMax.y - itemMin.y;
    float iconScale = 1.0f + 0.08f * hoverT + 0.12f * activeT;
    float iconSize = std::max(8.0f, (lineHeight - 6.0f) * iconScale);
    float labelStart = itemMin.x + ImGui::GetTreeNodeToLabelSpacing();
    ImVec2 iconPos(labelStart, itemMin.y + (lineHeight - iconSize) * 0.5f);
    ImU32 iconColor = GetHierarchyTypeColor(obj);
    DrawEmptyObjectIcon(ImGui::GetWindowDrawList(), iconPos, iconSize, iconColor);
    ImVec4 textCol = ImGui::GetStyleColorVec4(ImGuiCol_Text);
    textCol.x = std::min(1.0f, textCol.x + 0.15f * hoverT + 0.2f * activeT);
    textCol.y = std::min(1.0f, textCol.y + 0.15f * hoverT + 0.2f * activeT);
    textCol.z = std::min(1.0f, textCol.z + 0.15f * hoverT + 0.2f * activeT);
    textCol.w = std::min(1.0f, textCol.w + 0.2f * hoverT + 0.35f * activeT);
    float fontSize = ImGui::GetFontSize() * (1.0f + 0.06f * hoverT + 0.1f * activeT);
    ImVec2 textPos(iconPos.x + iconSize + 6.0f, itemMin.y + (lineHeight - fontSize) * 0.5f);
    ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(), fontSize, textPos,
                                        ImGui::GetColorU32(textCol), obj.name.c_str());

    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
        bool additive = ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeyShift;
        setPrimarySelection(obj.id, additive);
    }

    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
        ImGui::SetDragDropPayload("SCENE_OBJECT", &obj.id, sizeof(int));
        ImGui::Text("Moving: %s", obj.name.c_str());
        ImGui::EndDragDropSource();
    }

    auto importDroppedModel = [&](const fs::path& path, int parentId) {
        std::error_code ec;
        fs::directory_entry entry(path, ec);
        if (ec || !fileBrowser.isModelFile(entry)) {
            return;
        }
        size_t beforeCount = sceneObjects.size();
        if (fileBrowser.isOBJFile(entry)) {
            importOBJToScene(path.string(), "");
        } else {
            importModelToScene(path.string(), "");
        }
        if (sceneObjects.size() > beforeCount && parentId >= 0) {
            int newId = sceneObjects.back().id;
            setParent(newId, parentId);
        }
    };

    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCENE_OBJECT")) {
            int draggedId = *(const int*)payload->Data;
            if (draggedId != obj.id) {
                setParent(draggedId, obj.id);
            }
        }
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("FILE_PATH")) {
            const char* path = static_cast<const char*>(payload->Data);
            std::error_code ec;
            fs::directory_entry entry(path, ec);
            if (!ec) {
                if (fileBrowser.isModelFile(entry)) {
                    importDroppedModel(fs::path(path), obj.id);
                } else if (fileBrowser.getFileCategory(entry) == FileCategory::Script) {
                    auto alreadyAssigned = std::any_of(obj.scripts.begin(), obj.scripts.end(),
                        [&](const ScriptComponent& sc) { return sc.path == path; });
                    if (!alreadyAssigned) {
                        ScriptComponent sc;
                        sc.path = path;
                        std::string ext = fs::path(path).extension().string();
                        std::transform(ext.begin(), ext.end(), ext.begin(),
                                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                        if (ext == ".cs") {
                            sc.language = ScriptLanguage::CSharp;
                            sc.managedType = fs::path(path).stem().string();
                        } else if (ext == ".c") {
                            sc.language = ScriptLanguage::C;
                        } else {
                            sc.language = ScriptLanguage::Cpp;
                        }
                        obj.scripts.push_back(sc);
                        projectManager.currentProject.hasUnsavedChanges = true;
                        addConsoleMessage("Assigned script to " + obj.name, ConsoleMessageType::Success);
                    }
                }
            }
        }
        ImGui::EndDragDropTarget();
    }

    if (ImGui::BeginPopupContextItem()) {
        if (ImGui::MenuItem("Duplicate")) {
            setPrimarySelection(obj.id);
            duplicateSelected();
        }
        if (ImGui::MenuItem("Delete")) {
            setPrimarySelection(obj.id);
            deleteSelected();
        }
        ImGui::Separator();
        if (obj.hasUI && obj.ui.type == UIElementType::Canvas && ImGui::BeginMenu("Create UI Child")) {
            auto createChild = [&](ObjectType type, const std::string& baseName) {
                addObject(type, baseName);
                if (!sceneObjects.empty()) {
                    setParent(sceneObjects.back().id, obj.id);
                }
            };
            if (ImGui::MenuItem("UI Image")) createChild(ObjectType::UIImage, "UI Image");
            if (ImGui::MenuItem("UI Slider")) createChild(ObjectType::UISlider, "UI Slider");
            if (ImGui::MenuItem("UI Button")) createChild(ObjectType::UIButton, "UI Button");
            if (ImGui::MenuItem("UI Text")) createChild(ObjectType::UIText, "UI Text");
            if (ImGui::MenuItem("Sprite2D")) createChild(ObjectType::Sprite2D, "Sprite2D");
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Clear Parent") && obj.parentId != -1) {
            setParent(obj.id, -1);
        }
        ImGui::EndPopup();
    }

    if (hierarchyShowTexturePreview) {
        const std::string* previewPath = nullptr;
        if (!obj.albedoTexturePath.empty()) {
            previewPath = &obj.albedoTexturePath;
        } else if (obj.useOverlay && !obj.overlayTexturePath.empty()) {
            previewPath = &obj.overlayTexturePath;
        }

        if (previewPath) {
            auto overrideIt = texturePreviewFilterOverrides.find(*previewPath);
            bool previewNearest = (overrideIt != texturePreviewFilterOverrides.end())
                ? overrideIt->second
                : hierarchyPreviewNearest;
            Texture* previewTex = renderer.getTexturePreview(*previewPath, previewNearest);
            if (previewTex && previewTex->GetID()) {
                ImGuiStyle& style = ImGui::GetStyle();
                ImVec2 itemMin = ImGui::GetItemRectMin();
                ImVec2 itemMax = ImGui::GetItemRectMax();
                float lineHeight = itemMax.y - itemMin.y;
                float previewSize = std::max(12.0f, lineHeight - 4.0f);
                float rightEdge = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
                float previewX = rightEdge - previewSize - style.WindowPadding.x;
                float previewY = itemMin.y + (lineHeight - previewSize) * 0.5f;
                ImVec2 pMin(previewX, previewY);
                ImVec2 pMax(previewX + previewSize, previewY + previewSize);
                ImGui::GetWindowDrawList()->AddImage(
                    (ImTextureID)(intptr_t)previewTex->GetID(),
                    pMin, pMax, ImVec2(0, 1), ImVec2(1, 0));
            }
        }
    }

    if (nodeOpen) {
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, openT);
        std::vector<SceneObject*> visibleChildren;
        visibleChildren.reserve(obj.childIds.size());
        for (int childId : obj.childIds) {
            auto it = std::find_if(sceneObjects.begin(), sceneObjects.end(),
                [childId](const SceneObject& o) { return o.id == childId; });
            if (it != sceneObjects.end()) {
                std::string childLower = it->name;
                std::transform(childLower.begin(), childLower.end(), childLower.begin(), ::tolower);
                if (filter.empty() || childLower.find(filter) != std::string::npos) {
                    visibleChildren.push_back(&(*it));
                }
            }
        }

        ancestorHasNext.push_back(!isLast);
        for (size_t i = 0; i < visibleChildren.size(); ++i) {
            bool childLast = (i + 1 == visibleChildren.size());
            renderObjectNode(*visibleChildren[i], filter, ancestorHasNext, childLast, depth + 1, animStep);
        }
        ancestorHasNext.pop_back();
        ImGui::PopStyleVar();
        ImGui::TreePop();
    }
}
#pragma endregion

#pragma region Inspector Panel
void Engine::renderInspectorPanel() {
    ImGui::Begin("Inspector", &showInspector);

    fs::path selectedMaterialPath;
    bool browserHasMaterial = false;
    fs::path selectedAudioPath;
    bool browserHasAudio = false;
    const AudioClipPreview* selectedAudioPreview = nullptr;
    fs::path selectedTexturePath;
    bool browserHasTexture = false;
    if (!fileBrowser.selectedFile.empty() && fs::exists(fileBrowser.selectedFile)) {
        fs::directory_entry entry(fileBrowser.selectedFile);
        FileCategory cat = fileBrowser.getFileCategory(entry);
        if (cat == FileCategory::Material) {
            selectedMaterialPath = entry.path();
            browserHasMaterial = true;
            if (inspectedMaterialPath != selectedMaterialPath.string()) {
                inspectedMaterialValid = loadMaterialData(
                    selectedMaterialPath.string(),
                    inspectedMaterial,
                    inspectedAlbedo,
                    inspectedOverlay,
                    inspectedNormal,
                    inspectedUseOverlay,
                    &inspectedVertShader,
                    &inspectedFragShader
                );
                inspectedMaterialPath = selectedMaterialPath.string();
            }
        } else {
            inspectedMaterialPath.clear();
            inspectedMaterialValid = false;
        }
        if (cat == FileCategory::Audio) {
            selectedAudioPath = entry.path();
            browserHasAudio = true;
            selectedAudioPreview = audio.getPreview(selectedAudioPath.string());
        }
        if (cat == FileCategory::Texture) {
            selectedTexturePath = entry.path();
            browserHasTexture = true;
        }
    } else {
        inspectedMaterialPath.clear();
        inspectedMaterialValid = false;
    }

    if (browserHasAudio) {
        std::string selectedAudio = selectedAudioPath.string();
        if (selectedAudio != audioPreviewSelectedPath) {
            audioPreviewSelectedPath = selectedAudio;
            if (audioPreviewAutoPlay) {
                audio.playPreview(selectedAudio, 1.0f, audioPreviewLoop);
            }
        }
    } else {
        audioPreviewSelectedPath.clear();
    }

    auto drawWaveform = [&](const char* id, const AudioClipPreview* preview, const ImVec2& size, float progressRatio, float* seekRatioOut) {
        bool hasStereo = preview && preview->channels >= 2
            && !preview->waveformLeft.empty()
            && !preview->waveformRight.empty();
        if (!preview || (!hasStereo && preview->waveform.empty())) {
            ImGui::Dummy(size);
            return;
        }
        ImVec2 start = ImGui::GetCursorScreenPos();
        ImVec2 end = ImVec2(start.x + size.x, start.y + size.y);
        ImGui::InvisibleButton(id, size);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(start, end, IM_COL32(30, 35, 45, 180), 4.0f);
        float midY = (start.y + end.y) * 0.5f;
        float usableHeight = size.y * 0.45f;
        size_t count = hasStereo
            ? std::min(preview->waveformLeft.size(), preview->waveformRight.size())
            : preview->waveform.size();
        float step = count > 1 ? size.x / static_cast<float>(count - 1) : size.x;
        if (hasStereo) {
            ImU32 leftColor = IM_COL32(255, 190, 90, 200);
            ImU32 rightColor = IM_COL32(100, 200, 255, 200);
            float topMidY = start.y + size.y * 0.25f;
            float bottomMidY = start.y + size.y * 0.75f;
            float stereoHeight = size.y * 0.22f;
            for (size_t i = 0; i < count; ++i) {
                float leftAmp = std::clamp(preview->waveformLeft[i], 0.0f, 1.0f);
                float rightAmp = std::clamp(preview->waveformRight[i], 0.0f, 1.0f);
                float x = start.x + step * static_cast<float>(i);
                float leftOff = leftAmp * stereoHeight;
                float rightOff = rightAmp * stereoHeight;
                dl->AddLine(ImVec2(x, topMidY - leftOff), ImVec2(x, topMidY + leftOff), leftColor, 1.2f);
                dl->AddLine(ImVec2(x, bottomMidY - rightOff), ImVec2(x, bottomMidY + rightOff), rightColor, 1.2f);
            }
        } else {
            ImU32 color = IM_COL32(255, 180, 100, 200);
            for (size_t i = 0; i < count; ++i) {
                float amp = std::clamp(preview->waveform[i], 0.0f, 1.0f);
                float x = start.x + step * static_cast<float>(i);
                float yOff = amp * usableHeight;
                dl->AddLine(ImVec2(x, midY - yOff), ImVec2(x, midY + yOff), color, 1.2f);
            }
        }

        if (progressRatio >= 0.0f && progressRatio <= 1.0f) {
            float px = start.x + progressRatio * size.x;
            dl->AddLine(ImVec2(px, start.y), ImVec2(px, end.y), IM_COL32(120, 210, 255, 230), 2.0f);
        }

        if (seekRatioOut && ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            float mouseX = ImGui::GetIO().MousePos.x;
            float ratio = (mouseX - start.x) / size.x;
            ratio = std::clamp(ratio, 0.0f, 1.0f);
            *seekRatioOut = ratio;
        }
    };

    auto drawMaterialPreview = [&](const char* idSuffix,
                                   const MaterialProperties& material,
                                   const std::string& albedoPath,
                                   const std::string& overlayPath,
                                   const std::string& normalPath,
                                   bool useOverlay,
                                   const std::string& vertShaderPath,
                                   const std::string& fragShaderPath,
                                   float previewScale,
                                   int previewSlot) {
        ImGui::PushID(idSuffix);

        float availableWidth = ImGui::GetContentRegionAvail().x;
        float previewWidth = std::clamp(220.0f * previewScale, 120.0f, std::max(120.0f, availableWidth));
        float previewHeight = std::clamp(previewWidth * 0.62f, 110.0f, 220.0f);
        int targetWidth = std::max(64, static_cast<int>(previewWidth));
        int targetHeight = std::max(64, static_cast<int>(previewHeight));

        static const std::string kPreviewWhiteTexture = "Resources/Textures/editor_preview_white.ppm";

        Camera previewCamera;
        previewCamera.position = glm::vec3(0.0f, 0.3f, 3.2f);
        previewCamera.front = glm::normalize(glm::vec3(0.0f, -0.05f, -1.0f));
        previewCamera.up = glm::vec3(0.0f, 1.0f, 0.0f);

        std::vector<SceneObject> previewScene;
        previewScene.reserve(5);

        SceneObject keyLight("MatPreviewKey", ObjectType::PointLight, -9201);
        keyLight.hasLight = true;
        keyLight.position = glm::vec3(1.8f, 1.8f, 2.2f);
        keyLight.light.type = LightType::Point;
        keyLight.light.color = glm::vec3(1.0f, 0.97f, 0.92f);
        keyLight.light.intensity = 2.9f;
        keyLight.light.range = 10.0f;
        previewScene.push_back(keyLight);

        SceneObject fillLight("MatPreviewFill", ObjectType::PointLight, -9202);
        fillLight.hasLight = true;
        fillLight.position = glm::vec3(-2.0f, 0.8f, 1.4f);
        fillLight.light.type = LightType::Point;
        fillLight.light.color = glm::vec3(0.75f, 0.82f, 1.0f);
        fillLight.light.intensity = 1.2f;
        fillLight.light.range = 9.0f;
        previewScene.push_back(fillLight);

        SceneObject rimLight("MatPreviewRim", ObjectType::DirectionalLight, -9203);
        rimLight.hasLight = true;
        rimLight.light.type = LightType::Directional;
        rimLight.light.color = glm::vec3(0.95f, 0.98f, 1.0f);
        rimLight.light.intensity = 0.65f;
        rimLight.rotation = glm::vec3(18.0f, 210.0f, 0.0f);
        previewScene.push_back(rimLight);

        SceneObject previewSphere("MatPreviewSphere", ObjectType::Sphere, -9204);
        previewSphere.hasRenderer = true;
        previewSphere.renderType = RenderType::Sphere;
        previewSphere.position = glm::vec3(0.0f, 0.24f, 0.0f);
        previewSphere.rotation = glm::vec3(0.0f, -18.0f, 0.0f);
        previewSphere.scale = glm::vec3(1.35f);
        previewSphere.material = material;
        previewSphere.albedoTexturePath = albedoPath.empty() ? kPreviewWhiteTexture : albedoPath;
        previewSphere.overlayTexturePath = overlayPath;
        previewSphere.normalMapPath = normalPath;
        previewSphere.useOverlay = useOverlay;
        previewSphere.vertexShaderPath = vertShaderPath;
        previewSphere.fragmentShaderPath = fragShaderPath;
        previewScene.push_back(previewSphere);

        SceneObject previewGround("MatPreviewGround", ObjectType::Plane, -9205);
        previewGround.hasRenderer = true;
        previewGround.renderType = RenderType::Plane;
        previewGround.position = glm::vec3(0.0f, -0.95f, -0.2f);
        previewGround.rotation = glm::vec3(-90.0f, 0.0f, 0.0f);
        previewGround.scale = glm::vec3(4.6f, 4.6f, 1.0f);
        previewGround.material.color = glm::vec3(0.19f, 0.2f, 0.22f);
        previewGround.material.ambientStrength = 0.28f;
        previewGround.material.specularStrength = 0.07f;
        previewGround.material.shininess = 22.0f;
        previewGround.material.textureMix = 0.0f;
        previewGround.albedoTexturePath = kPreviewWhiteTexture;
        previewScene.push_back(previewGround);

        unsigned int previewTexture = renderer.renderScenePreview(
            previewCamera,
            previewScene,
            targetWidth,
            targetHeight,
            35.0f,
            0.1f,
            30.0f,
            false,
            previewSlot
        );

        if (previewTexture != 0) {
            ImVec2 imageSize(previewWidth, previewHeight);
            float padX = availableWidth - previewWidth;
            if (padX > 1.0f) {
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + padX * 0.5f);
            }
            ImGui::Image((ImTextureID)(intptr_t)previewTexture, imageSize, ImVec2(0, 1), ImVec2(1, 0));
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), IM_COL32(90, 102, 122, 210), 4.0f, 0, 1.0f);
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.55f, 1.0f), "Material preview unavailable.");
            ImGui::Dummy(ImVec2(previewWidth, previewHeight));
        }

        ImGui::PopID();
    };

    static float assetMaterialPreviewScale = 1.0f;
    static float objectMaterialPreviewScale = 1.0f;

    struct ComponentHeaderState {
        bool open = false;
        bool enabledChanged = false;
    };

    auto drawComponentHeader = [&](const char* label, const char* id, bool* enabled, bool defaultOpen,
                                   const std::function<void()>& menuFn) -> ComponentHeaderState {
        ComponentHeaderState state;
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_Framed;
        if (defaultOpen) {
            flags |= ImGuiTreeNodeFlags_DefaultOpen;
        }
        std::string headerId = std::string(label) + "##" + id;
        ImGui::SetNextItemAllowOverlap();
        ImGuiStyle& style = ImGui::GetStyle();
        ImVec4 headerCol = style.Colors[ImGuiCol_FrameBg];
        ImVec4 headerHover = style.Colors[ImGuiCol_FrameBgHovered];
        ImVec4 headerActive = style.Colors[ImGuiCol_FrameBgActive];
        ImGui::PushStyleColor(ImGuiCol_Header, headerCol);
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, headerHover);
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, headerActive);
        state.open = ImGui::CollapsingHeader(headerId.c_str(), flags);
        ImGui::PopStyleColor(3);

        ImVec2 headerMin = ImGui::GetItemRectMin();
        ImVec2 headerMax = ImGui::GetItemRectMax();
        ImVec2 cursorAfter = ImGui::GetCursorScreenPos();
        float headerHeight = headerMax.y - headerMin.y;
        float controlSize = ImGui::GetFrameHeight();
        float right = headerMax.x - style.FramePadding.x;

        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImU32 borderCol = ImGui::GetColorU32(ImGuiCol_Border);
        dl->AddRect(headerMin, headerMax, borderCol, style.FrameRounding, 0, 1.0f);
        ImU32 accentCol = ImGui::GetColorU32(ImGuiCol_TextDisabled);
        float accentWidth = 3.0f;
        dl->AddRectFilled(ImVec2(headerMin.x, headerMin.y + 1.0f),
                          ImVec2(headerMin.x + accentWidth, headerMax.y - 1.0f),
                          accentCol, style.FrameRounding, ImDrawFlags_RoundCornersLeft);

        ImGui::PushID(id);
        if (menuFn) {
            ImVec2 menuPos(right - controlSize, headerMin.y + (headerHeight - controlSize) * 0.5f);
            ImGui::SetCursorScreenPos(menuPos);
            if (ImGui::SmallButton("...")) {
                ImGui::OpenPopup("ComponentMenu");
            }
            if (ImGui::BeginPopup("ComponentMenu")) {
                menuFn();
                ImGui::EndPopup();
            }
            right = menuPos.x - style.ItemSpacing.x;
        }
        if (enabled) {
            ImVec2 checkPos(right - controlSize, headerMin.y + (headerHeight - controlSize) * 0.5f);
            ImGui::SetCursorScreenPos(checkPos);
            if (ImGui::Checkbox("##Enabled", enabled)) {
                state.enabledChanged = true;
            }
        }
        ImGui::PopID();

        ImGui::SetCursorScreenPos(cursorAfter);
        return state;
    };

    auto renderMaterialAssetPanel = [&](const char* headerTitle, bool allowApply) {
        if (!browserHasMaterial) return;

        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.35f, 0.35f, 0.55f, 1.0f));
        if (ImGui::CollapsingHeader(headerTitle, ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Indent(8.0f);
            if (!inspectedMaterialValid) {
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Failed to read material file.");
            } else {
                auto textureField = [&](const char* label, const char* idSuffix, std::string& path) {
                    bool changed = false;
                    ImGui::PushID(idSuffix);
                    ImGui::TextUnformatted(label);
                    ImGui::SetNextItemWidth(-140);
                    char buf[512] = {};
                    std::snprintf(buf, sizeof(buf), "%s", path.c_str());
                    if (ImGui::InputText("##Path", buf, sizeof(buf))) {
                        path = buf;
                        changed = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Clear")) {
                        path.clear();
                        changed = true;
                    }
                    ImGui::SameLine();
                    bool canUseTex = !fileBrowser.selectedFile.empty() && fs::exists(fileBrowser.selectedFile) &&
                                     fileBrowser.isTextureFile(fs::directory_entry(fileBrowser.selectedFile));
                    ImGui::BeginDisabled(!canUseTex);
                    std::string btnLabel = std::string("Use Selection##") + idSuffix;
                    if (ImGui::SmallButton(btnLabel.c_str())) {
                        path = fileBrowser.selectedFile.string();
                        changed = true;
                    }
                    ImGui::EndDisabled();
                    ImGui::PopID();
                    return changed;
                };

                ImGui::TextDisabled("%s", selectedMaterialPath.filename().string().c_str());
                ImGui::TextColored(ImVec4(0.7f, 0.85f, 1.0f, 1.0f), "%s", selectedMaterialPath.string().c_str());
                ImGui::Spacing();

                bool matChanged = false;
                if (ImGui::ColorEdit3("Base Color", &inspectedMaterial.color.x)) {
                    matChanged = true;
                }
                float metallic = inspectedMaterial.specularStrength;
                if (ImGui::SliderFloat("Metallic", &metallic, 0.0f, 1.0f)) {
                    inspectedMaterial.specularStrength = metallic;
                    matChanged = true;
                }
                float smoothness = inspectedMaterial.shininess / 256.0f;
                if (ImGui::SliderFloat("Smoothness", &smoothness, 0.0f, 1.0f)) {
                    smoothness = std::clamp(smoothness, 0.0f, 1.0f);
                    inspectedMaterial.shininess = smoothness * 256.0f;
                    matChanged = true;
                }
                if (ImGui::SliderFloat("Ambient Light", &inspectedMaterial.ambientStrength, 0.0f, 1.0f)) {
                    matChanged = true;
                }
                if (ImGui::SliderFloat("Detail Mix", &inspectedMaterial.textureMix, 0.0f, 1.0f)) {
                    matChanged = true;
                }

                ImGui::Spacing();
                matChanged |= textureField("Base Map", "PreviewAlbedo", inspectedAlbedo);
                if (ImGui::Checkbox("Use Detail Map", &inspectedUseOverlay)) {
                    matChanged = true;
                }
                matChanged |= textureField("Detail Map", "PreviewOverlay", inspectedOverlay);
                matChanged |= textureField("Normal Map", "PreviewNormal", inspectedNormal);

                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.9f, 0.8f, 0.5f, 1.0f), "Shader");
                auto shaderField = [&](const char* label, const char* idSuffix, std::string& path) {
                    bool changed = false;
                    ImGui::PushID(idSuffix);
                    ImGui::TextUnformatted(label);
                    ImGui::SetNextItemWidth(-140);
                    char buf[512] = {};
                    std::snprintf(buf, sizeof(buf), "%s", path.c_str());
                    if (ImGui::InputText("##Path", buf, sizeof(buf))) {
                        path = buf;
                        changed = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Clear")) {
                        path.clear();
                        changed = true;
                    }
                    bool selectionIsShader = false;
                    if (!fileBrowser.selectedFile.empty() && fs::exists(fileBrowser.selectedFile)) {
                        selectionIsShader = fileBrowser.getFileCategory(fs::directory_entry(fileBrowser.selectedFile)) == FileCategory::Shader;
                    }
                    ImGui::SameLine();
                    ImGui::BeginDisabled(!selectionIsShader);
                    std::string btn = std::string("Use Selection##") + idSuffix;
                    if (ImGui::SmallButton(btn.c_str())) {
                        path = fileBrowser.selectedFile.string();
                        changed = true;
                    }
                    ImGui::EndDisabled();
                    ImGui::PopID();
                    return changed;
                };
                matChanged |= shaderField("Vertex Shader", "PreviewVert", inspectedVertShader);
                matChanged |= shaderField("Fragment Shader", "PreviewFrag", inspectedFragShader);

                ImGui::BeginDisabled(inspectedVertShader.empty() && inspectedFragShader.empty());
                if (ImGui::Button("Reload Shader")) {
                    renderer.forceReloadShader(inspectedVertShader, inspectedFragShader);
                }
                ImGui::EndDisabled();

                ImGui::Spacing();
                if (ImGui::Button("Reload")) {
                    inspectedMaterialValid = loadMaterialData(
                        selectedMaterialPath.string(),
                        inspectedMaterial,
                        inspectedAlbedo,
                        inspectedOverlay,
                        inspectedNormal,
                        inspectedUseOverlay,
                        &inspectedVertShader,
                        &inspectedFragShader
                    );
                }
                ImGui::SameLine();
                if (ImGui::Button("Save")) {
                    if (saveMaterialData(
                            selectedMaterialPath.string(),
                            inspectedMaterial,
                            inspectedAlbedo,
                            inspectedOverlay,
                            inspectedNormal,
                            inspectedUseOverlay,
                            inspectedVertShader,
                            inspectedFragShader))
                    {
                        addConsoleMessage("Saved material: " + selectedMaterialPath.string(), ConsoleMessageType::Success);
                    } else {
                        addConsoleMessage("Failed to save material: " + selectedMaterialPath.string(), ConsoleMessageType::Error);
                    }
                }

                if (allowApply) {
                    ImGui::SameLine();
                    SceneObject* target = getSelectedObject();
                    bool canApply = target != nullptr;
                    ImGui::BeginDisabled(!canApply);
                    if (ImGui::Button("Apply to Selection")) {
                        if (target) {
                            target->material = inspectedMaterial;
                            target->albedoTexturePath = inspectedAlbedo;
                            target->overlayTexturePath = inspectedOverlay;
                            target->normalMapPath = inspectedNormal;
                            target->useOverlay = inspectedUseOverlay;
                            target->materialPath = selectedMaterialPath.string();
                            target->vertexShaderPath = inspectedVertShader;
                            target->fragmentShaderPath = inspectedFragShader;
                            projectManager.currentProject.hasUnsavedChanges = true;
                            addConsoleMessage("Applied material to " + target->name, ConsoleMessageType::Success);
                        }
                    }
                    ImGui::EndDisabled();
                }

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::TextDisabled("Preview");
                ImGui::SetNextItemWidth(180.0f);
                ImGui::SliderFloat("Size##AssetMaterialPreview", &assetMaterialPreviewScale, 0.6f, 1.8f, "%.2fx");
                drawMaterialPreview(
                    "AssetMaterialPreview",
                    inspectedMaterial,
                    inspectedAlbedo,
                    inspectedOverlay,
                    inspectedNormal,
                    inspectedUseOverlay,
                    inspectedVertShader,
                    inspectedFragShader,
                    assetMaterialPreviewScale,
                    1001
                );

                if (matChanged) {
                    inspectedMaterialValid = true;
                }
            }
            ImGui::Unindent(8.0f);
        }
        ImGui::PopStyleColor();
    };

    auto renderAudioAssetPanel = [&](const char* headerTitle, SceneObject* target) {
        if (!browserHasAudio) return;

        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.5f, 0.4f, 0.25f, 1.0f));
        if (ImGui::CollapsingHeader(headerTitle, ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Indent(8.0f);
            ImGui::TextDisabled("%s", selectedAudioPath.filename().string().c_str());
            ImGui::TextColored(ImVec4(0.8f, 0.9f, 1.0f, 1.0f), "%s", selectedAudioPath.string().c_str());
            ImGui::Spacing();

            if (selectedAudioPreview) {
                double cur = 0.0;
                double dur = 0.0;
                float progress = -1.0f;
                if (audio.getPreviewTime(selectedAudioPath.string(), cur, dur) && dur > 0.0001) {
                    progress = static_cast<float>(cur / dur);
                }
                ImGui::Text("Format: %u ch @ %u Hz", selectedAudioPreview->channels, selectedAudioPreview->sampleRate);
                ImGui::Text("Length: %.2f s", selectedAudioPreview->durationSeconds);
                ImVec2 waveSize(ImGui::GetContentRegionAvail().x, 96.0f);
                float seekRatio = -1.0f;
                drawWaveform("##AudioWaveAsset", selectedAudioPreview, waveSize, progress, &seekRatio);
                if (seekRatio >= 0.0f && dur > 0.0) {
                    audio.seekPreview(selectedAudioPath.string(), seekRatio * dur);
                }
                if (dur > 0.0) {
                    ImGui::TextDisabled("Time: %0.2f / %0.2f", cur, dur);
                }
            } else {
                ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.55f, 1.0f), "Unable to decode audio preview.");
            }

            ImGui::Spacing();
            bool isPlayingPreview = audio.isPreviewing(selectedAudioPath.string());
            if (ImGui::Button(isPlayingPreview ? "Stop" : "Play", ImVec2(72, 0))) {
                if (isPlayingPreview) {
                    audio.stopPreview();
                } else {
                    audio.playPreview(selectedAudioPath.string(), 1.0f, audioPreviewLoop);
                }
            }
            ImGui::SameLine();
            if (ImGui::Checkbox("Loop##AudioPreview", &audioPreviewLoop)) {
                if (isPlayingPreview) {
                    audio.setPreviewLoop(audioPreviewLoop);
                }
            }
            ImGui::SameLine();
            if (ImGui::Checkbox("Auto Play##AudioPreview", &audioPreviewAutoPlay)) {
                if (audioPreviewAutoPlay && !selectedAudioPath.empty() && !isPlayingPreview) {
                    audio.playPreview(selectedAudioPath.string(), 1.0f, audioPreviewLoop);
                }
            }

            if (target) {
                ImGui::SameLine();
                if (ImGui::SmallButton("Assign to Selection")) {
                    if (!target->hasAudioSource) {
                        target->hasAudioSource = true;
                        target->audioSource = AudioSourceComponent{};
                    }
                    target->audioSource.clipPath = selectedAudioPath.string();
                    projectManager.currentProject.hasUnsavedChanges = true;
                }
            }

            ImGui::Unindent(8.0f);
        }
        ImGui::PopStyleColor();
    };

    auto renderTextureAssetPanel = [&](const char* headerTitle) {
        if (!browserHasTexture) return;

        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.35f, 0.35f, 0.55f, 1.0f));
        if (ImGui::CollapsingHeader(headerTitle, ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Indent(8.0f);

            ImGui::TextDisabled("%s", selectedTexturePath.filename().string().c_str());
            ImGui::TextColored(ImVec4(0.8f, 0.65f, 0.95f, 1.0f), "%s", selectedTexturePath.string().c_str());

            bool hasOverride = texturePreviewFilterOverrides.find(selectedTexturePath.string()) != texturePreviewFilterOverrides.end();
            bool previewNearest = hasOverride ? texturePreviewFilterOverrides[selectedTexturePath.string()] : hierarchyPreviewNearest;
            Texture* previewTex = renderer.getTexturePreview(selectedTexturePath.string(), previewNearest);

            ImGui::Spacing();
            if (previewTex && previewTex->GetID()) {
                float maxWidth = ImGui::GetContentRegionAvail().x;
                float size = std::min(maxWidth, 160.0f);
                float aspect = previewTex->GetHeight() > 0 ? (previewTex->GetWidth() / static_cast<float>(previewTex->GetHeight())) : 1.0f;
                ImVec2 imageSize(size, size);
                if (aspect > 1.0f) {
                    imageSize.y = size / aspect;
                } else if (aspect > 0.0f) {
                    imageSize.x = size * aspect;
                }
                ImGui::Image((ImTextureID)(intptr_t)previewTex->GetID(), imageSize, ImVec2(0, 1), ImVec2(1, 0));
                ImGui::Text("Size: %d x %d", previewTex->GetWidth(), previewTex->GetHeight());
            } else {
                ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.6f, 1.0f), "Unable to load texture preview.");
            }

            ImGui::Spacing();
            if (ImGui::Checkbox("Override Hierarchy Filter", &hasOverride)) {
                if (hasOverride) {
                    texturePreviewFilterOverrides[selectedTexturePath.string()] = hierarchyPreviewNearest;
                } else {
                    texturePreviewFilterOverrides.erase(selectedTexturePath.string());
                }
            }
            ImGui::BeginDisabled(!hasOverride);
            const char* filterOptions[] = { "Bilinear", "Nearest" };
            int filterIndex = previewNearest ? 1 : 0;
            if (ImGui::Combo("Preview Filter", &filterIndex, filterOptions, IM_ARRAYSIZE(filterOptions))) {
                texturePreviewFilterOverrides[selectedTexturePath.string()] = (filterIndex == 1);
            }
            ImGui::EndDisabled();
            if (!hasOverride) {
                ImGui::TextDisabled("Using global: %s", hierarchyPreviewNearest ? "Nearest" : "Bilinear");
            }

            ImGui::Unindent(8.0f);
        }
        ImGui::PopStyleColor();
    };

    if (selectedObjectIds.empty()) {
        if (browserHasMaterial) {
            renderMaterialAssetPanel("Material Asset", true);
        } else if (browserHasAudio) {
            renderAudioAssetPanel("Audio Clip", nullptr);
        } else if (browserHasTexture) {
            renderTextureAssetPanel("Texture");
        } else {
            ImGui::TextDisabled("No object selected");
        }
        ImGui::End();
        return;
    }

    int primaryId = selectedObjectId;
    auto it = std::find_if(sceneObjects.begin(), sceneObjects.end(),
        [primaryId](const SceneObject& obj) { return obj.id == primaryId; });

    if (it == sceneObjects.end()) {
        ImGui::TextDisabled("Object not found");
        ImGui::End();
        return;
    }

    SceneObject& obj = *it;
    ImGui::PushID(obj.id); // Scope per-object widgets to avoid ID collisions
    auto isUIObject = [](const SceneObject& target) {
        return target.hasUI && target.ui.type != UIElementType::None;
    };

    if (selectedObjectIds.size() > 1) {
        ImGui::Text("Multiple objects selected: %zu", selectedObjectIds.size());
        ImGui::Separator();
    }

    auto objectHeader = drawComponentHeader("Object Info", "ObjectInfo", nullptr, true, std::function<void()>{});
    if (objectHeader.open) {
        char nameBuffer[128];
        strncpy(nameBuffer, obj.name.c_str(), sizeof(nameBuffer));
        nameBuffer[sizeof(nameBuffer) - 1] = '\0';

        ImGui::Text("Name:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("##Name", nameBuffer, sizeof(nameBuffer))) {
            obj.name = nameBuffer;
            projectManager.currentProject.hasUnsavedChanges = true;
        }

        ImGui::Text("Type:");
        ImGui::SameLine();
        const char* typeLabel = "Empty";
        if (obj.hasRenderer) {
            switch (obj.renderType) {
                case RenderType::Cube: typeLabel = "Cube"; break;
                case RenderType::Sphere: typeLabel = "Sphere"; break;
                case RenderType::Capsule: typeLabel = "Capsule"; break;
                case RenderType::OBJMesh: typeLabel = "OBJ Mesh"; break;
                case RenderType::Model: typeLabel = "Model"; break;
                case RenderType::Sprite: typeLabel = "Sprite"; break;
                case RenderType::Mirror: typeLabel = "Mirror"; break;
                case RenderType::Plane: typeLabel = "Plane"; break;
                case RenderType::Torus: typeLabel = "Torus"; break;
                case RenderType::None: break;
            }
        } else if (obj.hasUI) {
            switch (obj.ui.type) {
                case UIElementType::Canvas: typeLabel = "Canvas"; break;
                case UIElementType::Image: typeLabel = "UI Image"; break;
                case UIElementType::Slider: typeLabel = "UI Slider"; break;
                case UIElementType::Button: typeLabel = "UI Button"; break;
                case UIElementType::Text: typeLabel = "UI Text"; break;
                case UIElementType::Sprite2D: typeLabel = "Sprite2D"; break;
                case UIElementType::None: break;
            }
        } else if (obj.hasLight) {
            switch (obj.light.type) {
                case LightType::Directional: typeLabel = "Directional Light"; break;
                case LightType::Point: typeLabel = "Point Light"; break;
                case LightType::Spot: typeLabel = "Spot Light"; break;
                case LightType::Area: typeLabel = "Area Light"; break;
            }
        } else if (obj.hasCamera) {
            typeLabel = "Camera";
        } else if (obj.hasPostFX) {
            typeLabel = "Post FX Node";
        }
        ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "%s", typeLabel);

        ImGui::Text("ID:");
        ImGui::SameLine();
        ImGui::TextDisabled("%d", obj.id);

        if (ImGui::Checkbox("Enabled##ObjEnabled", &obj.enabled)) {
            projectManager.currentProject.hasUnsavedChanges = true;
        }

        ImGui::Text("Layer:");
        ImGui::SameLine();
        int layer = obj.layer;
        ImGui::SetNextItemWidth(120);
        if (ImGui::SliderInt("##Layer", &layer, 0, 31)) {
            obj.layer = layer;
            projectManager.currentProject.hasUnsavedChanges = true;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(0-31)");

        ImGui::Text("Tag:");
        ImGui::SameLine();
        char tagBuf[64] = {};
        std::snprintf(tagBuf, sizeof(tagBuf), "%s", obj.tag.c_str());
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("##Tag", tagBuf, sizeof(tagBuf))) {
            obj.tag = tagBuf;
            projectManager.currentProject.hasUnsavedChanges = true;
        }

        ImGui::Spacing();
        if (obj.hasPostFX) {
            ImGui::TextDisabled("Transform is ignored for post-processing nodes.");
        }
        if (isUIObject(obj)) {
            ImGui::TextDisabled("UI objects use the UI section for positioning.");
        }

        ImGui::Text("Position");
        ImGui::PushItemWidth(-1);
        if (ImGui::DragFloat3("##Position", &obj.position.x, 0.1f)) {
            syncLocalTransform(obj);
            projectManager.currentProject.hasUnsavedChanges = true;
        }
        ImGui::PopItemWidth();

        ImGui::Text("Rotation");
        ImGui::PushItemWidth(-1);
        if (ImGui::DragFloat3("##Rotation", &obj.rotation.x, 1.0f, -360.0f, 360.0f)) {
            obj.rotation = NormalizeEulerDegrees(obj.rotation);
            syncLocalTransform(obj);
            projectManager.currentProject.hasUnsavedChanges = true;
        }
        ImGui::PopItemWidth();

        ImGui::Text("Scale");
        ImGui::PushItemWidth(-1);
        if (ImGui::DragFloat3("##Scale", &obj.scale.x, 0.05f, 0.01f, 100.0f)) {
            syncLocalTransform(obj);
            projectManager.currentProject.hasUnsavedChanges = true;
        }
        ImGui::PopItemWidth();

        if (ImGui::Button("Reset Transform", ImVec2(-1, 0))) {
            obj.position = glm::vec3(0.0f);
            obj.rotation = glm::vec3(0.0f);
            obj.scale = glm::vec3(1.0f);
            syncLocalTransform(obj);
            projectManager.currentProject.hasUnsavedChanges = true;
        }
    }

    ImGui::Spacing();

    if (isUIObject(obj)) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.25f, 0.45f, 0.65f, 1.0f));
        bool changed = false;
        bool removeUi = false;
        auto header = drawComponentHeader("UI", "UI", nullptr, true, [&]() {
            if (ImGui::MenuItem("Remove")) {
                removeUi = true;
            }
        });
        if (header.open) {
            ImGui::PushID("UI");
            ImGui::Indent(10.0f);

            const char* anchors[] = { "Center", "Top Left", "Top Right", "Bottom Left", "Bottom Right" };
            int anchor = static_cast<int>(obj.ui.anchor);
            if (ImGui::Combo("Anchor", &anchor, anchors, IM_ARRAYSIZE(anchors))) {
                obj.ui.anchor = static_cast<UIAnchor>(anchor);
                changed = true;
            }

            if (ImGui::DragFloat2("Position (px)", &obj.ui.position.x, 1.0f)) {
                changed = true;
            }

            if (ImGui::DragFloat("Rotation (deg)", &obj.ui.rotation, 0.5f, -360.0f, 360.0f)) {
                glm::vec3 rot(0.0f, 0.0f, obj.ui.rotation);
                rot = NormalizeEulerDegrees(rot);
                obj.ui.rotation = rot.z;
                changed = true;
            }

            glm::vec2 minSize(8.0f, 8.0f);
            if (ImGui::DragFloat2("Size (px)", &obj.ui.size.x, 1.0f, minSize.x, 4096.0f)) {
                obj.ui.size.x = std::max(minSize.x, obj.ui.size.x);
                obj.ui.size.y = std::max(minSize.y, obj.ui.size.y);
                changed = true;
            }

            if (obj.ui.type == UIElementType::Canvas) {
                if (ImGui::Checkbox("Render In 3D", &obj.ui.renderIn3D)) {
                    changed = true;
                }
                if (obj.ui.renderIn3D) {
                    int size[2] = { obj.ui.renderTargetSize.x, obj.ui.renderTargetSize.y };
                    if (ImGui::DragInt2("Render Target (px)", size, 1.0f, 16, 4096)) {
                        obj.ui.renderTargetSize.x = std::max(16, size[0]);
                        obj.ui.renderTargetSize.y = std::max(16, size[1]);
                        changed = true;
                    }
                    ImGui::TextDisabled("Canvas renders on a 3D quad; use object scale for world size.");
                }
            }

            if (obj.ui.type == UIElementType::Button || obj.ui.type == UIElementType::Slider) {
                if (ImGui::Checkbox("Interactable", &obj.ui.interactable)) {
                    changed = true;
                }

                const auto& presets = getUIStylePresets();
                if (!presets.empty()) {
                    int presetIndex = findUIStylePreset(obj.ui.stylePreset);
                    if (presetIndex < 0) presetIndex = 0;
                    const char* currentPreset = presets[presetIndex].name.c_str();
                    if (ImGui::BeginCombo("Style Preset", currentPreset)) {
                        for (int i = 0; i < (int)presets.size(); ++i) {
                            bool selected = (i == presetIndex);
                            if (ImGui::Selectable(presets[i].name.c_str(), selected)) {
                                obj.ui.stylePreset = presets[i].name;
                                changed = true;
                            }
                            if (selected) ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                }
            }

            if (obj.ui.type == UIElementType::Button || obj.ui.type == UIElementType::Slider ||
                obj.ui.type == UIElementType::Image || obj.ui.type == UIElementType::Text ||
                obj.ui.type == UIElementType::Sprite2D) {
                char labelBuf[128] = {};
                std::snprintf(labelBuf, sizeof(labelBuf), "%s", obj.ui.label.c_str());
                if (ImGui::InputText(obj.ui.type == UIElementType::Text ? "Text" : "Label", labelBuf, sizeof(labelBuf))) {
                    obj.ui.label = labelBuf;
                    changed = true;
                }
            }
            if (obj.ui.type == UIElementType::Text) {
                if (ImGui::DragFloat("Text Size", &obj.ui.textScale, 0.05f, 0.1f, 10.0f, "%.2f")) {
                    obj.ui.textScale = std::max(0.1f, obj.ui.textScale);
                    changed = true;
                }
            }

            if (obj.ui.type == UIElementType::Image || obj.ui.type == UIElementType::Sprite2D) {
                ImGui::TextUnformatted("Texture");
                ImGui::SetNextItemWidth(-160);
                char texBuf[512] = {};
                std::snprintf(texBuf, sizeof(texBuf), "%s", obj.albedoTexturePath.c_str());
                if (ImGui::InputText("##UITexture", texBuf, sizeof(texBuf))) {
                    obj.albedoTexturePath = texBuf;
                    changed = true;
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Clear##UITexture")) {
                    obj.albedoTexturePath.clear();
                    changed = true;
                }
                ImGui::SameLine();
                bool canUseTex = !fileBrowser.selectedFile.empty() && fs::exists(fileBrowser.selectedFile) &&
                                 fileBrowser.isTextureFile(fs::directory_entry(fileBrowser.selectedFile));
                ImGui::BeginDisabled(!canUseTex);
                if (ImGui::SmallButton("Use Selection##UITexture")) {
                    obj.albedoTexturePath = fileBrowser.selectedFile.string();
                    changed = true;
                }
                ImGui::EndDisabled();
            }

            if (obj.ui.type == UIElementType::Slider) {
                const char* sliderStyles[] = { "ImGui", "Fill", "Circle" };
                int sliderStyle = static_cast<int>(obj.ui.sliderStyle);
                if (ImGui::Combo("Style", &sliderStyle, sliderStyles, IM_ARRAYSIZE(sliderStyles))) {
                    obj.ui.sliderStyle = static_cast<UISliderStyle>(sliderStyle);
                    changed = true;
                }
                if (ImGui::DragFloat("Min", &obj.ui.sliderMin, 0.1f)) {
                    changed = true;
                }
                if (ImGui::DragFloat("Max", &obj.ui.sliderMax, 0.1f)) {
                    changed = true;
                }
                if (obj.ui.sliderMax < obj.ui.sliderMin) {
                    std::swap(obj.ui.sliderMin, obj.ui.sliderMax);
                }
                if (ImGui::SliderFloat("Value", &obj.ui.sliderValue, obj.ui.sliderMin, obj.ui.sliderMax)) {
                    changed = true;
                }
            }

            ImVec4 uiColor(obj.ui.color.r, obj.ui.color.g, obj.ui.color.b, obj.ui.color.a);
            if (ImGui::ColorEdit4("Tint", &uiColor.x)) {
                obj.ui.color = glm::vec4(uiColor.x, uiColor.y, uiColor.z, uiColor.w);
                changed = true;
            }

            if (obj.ui.type == UIElementType::Button) {
                const char* buttonStyles[] = { "ImGui", "Outline" };
                int buttonStyle = static_cast<int>(obj.ui.buttonStyle);
                if (ImGui::Combo("Style", &buttonStyle, buttonStyles, IM_ARRAYSIZE(buttonStyles))) {
                    obj.ui.buttonStyle = static_cast<UIButtonStyle>(buttonStyle);
                    changed = true;
                }
                ImGui::TextDisabled("Last Pressed: %s", obj.ui.buttonPressed ? "yes" : "no");
            }

            ImGui::Unindent(10.0f);
            ImGui::PopID();
        }
        if (removeUi) {
            obj.hasUI = false;
            obj.ui.type = UIElementType::None;
            UpdateLegacyTypeFromComponents(obj);
            changed = true;
        }
        if (changed) {
            projectManager.currentProject.hasUnsavedChanges = true;
        }
        ImGui::PopStyleColor();
    }

    if (obj.hasCollider) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.35f, 0.5f, 0.35f, 1.0f));
        bool removeCollider = false;
        bool changed = false;
        auto header = drawComponentHeader("Collider", "Collider", &obj.collider.enabled, true, [&]() {
            if (ImGui::MenuItem("Remove")) {
                removeCollider = true;
            }
        });
        if (header.enabledChanged) {
            changed = true;
        }
        if (header.open) {
            ImGui::PushID("Collider");
            ImGui::Indent(10.0f);

            const char* colliderTypes[] = { "Box", "Mesh", "Convex Mesh", "Capsule" };
            int colliderType = static_cast<int>(obj.collider.type);
            if (ImGui::Combo("Type", &colliderType, colliderTypes, IM_ARRAYSIZE(colliderTypes))) {
                obj.collider.type = static_cast<ColliderType>(colliderType);
                changed = true;
            }

            if (obj.collider.type == ColliderType::Box) {
                if (ImGui::DragFloat3("Box Size", &obj.collider.boxSize.x, 0.01f, 0.01f, 1000.0f, "%.3f")) {
                    obj.collider.boxSize.x = std::max(0.01f, obj.collider.boxSize.x);
                    obj.collider.boxSize.y = std::max(0.01f, obj.collider.boxSize.y);
                    obj.collider.boxSize.z = std::max(0.01f, obj.collider.boxSize.z);
                    changed = true;
                }
                if (ImGui::SmallButton("Match Object Scale")) {
                    obj.collider.boxSize = glm::max(obj.scale, glm::vec3(0.01f));
                    changed = true;
                }
            } else if (obj.collider.type == ColliderType::Capsule) {
                float radius = std::max(0.05f, std::max(obj.collider.boxSize.x, obj.collider.boxSize.z) * 0.5f);
                float height = std::max(0.1f, obj.collider.boxSize.y);
                if (ImGui::DragFloat("Radius", &radius, 0.01f, 0.05f, 5.0f, "%.3f")) {
                    obj.collider.boxSize.x = obj.collider.boxSize.z = radius * 2.0f;
                    changed = true;
                }
                if (ImGui::DragFloat("Height", &height, 0.01f, 0.1f, 10.0f, "%.3f")) {
                    obj.collider.boxSize.y = height;
                    changed = true;
                }
                ImGui::TextDisabled("Capsule aligned to Y axis.");
            } else {
                if (ImGui::Checkbox("Use Convex Hull (required for Rigidbody3D)", &obj.collider.convex)) {
                    changed = true;
                }
                ImGui::TextDisabled("Uses mesh from the object (OBJ/Model). Non-convex is static-only.");
            }

            ImGui::SeparatorText("Surface");
            if (ImGui::DragFloat("Static Friction", &obj.collider.staticFriction, 0.01f, 0.0f, 4.0f, "%.2f")) {
                obj.collider.staticFriction = std::clamp(obj.collider.staticFriction, 0.0f, 4.0f);
                changed = true;
            }
            if (ImGui::DragFloat("Dynamic Friction", &obj.collider.dynamicFriction, 0.01f, 0.0f, 4.0f, "%.2f")) {
                obj.collider.dynamicFriction = std::clamp(obj.collider.dynamicFriction, 0.0f, 4.0f);
                changed = true;
            }
            if (ImGui::DragFloat("Restitution", &obj.collider.restitution, 0.01f, 0.0f, 1.0f, "%.2f")) {
                obj.collider.restitution = std::clamp(obj.collider.restitution, 0.0f, 1.0f);
                changed = true;
            }

            ImGui::Unindent(10.0f);
            ImGui::PopID();
        }
        if (removeCollider) {
            obj.hasCollider = false;
            changed = true;
        }
        if (changed) {
            projectManager.currentProject.hasUnsavedChanges = true;
        }
        ImGui::PopStyleColor();
    }

    if (obj.hasPlayerController) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.35f, 0.45f, 0.7f, 1.0f));
        bool removePlayerController = false;
        bool changed = false;
        auto header = drawComponentHeader("Player Controller", "PlayerController", &obj.playerController.enabled, true, [&]() {
            if (ImGui::MenuItem("Remove")) {
                removePlayerController = true;
            }
        });
        if (header.enabledChanged) {
            changed = true;
        }
        if (header.open) {
            ImGui::PushID("PlayerController");
            ImGui::Indent(10.0f);
            if (ImGui::DragFloat("Move Speed", &obj.playerController.moveSpeed, 0.1f, 0.1f, 100.0f, "%.2f")) {
                obj.playerController.moveSpeed = std::max(0.1f, obj.playerController.moveSpeed);
                obj.playerController.runSpeed = std::max(obj.playerController.moveSpeed, obj.playerController.runSpeed);
                changed = true;
            }
            if (ImGui::DragFloat("Run Speed", &obj.playerController.runSpeed, 0.1f, 0.1f, 140.0f, "%.2f")) {
                obj.playerController.runSpeed = std::max(obj.playerController.moveSpeed, obj.playerController.runSpeed);
                changed = true;
            }
            if (ImGui::DragFloat("Look Sensitivity", &obj.playerController.lookSensitivity, 0.01f, 0.01f, 2.0f, "%.2f")) {
                obj.playerController.lookSensitivity = std::clamp(obj.playerController.lookSensitivity, 0.01f, 2.0f);
                changed = true;
            }
            if (ImGui::DragFloat("Ground Accel", &obj.playerController.groundAcceleration, 0.1f, 0.0f, 200.0f, "%.2f")) {
                obj.playerController.groundAcceleration = std::clamp(obj.playerController.groundAcceleration, 0.0f, 200.0f);
                changed = true;
            }
            if (ImGui::DragFloat("Air Accel", &obj.playerController.airAcceleration, 0.1f, 0.0f, 200.0f, "%.2f")) {
                obj.playerController.airAcceleration = std::clamp(obj.playerController.airAcceleration, 0.0f, 200.0f);
                changed = true;
            }
            if (ImGui::DragFloat("Braking", &obj.playerController.braking, 0.1f, 0.0f, 200.0f, "%.2f")) {
                obj.playerController.braking = std::clamp(obj.playerController.braking, 0.0f, 200.0f);
                changed = true;
            }
            if (ImGui::DragFloat("Min Surface Control", &obj.playerController.minSurfaceControl, 0.01f, 0.0f, 1.0f, "%.2f")) {
                obj.playerController.minSurfaceControl = std::clamp(obj.playerController.minSurfaceControl, 0.0f, 1.0f);
                changed = true;
            }
            if (ImGui::DragFloat("Slide Gravity", &obj.playerController.slideGravity, 0.1f, 0.0f, 120.0f, "%.2f")) {
                obj.playerController.slideGravity = std::clamp(obj.playerController.slideGravity, 0.0f, 120.0f);
                changed = true;
            }
            if (ImGui::DragFloat("Platform Carry", &obj.playerController.platformCarry, 0.01f, 0.0f, 3.0f, "%.2f")) {
                obj.playerController.platformCarry = std::clamp(obj.playerController.platformCarry, 0.0f, 3.0f);
                changed = true;
            }
            if (ImGui::DragFloat("Height", &obj.playerController.height, 0.01f, 0.5f, 3.0f, "%.2f")) {
                obj.playerController.height = std::clamp(obj.playerController.height, 0.5f, 3.0f);
                obj.scale.y = obj.playerController.height;
                obj.collider.boxSize.y = obj.playerController.height;
                changed = true;
            }
            if (ImGui::DragFloat("Radius", &obj.playerController.radius, 0.01f, 0.2f, 1.2f, "%.2f")) {
                obj.playerController.radius = std::clamp(obj.playerController.radius, 0.2f, 1.2f);
                obj.scale.x = obj.scale.z = obj.playerController.radius * 2.0f;
                obj.collider.boxSize.x = obj.collider.boxSize.z = obj.playerController.radius * 2.0f;
                changed = true;
            }
            if (ImGui::DragFloat("Jump Strength", &obj.playerController.jumpStrength, 0.1f, 0.1f, 30.0f, "%.1f")) {
                obj.playerController.jumpStrength = std::max(0.1f, obj.playerController.jumpStrength);
                changed = true;
            }

            ImGui::Unindent(10.0f);
            ImGui::PopID();
        }
        if (removePlayerController) {
            obj.hasPlayerController = false;
            changed = true;
        }
        if (changed) {
            obj.hasCollider = true;
            obj.collider.type = ColliderType::Capsule;
            obj.collider.convex = true;
            obj.hasRigidbody = true;
            obj.rigidbody.enabled = true;
            obj.rigidbody.useGravity = true;
            projectManager.currentProject.hasUnsavedChanges = true;
        }
        ImGui::PopStyleColor();
    }

    if (obj.hasRigidbody) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.45f, 0.45f, 0.25f, 1.0f));
        bool removeRigidbody = false;
        bool changed = false;
        auto header = drawComponentHeader("Rigidbody3D", "Rigidbody3D", &obj.rigidbody.enabled, true, [&]() {
            if (ImGui::MenuItem("Remove")) {
                removeRigidbody = true;
            }
        });
        if (header.enabledChanged) {
            changed = true;
        }
        if (header.open) {
            ImGui::PushID("Rigidbody3D");
            ImGui::Indent(10.0f);
            ImGui::TextDisabled("Collider required for physics.");
            if (isUIObject(obj)) {
                ImGui::TextDisabled("Rigidbody3D is for 3D objects (use Rigidbody2D for UI/canvas).");
            }

            if (ImGui::DragFloat("Mass", &obj.rigidbody.mass, 0.05f, 0.01f, 1000.0f, "%.2f")) {
                obj.rigidbody.mass = std::max(0.01f, obj.rigidbody.mass);
                changed = true;
            }
            if (ImGui::Checkbox("Use Gravity", &obj.rigidbody.useGravity)) {
                changed = true;
            }
            if (ImGui::Checkbox("Kinematic", &obj.rigidbody.isKinematic)) {
                changed = true;
            }
            if (ImGui::DragFloat("Linear Damping", &obj.rigidbody.linearDamping, 0.01f, 0.0f, 10.0f)) {
                obj.rigidbody.linearDamping = std::clamp(obj.rigidbody.linearDamping, 0.0f, 10.0f);
                changed = true;
            }
            if (ImGui::DragFloat("Angular Damping", &obj.rigidbody.angularDamping, 0.01f, 0.0f, 10.0f)) {
                obj.rigidbody.angularDamping = std::clamp(obj.rigidbody.angularDamping, 0.0f, 10.0f);
                changed = true;
            }
            ImGui::TextDisabled("Rotation Constraints");
            if (ImGui::Checkbox("Lock Rotation X", &obj.rigidbody.lockRotationX)) {
                changed = true;
            }
            if (ImGui::Checkbox("Lock Rotation Y", &obj.rigidbody.lockRotationY)) {
                changed = true;
            }
            if (ImGui::Checkbox("Lock Rotation Z", &obj.rigidbody.lockRotationZ)) {
                changed = true;
            }
            ImGui::Unindent(10.0f);
            ImGui::PopID();
        }
        if (removeRigidbody) {
            obj.hasRigidbody = false;
            changed = true;
        }
        if (changed) {
            projectManager.currentProject.hasUnsavedChanges = true;
        }
        ImGui::PopStyleColor();
    }

    if (obj.hasRigidbody2D) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.35f, 0.55f, 0.45f, 1.0f));
        bool removeRigidbody2D = false;
        bool changed = false;
        auto header = drawComponentHeader("Rigidbody2D", "Rigidbody2D", &obj.rigidbody2D.enabled, true, [&]() {
            if (ImGui::MenuItem("Remove")) {
                removeRigidbody2D = true;
            }
        });
        if (header.enabledChanged) {
            changed = true;
        }
        if (header.open) {
            ImGui::PushID("Rigidbody2D");
            ImGui::Indent(10.0f);
            if (!isUIObject(obj)) {
                ImGui::TextDisabled("Rigidbody2D is for UI/canvas objects only.");
            }
            if (ImGui::Checkbox("Use Gravity", &obj.rigidbody2D.useGravity)) {
                changed = true;
            }
            if (ImGui::DragFloat("Gravity Scale", &obj.rigidbody2D.gravityScale, 0.05f, 0.0f, 10.0f, "%.2f")) {
                obj.rigidbody2D.gravityScale = std::max(0.0f, obj.rigidbody2D.gravityScale);
                changed = true;
            }
            if (ImGui::DragFloat("Linear Damping", &obj.rigidbody2D.linearDamping, 0.01f, 0.0f, 10.0f)) {
                obj.rigidbody2D.linearDamping = std::clamp(obj.rigidbody2D.linearDamping, 0.0f, 10.0f);
                changed = true;
            }
            if (ImGui::DragFloat2("Velocity", &obj.rigidbody2D.velocity.x, 0.1f)) {
                changed = true;
            }
            ImGui::Unindent(10.0f);
            ImGui::PopID();
        }
        if (removeRigidbody2D) {
            obj.hasRigidbody2D = false;
            changed = true;
        }
        if (changed) {
            projectManager.currentProject.hasUnsavedChanges = true;
        }
        ImGui::PopStyleColor();
    }

    if (obj.hasCollider2D) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.35f, 0.5f, 0.65f, 1.0f));
        bool removeCollider2D = false;
        bool changed = false;
        auto header = drawComponentHeader("Collider2D", "Collider2D", &obj.collider2D.enabled, true, [&]() {
            if (ImGui::MenuItem("Remove")) {
                removeCollider2D = true;
            }
        });
        if (header.enabledChanged) {
            changed = true;
        }
        if (header.open) {
            ImGui::PushID("Collider2D");
            ImGui::Indent(10.0f);
            if (!isUIObject(obj)) {
                ImGui::TextDisabled("Collider2D is for UI/canvas objects only.");
            }
            const char* colliderTypes[] = { "Box", "Polygon", "Edge" };
            int colliderType = static_cast<int>(obj.collider2D.type);
            if (ImGui::Combo("Type", &colliderType, colliderTypes, IM_ARRAYSIZE(colliderTypes))) {
                obj.collider2D.type = static_cast<Collider2DType>(colliderType);
                if (obj.collider2D.type == Collider2DType::Polygon) {
                    obj.collider2D.closed = true;
                } else if (obj.collider2D.type == Collider2DType::Edge) {
                    obj.collider2D.closed = false;
                }
                changed = true;
            }

            auto ensureHexagon = [&](Collider2DComponent& col, const glm::vec2& size) {
                if (!col.points.empty()) return;
                float radius = 0.5f * std::min(size.x, size.y);
                col.points.clear();
                for (int i = 0; i < 6; ++i) {
                    float ang = static_cast<float>(i) * (2.0f * PI / 6.0f);
                    col.points.emplace_back(std::cos(ang) * radius, std::sin(ang) * radius);
                }
            };
            auto ensureEdge = [&](Collider2DComponent& col, const glm::vec2& size) {
                if (col.points.size() >= 2) return;
                float half = size.x * 0.5f;
                col.points = { glm::vec2(-half, 0.0f), glm::vec2(half, 0.0f) };
            };

            if (obj.collider2D.type == Collider2DType::Box) {
                if (ImGui::DragFloat2("Box Size", &obj.collider2D.boxSize.x, 0.1f, 0.01f, 10000.0f, "%.2f")) {
                    obj.collider2D.boxSize.x = std::max(0.01f, obj.collider2D.boxSize.x);
                    obj.collider2D.boxSize.y = std::max(0.01f, obj.collider2D.boxSize.y);
                    changed = true;
                }
                if (ImGui::SmallButton("Match UI Size")) {
                    obj.collider2D.boxSize = glm::max(obj.ui.size, glm::vec2(1.0f));
                    changed = true;
                }
            } else if (obj.collider2D.type == Collider2DType::Polygon) {
                ensureHexagon(obj.collider2D, glm::max(obj.ui.size, glm::vec2(1.0f)));
                ImGui::TextDisabled("Points (local space)");
                for (size_t i = 0; i < obj.collider2D.points.size(); ++i) {
                    ImGui::PushID(static_cast<int>(i));
                    if (ImGui::DragFloat2("##point", &obj.collider2D.points[i].x, 0.1f)) {
                        changed = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Remove")) {
                        obj.collider2D.points.erase(obj.collider2D.points.begin() + static_cast<long>(i));
                        changed = true;
                        ImGui::PopID();
                        break;
                    }
                    ImGui::PopID();
                }
                if (ImGui::SmallButton("Add Point")) {
                    obj.collider2D.points.push_back(glm::vec2(0.0f));
                    changed = true;
                }
            } else if (obj.collider2D.type == Collider2DType::Edge) {
                ensureEdge(obj.collider2D, glm::max(obj.ui.size, glm::vec2(1.0f)));
                if (ImGui::Checkbox("Closed Loop", &obj.collider2D.closed)) {
                    changed = true;
                }
                if (ImGui::DragFloat("Thickness", &obj.collider2D.edgeThickness, 0.01f, 0.01f, 10.0f, "%.2f")) {
                    obj.collider2D.edgeThickness = std::max(0.01f, obj.collider2D.edgeThickness);
                    changed = true;
                }
                ImGui::TextDisabled("Points (local space)");
                for (size_t i = 0; i < obj.collider2D.points.size(); ++i) {
                    ImGui::PushID(static_cast<int>(i));
                    if (ImGui::DragFloat2("##edgepoint", &obj.collider2D.points[i].x, 0.1f)) {
                        changed = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Remove")) {
                        obj.collider2D.points.erase(obj.collider2D.points.begin() + static_cast<long>(i));
                        changed = true;
                        ImGui::PopID();
                        break;
                    }
                    ImGui::PopID();
                }
                if (ImGui::SmallButton("Add Point")) {
                    obj.collider2D.points.push_back(glm::vec2(0.0f));
                    changed = true;
                }
            }

            ImGui::Unindent(10.0f);
            ImGui::PopID();
        }
        if (removeCollider2D) {
            obj.hasCollider2D = false;
            changed = true;
        }
        if (changed) {
            projectManager.currentProject.hasUnsavedChanges = true;
        }
        ImGui::PopStyleColor();
    }

    if (obj.hasParallaxLayer2D) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.28f, 0.45f, 0.6f, 1.0f));
        bool removeParallax = false;
        bool changed = false;
        auto header = drawComponentHeader("Parallax Layer 2D", "ParallaxLayer2D", &obj.parallaxLayer2D.enabled, true, [&]() {
            if (ImGui::MenuItem("Remove")) {
                removeParallax = true;
            }
        });
        if (header.enabledChanged) {
            changed = true;
        }
        if (header.open) {
            ImGui::PushID("ParallaxLayer2D");
            ImGui::Indent(10.0f);
            if (!isUIObject(obj)) {
                ImGui::TextDisabled("Parallax layers are for UI world objects.");
            }
            if (ImGui::DragInt("Order", &obj.parallaxLayer2D.order, 1.0f)) {
                changed = true;
            }
            if (ImGui::DragFloat("Parallax Factor", &obj.parallaxLayer2D.factor, 0.01f, 0.0f, 1.0f, "%.2f")) {
                obj.parallaxLayer2D.factor = std::clamp(obj.parallaxLayer2D.factor, 0.0f, 1.0f);
                changed = true;
            }
            if (ImGui::Checkbox("Repeat X", &obj.parallaxLayer2D.repeatX)) {
                changed = true;
            }
            if (ImGui::Checkbox("Repeat Y", &obj.parallaxLayer2D.repeatY)) {
                changed = true;
            }
            if (ImGui::DragFloat2("Repeat Spacing", &obj.parallaxLayer2D.repeatSpacing.x, 0.1f, 0.0f, 10000.0f, "%.1f")) {
                obj.parallaxLayer2D.repeatSpacing.x = std::max(0.0f, obj.parallaxLayer2D.repeatSpacing.x);
                obj.parallaxLayer2D.repeatSpacing.y = std::max(0.0f, obj.parallaxLayer2D.repeatSpacing.y);
                changed = true;
            }
            ImGui::Unindent(10.0f);
            ImGui::PopID();
        }
        if (removeParallax) {
            obj.hasParallaxLayer2D = false;
            changed = true;
        }
        if (changed) {
            projectManager.currentProject.hasUnsavedChanges = true;
        }
        ImGui::PopStyleColor();
    }

    if (obj.hasAudioSource) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.55f, 0.4f, 0.3f, 1.0f));
        bool removeAudioSource = false;
        bool changed = false;
        auto header = drawComponentHeader("Audio Source", "AudioSource", &obj.audioSource.enabled, true, [&]() {
            if (ImGui::MenuItem("Remove")) {
                removeAudioSource = true;
            }
        });
        if (header.enabledChanged) {
            changed = true;
        }
        if (header.open) {
            ImGui::PushID("AudioSource");
            ImGui::Indent(10.0f);
            auto& src = obj.audioSource;

            char clipBuf[512] = {};
            std::snprintf(clipBuf, sizeof(clipBuf), "%s", src.clipPath.c_str());
            ImGui::TextDisabled("Clip");
            ImGui::SetNextItemWidth(-170);
            if (ImGui::InputText("##ClipPath", clipBuf, sizeof(clipBuf))) {
                src.clipPath = clipBuf;
                changed = true;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Clear##AudioClip")) {
                src.clipPath.clear();
                changed = true;
            }
            ImGui::SameLine();
            bool selectionIsAudio = false;
            if (!fileBrowser.selectedFile.empty() && fs::exists(fileBrowser.selectedFile)) {
                selectionIsAudio = fileBrowser.getFileCategory(fs::directory_entry(fileBrowser.selectedFile)) == FileCategory::Audio;
            }
            ImGui::BeginDisabled(!selectionIsAudio);
            if (ImGui::SmallButton("Use Selection##AudioClip")) {
                src.clipPath = fileBrowser.selectedFile.string();
                changed = true;
            }
            ImGui::EndDisabled();

            ImGui::Spacing();
            bool previewPlaying = !src.clipPath.empty() && audio.isPreviewing(src.clipPath);
            if (ImGui::Button(previewPlaying ? "Stop Preview" : "Play Preview")) {
                if (previewPlaying) {
                    audio.stopPreview();
                } else if (!src.clipPath.empty()) {
                    audio.playPreview(src.clipPath, src.volume, src.loop);
                }
            }
            ImGui::SameLine();
            ImGui::TextDisabled("%s", src.clipPath.empty() ? "No clip selected" : fs::path(src.clipPath).filename().string().c_str());

            if (ImGui::SliderFloat("Volume", &src.volume, 0.0f, 1.5f, "%.2f")) {
                changed = true;
            }
            if (ImGui::Checkbox("Loop", &src.loop)) {
                changed = true;
            }
            if (ImGui::Checkbox("Play On Start", &src.playOnStart)) {
                changed = true;
            }
            if (ImGui::Checkbox("3D Spatialization", &src.spatial)) {
                changed = true;
            }
            ImGui::BeginDisabled(!src.spatial);
            if (ImGui::DragFloat("Min Distance", &src.minDistance, 0.1f, 0.1f, 200.0f, "%.2f")) {
                src.minDistance = std::max(0.1f, src.minDistance);
                changed = true;
            }
            if (ImGui::DragFloat("Max Distance", &src.maxDistance, 0.1f, src.minDistance + 0.5f, 500.0f, "%.2f")) {
                src.maxDistance = std::max(src.maxDistance, src.minDistance + 0.5f);
                changed = true;
            }
            const char* rolloffModes[] = { "Logarithmic", "Linear", "Exponential", "Custom" };
            int rolloffIndex = static_cast<int>(src.rolloffMode);
            if (ImGui::Combo("Rolloff Mode", &rolloffIndex, rolloffModes, IM_ARRAYSIZE(rolloffModes))) {
                src.rolloffMode = static_cast<AudioRolloffMode>(rolloffIndex);
                changed = true;
            }
            if (src.rolloffMode != AudioRolloffMode::Custom) {
                if (ImGui::SliderFloat("Rolloff Factor", &src.rolloff, 0.1f, 4.0f, "%.2f")) {
                    src.rolloff = std::max(0.1f, src.rolloff);
                    changed = true;
                }
            } else {
                if (ImGui::SliderFloat("Mid Distance", &src.customMidDistance, 0.0f, 1.0f, "%.2f")) {
                    src.customMidDistance = std::clamp(src.customMidDistance, 0.0f, 1.0f);
                    changed = true;
                }
                if (ImGui::SliderFloat("Mid Gain", &src.customMidGain, 0.0f, 1.0f, "%.2f")) {
                    src.customMidGain = std::clamp(src.customMidGain, 0.0f, 1.0f);
                    changed = true;
                }
                if (ImGui::SliderFloat("End Gain", &src.customEndGain, 0.0f, 1.0f, "%.2f")) {
                    src.customEndGain = std::clamp(src.customEndGain, 0.0f, 1.0f);
                    changed = true;
                }
            }
            ImGui::EndDisabled();

            const AudioClipPreview* clipPreview = audio.getPreview(src.clipPath);
            ImGui::Separator();
            ImGui::TextDisabled("Waveform");
            ImVec2 waveSize(ImGui::GetContentRegionAvail().x, 80.0f);
            double cur = 0.0;
            double dur = clipPreview ? clipPreview->durationSeconds : 0.0;
            float progress = -1.0f;
            if (audio.getPreviewTime(src.clipPath, cur, dur) && dur > 0.0001) {
                progress = static_cast<float>(cur / dur);
            }
            float seekRatio = -1.0f;
            drawWaveform("##AudioWaveComponent", clipPreview, waveSize, progress, &seekRatio);
            if (seekRatio >= 0.0f && dur > 0.0) {
                audio.seekPreview(src.clipPath, seekRatio * dur);
            }
            if (dur > 0.0) {
                ImGui::TextDisabled("Time: %0.2f / %0.2f", cur, dur);
            }
            if (clipPreview) {
                ImGui::TextDisabled("Length: %.2fs | %u channels @ %u Hz",
                    clipPreview->durationSeconds,
                    clipPreview->channels,
                    clipPreview->sampleRate);
            }

            ImGui::Unindent(10.0f);
            ImGui::PopID();
        }
        if (removeAudioSource) {
            if (audio.isPreviewing(obj.audioSource.clipPath)) {
                audio.stopPreview();
            }
            obj.hasAudioSource = false;
            changed = true;
        }
        if (changed) {
            projectManager.currentProject.hasUnsavedChanges = true;
        }
        ImGui::PopStyleColor();
    }

    if (obj.hasAnimation) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.4f, 0.35f, 0.55f, 1.0f));
        bool removeAnimation = false;
        bool changed = false;
        auto header = drawComponentHeader("Animation", "Animation", &obj.animation.enabled, true, [&]() {
            if (ImGui::MenuItem("Open Animator")) {
                showAnimationWindow = true;
                animationTargetId = obj.id;
            }
            if (ImGui::MenuItem("Remove")) {
                removeAnimation = true;
            }
        });
        if (header.enabledChanged) {
            changed = true;
        }
        if (header.open) {
            ImGui::PushID("Animation");
            ImGui::Indent(10.0f);
            if (ImGui::Button("Open Animator")) {
                showAnimationWindow = true;
                animationTargetId = obj.id;
            }
            ImGui::SameLine();
            ImGui::TextDisabled("Keyframes: %zu", obj.animation.keyframes.size());

            if (ImGui::DragFloat("Clip Length", &obj.animation.clipLength, 0.05f, 0.1f, 120.0f, "%.2f")) {
                obj.animation.clipLength = std::max(0.1f, obj.animation.clipLength);
                changed = true;
            }
            if (ImGui::DragFloat("Play Speed", &obj.animation.playSpeed, 0.05f, 0.05f, 8.0f, "%.2f")) {
                obj.animation.playSpeed = std::max(0.05f, obj.animation.playSpeed);
                changed = true;
            }
            if (ImGui::Checkbox("Loop", &obj.animation.loop)) {
                changed = true;
            }
            if (ImGui::Checkbox("Apply On Scrub", &obj.animation.applyOnScrub)) {
                changed = true;
            }

            if (ImGui::Button("Clear Keyframes")) {
                obj.animation.keyframes.clear();
                changed = true;
            }

            ImGui::Unindent(10.0f);
            ImGui::PopID();
        }
        if (removeAnimation) {
            obj.hasAnimation = false;
            obj.animation = AnimationComponent{};
            changed = true;
        }
        if (changed) {
            projectManager.currentProject.hasUnsavedChanges = true;
        }
        ImGui::PopStyleColor();
    }

    if (obj.hasSkeletalAnimation) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.35f, 0.4f, 0.6f, 1.0f));
        bool removeSkeletal = false;
        bool changed = false;
        auto header = drawComponentHeader("Skeletal", "Skeletal", &obj.skeletal.enabled, true, [&]() {
            if (ImGui::MenuItem("Remove")) {
                removeSkeletal = true;
            }
        });
        if (header.enabledChanged) {
            changed = true;
        }
        if (header.open) {
            ImGui::PushID("Skeletal");
            ImGui::Indent(10.0f);

            ModelSceneData sceneData;
            std::string err;
            bool hasClips = !obj.meshPath.empty() && getModelLoader().loadModelScene(obj.meshPath, sceneData, err) &&
                            !sceneData.animations.empty();
            if (hasClips) {
                std::vector<const char*> clipNames;
                clipNames.reserve(sceneData.animations.size());
                for (const auto& clip : sceneData.animations) {
                    clipNames.push_back(clip.name.c_str());
                }
                int clipIndex = std::clamp(obj.skeletal.clipIndex, 0, (int)clipNames.size() - 1);
                if (ImGui::Combo("Clip", &clipIndex, clipNames.data(), (int)clipNames.size())) {
                    obj.skeletal.clipIndex = clipIndex;
                    obj.skeletal.time = 0.0f;
                    changed = true;
                }
            } else {
                ImGui::TextDisabled("No animation clips found");
            }

            if (ImGui::Checkbox("Use Animation", &obj.skeletal.useAnimation)) {
                changed = true;
            }
            if (ImGui::DragFloat("Play Speed", &obj.skeletal.playSpeed, 0.05f, 0.05f, 8.0f, "%.2f")) {
                obj.skeletal.playSpeed = std::max(0.05f, obj.skeletal.playSpeed);
                changed = true;
            }
            if (ImGui::Checkbox("Loop", &obj.skeletal.loop)) {
                changed = true;
            }
            if (ImGui::Checkbox("GPU Skinning", &obj.skeletal.useGpuSkinning)) {
                changed = true;
            }
            if (ImGui::Checkbox("Allow CPU Fallback", &obj.skeletal.allowCpuFallback)) {
                changed = true;
            }
            if (ImGui::DragInt("Max Bones", &obj.skeletal.maxBones, 1, 8, 256)) {
                obj.skeletal.maxBones = std::clamp(obj.skeletal.maxBones, 8, 256);
                changed = true;
            }

            ImGui::Unindent(10.0f);
            ImGui::PopID();
        }
        if (removeSkeletal) {
            obj.hasSkeletalAnimation = false;
            obj.skeletal = SkeletalAnimationComponent{};
            changed = true;
        }
        if (changed) {
            projectManager.currentProject.hasUnsavedChanges = true;
        }
        ImGui::PopStyleColor();
    }

    if (obj.hasReverbZone) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.4f, 0.45f, 0.6f, 1.0f));
        bool removeReverbZone = false;
        bool changed = false;
        auto header = drawComponentHeader("Reverb Zone", "ReverbZone", &obj.reverbZone.enabled, true, [&]() {
            if (ImGui::MenuItem("Remove")) {
                removeReverbZone = true;
            }
        });
        if (header.enabledChanged) {
            changed = true;
        }
        if (header.open) {
            ImGui::PushID("ReverbZone");
            ImGui::Indent(10.0f);
            auto& zone = obj.reverbZone;

            const char* presets[] = { "Room", "Living Room", "Hall", "Forest", "Custom" };
            int presetIndex = static_cast<int>(zone.preset);
            if (ImGui::Combo("Preset", &presetIndex, presets, IM_ARRAYSIZE(presets))) {
                ApplyReverbPreset(zone, static_cast<ReverbPreset>(presetIndex));
                changed = true;
            }

            const char* shapes[] = { "Box", "Sphere" };
            int shapeIndex = static_cast<int>(zone.shape);
            if (ImGui::Combo("Shape", &shapeIndex, shapes, IM_ARRAYSIZE(shapes))) {
                zone.shape = static_cast<ReverbZoneShape>(shapeIndex);
                changed = true;
            }

            if (zone.shape == ReverbZoneShape::Sphere) {
                if (ImGui::DragFloat("Radius", &zone.radius, 0.1f, 0.1f, 500.0f, "%.2f")) {
                    zone.radius = std::max(0.1f, zone.radius);
                    changed = true;
                }
            } else {
                if (ImGui::DragFloat3("Box Size", &zone.boxSize.x, 0.1f, 0.1f, 500.0f, "%.2f")) {
                    zone.boxSize = glm::max(zone.boxSize, glm::vec3(0.1f));
                    changed = true;
                }
            }

            if (zone.shape == ReverbZoneShape::Sphere) {
                if (ImGui::DragFloat("Min Distance", &zone.minDistance, 0.05f, 0.0f, 500.0f, "%.2f")) {
                    zone.minDistance = std::max(0.0f, zone.minDistance);
                    changed = true;
                }
                if (ImGui::DragFloat("Max Distance", &zone.maxDistance, 0.05f, zone.minDistance + 0.1f, 1000.0f, "%.2f")) {
                    zone.maxDistance = std::max(zone.maxDistance, zone.minDistance + 0.1f);
                    changed = true;
                }
            } else if (ImGui::DragFloat("Blend Distance", &zone.blendDistance, 0.05f, 0.0f, 50.0f, "%.2f")) {
                zone.blendDistance = std::max(0.0f, zone.blendDistance);
                changed = true;
            }

            if (ImGui::SliderFloat("Room", &zone.room, -10000.0f, 0.0f, "%.0f dB")) {
                zone.preset = ReverbPreset::Custom;
                changed = true;
            }
            if (ImGui::SliderFloat("Room HF", &zone.roomHF, -10000.0f, 0.0f, "%.0f dB")) {
                zone.preset = ReverbPreset::Custom;
                changed = true;
            }
            if (ImGui::SliderFloat("Room LF", &zone.roomLF, -10000.0f, 0.0f, "%.0f dB")) {
                zone.preset = ReverbPreset::Custom;
                changed = true;
            }
            if (ImGui::SliderFloat("Decay Time", &zone.decayTime, 0.1f, 20.0f, "%.2f s")) {
                zone.preset = ReverbPreset::Custom;
                changed = true;
            }
            if (ImGui::SliderFloat("Decay HF Ratio", &zone.decayHFRatio, 0.1f, 2.0f, "%.2f")) {
                zone.preset = ReverbPreset::Custom;
                changed = true;
            }
            if (ImGui::SliderFloat("Reflections", &zone.reflections, -10000.0f, 1000.0f, "%.0f dB")) {
                zone.preset = ReverbPreset::Custom;
                changed = true;
            }
            if (ImGui::SliderFloat("Reflections Delay", &zone.reflectionsDelay, 0.0f, 0.1f, "%.3f s")) {
                zone.preset = ReverbPreset::Custom;
                changed = true;
            }
            if (ImGui::SliderFloat("Reverb", &zone.reverb, -10000.0f, 2000.0f, "%.0f dB")) {
                zone.preset = ReverbPreset::Custom;
                changed = true;
            }
            if (ImGui::SliderFloat("Reverb Delay", &zone.reverbDelay, 0.0f, 0.1f, "%.3f s")) {
                zone.preset = ReverbPreset::Custom;
                changed = true;
            }
            if (ImGui::SliderFloat("HF Reference", &zone.hfReference, 1000.0f, 20000.0f, "%.0f Hz")) {
                zone.preset = ReverbPreset::Custom;
                changed = true;
            }
            if (ImGui::SliderFloat("LF Reference", &zone.lfReference, 20.0f, 1000.0f, "%.0f Hz")) {
                zone.preset = ReverbPreset::Custom;
                changed = true;
            }
            if (ImGui::SliderFloat("Room Rolloff Factor", &zone.roomRolloffFactor, 0.0f, 10.0f, "%.2f")) {
                zone.preset = ReverbPreset::Custom;
                changed = true;
            }
            if (ImGui::SliderFloat("Diffusion", &zone.diffusion, 0.0f, 100.0f, "%.0f")) {
                zone.preset = ReverbPreset::Custom;
                changed = true;
            }
            if (ImGui::SliderFloat("Density", &zone.density, 0.0f, 100.0f, "%.0f")) {
                zone.preset = ReverbPreset::Custom;
                changed = true;
            }

            ImGui::Unindent(10.0f);
            ImGui::PopID();
        }
        if (removeReverbZone) {
            obj.hasReverbZone = false;
            changed = true;
        }
        if (changed) {
            projectManager.currentProject.hasUnsavedChanges = true;
        }
        ImGui::PopStyleColor();
    }

    if (obj.hasCamera) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.45f, 0.35f, 0.65f, 1.0f));
        bool changed = false;
        bool removeCamera = false;
        auto header = drawComponentHeader("Camera", "Camera", nullptr, true, [&]() {
            if (ImGui::MenuItem("Remove")) {
                removeCamera = true;
            }
        });
        if (header.open) {
            ImGui::PushID("Camera");
            ImGui::Indent(10.0f);
            const char* cameraTypes[] = { "Scene", "Player" };
            int camType = static_cast<int>(obj.camera.type);
            if (ImGui::Combo("Type", &camType, cameraTypes, IM_ARRAYSIZE(cameraTypes))) {
                obj.camera.type = static_cast<SceneCameraType>(camType);
                changed = true;
            }

            if (ImGui::SliderFloat("FOV", &obj.camera.fov, 20.0f, 120.0f, "%.0f deg")) {
                changed = true;
            }
            if (ImGui::DragFloat("Near Clip", &obj.camera.nearClip, 0.01f, 0.01f, obj.camera.farClip - 0.01f, "%.3f")) {
                obj.camera.nearClip = std::max(0.01f, std::min(obj.camera.nearClip, obj.camera.farClip - 0.01f));
                changed = true;
            }
            if (ImGui::DragFloat("Far Clip", &obj.camera.farClip, 0.1f, obj.camera.nearClip + 0.05f, 1000.0f, "%.1f")) {
                obj.camera.farClip = std::max(obj.camera.nearClip + 0.05f, obj.camera.farClip);
                changed = true;
            }
            if (ImGui::Checkbox("Apply Post Processing", &obj.camera.applyPostFX)) {
                changed = true;
            }
            if (ImGui::Checkbox("2D Camera", &obj.camera.use2D)) {
                changed = true;
            }
            if (obj.camera.use2D) {
                if (ImGui::DragFloat("Pixels Per Unit", &obj.camera.pixelsPerUnit, 1.0f, 1.0f, 2000.0f, "%.1f")) {
                    obj.camera.pixelsPerUnit = std::max(1.0f, obj.camera.pixelsPerUnit);
                    changed = true;
                }
                ImGui::TextDisabled("Uses X/Y for 2D view; Z stays fixed.");
            }
            ImGui::Unindent(10.0f);
            ImGui::PopID();
        }
        if (removeCamera) {
            obj.hasCamera = false;
            UpdateLegacyTypeFromComponents(obj);
            changed = true;
        }
        if (changed) {
            projectManager.currentProject.hasUnsavedChanges = true;
        }
        ImGui::PopStyleColor();
    }

    if (obj.hasCameraFollow2D) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.35f, 0.55f, 0.4f, 1.0f));
        bool changed = false;
        bool removeFollow = false;
        auto header = drawComponentHeader("Camera Follow 2D", "CameraFollow2D", &obj.cameraFollow2D.enabled, true, [&]() {
            if (ImGui::MenuItem("Remove")) {
                removeFollow = true;
            }
        });
        if (header.enabledChanged) {
            changed = true;
        }
        if (header.open) {
            ImGui::PushID("CameraFollow2D");
            ImGui::Indent(10.0f);
            if (!obj.hasCamera) {
                ImGui::TextDisabled("Requires a Camera component.");
            }

            std::string targetLabel = "None";
            if (obj.cameraFollow2D.targetId >= 0) {
                auto it = std::find_if(sceneObjects.begin(), sceneObjects.end(),
                    [&](const SceneObject& o) { return o.id == obj.cameraFollow2D.targetId; });
                if (it != sceneObjects.end()) {
                    targetLabel = it->name + " (" + std::to_string(it->id) + ")";
                }
            }
            if (ImGui::BeginCombo("Target", targetLabel.c_str())) {
                if (ImGui::Selectable("None", obj.cameraFollow2D.targetId < 0)) {
                    obj.cameraFollow2D.targetId = -1;
                    changed = true;
                }
                for (const auto& candidate : sceneObjects) {
                    if (candidate.id == obj.id) continue;
                    std::string label = candidate.name + " (" + std::to_string(candidate.id) + ")";
                    bool selected = (candidate.id == obj.cameraFollow2D.targetId);
                    if (ImGui::Selectable(label.c_str(), selected)) {
                        obj.cameraFollow2D.targetId = candidate.id;
                        changed = true;
                    }
                    if (selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
            if (ImGui::Button("Use Selected")) {
                if (selectedObjectId >= 0 && selectedObjectId != obj.id) {
                    obj.cameraFollow2D.targetId = selectedObjectId;
                    changed = true;
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Clear Target")) {
                obj.cameraFollow2D.targetId = -1;
                changed = true;
            }
            if (ImGui::DragFloat2("Offset", &obj.cameraFollow2D.offset.x, 0.1f)) {
                changed = true;
            }
            if (ImGui::DragFloat("Smooth Time", &obj.cameraFollow2D.smoothTime, 0.01f, 0.0f, 10.0f, "%.2f s")) {
                obj.cameraFollow2D.smoothTime = std::max(0.0f, obj.cameraFollow2D.smoothTime);
                changed = true;
            }
            ImGui::Unindent(10.0f);
            ImGui::PopID();
        }
        if (removeFollow) {
            obj.hasCameraFollow2D = false;
            changed = true;
        }
        if (changed) {
            projectManager.currentProject.hasUnsavedChanges = true;
        }
        ImGui::PopStyleColor();
    }

    if (obj.hasPostFX) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.25f, 0.55f, 0.6f, 1.0f));
        bool changed = false;
        bool removePostFx = false;
        auto header = drawComponentHeader("Post Processing", "PostFX", &obj.postFx.enabled, true, [&]() {
            if (ImGui::MenuItem("Remove")) {
                removePostFx = true;
            }
        });
        if (header.enabledChanged) {
            changed = true;
        }
        if (header.open) {
            ImGui::PushID("PostFX");
            ImGui::Indent(10.0f);

            ImGui::Separator();
            ImGui::TextDisabled("Bloom");
            if (ImGui::Checkbox("Bloom Enabled", &obj.postFx.bloomEnabled)) {
                changed = true;
            }
            ImGui::BeginDisabled(!obj.postFx.bloomEnabled);
            if (ImGui::SliderFloat("Threshold", &obj.postFx.bloomThreshold, 0.0f, 3.0f, "%.2f")) {
                changed = true;
            }
            if (ImGui::SliderFloat("Intensity##Bloom", &obj.postFx.bloomIntensity, 0.0f, 3.0f, "%.2f")) {
                changed = true;
            }
            if (ImGui::SliderFloat("Spread", &obj.postFx.bloomRadius, 0.5f, 3.5f, "%.2f")) {
                changed = true;
            }
            ImGui::EndDisabled();

            ImGui::Separator();
            ImGui::TextDisabled("Color Adjustments");
            if (ImGui::Checkbox("Enable Color Adjust", &obj.postFx.colorAdjustEnabled)) {
                changed = true;
            }
            ImGui::BeginDisabled(!obj.postFx.colorAdjustEnabled);
            if (ImGui::SliderFloat("Exposure (EV)", &obj.postFx.exposure, -5.0f, 5.0f, "%.2f")) {
                changed = true;
            }
            if (ImGui::SliderFloat("Contrast", &obj.postFx.contrast, 0.0f, 2.5f, "%.2f")) {
                changed = true;
            }
            if (ImGui::SliderFloat("Saturation", &obj.postFx.saturation, 0.0f, 2.5f, "%.2f")) {
                changed = true;
            }
            if (ImGui::ColorEdit3("Color Filter", &obj.postFx.colorFilter.x)) {
                changed = true;
            }
            ImGui::EndDisabled();

            ImGui::Separator();
            ImGui::TextDisabled("Motion Blur");
            if (ImGui::Checkbox("Enable Motion Blur", &obj.postFx.motionBlurEnabled)) {
                changed = true;
            }
            ImGui::BeginDisabled(!obj.postFx.motionBlurEnabled);
            if (ImGui::SliderFloat("Strength", &obj.postFx.motionBlurStrength, 0.0f, 0.95f, "%.2f")) {
                changed = true;
            }
            ImGui::EndDisabled();

            ImGui::Separator();
            ImGui::TextDisabled("Vignette");
            if (ImGui::Checkbox("Enable Vignette", &obj.postFx.vignetteEnabled)) {
                changed = true;
            }
            ImGui::BeginDisabled(!obj.postFx.vignetteEnabled);
            if (ImGui::SliderFloat("Intensity##Vignette", &obj.postFx.vignetteIntensity, 0.0f, 1.5f, "%.2f")) {
                changed = true;
            }
            if (ImGui::SliderFloat("Smoothness", &obj.postFx.vignetteSmoothness, 0.05f, 1.0f, "%.2f")) {
                changed = true;
            }
            ImGui::EndDisabled();

            ImGui::Separator();
            ImGui::TextDisabled("Ambient Occlusion");
            if (ImGui::Checkbox("Enable AO", &obj.postFx.ambientOcclusionEnabled)) {
                changed = true;
            }
            ImGui::BeginDisabled(!obj.postFx.ambientOcclusionEnabled);
            if (ImGui::SliderFloat("AO Radius", &obj.postFx.aoRadius, 0.0005f, 0.01f, "%.4f")) {
                changed = true;
            }
            if (ImGui::SliderFloat("AO Strength", &obj.postFx.aoStrength, 0.0f, 2.0f, "%.2f")) {
                changed = true;
            }
            ImGui::EndDisabled();

            ImGui::Separator();
            ImGui::TextDisabled("Chromatic Aberration");
            if (ImGui::Checkbox("Enable Chromatic", &obj.postFx.chromaticAberrationEnabled)) {
                changed = true;
            }
            ImGui::BeginDisabled(!obj.postFx.chromaticAberrationEnabled);
            if (ImGui::SliderFloat("Fringe Amount", &obj.postFx.chromaticAmount, 0.0f, 0.01f, "%.4f")) {
                changed = true;
            }
            ImGui::EndDisabled();

            ImGui::TextDisabled("Nodes stack in hierarchy order; latest node overrides previous settings.");
            ImGui::TextDisabled("Wireframe/line mode auto-disables post effects.");
            ImGui::Unindent(10.0f);
            ImGui::PopID();
        }
        if (removePostFx) {
            obj.hasPostFX = false;
            UpdateLegacyTypeFromComponents(obj);
            changed = true;
        }
        if (changed) {
            projectManager.currentProject.hasUnsavedChanges = true;
        }
        ImGui::PopStyleColor();
    }

    if (obj.hasRenderer) {
        ImGui::Spacing();
        bool rendererChanged = false;
        bool removeRenderer = false;
        auto rendererHeader = drawComponentHeader("Renderer", "Renderer", nullptr, true, [&]() {
            if (ImGui::MenuItem("Remove")) {
                removeRenderer = true;
            }
        });
        if (rendererHeader.open) {
            ImGui::Indent(10.0f);
            const char* renderLabel = "None";
            switch (obj.renderType) {
                case RenderType::Cube: renderLabel = "Cube"; break;
                case RenderType::Sphere: renderLabel = "Sphere"; break;
                case RenderType::Capsule: renderLabel = "Capsule"; break;
                case RenderType::OBJMesh: renderLabel = "OBJ Mesh"; break;
                case RenderType::Model: renderLabel = "Model"; break;
                case RenderType::Mirror: renderLabel = "Mirror"; break;
                case RenderType::Plane: renderLabel = "Plane"; break;
                case RenderType::Torus: renderLabel = "Torus"; break;
                case RenderType::Sprite: renderLabel = "Sprite"; break;
                case RenderType::None: break;
            }
            ImGui::Text("Render Type: %s", renderLabel);

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::TextDisabled("Material");
            ImGui::PushID("Material");

            auto textureField = [&](const char* label, const char* idSuffix, std::string& path) {
                bool changed = false;
                ImGui::PushID(idSuffix);
                ImGui::TextUnformatted(label);
                ImGui::SetNextItemWidth(-160);
                char buf[512] = {};
                std::snprintf(buf, sizeof(buf), "%s", path.c_str());
                if (ImGui::InputText("##Path", buf, sizeof(buf))) {
                    path = buf;
                    changed = true;
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Clear")) {
                    path.clear();
                    changed = true;
                }
                ImGui::SameLine();
                bool canUseTex = !fileBrowser.selectedFile.empty() && fs::exists(fileBrowser.selectedFile) &&
                                 fileBrowser.isTextureFile(fs::directory_entry(fileBrowser.selectedFile));
                ImGui::BeginDisabled(!canUseTex);
                std::string btnLabel = std::string("Use Selection##") + idSuffix;
                if (ImGui::SmallButton(btnLabel.c_str())) {
                    path = fileBrowser.selectedFile.string();
                    changed = true;
                }
                ImGui::EndDisabled();
                ImGui::PopID();
                return changed;
            };

            bool materialChanged = false;

            ImGui::TextDisabled("Surface Inputs");
            if (ImGui::ColorEdit3("Base Color", &obj.material.color.x)) {
                materialChanged = true;
            }

            float metallic = obj.material.specularStrength;
            if (ImGui::SliderFloat("Metallic", &metallic, 0.0f, 1.0f)) {
                obj.material.specularStrength = metallic;
                materialChanged = true;
            }

            float smoothness = obj.material.shininess / 256.0f;
            if (ImGui::SliderFloat("Smoothness", &smoothness, 0.0f, 1.0f)) {
                smoothness = std::clamp(smoothness, 0.0f, 1.0f);
                obj.material.shininess = smoothness * 256.0f;
                materialChanged = true;
            }

            if (ImGui::SliderFloat("Ambient Light", &obj.material.ambientStrength, 0.0f, 1.0f)) {
                materialChanged = true;
            }
            if (ImGui::SliderFloat("Detail Mix", &obj.material.textureMix, 0.0f, 1.0f)) {
                materialChanged = true;
            }

            ImGui::Spacing();
            ImGui::TextDisabled("Maps");
            materialChanged |= textureField("Base Map", "ObjAlbedo", obj.albedoTexturePath);
            if (ImGui::Checkbox("Use Detail Map", &obj.useOverlay)) {
                materialChanged = true;
            }
            materialChanged |= textureField("Detail Map", "ObjOverlay", obj.overlayTexturePath);
            materialChanged |= textureField("Normal Map", "ObjNormal", obj.normalMapPath);

            ImGui::Spacing();
            ImGui::TextDisabled("Shader");
            auto shaderField = [&](const char* label, const char* idSuffix, std::string& path) {
                bool changed = false;
                ImGui::PushID(idSuffix);
                ImGui::TextUnformatted(label);
                ImGui::SetNextItemWidth(-160);
                char buf[512] = {};
                std::snprintf(buf, sizeof(buf), "%s", path.c_str());
                if (ImGui::InputText("##Path", buf, sizeof(buf))) {
                    path = buf;
                    changed = true;
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Clear")) {
                    path.clear();
                    changed = true;
                }
                bool selectionIsShader = false;
                if (!fileBrowser.selectedFile.empty() && fs::exists(fileBrowser.selectedFile)) {
                    selectionIsShader = fileBrowser.getFileCategory(fs::directory_entry(fileBrowser.selectedFile)) == FileCategory::Shader;
                }
                ImGui::SameLine();
                ImGui::BeginDisabled(!selectionIsShader);
                std::string btn = std::string("Use Selection##") + idSuffix;
                if (ImGui::SmallButton(btn.c_str())) {
                    path = fileBrowser.selectedFile.string();
                    changed = true;
                }
                ImGui::EndDisabled();
                ImGui::PopID();
                return changed;
            };
            materialChanged |= shaderField("Vertex Shader", "ObjVert", obj.vertexShaderPath);
            materialChanged |= shaderField("Fragment Shader", "ObjFrag", obj.fragmentShaderPath);

            ImGui::BeginDisabled(obj.vertexShaderPath.empty() && obj.fragmentShaderPath.empty());
            if (ImGui::Button("Reload Shader")) {
                renderer.forceReloadShader(obj.vertexShaderPath, obj.fragmentShaderPath);
            }
            ImGui::EndDisabled();

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::TextDisabled("Material Asset");
            ImGui::TextDisabled("%s", obj.materialPath.empty() ? "Unsaved Material" : fs::path(obj.materialPath).filename().string().c_str());

            char matPathBuf[512] = {};
            std::snprintf(matPathBuf, sizeof(matPathBuf), "%s", obj.materialPath.c_str());
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputText("##MaterialPath", matPathBuf, sizeof(matPathBuf))) {
                obj.materialPath = matPathBuf;
                materialChanged = true;
            }

            bool hasMatPath = obj.materialPath.size() > 0;
            ImGui::BeginDisabled(!hasMatPath);
            if (ImGui::Button("Save Material")) {
                saveMaterialToFile(obj);
            }
            ImGui::SameLine();
            if (ImGui::Button("Reload Material")) {
                loadMaterialFromFile(obj);
            }
            ImGui::EndDisabled();

            ImGui::SameLine();
            ImGui::BeginDisabled(!browserHasMaterial);
            if (ImGui::Button("Load Selected")) {
                obj.materialPath = selectedMaterialPath.string();
                loadMaterialFromFile(obj);
                materialChanged = true;
            }
            ImGui::EndDisabled();

            ImGui::Spacing();
            ImGui::TextDisabled("Material Slots");
            for (size_t slot = 0; slot < obj.additionalMaterialPaths.size(); ++slot) {
                ImGui::PushID(static_cast<int>(slot));
                char slotBuf[512] = {};
                std::snprintf(slotBuf, sizeof(slotBuf), "%s", obj.additionalMaterialPaths[slot].c_str());
                ImGui::SetNextItemWidth(-140);
                if (ImGui::InputText("##AdditionalMat", slotBuf, sizeof(slotBuf))) {
                    obj.additionalMaterialPaths[slot] = slotBuf;
                    materialChanged = true;
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Use Selection / Blender")) {
                    if (!fileBrowser.selectedFile.empty() && fs::exists(fileBrowser.selectedFile)) {
                        fs::directory_entry entry(fileBrowser.selectedFile);
                        if (fileBrowser.getFileCategory(entry) == FileCategory::Material) {
                            obj.additionalMaterialPaths[slot] = entry.path().string();
                            materialChanged = true;
                        }
                    }
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Remove")) {
                    obj.additionalMaterialPaths.erase(obj.additionalMaterialPaths.begin() + static_cast<long>(slot));
                    materialChanged = true;
                    ImGui::PopID();
                    break;
                }
                ImGui::PopID();
            }
            if (ImGui::SmallButton("Add Material Slot")) {
                obj.additionalMaterialPaths.push_back("");
                materialChanged = true;
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::TextDisabled("Preview");
            ImGui::SetNextItemWidth(180.0f);
            ImGui::SliderFloat("Size##ObjectMaterialPreview", &objectMaterialPreviewScale, 0.6f, 1.8f, "%.2fx");
            drawMaterialPreview(
                "ObjectMaterialPreview",
                obj.material,
                obj.albedoTexturePath,
                obj.overlayTexturePath,
                obj.normalMapPath,
                obj.useOverlay,
                obj.vertexShaderPath,
                obj.fragmentShaderPath,
                objectMaterialPreviewScale,
                1002
            );

            if (materialChanged) {
                projectManager.currentProject.hasUnsavedChanges = true;
            }

            ImGui::PopID();

            if (obj.renderType == RenderType::OBJMesh) {
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::TextDisabled("Mesh Info");

                const auto* meshInfo = g_objLoader.getMeshInfo(obj.meshId);
                if (meshInfo) {
                    ImGui::Text("Source File:");
                    ImGui::TextDisabled("%s", fs::path(meshInfo->path).filename().string().c_str());

                    ImGui::Spacing();

                    ImGui::Text("Vertices: %d", meshInfo->vertexCount);
                    ImGui::Text("Faces: %d", meshInfo->faceCount);
                    ImGui::Text("Has Normals: %s", meshInfo->hasNormals ? "Yes" : "No");
                    ImGui::Text("Has UVs: %s", meshInfo->hasTexCoords ? "Yes" : "No");

                    ImGui::Spacing();

                    if (ImGui::Button("Reload Mesh", ImVec2(-1, 0))) {
                        std::string errMsg;
                        int newId = g_objLoader.loadOBJ(obj.meshPath, errMsg);
                        if (newId >= 0) {
                            obj.meshId = newId;
                            addConsoleMessage("Reloaded mesh: " + obj.name, ConsoleMessageType::Success);
                        } else {
                            addConsoleMessage("Failed to reload: " + errMsg, ConsoleMessageType::Error);
                        }
                    }
                } else {
                    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Mesh data not found!");
                    ImGui::TextDisabled("Path: %s", obj.meshPath.c_str());

                    if (ImGui::Button("Try Reload", ImVec2(-1, 0))) {
                        std::string errMsg;
                        int newId = g_objLoader.loadOBJ(obj.meshPath, errMsg);
                        if (newId >= 0) {
                            obj.meshId = newId;
                            addConsoleMessage("Reloaded mesh: " + obj.name, ConsoleMessageType::Success);
                        } else {
                            addConsoleMessage("Failed to reload: " + errMsg, ConsoleMessageType::Error);
                        }
                    }
                }
            }

            if (obj.renderType == RenderType::Model) {
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::TextDisabled("Model Info");

                const auto* meshInfo = getModelLoader().getMeshInfo(obj.meshId);
                if (meshInfo) {
                    ImGui::Text("Source File:");
                    ImGui::TextDisabled("%s", fs::path(meshInfo->path).filename().string().c_str());

                    ImGui::Spacing();

                    ImGui::Text("Vertices: %d", meshInfo->vertexCount);
                    ImGui::Text("Faces: %d", meshInfo->faceCount);
                    ImGui::Text("Has Normals: %s", meshInfo->hasNormals ? "Yes" : "No");
                    ImGui::Text("Has UVs: %s", meshInfo->hasTexCoords ? "Yes" : "No");

                    ImGui::Spacing();

                    if (ImGui::Button("Reload Model", ImVec2(-1, 0))) {
                        bool reloaded = false;
                        if (obj.meshSourceIndex >= 0) {
                            ModelSceneData sceneData;
                            std::string err;
                            if (getModelLoader().loadModelScene(obj.meshPath, sceneData, err)) {
                                int sourceIndex = obj.meshSourceIndex;
                                if (sourceIndex >= 0 && sourceIndex < (int)sceneData.meshIndices.size()) {
                                    obj.meshId = sceneData.meshIndices[sourceIndex];
                                    reloaded = true;
                                }
                            }
                        }
                        if (!reloaded) {
                            ModelLoadResult result = getModelLoader().loadModel(obj.meshPath);
                            if (result.success) {
                                obj.meshId = result.meshIndex;
                                reloaded = true;
                            } else {
                                addConsoleMessage("Failed to reload: " + result.errorMessage, ConsoleMessageType::Error);
                            }
                        }
                        if (reloaded) {
                            addConsoleMessage("Reloaded model: " + obj.name, ConsoleMessageType::Success);
                        }
                    }
                } else {
                    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Model data not found!");
                    ImGui::TextDisabled("Path: %s", obj.meshPath.c_str());

                    if (ImGui::Button("Try Reload", ImVec2(-1, 0))) {
                        bool reloaded = false;
                        if (obj.meshSourceIndex >= 0) {
                            ModelSceneData sceneData;
                            std::string err;
                            if (getModelLoader().loadModelScene(obj.meshPath, sceneData, err)) {
                                int sourceIndex = obj.meshSourceIndex;
                                if (sourceIndex >= 0 && sourceIndex < (int)sceneData.meshIndices.size()) {
                                    obj.meshId = sceneData.meshIndices[sourceIndex];
                                    reloaded = true;
                                }
                            }
                        }
                        if (!reloaded) {
                            ModelLoadResult result = getModelLoader().loadModel(obj.meshPath);
                            if (result.success) {
                                obj.meshId = result.meshIndex;
                                reloaded = true;
                            } else {
                                addConsoleMessage("Failed to reload: " + result.errorMessage, ConsoleMessageType::Error);
                            }
                        }
                        if (reloaded) {
                            addConsoleMessage("Reloaded model: " + obj.name, ConsoleMessageType::Success);
                        }
                    }
                }
            }

            ImGui::Unindent(10.0f);
        }
        if (removeRenderer) {
            obj.hasRenderer = false;
            obj.renderType = RenderType::None;
            UpdateLegacyTypeFromComponents(obj);
            rendererChanged = true;
        }
        if (rendererChanged) {
            projectManager.currentProject.hasUnsavedChanges = true;
        }
    }

    if (obj.hasLight) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.5f, 0.45f, 0.2f, 1.0f));
        bool changed = false;
        bool removeLight = false;
        auto header = drawComponentHeader("Light", "Light", &obj.light.enabled, true, [&]() {
            if (ImGui::MenuItem("Remove")) {
                removeLight = true;
            }
        });
        if (header.enabledChanged) {
            changed = true;
        }
        if (header.open) {
            ImGui::PushID("Light");
            ImGui::Indent(10.0f);

            int currentType = static_cast<int>(obj.light.type);
            const char* typeLabels[] = { "Directional", "Point", "Spot", "Area" };
            if (ImGui::Combo("Type", &currentType, typeLabels, IM_ARRAYSIZE(typeLabels))) {
                obj.light.type = (currentType == 0 ? LightType::Directional :
                                  currentType == 1 ? LightType::Point :
                                  currentType == 2 ? LightType::Spot : LightType::Area);
                // Reset sensible defaults when type changes
                if (obj.light.type == LightType::Directional) {
                    obj.light.intensity = 1.0f;
                } else if (obj.light.type == LightType::Point) {
                    obj.light.range = 12.0f;
                    obj.light.intensity = 2.0f;
                } else if (obj.light.type == LightType::Spot) {
                    obj.light.range = 15.0f;
                    obj.light.intensity = 2.5f;
                    obj.light.innerAngle = 15.0f;
                    obj.light.outerAngle = 25.0f;
                } else if (obj.light.type == LightType::Area) {
                    obj.light.range = 10.0f;
                    obj.light.intensity = 3.0f;
                    obj.light.size = glm::vec2(2.0f, 2.0f);
                    obj.light.edgeFade = 0.2f;
                }
                changed = true;
            }

            if (ImGui::ColorEdit3("Color", &obj.light.color.x)) {
                changed = true;
            }
            if (ImGui::SliderFloat("Intensity", &obj.light.intensity, 0.0f, 10.0f)) {
                changed = true;
            }
            if (obj.light.type != LightType::Directional) {
                if (ImGui::SliderFloat("Range", &obj.light.range, 0.0f, 50.0f)) {
                    changed = true;
                }
            }

            if (obj.light.type == LightType::Spot) {
                if (ImGui::SliderFloat("Inner Angle", &obj.light.innerAngle, 1.0f, 90.0f)) {
                    changed = true;
                }
                if (ImGui::SliderFloat("Outer Angle", &obj.light.outerAngle, obj.light.innerAngle, 120.0f)) {
                    changed = true;
                }
            }

            if (obj.light.type == LightType::Area) {
                if (ImGui::DragFloat2("Size", &obj.light.size.x, 0.05f, 0.1f, 10.0f)) {
                    changed = true;
                }
                if (ImGui::SliderFloat("Edge Softness", &obj.light.edgeFade, 0.0f, 1.0f, "%.2f")) {
                    changed = true;
                }
            }

            if (obj.light.type != LightType::Directional) {
                if (ImGui::Checkbox("Cast Shadows", &obj.light.castShadows)) {
                    changed = true;
                }
                if (obj.light.castShadows) {
                    if (ImGui::Checkbox("Soft Shadows", &obj.light.softShadows)) {
                        changed = true;
                    }
                    if (ImGui::SliderFloat("Shadow Bias", &obj.light.shadowBias, 0.0001f, 0.20f, "%.4f")) {
                        changed = true;
                    }
                    if (obj.light.softShadows) {
                        if (ImGui::SliderFloat("Shadow Softness", &obj.light.shadowSoftness, 0.001f, 0.20f, "%.3f")) {
                            changed = true;
                        }
                    }
                }
            }

            ImGui::Unindent(10.0f);
            ImGui::PopID();
        }
        if (removeLight) {
            obj.hasLight = false;
            UpdateLegacyTypeFromComponents(obj);
            changed = true;
        }
        if (changed) {
            projectManager.currentProject.hasUnsavedChanges = true;
        }
        ImGui::PopStyleColor();
    }

    bool scriptsChanged = false;
    int scriptToRemove = -1;
    auto isNativeScriptLanguage = [](ScriptLanguage language) {
        return language == ScriptLanguage::Cpp || language == ScriptLanguage::C;
    };
    auto inferNativeLanguageFromPath = [](const fs::path& path) {
        std::string ext = path.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return (ext == ".c") ? ScriptLanguage::C : ScriptLanguage::Cpp;
    };
    auto isNativeScriptSourcePath = [](const fs::path& path) {
        std::string ext = path.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return ext == ".c" || ext == ".cc" || ext == ".cpp" || ext == ".cxx";
    };

    for (size_t i = 0; i < obj.scripts.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        ScriptComponent& sc = obj.scripts[i];

        std::string headerLabel = "Script";
        if (sc.language == ScriptLanguage::CSharp && !sc.managedType.empty()) {
            headerLabel = sc.managedType;
        } else if (!sc.path.empty()) {
            headerLabel = fs::path(sc.path).filename().string();
        }
        std::string scriptId = "ScriptComponent" + std::to_string(i);
        auto header = drawComponentHeader(headerLabel.c_str(), scriptId.c_str(), &sc.enabled, true, [&]() {
            bool nativeScript = isNativeScriptLanguage(sc.language);
            if (ImGui::MenuItem("Compile", nullptr, false, nativeScript ? !sc.path.empty() : true)) {
                if (nativeScript) {
                    compileScriptFile(sc.path);
                } else {
                    compileManagedScripts();
                }
            }
            if (ImGui::MenuItem("Remove")) {
                scriptToRemove = static_cast<int>(i);
            }
        });
        if (header.enabledChanged) {
            scriptsChanged = true;
        }

        if (scriptToRemove == static_cast<int>(i)) {
            ImGui::PopID();
            continue;
        }

        if (header.open) {
            const char* languageLabels[] = {"C++", "C", "C#"};
            int languageIndex = 0;
            if (sc.language == ScriptLanguage::C) {
                languageIndex = 1;
            } else if (sc.language == ScriptLanguage::CSharp) {
                languageIndex = 2;
            }
            ImGui::TextDisabled("Language");
            ImGui::SetNextItemWidth(140);
            if (ImGui::Combo("##ScriptLanguage", &languageIndex, languageLabels, IM_ARRAYSIZE(languageLabels))) {
                if (languageIndex == 2) {
                    sc.language = ScriptLanguage::CSharp;
                } else if (languageIndex == 1) {
                    sc.language = ScriptLanguage::C;
                } else {
                    sc.language = ScriptLanguage::Cpp;
                }
                scriptsChanged = true;
                if (sc.language == ScriptLanguage::CSharp) {
                    std::string stem = fs::path(sc.path).stem().string();
                    if (sc.managedType.empty() || sc.managedType == stem) {
                        if (auto inferred = InferManagedTypeFromFile(sc.path)) {
                            sc.managedType = *inferred;
                        } else if (!stem.empty()) {
                            sc.managedType = stem;
                        }
                    }
                }
            }

            char pathBuf[512] = {};
            std::snprintf(pathBuf, sizeof(pathBuf), "%s", sc.path.c_str());
            ImGui::TextDisabled(sc.language == ScriptLanguage::CSharp ? "Assembly Path" : "Path");
            ImGui::SetNextItemWidth(-140);
            if (ImGui::InputText("##ScriptPath", pathBuf, sizeof(pathBuf))) {
                sc.path = pathBuf;
                scriptsChanged = true;
                if (sc.language == ScriptLanguage::CSharp) {
                    std::string stem = fs::path(sc.path).stem().string();
                    if (sc.managedType.empty() || sc.managedType == stem) {
                        if (auto inferred = InferManagedTypeFromFile(sc.path)) {
                            sc.managedType = *inferred;
                        } else if (!stem.empty()) {
                            sc.managedType = stem;
                        }
                    }
                } else if (!sc.path.empty()) {
                    sc.language = inferNativeLanguageFromPath(sc.path);
                }
            }

            ImGui::SameLine();
            if (ImGui::SmallButton("Use Selection")) {
                if (!fileBrowser.selectedFile.empty()) {
                    fs::directory_entry entry(fileBrowser.selectedFile);
                    bool useSelection = false;
                    if (isNativeScriptLanguage(sc.language)) {
                        useSelection = isNativeScriptSourcePath(entry.path());
                    } else {
                        std::string ext = entry.path().extension().string();
                        useSelection = (ext == ".dll" || ext == ".cs");
                    }
                    if (useSelection) {
                        sc.path = entry.path().string();
                        scriptsChanged = true;
                        if (isNativeScriptLanguage(sc.language)) {
                            sc.language = inferNativeLanguageFromPath(entry.path());
                        } else if (sc.language == ScriptLanguage::CSharp) {
                            std::string stem = entry.path().stem().string();
                            if (sc.managedType.empty() || sc.managedType == stem) {
                                if (auto inferred = InferManagedTypeFromFile(entry.path())) {
                                    sc.managedType = *inferred;
                                } else if (!stem.empty()) {
                                    sc.managedType = stem;
                                }
                            }
                        }
                    }
                }
            }

            if (sc.language == ScriptLanguage::CSharp) {
                char typeBuf[256] = {};
                std::snprintf(typeBuf, sizeof(typeBuf), "%s", sc.managedType.c_str());
                ImGui::TextDisabled("Type");
                ImGui::SetNextItemWidth(-140);
                if (ImGui::InputText("##ScriptType", typeBuf, sizeof(typeBuf))) {
                    sc.managedType = typeBuf;
                    scriptsChanged = true;
                }
            }

            if (!sc.path.empty()) {
                ScriptContext ctx;
                ctx.engine = this;
                ctx.object = &obj;
                ctx.script = &sc;
                // Scope script inspector to avoid shared ImGui IDs across objects or multiple instances
                std::string inspectorId = "ScriptInspector##" + std::to_string(obj.id) + sc.path;
                if (isNativeScriptLanguage(sc.language)) {
                    fs::path binary = resolveScriptBinary(sc.path);
                    if (binary.empty() && !sc.lastBinaryPath.empty()) {
                        fs::path fallback = sc.lastBinaryPath;
                        if (fs::exists(fallback)) {
                            binary = fallback;
                        }
                    }
                    sc.lastBinaryPath = binary.string();
                    ScriptRuntime::InspectorFn inspector = scriptRuntime.getInspector(binary);
                    if (inspector) {
                        ImGui::Separator();
                        ImGui::TextDisabled("Inspector (from script)");
                        ImGui::PushID(inspectorId.c_str());
                        inspector(ctx);
                        ImGui::PopID();
                        ctx.SaveAutoSettings();
                    } else if (!scriptRuntime.getLastError().empty()) {
                        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.6f, 1.0f), "Inspector load failed");
                        ImGui::TextWrapped("%s", scriptRuntime.getLastError().c_str());
                    } else {
                        ImGui::TextDisabled("No inspector exported (Script_OnInspector)");
                    }
                } else {
                    fs::path assembly = resolveManagedAssembly(sc.path);
                    if (assembly.empty() && !sc.lastBinaryPath.empty()) {
                        fs::path fallback = sc.lastBinaryPath;
                        if (fs::exists(fallback)) {
                            assembly = fallback;
                        }
                    }
                    sc.lastBinaryPath = assembly.string();
                    bool hasInspector = managedRuntime.hasInspector(assembly, sc.managedType);
                    if (hasInspector) {
                        ImGui::Separator();
                        ImGui::TextDisabled("Inspector (from managed script)");
                        ImGui::PushID(inspectorId.c_str());
                        bool ranInspector = managedRuntime.invokeInspector(assembly, sc.managedType, ctx);
                        ImGui::PopID();
                        if (ranInspector) {
                            ctx.SaveAutoSettings();
                        } else if (!managedRuntime.getLastError().empty()) {
                            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.6f, 1.0f), "Inspector load failed");
                            ImGui::TextWrapped("%s", managedRuntime.getLastError().c_str());
                        }
                    } else if (!managedRuntime.getLastError().empty()) {
                        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.6f, 1.0f), "Inspector load failed");
                        ImGui::TextWrapped("%s", managedRuntime.getLastError().c_str());
                    } else {
                        ImGui::TextDisabled("No inspector exported (Script_OnInspector)");
                    }
                }
            }

            constexpr bool showScriptSettings = false;
            if (showScriptSettings) {
                ImGui::TextDisabled("Settings");
                for (size_t s = 0; s < sc.settings.size(); ++s) {
                    ImGui::PushID(static_cast<int>(s));
                    char keyBuf[128] = {};
                    char valBuf[256] = {};
                    std::snprintf(keyBuf, sizeof(keyBuf), "%s", sc.settings[s].key.c_str());
                    std::snprintf(valBuf, sizeof(valBuf), "%s", sc.settings[s].value.c_str());
                    auto isBoolString = [](const std::string& v, bool& out) {
                        if (v == "1" || v == "true" || v == "True") { out = true; return true; }
                        if (v == "0" || v == "false" || v == "False") { out = false; return true; }
                        return false;
                    };
                    auto isNumberString = [](const std::string& v, float& out) {
                        if (v.empty()) return false;
                        char* end = nullptr;
                        out = std::strtof(v.c_str(), &end);
                        return end && *end == '\0';
                    };
                    bool boolVal = false;
                    bool hasBool = isBoolString(sc.settings[s].value, boolVal);
                    float numVal = 0.0f;
                    bool hasNumber = isNumberString(sc.settings[s].value, numVal);
                    ImGui::SetNextItemWidth(140);
                    if (ImGui::InputText("##Key", keyBuf, sizeof(keyBuf))) {
                        sc.settings[s].key = keyBuf;
                        scriptsChanged = true;
                    }
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(-200);
                    if (hasBool) {
                        if (ImGui::Checkbox("##BoolVal", &boolVal)) {
                            sc.settings[s].value = boolVal ? "1" : "0";
                            scriptsChanged = true;
                        }
                    } else if (hasNumber) {
                        if (ImGui::InputFloat("##NumVal", &numVal, 0.0f, 0.0f, "%.4f")) {
                            sc.settings[s].value = std::to_string(numVal);
                            scriptsChanged = true;
                        }
                    } else {
                        if (ImGui::InputText("##Value", valBuf, sizeof(valBuf))) {
                            sc.settings[s].value = valBuf;
                            scriptsChanged = true;
                        }
                    }
                    ImGui::SameLine();
                    ImGui::BeginDisabled(hasBool);
                    if (ImGui::SmallButton("As Bool")) {
                        sc.settings[s].value = (!sc.settings[s].value.empty() && sc.settings[s].value != "0" && sc.settings[s].value != "false") ? "1" : "0";
                        scriptsChanged = true;
                    }
                    ImGui::EndDisabled();
                    ImGui::SameLine();
                    ImGui::BeginDisabled(hasNumber);
                    if (ImGui::SmallButton("As Number")) {
                        float parsed = 0.0f;
                        if (!isNumberString(sc.settings[s].value, parsed)) parsed = 0.0f;
                        sc.settings[s].value = std::to_string(parsed);
                        scriptsChanged = true;
                    }
                    ImGui::EndDisabled();
                    ImGui::SameLine();
                    if (ImGui::SmallButton("X")) {
                        sc.settings.erase(sc.settings.begin() + static_cast<long>(s));
                        scriptsChanged = true;
                        ImGui::PopID();
                        break;
                    }
                    ImGui::PopID();
                }

                if (ImGui::SmallButton("Add Setting")) {
                    sc.settings.push_back(ScriptSetting{"", ""});
                    scriptsChanged = true;
                }
            }
        }

        ImGui::PopID();
    }

    if (scriptToRemove >= 0 && scriptToRemove < static_cast<int>(obj.scripts.size())) {
        obj.scripts.erase(obj.scripts.begin() + scriptToRemove);
        scriptsChanged = true;
    }

    if (obj.hasRenderer) {
        std::string matName = obj.materialPath.empty()
            ? "In-Built Material"
            : fs::path(obj.materialPath).filename().string();
        std::string matLine = "Material: " + matName;
        float textWidth = ImGui::CalcTextSize(matLine.c_str()).x;
        float availWidth = ImGui::GetContentRegionAvail().x;
        float x = ImGui::GetCursorPosX() + std::max(0.0f, availWidth - textWidth);
        ImGui::SetCursorPosX(x);
        ImGui::TextDisabled("%s", matLine.c_str());
    }

    ImGui::Spacing();
    ImGui::Separator();
    bool componentChanged = false;
    ImGui::PushID("AddComponent");
    if (ImGui::Button("Add Component", ImVec2(-1, 0))) {
        ImGui::OpenPopup("AddComponentPopup");
    }
    ImGui::SetNextWindowSize(ImVec2(360.0f, 420.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopup("AddComponentPopup")) {
        bool isUIType = isUIObject(obj);
        auto applyUiDefaults = [](SceneObject& target, UIElementType type) {
            target.ui.type = type;
            switch (type) {
                case UIElementType::Canvas:
                    target.ui.label = "Canvas";
                    target.ui.size = glm::vec2(600.0f, 400.0f);
                    break;
                case UIElementType::Image:
                    target.ui.label = "Image";
                    target.ui.size = glm::vec2(200.0f, 200.0f);
                    break;
                case UIElementType::Slider:
                    target.ui.label = "Slider";
                    target.ui.size = glm::vec2(240.0f, 32.0f);
                    break;
                case UIElementType::Button:
                    target.ui.label = "Button";
                    target.ui.size = glm::vec2(160.0f, 40.0f);
                    break;
                case UIElementType::Text:
                    target.ui.label = "Text";
                    target.ui.size = glm::vec2(240.0f, 32.0f);
                    break;
                case UIElementType::Sprite2D:
                    target.ui.label = "Sprite2D";
                    target.ui.size = glm::vec2(128.0f, 128.0f);
                    break;
                case UIElementType::None:
                    break;
            }
        };

        static char componentFilter[96] = "";
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##ComponentFilter", "Search components...", componentFilter, sizeof(componentFilter));

        std::string filterLower = componentFilter;
        std::transform(filterLower.begin(), filterLower.end(), filterLower.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        struct ComponentEntry {
            std::string path;
            bool enabled = true;
            std::function<void()> action;
        };
        std::vector<ComponentEntry> entries;
        auto addEntry = [&](const std::string& path, bool enabled, const std::function<void()>& action) {
            entries.push_back({path, enabled, action});
        };

        addEntry("Physics/Rigidbody 3D", !obj.hasRigidbody && !isUIType, [&]() {
            obj.hasRigidbody = true;
            obj.rigidbody = RigidbodyComponent{};
            componentChanged = true;
        });
        addEntry("Physics/Rigidbody 2D", !obj.hasRigidbody2D && isUIType, [&]() {
            obj.hasRigidbody2D = true;
            obj.rigidbody2D = Rigidbody2DComponent{};
            componentChanged = true;
        });
        addEntry("Physics/Collider 2D", !obj.hasCollider2D && isUIType, [&]() {
            obj.hasCollider2D = true;
            obj.collider2D = Collider2DComponent{};
            obj.collider2D.boxSize = glm::max(obj.ui.size, glm::vec2(1.0f));
            componentChanged = true;
        });
        addEntry("Physics/Parallax Layer 2D", !obj.hasParallaxLayer2D && isUIType, [&]() {
            obj.hasParallaxLayer2D = true;
            obj.parallaxLayer2D = ParallaxLayer2DComponent{};
            componentChanged = true;
        });
        addEntry("Gameplay/Player Controller", !obj.hasPlayerController, [&]() {
            obj.hasPlayerController = true;
            obj.playerController = PlayerControllerComponent{};
            obj.hasCollider = true;
            obj.collider.type = ColliderType::Capsule;
            obj.collider.boxSize = glm::vec3(obj.playerController.radius * 2.0f, obj.playerController.height, obj.playerController.radius * 2.0f);
            obj.collider.convex = true;
            obj.hasRigidbody = true;
            obj.rigidbody.enabled = true;
            obj.rigidbody.useGravity = true;
            obj.rigidbody.isKinematic = false;
            obj.scale = glm::vec3(obj.playerController.radius * 2.0f, obj.playerController.height, obj.playerController.radius * 2.0f);
            syncLocalTransform(obj);
            componentChanged = true;
        });
        addEntry("Audio/Audio Source", !obj.hasAudioSource, [&]() {
            obj.hasAudioSource = true;
            obj.audioSource = AudioSourceComponent{};
            componentChanged = true;
        });
        addEntry("Audio/Reverb Zone", !obj.hasReverbZone && !isUIType, [&]() {
            obj.hasReverbZone = true;
            obj.reverbZone = ReverbZoneComponent{};
            obj.reverbZone.boxSize = glm::max(obj.scale, glm::vec3(1.0f));
            componentChanged = true;
        });
        addEntry("Animation/Animation", !obj.hasAnimation, [&]() {
            obj.hasAnimation = true;
            obj.animation = AnimationComponent{};
            showAnimationWindow = true;
            animationTargetId = obj.id;
            componentChanged = true;
        });
        addEntry("Rendering/Camera", !obj.hasCamera, [&]() {
            obj.hasCamera = true;
            obj.camera = CameraComponent{};
            UpdateLegacyTypeFromComponents(obj);
            componentChanged = true;
        });
        addEntry("Rendering/Camera Follow 2D", !obj.hasCameraFollow2D && obj.hasCamera, [&]() {
            obj.hasCameraFollow2D = true;
            obj.cameraFollow2D = CameraFollow2DComponent{};
            componentChanged = true;
        });
        addEntry("Rendering/Post Processing", !obj.hasPostFX, [&]() {
            obj.hasPostFX = true;
            obj.postFx = PostFXSettings{};
            UpdateLegacyTypeFromComponents(obj);
            componentChanged = true;
        });
        addEntry("Lights/Directional", !obj.hasLight, [&]() {
            obj.hasLight = true;
            obj.light = LightComponent{};
            obj.light.type = LightType::Directional;
            UpdateLegacyTypeFromComponents(obj);
            componentChanged = true;
        });
        addEntry("Lights/Point", !obj.hasLight, [&]() {
            obj.hasLight = true;
            obj.light = LightComponent{};
            obj.light.type = LightType::Point;
            obj.light.range = 12.0f;
            obj.light.intensity = 2.0f;
            UpdateLegacyTypeFromComponents(obj);
            componentChanged = true;
        });
        addEntry("Lights/Spot", !obj.hasLight, [&]() {
            obj.hasLight = true;
            obj.light = LightComponent{};
            obj.light.type = LightType::Spot;
            obj.light.range = 15.0f;
            obj.light.intensity = 2.5f;
            UpdateLegacyTypeFromComponents(obj);
            componentChanged = true;
        });
        addEntry("Lights/Area", !obj.hasLight, [&]() {
            obj.hasLight = true;
            obj.light = LightComponent{};
            obj.light.type = LightType::Area;
            obj.light.range = 10.0f;
            obj.light.intensity = 3.0f;
            obj.light.size = glm::vec2(2.0f, 2.0f);
            UpdateLegacyTypeFromComponents(obj);
            componentChanged = true;
        });
        addEntry("Renderer/Cube", true, [&]() {
            obj.hasRenderer = true;
            obj.renderType = RenderType::Cube;
            UpdateLegacyTypeFromComponents(obj);
            componentChanged = true;
        });
        addEntry("Renderer/Sphere", true, [&]() {
            obj.hasRenderer = true;
            obj.renderType = RenderType::Sphere;
            UpdateLegacyTypeFromComponents(obj);
            componentChanged = true;
        });
        addEntry("Renderer/Capsule", true, [&]() {
            obj.hasRenderer = true;
            obj.renderType = RenderType::Capsule;
            UpdateLegacyTypeFromComponents(obj);
            componentChanged = true;
        });
        addEntry("Renderer/Plane", true, [&]() {
            obj.hasRenderer = true;
            obj.renderType = RenderType::Plane;
            obj.scale = glm::vec3(2.0f, 2.0f, 0.05f);
            syncLocalTransform(obj);
            UpdateLegacyTypeFromComponents(obj);
            componentChanged = true;
        });
        addEntry("Renderer/Torus", true, [&]() {
            obj.hasRenderer = true;
            obj.renderType = RenderType::Torus;
            UpdateLegacyTypeFromComponents(obj);
            componentChanged = true;
        });
        addEntry("Renderer/Sprite (Quad)", true, [&]() {
            obj.hasRenderer = true;
            obj.renderType = RenderType::Sprite;
            obj.scale = glm::vec3(1.0f, 1.0f, 0.05f);
            obj.material.ambientStrength = 1.0f;
            syncLocalTransform(obj);
            UpdateLegacyTypeFromComponents(obj);
            componentChanged = true;
        });
        addEntry("Renderer/Mirror", true, [&]() {
            obj.hasRenderer = true;
            obj.renderType = RenderType::Mirror;
            obj.useOverlay = true;
            obj.material.textureMix = 1.0f;
            obj.material.color = glm::vec3(1.0f);
            obj.scale = glm::vec3(2.0f, 2.0f, 0.05f);
            syncLocalTransform(obj);
            UpdateLegacyTypeFromComponents(obj);
            componentChanged = true;
        });
        addEntry("UI/Canvas", true, [&]() {
            obj.hasUI = true;
            applyUiDefaults(obj, UIElementType::Canvas);
            UpdateLegacyTypeFromComponents(obj);
            componentChanged = true;
        });
        addEntry("UI/Image", true, [&]() {
            obj.hasUI = true;
            applyUiDefaults(obj, UIElementType::Image);
            UpdateLegacyTypeFromComponents(obj);
            componentChanged = true;
        });
        addEntry("UI/Slider", true, [&]() {
            obj.hasUI = true;
            applyUiDefaults(obj, UIElementType::Slider);
            UpdateLegacyTypeFromComponents(obj);
            componentChanged = true;
        });
        addEntry("UI/Button", true, [&]() {
            obj.hasUI = true;
            applyUiDefaults(obj, UIElementType::Button);
            UpdateLegacyTypeFromComponents(obj);
            componentChanged = true;
        });
        addEntry("UI/Text", true, [&]() {
            obj.hasUI = true;
            applyUiDefaults(obj, UIElementType::Text);
            UpdateLegacyTypeFromComponents(obj);
            componentChanged = true;
        });
        addEntry("UI/Sprite2D", true, [&]() {
            obj.hasUI = true;
            applyUiDefaults(obj, UIElementType::Sprite2D);
            UpdateLegacyTypeFromComponents(obj);
            componentChanged = true;
        });
        addEntry("Collider/Box Collider", !obj.hasCollider, [&]() {
            obj.hasCollider = true;
            obj.collider = ColliderComponent{};
            obj.collider.boxSize = glm::max(obj.scale, glm::vec3(0.01f));
            componentChanged = true;
        });
        addEntry("Collider/Mesh Collider (Triangle)", !obj.hasCollider, [&]() {
            obj.hasCollider = true;
            obj.collider = ColliderComponent{};
            obj.collider.type = ColliderType::Mesh;
            obj.collider.convex = false;
            componentChanged = true;
        });
        addEntry("Collider/Mesh Collider (Convex)", !obj.hasCollider, [&]() {
            obj.hasCollider = true;
            obj.collider = ColliderComponent{};
            obj.collider.type = ColliderType::ConvexMesh;
            obj.collider.convex = true;
            componentChanged = true;
        });
        addEntry("Scripting/Empty Script Component", true, [&]() {
            obj.scripts.push_back(ScriptComponent{});
            scriptsChanged = true;
            componentChanged = true;
        });

        static std::vector<fs::path> cachedScriptSources;
        static std::vector<fs::path> cachedScriptBinaries;
        static std::string cachedScriptRoot;
        static double cachedScriptRefresh = 0.0;
        double now = glfwGetTime();
        std::string projectRoot = projectManager.currentProject.projectPath.string();
        if (cachedScriptRoot != projectRoot || now - cachedScriptRefresh > 2.0) {
            cachedScriptRoot = projectRoot;
            cachedScriptRefresh = now;
            cachedScriptSources.clear();
            cachedScriptBinaries.clear();

            fs::path scriptsDir = projectManager.currentProject.projectPath / "Scripts";
            fs::path outDir;
            ScriptBuildConfig config;
            std::string error;
            fs::path cfgPath = resolveScriptsConfigPath(projectManager.currentProject);
            if (scriptCompiler.loadConfig(cfgPath, config, error)) {
                scriptsDir = config.scriptsDir;
                outDir = config.outDir;
            }

            std::error_code ec;
            if (fs::exists(scriptsDir, ec)) {
                for (auto it = fs::recursive_directory_iterator(scriptsDir, ec);
                     it != fs::recursive_directory_iterator(); ++it) {
                    if (it->is_directory()) continue;
                    std::string ext = it->path().extension().string();
                    std::transform(ext.begin(), ext.end(), ext.begin(),
                                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                    if (ext == ".cpp" || ext == ".c" || ext == ".cs") {
                        cachedScriptSources.push_back(it->path());
                    }
                }
            }

            if (!outDir.empty() && fs::exists(outDir, ec)) {
                for (auto it = fs::recursive_directory_iterator(outDir, ec);
                     it != fs::recursive_directory_iterator(); ++it) {
                    if (it->is_directory()) continue;
                    std::string ext = it->path().extension().string();
                    std::transform(ext.begin(), ext.end(), ext.begin(),
                                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
#ifdef _WIN32
                    if (ext == ".dll") {
                        cachedScriptBinaries.push_back(it->path());
                    }
#elif __APPLE__
                    if (ext == ".dylib") {
                        cachedScriptBinaries.push_back(it->path());
                    }
#else
                    if (ext == ".so") {
                        cachedScriptBinaries.push_back(it->path());
                    }
#endif
                }
            }
        }

        std::unordered_map<std::string, fs::path> sourceByStem;
        sourceByStem.reserve(cachedScriptSources.size());
        for (const auto& path : cachedScriptSources) {
            sourceByStem.emplace(path.stem().string(), path);
        }

        for (const auto& path : cachedScriptSources) {
            std::string label = "Scripting/" + path.filename().string();
            addEntry(label, true, [&, path]() {
                ScriptComponent sc;
                std::string ext = path.extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (ext == ".cs") {
                    sc.language = ScriptLanguage::CSharp;
                } else if (ext == ".c") {
                    sc.language = ScriptLanguage::C;
                } else {
                    sc.language = ScriptLanguage::Cpp;
                }
                sc.path = path.string();
                if (sc.language == ScriptLanguage::CSharp) {
                    sc.managedType = path.stem().string();
                }
                obj.scripts.push_back(std::move(sc));
                scriptsChanged = true;
                componentChanged = true;
            });
        }

        for (const auto& bin : cachedScriptBinaries) {
            std::string stem = bin.stem().string();
            if (sourceByStem.find(stem) != sourceByStem.end()) {
                continue;
            }
            std::string label = "Scripting Compiled/" + stem;
            addEntry(label, true, [&, bin]() {
                ScriptComponent sc;
                sc.language = ScriptLanguage::Cpp;
                sc.path = bin.string();
                sc.lastBinaryPath = bin.string();
                obj.scripts.push_back(std::move(sc));
                scriptsChanged = true;
                componentChanged = true;
            });
        }

        auto toLower = [](const std::string& value) {
            std::string lowered = value;
            std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return lowered;
        };
        auto splitPath = [](const std::string& path) {
            size_t slash = path.find('/');
            if (slash == std::string::npos) {
                return std::pair<std::string, std::string>("Misc", path);
            }
            return std::pair<std::string, std::string>(path.substr(0, slash), path.substr(slash + 1));
        };

        std::vector<const ComponentEntry*> filteredEntries;
        filteredEntries.reserve(entries.size());
        for (const auto& entry : entries) {
            if (!filterLower.empty()) {
                std::string loweredPath = toLower(entry.path);
                if (loweredPath.find(filterLower) == std::string::npos) {
                    continue;
                }
            }
            filteredEntries.push_back(&entry);
        }

        std::vector<std::string> categoryOrder;
        std::unordered_map<std::string, std::vector<const ComponentEntry*>> categorizedEntries;
        categorizedEntries.reserve(filteredEntries.size());
        for (const ComponentEntry* entry : filteredEntries) {
            auto split = splitPath(entry->path);
            auto itCat = categorizedEntries.find(split.first);
            if (itCat == categorizedEntries.end()) {
                categoryOrder.push_back(split.first);
            }
            categorizedEntries[split.first].push_back(entry);
        }

        ImGui::Spacing();
        ImGui::TextDisabled("%s", filterLower.empty() ? "Browse categories" : "Search results");
        ImVec2 listSize(ImGui::GetContentRegionAvail().x, 260.0f);
        if (ImGui::BeginChild("ComponentList", listSize, true)) {
            if (filteredEntries.empty()) {
                ImGui::TextDisabled("No components match the filter.");
            } else if (!filterLower.empty()) {
                for (const ComponentEntry* entry : filteredEntries) {
                    if (!entry->enabled) {
                        ImGui::BeginDisabled();
                    }
                    if (ImGui::Selectable(entry->path.c_str())) {
                        entry->action();
                        ImGui::CloseCurrentPopup();
                    }
                    if (!entry->enabled) {
                        ImGui::EndDisabled();
                    }
                }
            } else {
                for (const auto& category : categoryOrder) {
                    auto itCat = categorizedEntries.find(category);
                    if (itCat == categorizedEntries.end()) continue;
                    if (ImGui::BeginMenu(category.c_str())) {
                        for (const ComponentEntry* entry : itCat->second) {
                            auto split = splitPath(entry->path);
                            if (!entry->enabled) {
                                ImGui::BeginDisabled();
                            }
                            if (ImGui::MenuItem(split.second.c_str())) {
                                entry->action();
                                ImGui::CloseCurrentPopup();
                            }
                            if (!entry->enabled) {
                                ImGui::EndDisabled();
                            }
                        }
                        ImGui::EndMenu();
                    }
                }
            }
        }
        ImGui::EndChild();
        ImGui::EndPopup();
    }
    ImGui::PopID();

    if (scriptsChanged) {
        projectManager.currentProject.hasUnsavedChanges = true;
    }
    if (componentChanged) {
        projectManager.currentProject.hasUnsavedChanges = true;
    }

    if (browserHasAudio) {
        ImGui::Spacing();
        renderAudioAssetPanel("Audio Clip (File Browser)", &obj);
    }
    if (browserHasMaterial) {
        ImGui::Spacing();
        renderMaterialAssetPanel("Material Asset (File Browser)", true);
    }

    ImGui::PopID(); // object scope
    ImGui::End();
}

#pragma endregion

#pragma region Console Panel
void Engine::renderConsolePanel() {
    const ImGuiViewport* mainViewport = ImGui::GetMainViewport();
    if (!mainViewport) return;

    ImVec2 anchorMin = mainViewport->WorkPos;
    ImVec2 anchorMax = ImVec2(mainViewport->WorkPos.x + mainViewport->WorkSize.x,
                              mainViewport->WorkPos.y + mainViewport->WorkSize.y);
    if (ImGuiWindow* viewportWindow = ImGui::FindWindowByName("Viewport")) {
        if (viewportWindow->WasActive) {
            anchorMin = viewportWindow->InnerRect.Min;
            anchorMax = viewportWindow->InnerRect.Max;
        }
    }

    static bool consolePopoutOpen = false;
    static bool autoScroll = true;

    const float margin = 12.0f;
    const ImVec2 tabSize(96.0f, 30.0f);
    ImVec2 tabPos(anchorMax.x - tabSize.x - margin, anchorMax.y - tabSize.y - margin);
    tabPos.x = ImMax(anchorMin.x + 4.0f, tabPos.x);
    tabPos.y = ImMax(anchorMin.y + 4.0f, tabPos.y);

    ImGuiWindowFlags tabFlags = ImGuiWindowFlags_NoDecoration |
                                ImGuiWindowFlags_NoDocking |
                                ImGuiWindowFlags_NoSavedSettings |
                                ImGuiWindowFlags_NoMove |
                                ImGuiWindowFlags_NoResize |
                                ImGuiWindowFlags_NoNav |
                                ImGuiWindowFlags_NoFocusOnAppearing;
    ImGui::SetNextWindowPos(tabPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(tabSize, ImGuiCond_Always);
    ImGui::SetNextWindowViewport(mainViewport->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 7.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    if (ImGui::Begin("ConsoleTab##ViewportMini", nullptr, tabFlags)) {
        const bool wasOpen = consolePopoutOpen;
        if (wasOpen) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        }
        if (ImGui::Button("Console", ImGui::GetContentRegionAvail())) {
            consolePopoutOpen = !consolePopoutOpen;
        }
        if (wasOpen) {
            ImGui::PopStyleColor(3);
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(3);

    if (!consolePopoutOpen) {
        return;
    }

    ImVec2 miniSize(ImMin(560.0f, anchorMax.x - anchorMin.x - margin * 2.0f),
                    ImMin(320.0f, anchorMax.y - anchorMin.y - tabSize.y - margin * 3.0f));
    if (miniSize.x < 260.0f || miniSize.y < 140.0f) {
        return;
    }

    ImVec2 miniPos(anchorMax.x - miniSize.x - margin, tabPos.y - miniSize.y - 8.0f);
    miniPos.y = ImMax(anchorMin.y + margin, miniPos.y);

    bool keepOpen = consolePopoutOpen;
    ImGuiWindowFlags miniFlags = ImGuiWindowFlags_NoDocking |
                                 ImGuiWindowFlags_NoSavedSettings |
                                 ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_NoCollapse;
    ImGui::SetNextWindowPos(miniPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(miniSize, ImGuiCond_Always);
    ImGui::SetNextWindowViewport(mainViewport->ID);
    if (!ImGui::Begin("Console##MiniLogPanel", &keepOpen, miniFlags)) {
        ImGui::End();
        consolePopoutOpen = keepOpen;
        return;
    }
    consolePopoutOpen = keepOpen;

    bool settingsChanged = false;
    if (ImGui::Button("Clear")) {
        consoleLog.clear();
        latestErrorMessage.clear();
        latestErrorTimestamp.clear();
    }

    ImGui::SameLine();
    ImGui::Checkbox("Auto-scroll", &autoScroll);
    ImGui::SameLine();
    if (ImGui::Checkbox("Wrap Text", &consoleWrapText)) {
        settingsChanged = true;
    }

    ImGui::Separator();

    Texture* infoLogo = renderer.getTexture("Resources/Engine-Root/Info Logo.png");
    Texture* warningLogo = renderer.getTexture("Resources/Engine-Root/Warning Logo.png");
    Texture* errorLogo = renderer.getTexture("Resources/Engine-Root/Error Logo.png");
    Texture* successLogo = renderer.getTexture("Resources/Engine-Root/Modu-Logo.png");

    auto typeLabel = [](ConsoleMessageType type) -> const char* {
        switch (type) {
            case ConsoleMessageType::Warning: return "Warning";
            case ConsoleMessageType::Error: return "Error";
            case ConsoleMessageType::Success: return "Success";
            case ConsoleMessageType::Info:
            default:
                return "Info";
        }
    };

    auto typeColor = [](ConsoleMessageType type) -> ImVec4 {
        switch (type) {
            case ConsoleMessageType::Warning: return ImVec4(1.0f, 0.84f, 0.35f, 1.0f);
            case ConsoleMessageType::Error: return ImVec4(1.0f, 0.46f, 0.46f, 1.0f);
            case ConsoleMessageType::Success: return ImVec4(0.50f, 0.95f, 0.55f, 1.0f);
            case ConsoleMessageType::Info:
            default:
                return ImVec4(0.65f, 0.84f, 1.0f, 1.0f);
        }
    };

    auto rowColor = [](ConsoleMessageType type) -> ImU32 {
        switch (type) {
            case ConsoleMessageType::Warning: return IM_COL32(74, 60, 20, 195);
            case ConsoleMessageType::Error: return IM_COL32(88, 28, 28, 205);
            case ConsoleMessageType::Success: return IM_COL32(20, 70, 30, 190);
            case ConsoleMessageType::Info:
            default:
                return IM_COL32(30, 52, 78, 185);
        }
    };

    auto typeIcon = [&](ConsoleMessageType type) -> Texture* {
        switch (type) {
            case ConsoleMessageType::Warning: return warningLogo;
            case ConsoleMessageType::Error: return errorLogo;
            case ConsoleMessageType::Success: return successLogo;
            case ConsoleMessageType::Info:
            default:
                return infoLogo;
        }
    };

    ImGui::BeginChild("ConsoleOutput", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);

    const bool shouldScroll = autoScroll && (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 2.0f);

    if (consoleLog.empty()) {
        ImGui::TextDisabled("No console messages yet.");
    } else {
        for (size_t i = 0; i < consoleLog.size(); ++i) {
            const ConsoleEntry& log = consoleLog[i];
            ImGui::PushID(static_cast<int>(i));

            Texture* icon = typeIcon(log.type);
            const bool hasIcon = icon && icon->GetID();
            const float iconSize = 16.0f;
            float rowWidth = ImGui::GetContentRegionAvail().x;
            const float textLeft = hasIcon ? 36.0f : 10.0f;
            const std::string header = "[" + log.timestamp + "] " + typeLabel(log.type);

            const float minTextWidth = 32.0f;
            float textWidth = ImMax(minTextWidth, rowWidth - textLeft - 10.0f);

            ImVec2 headerSize = ImGui::CalcTextSize(header.c_str(), nullptr, false, FLT_MAX);
            ImVec2 msgSize = ImGui::CalcTextSize(log.message.c_str(), nullptr, false,
                                                 consoleWrapText ? textWidth : FLT_MAX);
            if (!consoleWrapText) {
                float requiredWidth = textLeft + ImMax(headerSize.x, msgSize.x) + 10.0f;
                rowWidth = ImMax(rowWidth, requiredWidth);
                textWidth = ImMax(minTextWidth, rowWidth - textLeft - 10.0f);
                msgSize = ImGui::CalcTextSize(log.message.c_str(), nullptr, false, FLT_MAX);
            } else {
                msgSize = ImGui::CalcTextSize(log.message.c_str(), nullptr, false, textWidth);
            }

            const float topPad = 4.0f;
            const float bottomPad = 4.0f;
            const float lineGap = 2.0f;
            const float rowHeight = ImMax(26.0f, topPad + ImGui::GetTextLineHeight() + lineGap + msgSize.y + bottomPad);

            const ImVec2 rowMin = ImGui::GetCursorScreenPos();
            ImGui::Dummy(ImVec2(rowWidth, rowHeight));
            const ImVec2 rowMax = ImVec2(rowMin.x + rowWidth, rowMin.y + rowHeight);

            ImDrawList* draw = ImGui::GetWindowDrawList();
            draw->AddRectFilled(rowMin, rowMax, rowColor(log.type), 6.0f);
            draw->PushClipRect(rowMin, rowMax, true);

            if (hasIcon) {
                const ImVec2 iconMin(rowMin.x + 8.0f, rowMin.y + (rowHeight - iconSize) * 0.5f);
                const ImVec2 iconMax(iconMin.x + iconSize, iconMin.y + iconSize);
                draw->AddImage((ImTextureID)(intptr_t)icon->GetID(), iconMin, iconMax,
                               ImVec2(0, 1), ImVec2(1, 0), IM_COL32_WHITE);
            }

            const float textX = rowMin.x + textLeft;
            draw->AddText(ImVec2(textX, rowMin.y + topPad),
                          ImGui::ColorConvertFloat4ToU32(typeColor(log.type)),
                          header.c_str());
            draw->AddText(ImGui::GetFont(), ImGui::GetFontSize(),
                          ImVec2(textX, rowMin.y + topPad + ImGui::GetTextLineHeight() + lineGap),
                          ImGui::GetColorU32(ImGuiCol_Text),
                          log.message.c_str(), nullptr,
                          consoleWrapText ? textWidth : 0.0f);

            draw->PopClipRect();
            ImGui::Dummy(ImVec2(0.0f, 3.0f));
            ImGui::PopID();
        }
    }

    if (shouldScroll) {
        ImGui::SetScrollHereY(1.0f);
    }

    ImGui::EndChild();
    if (settingsChanged) {
        saveEditorUserSettings();
    }

    ImGui::End();
}

void Engine::renderLatestErrorBar() {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    if (!viewport) return;

    const bool hasError = !latestErrorMessage.empty();
    Texture* errorLogo = renderer.getTexture("Resources/Engine-Root/Error Logo.png");
    Texture* infoLogo = renderer.getTexture("Resources/Engine-Root/Info Logo.png");
    Texture* icon = hasError ? errorLogo : infoLogo;

    std::string bodyText = hasError
        ? ("[" + latestErrorTimestamp + "] " + latestErrorMessage)
        : "No errors yet.";

    const float margin = 8.0f;
    const float barHeight = 30.0f;
    ImVec2 barPos(viewport->WorkPos.x + margin,
                  viewport->WorkPos.y + viewport->WorkSize.y - barHeight - margin);
    ImVec2 barSize(viewport->WorkSize.x - margin * 2.0f, barHeight);
    if (barSize.x <= 40.0f || barSize.y <= 8.0f) return;

    ImGuiWindowFlags barFlags = ImGuiWindowFlags_NoDecoration |
                                ImGuiWindowFlags_NoDocking |
                                ImGuiWindowFlags_NoSavedSettings |
                                ImGuiWindowFlags_NoMove |
                                ImGuiWindowFlags_NoNav |
                                ImGuiWindowFlags_NoBringToFrontOnFocus |
                                ImGuiWindowFlags_NoInputs;

    ImGui::SetNextWindowPos(barPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(barSize, ImGuiCond_Always);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 6.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg,
                          hasError ? ImVec4(0.30f, 0.10f, 0.10f, 0.93f)
                                   : ImVec4(0.10f, 0.18f, 0.24f, 0.88f));
    ImGui::PushStyleColor(ImGuiCol_Border,
                          hasError ? ImVec4(0.85f, 0.25f, 0.25f, 0.80f)
                                   : ImVec4(0.30f, 0.50f, 0.70f, 0.60f));

    if (ImGui::Begin("##LatestErrorBar", nullptr, barFlags)) {
        if (icon && icon->GetID()) {
            ImGui::Image((ImTextureID)(intptr_t)icon->GetID(), ImVec2(16.0f, 16.0f),
                         ImVec2(0, 1), ImVec2(1, 0));
            ImGui::SameLine();
        }

        ImGui::TextColored(hasError ? ImVec4(1.0f, 0.46f, 0.46f, 1.0f)
                                    : ImVec4(0.70f, 0.88f, 1.0f, 1.0f),
                           "%s:",
                           hasError ? "Latest Error" : "Status");
        ImGui::SameLine();
        ImGui::TextUnformatted(bodyText.c_str());
    }
    ImGui::End();

    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
}

#pragma endregion

#pragma region Mesh Builder Panel
void Engine::renderMeshBuilderPanel() {
    ImGui::Begin("Mesh Builder", &showMeshBuilder);

    auto recalcBounds = [this]() {
        if (!meshBuilder.hasMesh || meshBuilder.mesh.positions.empty()) return;
        meshBuilder.mesh.boundsMin = glm::vec3(FLT_MAX);
        meshBuilder.mesh.boundsMax = glm::vec3(-FLT_MAX);
        for (const auto& p : meshBuilder.mesh.positions) {
            meshBuilder.mesh.boundsMin.x = std::min(meshBuilder.mesh.boundsMin.x, p.x);
            meshBuilder.mesh.boundsMin.y = std::min(meshBuilder.mesh.boundsMin.y, p.y);
            meshBuilder.mesh.boundsMin.z = std::min(meshBuilder.mesh.boundsMin.z, p.z);
            meshBuilder.mesh.boundsMax.x = std::max(meshBuilder.mesh.boundsMax.x, p.x);
            meshBuilder.mesh.boundsMax.y = std::max(meshBuilder.mesh.boundsMax.y, p.y);
            meshBuilder.mesh.boundsMax.z = std::max(meshBuilder.mesh.boundsMax.z, p.z);
        }
    };

    ImGui::InputText("Mesh Path", meshBuilderPath, sizeof(meshBuilderPath));
    ImGui::SameLine();
    if (ImGui::Button("Load")) {
        std::string err;
        if (!meshBuilder.load(meshBuilderPath, err)) {
            addConsoleMessage("MeshBuilder load failed: " + err, ConsoleMessageType::Error);
        } else {
            addConsoleMessage("Loaded raw mesh: " + meshBuilder.loadedPath, ConsoleMessageType::Success);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Save")) {
        std::string err;
        std::string path = strlen(meshBuilderPath) ? meshBuilderPath : meshBuilder.loadedPath;
        if (!meshBuilder.save(path, err)) {
            addConsoleMessage("MeshBuilder save failed: " + err, ConsoleMessageType::Error);
        } else {
            addConsoleMessage("Saved raw mesh: " + meshBuilder.loadedPath, ConsoleMessageType::Success);
            strncpy(meshBuilderPath, meshBuilder.loadedPath.c_str(), sizeof(meshBuilderPath) - 1);
            meshBuilderPath[sizeof(meshBuilderPath) - 1] = '\0';
        }
    }

    if (ImGui::Button("Load Selected File")) {
        if (!fileBrowser.selectedFile.empty() && fs::path(fileBrowser.selectedFile).extension() == ".rmesh") {
            strncpy(meshBuilderPath, fileBrowser.selectedFile.string().c_str(), sizeof(meshBuilderPath) - 1);
            meshBuilderPath[sizeof(meshBuilderPath) - 1] = '\0';
            std::string err;
            if (!meshBuilder.load(meshBuilderPath, err)) {
                addConsoleMessage("MeshBuilder load failed: " + err, ConsoleMessageType::Error);
            } else {
                addConsoleMessage("Loaded raw mesh: " + meshBuilder.loadedPath, ConsoleMessageType::Success);
            }
        } else {
            addConsoleMessage("Select a .rmesh file in the browser to load", ConsoleMessageType::Warning);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Recompute Normals")) {
        meshBuilder.recomputeNormals();
    }

    ImGui::Separator();

    if (!meshBuilder.hasMesh) {
        ImGui::TextDisabled("No mesh loaded.");
        ImGui::End();
        return;
    }

    ImGui::Text("Vertices: %zu", meshBuilder.mesh.positions.size());
    ImGui::Text("Faces: %zu", meshBuilder.mesh.faces.size());
    ImGui::Text("Bounds Min: (%.3f, %.3f, %.3f)", meshBuilder.mesh.boundsMin.x, meshBuilder.mesh.boundsMin.y, meshBuilder.mesh.boundsMin.z);
    ImGui::Text("Bounds Max: (%.3f, %.3f, %.3f)", meshBuilder.mesh.boundsMax.x, meshBuilder.mesh.boundsMax.y, meshBuilder.mesh.boundsMax.z);
    if (meshBuilder.dirty) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1,0.7f,0.2f,1),"*modified");
    }

    ImGui::SeparatorText("Vertices");
    ImGui::SetNextItemWidth(100);
    ImGui::InputInt("Selected", &meshBuilder.selectedVertex);
    if (meshBuilder.selectedVertex < 0 || meshBuilder.selectedVertex >= (int)meshBuilder.mesh.positions.size()) {
        meshBuilder.selectedVertex = meshBuilder.mesh.positions.empty() ? -1 : 0;
    }

    if (meshBuilder.selectedVertex >= 0 && meshBuilder.selectedVertex < (int)meshBuilder.mesh.positions.size()) {
        glm::vec3& pos = meshBuilder.mesh.positions[meshBuilder.selectedVertex];
        float edit[3] = { pos.x, pos.y, pos.z };
        if (ImGui::InputFloat3("Position", edit, "%.4f")) {
            pos = glm::vec3(edit[0], edit[1], edit[2]);
            recalcBounds();
            meshBuilder.recomputeNormals();
            meshBuilder.dirty = true;
        }
        if (meshBuilder.mesh.hasUVs && meshBuilder.selectedVertex < (int)meshBuilder.mesh.uvs.size()) {
            glm::vec2& uv = meshBuilder.mesh.uvs[meshBuilder.selectedVertex];
            float uvEdit[2] = { uv.x, uv.y };
            if (ImGui::InputFloat2("UV", uvEdit, "%.4f")) {
                uv = glm::vec2(uvEdit[0], uvEdit[1]);
                meshBuilder.dirty = true;
            }
        }
    }

    ImGui::SeparatorText("Add Face / Fill");
    ImGui::InputTextWithHint("Indices", "e.g. 0,1,2 or 0 1 2 3", meshBuilderFaceInput, sizeof(meshBuilderFaceInput));
    ImGui::SameLine();
    if (ImGui::Button("Add Face")) {
        std::vector<uint32_t> indices;
        std::string token;
        std::stringstream ss(meshBuilderFaceInput);
        while (std::getline(ss, token, ',')) {
            std::stringstream inner(token);
            std::string sub;
            while (inner >> sub) {
                try {
                    uint32_t idx = static_cast<uint32_t>(std::stoul(sub));
                    indices.push_back(idx);
                } catch (...) {}
            }
        }
        if (indices.empty()) {
            addConsoleMessage("Enter vertex indices separated by commas or spaces", ConsoleMessageType::Warning);
        } else {
            std::string err;
            if (!meshBuilder.addFace(indices, err)) {
                addConsoleMessage("Add face failed: " + err, ConsoleMessageType::Error);
            } else {
                addConsoleMessage("Added face with " + std::to_string(indices.size()) + " verts", ConsoleMessageType::Success);
            }
        }
    }

    ImGui::SeparatorText("Faces (first 16)");
    int maxFaces = std::min<int>(16, meshBuilder.mesh.faces.size());
    for (int i = 0; i < maxFaces; i++) {
        const auto& f = meshBuilder.mesh.faces[i];
        ImGui::Text("%d: %u, %u, %u", i, f.x, f.y, f.z);
    }

    ImGui::End();
}

#pragma endregion

#pragma region Dialogs
void Engine::renderDialogs() {
    if (showNewSceneDialog) {
        ImGuiIO& io = ImGui::GetIO();
        ImVec2 center = ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(350, 130), ImGuiCond_Appearing);

        if (ImGui::Begin("New Scene", &showNewSceneDialog,
                        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoDocking)) {
            ImGui::Text("Scene Name:");
            ImGui::SetNextItemWidth(-1);
            ImGui::InputText("##NewSceneName", newSceneName, sizeof(newSceneName));

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            float buttonWidth = 80;
            ImGui::SetCursorPosX(ImGui::GetWindowWidth() - buttonWidth * 2 - 20);

            if (ImGui::Button("Cancel", ImVec2(buttonWidth, 0))) {
                showNewSceneDialog = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("Create", ImVec2(buttonWidth, 0))) {
                if (strlen(newSceneName) > 0) {
                    createNewScene(newSceneName);
                    showNewSceneDialog = false;
                    memset(newSceneName, 0, sizeof(newSceneName));
                }
            }
        }
        ImGui::End();
    }

    if (showCompilePopup) {
        if (!compilePopupOpened) {
            ImGui::OpenPopup("Script Compile");
            compilePopupOpened = true;
        }
        ImGuiIO& io = ImGui::GetIO();
        ImVec2 center = ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
        ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(520, 260), ImGuiCond_FirstUseEver);
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                                 ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings;
        bool allowClose = !compileInProgress;
        if (ImGui::BeginPopupModal("Script Compile", allowClose ? &showCompilePopup : nullptr, flags)) {
            ImGui::TextWrapped("%s", lastCompileStatus.c_str());
            float progress = 1.0f;
            std::string stageText;
            {
                std::lock_guard<std::mutex> lock(compileMutex);
                progress = compileInProgress ? compileProgress : 1.0f;
                stageText = compileInProgress ? compileStage : (lastCompileSuccess ? "Done" : "Failed");
            }
            const char* stageLabel = stageText.empty() ? "Working..." : stageText.c_str();
            if (progress <= 0.0f) progress = 0.02f;
            ImGui::ProgressBar(progress, ImVec2(-1, 0), stageLabel);
            ImGui::Separator();
            ImGui::BeginChild("CompileLog", ImVec2(0, -40), true);
            if (lastCompileLog.empty() && compileInProgress) {
                ImGui::TextUnformatted("Waiting for compiler output...");
            } else {
                ImGui::TextUnformatted(lastCompileLog.c_str());
            }
            ImGui::EndChild();
            ImGui::Spacing();
            if (allowClose && ImGui::Button("Close", ImVec2(80, 0))) {
                showCompilePopup = false;
                ImGui::CloseCurrentPopup();
                compilePopupOpened = false;
            }
            ImGui::EndPopup();
        }
    } else {
        compilePopupOpened = false;
    }

    if (showSaveSceneAsDialog) {
        ImGuiIO& io = ImGui::GetIO();
        ImVec2 center = ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(350, 130), ImGuiCond_Appearing);

        if (ImGui::Begin("Save Scene As", &showSaveSceneAsDialog,
                        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoDocking)) {
            ImGui::Text("Scene Name:");
            ImGui::SetNextItemWidth(-1);
            ImGui::InputText("##SaveSceneAsName", saveSceneAsName, sizeof(saveSceneAsName));

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            float buttonWidth = 80;
            ImGui::SetCursorPosX(ImGui::GetWindowWidth() - buttonWidth * 2 - 20);

            if (ImGui::Button("Cancel", ImVec2(buttonWidth, 0))) {
                showSaveSceneAsDialog = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("Save", ImVec2(buttonWidth, 0))) {
                if (strlen(saveSceneAsName) > 0) {
                    projectManager.currentProject.currentSceneName = saveSceneAsName;
                    saveCurrentScene();
                    showSaveSceneAsDialog = false;
                    memset(saveSceneAsName, 0, sizeof(saveSceneAsName));
                }
            }
        }
        ImGui::End();
    }
    
    // OBJ Import dialog
    if (showImportOBJDialog) {
        ImGuiIO& io = ImGui::GetIO();
        ImVec2 center = ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(400, 160), ImGuiCond_Appearing);

        if (ImGui::Begin("Import OBJ Model", &showImportOBJDialog,
                        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoDocking)) {
            ImGui::Text("File: %s", fs::path(pendingOBJPath).filename().string().c_str());
            ImGui::TextDisabled("%s", pendingOBJPath.c_str());
            
            ImGui::Spacing();
            
            ImGui::Text("Object Name:");
            ImGui::SetNextItemWidth(-1);
            ImGui::InputText("##ImportOBJName", importOBJName, sizeof(importOBJName));

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            float buttonWidth = 80;
            ImGui::SetCursorPosX(ImGui::GetWindowWidth() - buttonWidth * 2 - 20);

            if (ImGui::Button("Cancel", ImVec2(buttonWidth, 0))) {
                showImportOBJDialog = false;
                pendingOBJPath.clear();
            }
            ImGui::SameLine();
            
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 0.3f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.6f, 0.4f, 1.0f));
            if (ImGui::Button("Import", ImVec2(buttonWidth, 0))) {
                importOBJToScene(pendingOBJPath, importOBJName);
                showImportOBJDialog = false;
                pendingOBJPath.clear();
                memset(importOBJName, 0, sizeof(importOBJName));
            }
            ImGui::PopStyleColor(2);
        }
        ImGui::End();
    }

    // General model import dialog (Assimp-backed)
    if (showImportModelDialog) {
        ImGuiIO& io = ImGui::GetIO();
        ImVec2 center = ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(420, 180), ImGuiCond_Appearing);

        if (ImGui::Begin("Import Model", &showImportModelDialog,
                        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoDocking)) {
            ImGui::Text("File: %s", fs::path(pendingModelPath).filename().string().c_str());
            ImGui::TextDisabled("%s", pendingModelPath.c_str());

            ImGui::Spacing();

            ImGui::Text("Object Name:");
            ImGui::SetNextItemWidth(-1);
            ImGui::InputText("##ImportModelName", importModelName, sizeof(importModelName));

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            float buttonWidth = 80;
            ImGui::SetCursorPosX(ImGui::GetWindowWidth() - buttonWidth * 2 - 20);

            if (ImGui::Button("Cancel", ImVec2(buttonWidth, 0))) {
                showImportModelDialog = false;
                pendingModelPath.clear();
            }
            ImGui::SameLine();

            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 0.3f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.6f, 0.4f, 1.0f));
            if (ImGui::Button("Import", ImVec2(buttonWidth, 0))) {
                importModelToScene(pendingModelPath, importModelName);
                showImportModelDialog = false;
                pendingModelPath.clear();
                memset(importModelName, 0, sizeof(importModelName));
            }
            ImGui::PopStyleColor(2);
        }
        ImGui::End();
    }
}
