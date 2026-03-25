#include "Engine.h"
#include "ModelLoader.h"
#include "../SpritesheetFormat.h"
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
#include <iomanip>
#include <regex>
#include <unordered_set>
#include <unordered_map>
#include <optional>

#ifdef _WIN32
#include <shlobj.h>
#endif

#pragma region Hierarchy Helpers
namespace {
    bool IsSpriteSheetSidecarPath(const fs::path& path) {
        return path.extension() == ".spritesheet";
    }

    fs::path ResolveSpriteSheetImagePath(const fs::path& path) {
        if (!IsSpriteSheetSidecarPath(path)) {
            return path;
        }
        fs::path imagePath = path;
        imagePath.replace_extension();
        return imagePath;
    }

    std::optional<SpritesheetDocument> LoadSpriteSheetDocument(const fs::path& sidecarPath) {
        std::ifstream sidecar(sidecarPath);
        if (!sidecar.is_open()) {
            return std::nullopt;
        }
        std::ostringstream buffer;
        buffer << sidecar.rdbuf();
        return ParseSpritesheet(buffer.str()).document;
    }

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
        if (obj.hasLight2D) return IM_COL32(255, 228, 108, 230);
        if (obj.hasShadowCaster2D) return IM_COL32(120, 150, 190, 230);
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

    struct HierarchyFrameCache {
        std::unordered_map<int, size_t> visibleIndex;
        std::unordered_set<int> selectedIds;
    };

    struct ConsoleRowMetrics {
        std::string header;
        ImVec2 headerSize = ImVec2(0.0f, 0.0f);
        ImVec2 messageSize = ImVec2(0.0f, 0.0f);
        float rowWidth = 0.0f;
        float rowHeight = 0.0f;
        float textWidth = 0.0f;
    };

    struct ConsolePanelCache {
        size_t entryCount = 0;
        bool wrapText = false;
        bool iconsAvailable = false;
        float contentWidth = 0.0f;
        uint64_t fingerprint = 0;
        std::vector<ConsoleRowMetrics> rows;
    };

    HierarchyFrameCache gHierarchyFrameCache;
    ConsolePanelCache gConsolePanelCache;

    uint64_t hashCombine64(uint64_t seed, uint64_t value) {
        return seed ^ (value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2));
    }

    template <typename Entry>
    uint64_t hashConsoleEntryFingerprint(const Entry& entry) {
        uint64_t hash = std::hash<std::string>{}(entry.timestamp);
        hash = hashCombine64(hash, std::hash<std::string>{}(entry.message));
        hash = hashCombine64(hash, static_cast<uint64_t>(entry.type));
        return hash;
    }

    template <typename Entry>
    uint64_t computeConsoleLogFingerprint(const std::vector<Entry>& log) {
        uint64_t fingerprint = hashCombine64(0x6d6f64756c617265ULL, static_cast<uint64_t>(log.size()));
        if (log.empty()) {
            return fingerprint;
        }

        fingerprint = hashCombine64(fingerprint, hashConsoleEntryFingerprint(log.front()));
        if (log.size() > 1) {
            fingerprint = hashCombine64(fingerprint, hashConsoleEntryFingerprint(log.back()));
        }
        if (log.size() > 2) {
            fingerprint = hashCombine64(fingerprint, hashConsoleEntryFingerprint(log[log.size() / 2]));
        }
        return fingerprint;
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
        if (target.hasLight2D) {
            switch (target.light2D.type) {
                case Light2DType::Point: target.type = ObjectType::Light2D; break;
                case Light2DType::Spot: target.type = ObjectType::Light2D; break;
                case Light2DType::Freeform: target.type = ObjectType::Light2D; break;
                case Light2DType::Sprite: target.type = ObjectType::Light2D; break;
                case Light2DType::Global: target.type = ObjectType::Light2D; break;
            }
            return;
        }
        if (target.hasShadowCaster2D) {
            target.type = ObjectType::ShadowCaster2D;
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

void EnsureSpriteClipNames(std::vector<std::string>& names, size_t count) {
    if (names.size() < count) {
        for (size_t i = names.size(); i < count; ++i) {
            names.push_back("Rect_" + std::to_string(i));
        }
    } else if (names.size() > count) {
        names.resize(count);
    }
}

void EnsureSpriteClipScales(std::vector<glm::vec2>& scales, size_t count) {
    if (scales.size() < count) {
        scales.resize(count, glm::vec2(1.0f));
    } else if (scales.size() > count) {
        scales.resize(count);
    }
    for (glm::vec2& scale : scales) {
        scale.x = std::max(0.01f, scale.x);
        scale.y = std::max(0.01f, scale.y);
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

    gHierarchyFrameCache.visibleIndex.clear();
    gHierarchyFrameCache.visibleIndex.reserve(sceneObjects.size());
    gHierarchyFrameCache.selectedIds.clear();
    gHierarchyFrameCache.selectedIds.reserve(selectedObjectIds.size());
    for (int id : selectedObjectIds) {
        gHierarchyFrameCache.selectedIds.insert(id);
    }

    refreshSceneObjectIndexCache();
    std::vector<size_t> rootIndices;
    rootIndices.reserve(sceneObjects.size());
    for (size_t i = 0; i < sceneObjects.size(); i++) {
        int parentId = sceneObjects[i].parentId;
        if (parentId == -1 || sceneObjectIndexById.find(parentId) == sceneObjectIndexById.end()) {
            rootIndices.push_back(i);
        }
    }

    hierarchyVisibleOrder.clear();
    hierarchyVisibleOrder.reserve(sceneObjects.size());
    std::vector<bool> ancestorHasNext;
    for (size_t i = 0; i < rootIndices.size(); ++i) {
        const bool isLastRoot = (i + 1 == rootIndices.size());
        renderObjectNode(sceneObjects[rootIndices[i]], filter, ancestorHasNext, isLastRoot, 0, animStep);
    }

    const bool hierarchyBackgroundLeftClicked =
        ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
        !ImGui::IsAnyItemHovered();
    if (hierarchyBackgroundLeftClicked) {
        clearSelection();
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
                if (ImGui::MenuItem("2.5D Object")) addObject(ObjectType::Sprite25D, "2.5D Object");
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
                if (has2DWorldPackage()) {
                    ImGui::Separator();
                    if (ImGui::MenuItem("2D Point Light"))    addObject(ObjectType::Light2D, "2D Point Light");
                    if (ImGui::MenuItem("2D Spot Light")) {
                        addObject(ObjectType::Light2D, "2D Spot Light");
                        if (!sceneObjects.empty()) {
                            sceneObjects.back().light2D.type = Light2DType::Spot;
                        }
                    }
                    if (ImGui::MenuItem("2D Freeform Light")) {
                        addObject(ObjectType::Light2D, "2D Freeform Light");
                        if (!sceneObjects.empty()) {
                            sceneObjects.back().light2D.type = Light2DType::Freeform;
                            sceneObjects.back().light2D.shapePoints = {
                                glm::vec2(-2.0f, -1.5f),
                                glm::vec2(2.0f, -1.5f),
                                glm::vec2(2.5f, 1.0f),
                                glm::vec2(0.0f, 2.5f),
                                glm::vec2(-2.5f, 1.0f)
                            };
                        }
                    }
                    if (ImGui::MenuItem("2D Global Light")) {
                        addObject(ObjectType::Light2D, "2D Global Light");
                        if (!sceneObjects.empty()) {
                            sceneObjects.back().light2D.type = Light2DType::Global;
                            sceneObjects.back().light2D.intensity = 0.35f;
                            sceneObjects.back().light2D.color = glm::vec4(0.45f, 0.52f, 0.72f, 1.0f);
                        }
                    }
                    if (ImGui::MenuItem("2D Shadow Caster")) addObject(ObjectType::ShadowCaster2D, "2D Shadow Caster");
                }
                ImGui::EndMenu();
            }

            // ── Other / Effects ───────────────────────
            if (ImGui::BeginMenu("Effects"))
            {
                if (ImGui::MenuItem("ModuVolume")) addObject(ObjectType::PostFXNode, "ModuVolume");
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
                if (has2DWorldPackage() && ImGui::MenuItem("Sprite2D")) createUIWithCanvas(ObjectType::Sprite2D, "Sprite2D");
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
    if (!filter.empty()) {
        std::string nameLower = obj.name;
        std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);
        if (nameLower.find(filter) == std::string::npos) {
            return;
        }
    }

    hierarchyVisibleOrder.push_back(obj.id);
    gHierarchyFrameCache.visibleIndex[obj.id] = hierarchyVisibleOrder.size() - 1;

    bool hasChildren = !obj.childIds.empty();
    bool isSelected = gHierarchyFrameCache.selectedIds.find(obj.id) != gHierarchyFrameCache.selectedIds.end();

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

    if (obj.hasParallaxLayer2D && obj.parallaxLayer2D.enabled) {
        std::string badgeText = "L" + std::to_string(obj.parallaxLayer2D.order);
        ImVec2 badgeTextSize = ImGui::CalcTextSize(badgeText.c_str());
        ImVec2 labelTextSize = ImGui::CalcTextSize(obj.name.c_str());
        float badgePadX = 6.0f;
        float badgePadY = 2.0f;
        float rightEdge = itemMax.x - 8.0f;
        if (hierarchyShowTexturePreview) {
            float previewSize = std::max(12.0f, lineHeight - 4.0f);
            rightEdge -= (previewSize + 8.0f);
        }
        ImVec2 badgeMin(rightEdge - badgeTextSize.x - badgePadX * 2.0f,
                        itemMin.y + (lineHeight - (badgeTextSize.y + badgePadY * 2.0f)) * 0.5f);
        ImVec2 badgeMax(rightEdge, badgeMin.y + badgeTextSize.y + badgePadY * 2.0f);
        float minAllowedX = textPos.x + labelTextSize.x + 8.0f;
        if (badgeMin.x > minAllowedX) {
            ImU32 badgeBg = ImGui::GetColorU32(ImVec4(0.20f, 0.40f, 0.62f, 0.78f));
            ImU32 badgeBorder = ImGui::GetColorU32(ImVec4(0.52f, 0.74f, 0.96f, 0.88f));
            ImU32 badgeFg = ImGui::GetColorU32(ImVec4(0.92f, 0.96f, 1.0f, 0.96f));
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(badgeMin, badgeMax, badgeBg, 4.0f);
            dl->AddRect(badgeMin, badgeMax, badgeBorder, 4.0f, 0, 1.0f);
            dl->AddText(ImVec2(badgeMin.x + badgePadX, badgeMin.y + badgePadY), badgeFg, badgeText.c_str());
        }
    }

    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
        const bool shift = ImGui::GetIO().KeyShift;
        const bool ctrl = ImGui::GetIO().KeyCtrl;

        if (shift && !hierarchyVisibleOrder.empty()) {
            int anchorId = (hierarchyRangeAnchorId >= 0) ? hierarchyRangeAnchorId : selectedObjectId;
            if (anchorId < 0) {
                anchorId = obj.id;
            }

            auto findOrderIndex = [&](int id) -> int {
                auto it = gHierarchyFrameCache.visibleIndex.find(id);
                if (it == gHierarchyFrameCache.visibleIndex.end()) return -1;
                return static_cast<int>(it->second);
            };

            const int anchorIndex = findOrderIndex(anchorId);
            const int currentIndex = findOrderIndex(obj.id);
            if (anchorIndex >= 0 && currentIndex >= 0) {
                if (!ctrl) {
                    selectedObjectIds.clear();
                }
                int rangeStart = std::min(anchorIndex, currentIndex);
                int rangeEnd = std::max(anchorIndex, currentIndex);
                for (int i = rangeStart; i <= rangeEnd; ++i) {
                    int rangeId = hierarchyVisibleOrder[static_cast<size_t>(i)];
                    if (std::find(selectedObjectIds.begin(), selectedObjectIds.end(), rangeId) == selectedObjectIds.end()) {
                        selectedObjectIds.push_back(rangeId);
                    }
                }
                selectedObjectId = obj.id;
            } else {
                setPrimarySelection(obj.id, ctrl);
            }
        } else if (ctrl) {
            auto selectedIt = std::find(selectedObjectIds.begin(), selectedObjectIds.end(), obj.id);
            if (selectedIt == selectedObjectIds.end()) {
                selectedObjectIds.push_back(obj.id);
                selectedObjectId = obj.id;
            } else {
                selectedObjectIds.erase(selectedIt);
                if (selectedObjectId == obj.id) {
                    selectedObjectId = selectedObjectIds.empty() ? -1 : selectedObjectIds.back();
                }
            }
            hierarchyRangeAnchorId = obj.id;
        } else {
            setPrimarySelection(obj.id, false);
        }
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
                auto nextSiblingId = [&]() -> int {
                    if (obj.parentId != -1) {
                        SceneObject* parent = findObjectById(obj.parentId);
                        if (!parent) return -1;
                        auto it = std::find(parent->childIds.begin(), parent->childIds.end(), obj.id);
                        if (it == parent->childIds.end()) return -1;
                        ++it;
                        while (it != parent->childIds.end()) {
                            if (*it != draggedId) return *it;
                            ++it;
                        }
                        return -1;
                    }

                    bool found = false;
                    for (const auto& candidate : sceneObjects) {
                        if (candidate.parentId != -1) continue;
                        if (candidate.id == draggedId) continue;
                        if (found) return candidate.id;
                        if (candidate.id == obj.id) found = true;
                    }
                    return -1;
                };

                float mouseY = ImGui::GetMousePos().y;
                float upperThreshold = itemMin.y + lineHeight * 0.25f;
                float lowerThreshold = itemMax.y - lineHeight * 0.25f;
                if (mouseY <= upperThreshold) {
                    // Reorder before this object at current depth.
                    setParent(draggedId, obj.parentId, obj.id);
                } else if (mouseY >= lowerThreshold) {
                    // Reorder after this object at current depth.
                    setParent(draggedId, obj.parentId, nextSiblingId());
                } else {
                    // Default center drop: parent under this object.
                    setParent(draggedId, obj.id, -1);
                }
            }
        }
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("FILE_PATH")) {
            const char* path = static_cast<const char*>(payload->Data);
            std::error_code ec;
            fs::directory_entry entry(path, ec);
            if (!ec) {
                if (fileBrowser.isModelFile(entry)) {
                    importDroppedModel(fs::path(path), obj.id);
                } else if (fileBrowser.getFileCategory(entry) == FileCategory::Material) {
                    obj.materialPath = entry.path().string();
                    loadMaterialFromFile(obj);
                    projectManager.currentProject.hasUnsavedChanges = true;
                    addConsoleMessage("Applied material to " + obj.name, ConsoleMessageType::Success);
                } else if (fileBrowser.getFileCategory(entry) == FileCategory::Script) {
                    auto alreadyAssigned = std::any_of(obj.scripts.begin(), obj.scripts.end(),
                        [&](const ScriptComponent& sc) { return sc.path == path; });
                    if (!alreadyAssigned) {
                        ScriptComponent sc;
                        sc.path = path;
                        sc.lastBinaryPath.clear();
                        sc.lastBinaryVerified = false;
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
                        markRuntimeScriptBindingsDirty();
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
            Texture* previewTex = renderer.getTexture(*previewPath, obj.material.textureFilter);
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
            selectedAudioPreview = nullptr;
            browserHasAudio = false;

            if (!selectedAudioPath.empty()) {
                selectedAudioPreview = audio.getPreview(selectedAudioPath.string());
                browserHasAudio = (selectedAudioPreview != nullptr);
            }
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

    struct AudioPlayerUiIcon {
        ImTextureID id = static_cast<ImTextureID>(0);
        bool flipY = false;
    };

    const bool hasVulkanUiImages = usingVulkan() && vulkanRendererInitialized && (vulkanRenderer != nullptr);
    auto resolveAudioPlayerIcon = [&](const char* iconPath) -> AudioPlayerUiIcon {
        if (!iconPath || !*iconPath) {
            return {};
        }
        if (rendererInitialized) {
            if (Texture* icon = renderer.getTexture(iconPath, MaterialProperties::TextureFilter::Bilinear);
                icon && icon->GetID()) {
                return { static_cast<ImTextureID>(icon->GetID()), true };
            }
        }
        if (hasVulkanUiImages && vulkanRenderer) {
            ImTextureID icon = vulkanRenderer->getOrCreateUIImage(iconPath);
            if (icon != static_cast<ImTextureID>(0)) {
                return { icon, false };
            }
        }
        return {};
    };

    auto formatAudioClock = [](double seconds, bool roundUp) -> std::string {
        if (!std::isfinite(seconds) || seconds <= 0.0) {
            return "0:00";
        }

        const double quantized = roundUp
            ? std::ceil(std::max(0.0, seconds) - 0.0001)
            : std::floor(std::max(0.0, seconds) + 0.0001);
        const long long totalSeconds = static_cast<long long>(std::max(0.0, quantized));
        const long long hours = totalSeconds / 3600;
        const long long minutes = (totalSeconds / 60) % 60;
        const long long secs = totalSeconds % 60;

        std::ostringstream out;
        out << std::setfill('0');
        if (hours > 0) {
            out << hours << ':' << std::setw(2) << minutes << ':' << std::setw(2) << secs;
        } else {
            out << (totalSeconds / 60) << ':' << std::setw(2) << secs;
        }
        return out.str();
    };

    auto drawAudioTimeReadout = [&](double cursorSeconds, double durationSeconds) {
        const double safeCursor = std::max(0.0, cursorSeconds);
        const double safeDuration = std::max(0.0, durationSeconds);
        const std::string currentClock = formatAudioClock(safeCursor, false);
        const std::string durationClock = formatAudioClock(safeDuration, true);
        ImGui::TextColored(ImVec4(0.98f, 0.82f, 0.55f, 1.0f), "%s / %s", currentClock.c_str(), durationClock.c_str());
        ImGui::TextDisabled("Timing: %.2fs / %.2fs", safeCursor, safeDuration);
    };

    auto drawTrimmedPathText = [&](const std::string& path, const ImVec4& color) {
        const float maxWidth = std::max(32.0f, ImGui::GetContentRegionAvail().x);
        std::string display = path;
        if (ImGui::CalcTextSize(display.c_str()).x > maxWidth) {
            constexpr const char* kEllipsis = "...";
            std::string suffix = path;
            while (!suffix.empty()) {
                std::string candidate = std::string(kEllipsis) + suffix;
                if (ImGui::CalcTextSize(candidate.c_str()).x <= maxWidth) {
                    display = std::move(candidate);
                    break;
                }
                suffix.erase(suffix.begin());
            }
            if (suffix.empty()) {
                display = kEllipsis;
            }
        }

        ImGui::TextColored(color, "%s", display.c_str());
        if (display != path && ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", path.c_str());
        }
    };

    auto drawAudioPlayerIconButton = [&](const char* id,
                                         const char* iconPath,
                                         const char* fallbackText,
                                         const char* tooltip,
                                         bool active,
                                         bool disabled,
                                         const ImVec2& size,
                                         const ImVec4& accentColor) -> bool {
        if (disabled) {
            ImGui::BeginDisabled();
        }

        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const bool pressed = ImGui::InvisibleButton(id, size);
        const bool hovered = ImGui::IsItemHovered();
        const bool held = ImGui::IsItemActive();
        const ImVec2 max(pos.x + size.x, pos.y + size.y);
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const float rounding = 10.0f;

        ImVec4 bg = active
            ? ImVec4(accentColor.x * 0.70f, accentColor.y * 0.70f, accentColor.z * 0.70f, 0.95f)
            : hovered
                ? ImVec4(0.24f, 0.27f, 0.35f, 0.95f)
                : ImVec4(0.15f, 0.17f, 0.22f, 0.92f);
        ImVec4 border = active
            ? ImVec4(accentColor.x, accentColor.y, accentColor.z, 1.0f)
            : hovered
                ? ImVec4(0.58f, 0.66f, 0.78f, 0.95f)
                : ImVec4(0.28f, 0.32f, 0.41f, 0.92f);
        if (held) {
            bg = ImVec4(bg.x + 0.05f, bg.y + 0.05f, bg.z + 0.05f, bg.w);
        }

        if (disabled) {
            bg.w *= 0.55f;
            border.w *= 0.45f;
        }

        drawList->AddRectFilled(pos, max, ImGui::ColorConvertFloat4ToU32(bg), rounding);
        drawList->AddRect(pos, max, ImGui::ColorConvertFloat4ToU32(border), rounding, 0, active ? 2.0f : 1.0f);

        const AudioPlayerUiIcon icon = resolveAudioPlayerIcon(iconPath);
        const float inset = size.x >= 40.0f ? 8.0f : 6.0f;
        const ImVec2 iconMin(pos.x + inset, pos.y + inset);
        const ImVec2 iconMax(max.x - inset, max.y - inset);
        const int alpha = disabled ? 110 : active ? 255 : hovered ? 240 : 215;
        if (icon.id != static_cast<ImTextureID>(0)) {
            const ImVec2 uvMin = icon.flipY ? ImVec2(0.0f, 1.0f) : ImVec2(0.0f, 0.0f);
            const ImVec2 uvMax = icon.flipY ? ImVec2(1.0f, 0.0f) : ImVec2(1.0f, 1.0f);
            drawList->AddImage(icon.id, iconMin, iconMax, uvMin, uvMax, IM_COL32(255, 255, 255, alpha));
        } else if (fallbackText && *fallbackText) {
            const ImVec2 textSize = ImGui::CalcTextSize(fallbackText);
            drawList->AddText(
                ImVec2(pos.x + (size.x - textSize.x) * 0.5f, pos.y + (size.y - textSize.y) * 0.5f),
                IM_COL32(255, 255, 255, alpha),
                fallbackText);
        }

        if (hovered && tooltip && *tooltip) {
            ImGui::SetTooltip("%s", tooltip);
        }

        if (disabled) {
            ImGui::EndDisabled();
        }
        return !disabled && pressed;
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

    enum class MaterialShaderPreset : int {
        Custom = 0,
        EngineLit = 1,
        ScrollingUV = 2
    };

    auto resolveScrollingShaderPaths = [&]() {
        std::string vertPath = "Resources/Shaders/scroll_texture_vert.glsl";
        std::string fragPath = "Resources/Shaders/scroll_texture_frag.glsl";
        if (projectManager.currentProject.isLoaded) {
            fs::path projVert = projectManager.currentProject.assetsPath / "Shaders" / "scroll_texture_vert.glsl";
            fs::path projFrag = projectManager.currentProject.assetsPath / "Shaders" / "scroll_texture_frag.glsl";
            std::error_code ec;
            if (fs::exists(projVert, ec) && !ec) {
                vertPath = projVert.string();
            }
            ec.clear();
            if (fs::exists(projFrag, ec) && !ec) {
                fragPath = projFrag.string();
            }
        }
        return std::pair<std::string, std::string>{vertPath, fragPath};
    };

    auto shaderPresetFromPaths = [](const std::string& vert, const std::string& frag) {
        if (vert.empty() && frag.empty()) {
            return MaterialShaderPreset::EngineLit;
        }
        std::string vertFile = fs::path(vert).filename().string();
        std::string fragFile = fs::path(frag).filename().string();
        std::transform(vertFile.begin(), vertFile.end(), vertFile.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        std::transform(fragFile.begin(), fragFile.end(), fragFile.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (vertFile == "scroll_texture_vert.glsl" && fragFile == "scroll_texture_frag.glsl") {
            return MaterialShaderPreset::ScrollingUV;
        }
        return MaterialShaderPreset::Custom;
    };

    auto applyShaderPreset = [&](MaterialShaderPreset preset, std::string& vert, std::string& frag) {
        std::string nextVert = vert;
        std::string nextFrag = frag;
        if (preset == MaterialShaderPreset::EngineLit) {
            nextVert.clear();
            nextFrag.clear();
        } else if (preset == MaterialShaderPreset::ScrollingUV) {
            auto scrolling = resolveScrollingShaderPaths();
            nextVert = scrolling.first;
            nextFrag = scrolling.second;
        }
        bool changed = (nextVert != vert) || (nextFrag != frag);
        if (changed) {
            vert = std::move(nextVert);
            frag = std::move(nextFrag);
        }
        return changed;
    };

    auto isTextureOrSpriteSheetSelection = [&](const fs::path& path) {
        if (path.empty() || !fs::exists(path)) return false;
        std::error_code ec;
        fs::directory_entry entry(path, ec);
        if (ec) return false;
        return fileBrowser.isTextureFile(entry) || IsSpriteSheetSidecarPath(path);
    };
    auto assignSpriteTextureOrClips = [&](SceneObject& target, const fs::path& sourcePath) -> bool {
        if (sourcePath.empty() || !fs::exists(sourcePath)) {
            return false;
        }

        const fs::path imagePath = ResolveSpriteSheetImagePath(sourcePath);
        if (!fs::exists(imagePath)) {
            return false;
        }

        target.albedoTexturePath = imagePath.string();
        const fs::path sidecarPath = IsSpriteSheetSidecarPath(sourcePath) ? sourcePath : fs::path(imagePath.string() + ".spritesheet");
        std::vector<glm::ivec4> clips;
        std::vector<std::string> clipNames;
        std::vector<glm::vec2> clipScales;
        // I swear, trying to get this working with the spritesheet management system took far too long lmfao, at least I got it down.
        if (fs::exists(sidecarPath)) {
            if (std::optional<SpritesheetDocument> sidecar = LoadSpriteSheetDocument(sidecarPath)) {
                clips = std::move(sidecar->rects);
                clipNames = std::move(sidecar->names);
                clipScales = std::move(sidecar->scales);
            }
        }

        target.ui.spriteCustomFrames = std::move(clips);
        target.ui.spriteCustomFrameNames = std::move(clipNames);
        target.ui.spriteCustomFrameScales = std::move(clipScales);
        EnsureSpriteClipNames(target.ui.spriteCustomFrameNames, target.ui.spriteCustomFrames.size());
        EnsureSpriteClipScales(target.ui.spriteCustomFrameScales, target.ui.spriteCustomFrames.size());
        target.ui.spriteCustomFramesEnabled = !target.ui.spriteCustomFrames.empty();
        target.ui.spriteSheetEnabled = target.ui.spriteCustomFramesEnabled || target.ui.spriteSheetEnabled;
        target.ui.spriteSheetFrame = 0;
        target.ui.spriteSourceWidth = 0;
        target.ui.spriteSourceHeight = 0;

        if (Texture* tex = renderer.getTexture(target.albedoTexturePath, MaterialProperties::TextureFilter::Point)) {
            target.ui.spriteSourceWidth = tex->GetWidth();
            target.ui.spriteSourceHeight = tex->GetHeight();
        }

        if (target.ui.spriteCustomFramesEnabled) {
            // GOD damn, this sprite used to hate scaling properly to the object. BRUH.
            target.ui.size.x = static_cast<float>(std::max(1, target.ui.spriteCustomFrames[0].z));
            target.ui.size.y = static_cast<float>(std::max(1, target.ui.spriteCustomFrames[0].w));
        }
        return true;
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
                    if (ImGui::BeginDragDropTarget()) {
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("FILE_PATH")) {
                            const char* dropped = static_cast<const char*>(payload->Data);
                            std::error_code ec;
                            fs::directory_entry droppedEntry(fs::path(dropped), ec);
                            const fs::path droppedPath(dropped);
                            if ((!ec && fileBrowser.isTextureFile(droppedEntry)) || IsSpriteSheetSidecarPath(droppedPath)) {
                                path = ResolveSpriteSheetImagePath(droppedPath).string();
                                changed = true;
                            }
                        }
                        ImGui::EndDragDropTarget();
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Clear")) {
                        path.clear();
                        changed = true;
                    }
                    ImGui::SameLine();
                    bool canUseTex = isTextureOrSpriteSheetSelection(fileBrowser.selectedFile);
                    ImGui::BeginDisabled(!canUseTex);
                    std::string btnLabel = std::string("Use Selection##") + idSuffix;
                    if (ImGui::SmallButton(btnLabel.c_str())) {
                        path = ResolveSpriteSheetImagePath(fileBrowser.selectedFile).string();
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
                glm::vec4 inspectedBaseColor(inspectedMaterial.color, inspectedMaterial.alpha);
                if (ImGui::ColorEdit4("Base Color", &inspectedBaseColor.x)) {
                    inspectedMaterial.color = glm::vec3(inspectedBaseColor);
                    // Can this alpha properly work??
                    // Okay, never mind, that worked just fine; Modularity just didn't pass alpha to the ImGui renderer for some odd reason.
                    inspectedMaterial.alpha = std::clamp(inspectedBaseColor.w, 0.0f, 1.0f);
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
                const char* texFilterOptions[] = { "Bilinear", "Point" };
                int texFilterIndex = (inspectedMaterial.textureFilter == MaterialProperties::TextureFilter::Point) ? 1 : 0;
                if (ImGui::Combo("Texture Filter", &texFilterIndex, texFilterOptions, IM_ARRAYSIZE(texFilterOptions))) {
                    inspectedMaterial.textureFilter =
                        (texFilterIndex == 1) ? MaterialProperties::TextureFilter::Point
                                              : MaterialProperties::TextureFilter::Bilinear;
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
                const char* shaderPresetOptions[] = { "Custom", "Engine Lit (Default)", "Scrolling UV" };
                MaterialShaderPreset inspectedPreset = shaderPresetFromPaths(inspectedVertShader, inspectedFragShader);
                int inspectedPresetIndex = static_cast<int>(inspectedPreset);
                if (ImGui::Combo("Shader Type", &inspectedPresetIndex,
                                 shaderPresetOptions, IM_ARRAYSIZE(shaderPresetOptions)))
                {
                    if (applyShaderPreset(static_cast<MaterialShaderPreset>(inspectedPresetIndex),
                                          inspectedVertShader, inspectedFragShader))
                    {
                        matChanged = true;
                    }
                }
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
                    if (ImGui::BeginDragDropTarget()) {
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("FILE_PATH")) {
                            const char* dropped = static_cast<const char*>(payload->Data);
                            std::error_code ec;
                            fs::directory_entry droppedEntry(fs::path(dropped), ec);
                            if (!ec && fileBrowser.getFileCategory(droppedEntry) == FileCategory::Shader) {
                                path = dropped;
                                changed = true;
                            }
                        }
                        ImGui::EndDragDropTarget();
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
            bool isPlayingPreview = audio.isPreviewing(selectedAudioPath.string());

            ImGui::TextDisabled("%s", selectedAudioPath.filename().string().c_str());
            drawTrimmedPathText(selectedAudioPath.string(), ImVec4(0.78f, 0.88f, 1.0f, 1.0f));
            ImGui::Spacing();

            if (drawAudioPlayerIconButton(
                    "##AudioPreviewPlayButton",
                    isPlayingPreview
                        ? "Resources/Engine-Root/Audio Player/Play Button Toggled On.png"
                        : "Resources/Engine-Root/Audio Player/Play Button Toggled Off.png",
                    "Play",
                    isPlayingPreview ? "Stop preview" : "Play preview",
                    isPlayingPreview,
                    false,
                    ImVec2(42.0f, 42.0f),
                    ImVec4(0.92f, 0.55f, 0.30f, 1.0f))) {
                if (isPlayingPreview) {
                    audio.stopPreview();
                } else {
                    audio.playPreview(selectedAudioPath.string(), 1.0f, audioPreviewLoop);
                }
            }
            ImGui::SameLine();
            if (drawAudioPlayerIconButton(
                    "##AudioPreviewLoopButton",
                    audioPreviewLoop
                        ? "Resources/Engine-Root/Audio Player/Loop Toggled On.png"
                        : "Resources/Engine-Root/Audio Player/Loop Toggled Off.png",
                    "Loop",
                    audioPreviewLoop ? "Disable loop" : "Enable loop",
                    audioPreviewLoop,
                    false,
                    ImVec2(36.0f, 36.0f),
                    ImVec4(0.42f, 0.76f, 1.0f, 1.0f))) {
                audioPreviewLoop = !audioPreviewLoop;
                if (isPlayingPreview) {
                    audio.setPreviewLoop(audioPreviewLoop);
                }
            }
            ImGui::SameLine();
            if (drawAudioPlayerIconButton(
                    "##AudioPreviewAutoplayButton",
                    audioPreviewAutoPlay
                        ? "Resources/Engine-Root/Audio Player/Auto Play Toggled On.png"
                        : "Resources/Engine-Root/Audio Player/Auto Play Toggled Off.png",
                    "Auto",
                    audioPreviewAutoPlay ? "Disable auto play" : "Enable auto play",
                    audioPreviewAutoPlay,
                    false,
                    ImVec2(36.0f, 36.0f),
                    ImVec4(0.45f, 0.88f, 0.76f, 1.0f))) {
                audioPreviewAutoPlay = !audioPreviewAutoPlay;
                if (audioPreviewAutoPlay && !selectedAudioPath.empty() && !isPlayingPreview) {
                    audio.playPreview(selectedAudioPath.string(), 1.0f, audioPreviewLoop);
                }
            }

            if (selectedAudioPreview) {
                double cur = 0.0;
                double dur = selectedAudioPreview->durationSeconds;
                float progress = -1.0f;
                if (audio.getPreviewTime(selectedAudioPath.string(), cur, dur) && dur > 0.0001) {
                    progress = static_cast<float>(cur / dur);
                }
                ImGui::TextDisabled("%u channels  |  %u Hz  |  %.2fs",
                    selectedAudioPreview->channels,
                    selectedAudioPreview->sampleRate,
                    selectedAudioPreview->durationSeconds);
                ImVec2 waveSize(ImGui::GetContentRegionAvail().x, 80.0f);
                float seekRatio = -1.0f;
                drawWaveform("##AudioWaveAsset", selectedAudioPreview, waveSize, progress, &seekRatio);
                if (seekRatio >= 0.0f && dur > 0.0) {
                    audio.seekPreview(selectedAudioPath.string(), seekRatio * dur);
                }
                ImGui::Spacing();
                drawAudioTimeReadout(cur, dur);
            } else {
                ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.55f, 1.0f), "Unable to decode audio preview.");
            }

            if (target) {
                const float buttonWidth = 146.0f;
                const float rightEdge = ImGui::GetWindowContentRegionMax().x;
                const float cursorX = ImGui::GetCursorPosX();
                if (rightEdge - buttonWidth > cursorX) {
                    ImGui::SameLine(rightEdge - buttonWidth);
                } else {
                    ImGui::SameLine();
                }
                if (ImGui::Button("Assign to Selection", ImVec2(buttonWidth, 0.0f))) {
                    if (!target->hasAudioSource) {
                        target->hasAudioSource = true;
                        target->audioSource = AudioSourceComponent{};
                    }
                    target->audioSource.clipPath = selectedAudioPath.string();
                    projectManager.currentProject.hasUnsavedChanges = true;
                }
            }
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

            static float textureAssetPreviewZoom = 1.0f;
            Texture* previewTex = renderer.getTexture(selectedTexturePath.string(), MaterialProperties::TextureFilter::Point);

            ImGui::Spacing();
            if (previewTex && previewTex->GetID()) {
                ImGui::SliderFloat("Preview Zoom", &textureAssetPreviewZoom, 0.25f, 16.0f, "%.2fx", ImGuiSliderFlags_Logarithmic);
                float maxWidth = ImGui::GetContentRegionAvail().x;
                float size = std::min(maxWidth, 160.0f * textureAssetPreviewZoom);
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

    std::vector<SceneObject*> selectedObjects;
    selectedObjects.reserve(selectedObjectIds.size() + 1);
    std::unordered_set<int> seenSelectedIds;
    for (int id : selectedObjectIds) {
        if (id < 0 || seenSelectedIds.count(id) > 0) continue;
        if (SceneObject* selectedObj = findObjectById(id)) {
            selectedObjects.push_back(selectedObj);
            seenSelectedIds.insert(id);
        }
    }
    if (seenSelectedIds.count(obj.id) == 0) {
        selectedObjects.push_back(&obj);
    }

    const bool multiSelection = selectedObjects.size() > 1;

    auto allSelected = [&](auto&& predicate) {
        if (selectedObjects.empty()) return false;
        for (const SceneObject* selectedObj : selectedObjects) {
            if (!selectedObj || !predicate(*selectedObj)) return false;
        }
        return true;
    };
    auto hasMixedSelected = [&](auto&& predicate) {
        bool any = false;
        bool all = true;
        for (const SceneObject* selectedObj : selectedObjects) {
            if (!selectedObj) continue;
            const bool value = predicate(*selectedObj);
            any = any || value;
            all = all && value;
        }
        return any && !all;
    };

    const bool sharedUIObject = allSelected([&](const SceneObject& candidate) { return isUIObject(candidate); });
    const bool sharedCollider = allSelected([](const SceneObject& candidate) { return candidate.hasCollider; });
    const bool sharedPlayerController = allSelected([](const SceneObject& candidate) { return candidate.hasPlayerController; });
    const bool sharedRigidbody = allSelected([](const SceneObject& candidate) { return candidate.hasRigidbody; });
    const bool sharedRigidbody2D = allSelected([](const SceneObject& candidate) { return candidate.hasRigidbody2D; });
    const bool sharedCollider2D = allSelected([](const SceneObject& candidate) { return candidate.hasCollider2D; });
    const bool sharedParallax2D = allSelected([](const SceneObject& candidate) { return candidate.hasParallaxLayer2D; });
    const bool sharedAudioSource = allSelected([](const SceneObject& candidate) { return candidate.hasAudioSource; });
    const bool sharedGroundBaked = allSelected([](const SceneObject& candidate) { return candidate.hasGroundBakedType; });
    const bool sharedObstacle = allSelected([](const SceneObject& candidate) { return candidate.hasObsticleObject; });
    const bool sharedAgent = allSelected([](const SceneObject& candidate) { return candidate.hasAIAgent; });
    const bool sharedAnimation = allSelected([](const SceneObject& candidate) { return candidate.hasAnimation; });
    const bool sharedSkeletal = allSelected([](const SceneObject& candidate) { return candidate.hasSkeletalAnimation; });
    const bool sharedReverb = allSelected([](const SceneObject& candidate) { return candidate.hasReverbZone; });
    const bool sharedCamera = allSelected([](const SceneObject& candidate) { return candidate.hasCamera; });
    const bool sharedCameraFollow2D = allSelected([](const SceneObject& candidate) { return candidate.hasCameraFollow2D; });
    const bool sharedPostFX = allSelected([](const SceneObject& candidate) { return candidate.hasPostFX; });
    const bool sharedRenderer = allSelected([](const SceneObject& candidate) { return candidate.hasRenderer; });
    const bool sharedLight = allSelected([](const SceneObject& candidate) { return candidate.hasLight; });
    const bool sharedLight2D = allSelected([](const SceneObject& candidate) { return candidate.hasLight2D; });
    const bool sharedShadowCaster2D = allSelected([](const SceneObject& candidate) { return candidate.hasShadowCaster2D; });

    auto scriptSignature = [](const ScriptComponent& script) {
        return std::to_string(static_cast<int>(script.language)) + "|" + script.path + "|" + script.managedType;
    };
    auto hasScriptSignature = [&](const SceneObject& candidate, const std::string& signature) {
        for (const ScriptComponent& script : candidate.scripts) {
            if (scriptSignature(script) == signature) return true;
        }
        return false;
    };
    const bool sharedScriptsLayout = allSelected([&](const SceneObject& candidate) {
        if (candidate.scripts.size() != obj.scripts.size()) return false;
        for (size_t i = 0; i < obj.scripts.size(); ++i) {
            if (scriptSignature(candidate.scripts[i]) != scriptSignature(obj.scripts[i])) {
                return false;
            }
        }
        return true;
    });
    std::vector<bool> sharedScriptByIndex(obj.scripts.size(), !multiSelection);
    std::vector<std::string> sharedScriptSignatures(obj.scripts.size());
    if (multiSelection) {
        for (size_t i = 0; i < obj.scripts.size(); ++i) {
            const std::string signature = scriptSignature(obj.scripts[i]);
            sharedScriptSignatures[i] = signature;
            bool shared = true;
            for (const SceneObject* selectedObj : selectedObjects) {
                if (!selectedObj || selectedObj->id == obj.id) continue;
                if (!hasScriptSignature(*selectedObj, signature)) {
                    shared = false;
                    break;
                }
            }
            sharedScriptByIndex[i] = shared;
        }
    } else {
        for (size_t i = 0; i < obj.scripts.size(); ++i) {
            sharedScriptSignatures[i] = scriptSignature(obj.scripts[i]);
        }
    }

    const bool hasMixedComponents = multiSelection && (
        hasMixedSelected([&](const SceneObject& candidate) { return isUIObject(candidate); }) ||
        hasMixedSelected([](const SceneObject& candidate) { return candidate.hasCollider; }) ||
        hasMixedSelected([](const SceneObject& candidate) { return candidate.hasPlayerController; }) ||
        hasMixedSelected([](const SceneObject& candidate) { return candidate.hasRigidbody; }) ||
        hasMixedSelected([](const SceneObject& candidate) { return candidate.hasRigidbody2D; }) ||
        hasMixedSelected([](const SceneObject& candidate) { return candidate.hasCollider2D; }) ||
        hasMixedSelected([](const SceneObject& candidate) { return candidate.hasParallaxLayer2D; }) ||
        hasMixedSelected([](const SceneObject& candidate) { return candidate.hasAudioSource; }) ||
        hasMixedSelected([](const SceneObject& candidate) { return candidate.hasGroundBakedType; }) ||
        hasMixedSelected([](const SceneObject& candidate) { return candidate.hasObsticleObject; }) ||
        hasMixedSelected([](const SceneObject& candidate) { return candidate.hasAIAgent; }) ||
        hasMixedSelected([](const SceneObject& candidate) { return candidate.hasAnimation; }) ||
        hasMixedSelected([](const SceneObject& candidate) { return candidate.hasSkeletalAnimation; }) ||
        hasMixedSelected([](const SceneObject& candidate) { return candidate.hasReverbZone; }) ||
        hasMixedSelected([](const SceneObject& candidate) { return candidate.hasCamera; }) ||
        hasMixedSelected([](const SceneObject& candidate) { return candidate.hasCameraFollow2D; }) ||
        hasMixedSelected([](const SceneObject& candidate) { return candidate.hasPostFX; }) ||
        hasMixedSelected([](const SceneObject& candidate) { return candidate.hasRenderer; }) ||
        hasMixedSelected([](const SceneObject& candidate) { return candidate.hasLight; }) ||
        hasMixedSelected([](const SceneObject& candidate) { return candidate.hasLight2D; }) ||
        hasMixedSelected([](const SceneObject& candidate) { return candidate.hasShadowCaster2D; }) ||
        !sharedScriptsLayout
    );

    if (multiSelection) {
        ImGui::Text("Multiple objects selected: %zu", selectedObjects.size());
        ImGui::Separator();
    }

    bool objectNameChanged = false;
    bool objectEnabledChanged = false;
    bool objectLayerChanged = false;
    bool objectTagChanged = false;
    bool objectTransformChanged = false;
    bool uiSectionChanged = false;
    bool colliderSectionChanged = false;
    bool playerControllerSectionChanged = false;
    bool rigidbodySectionChanged = false;
    bool rigidbody2DSectionChanged = false;
    bool collider2DSectionChanged = false;
    bool parallax2DSectionChanged = false;
    bool audioSourceSectionChanged = false;
    bool groundBakedSectionChanged = false;
    bool obstacleSectionChanged = false;
    bool agentSectionChanged = false;
    bool animationSectionChanged = false;
    bool skeletalSectionChanged = false;
    bool reverbSectionChanged = false;
    bool cameraSectionChanged = false;
    bool cameraFollowSectionChanged = false;
    bool postFxSectionChanged = false;
    bool rendererSectionChanged = false;
    bool lightSectionChanged = false;
    bool light2DSectionChanged = false;
    bool shadowCaster2DSectionChanged = false;

    auto objectHeader = drawComponentHeader("Object Info", "ObjectInfo", nullptr, true, std::function<void()>{});
    if (objectHeader.open) {
        char nameBuffer[128];
        strncpy(nameBuffer, obj.name.c_str(), sizeof(nameBuffer));
        nameBuffer[sizeof(nameBuffer) - 1] = '\0';

        ImGui::Text("Name:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("##Name", nameBuffer, sizeof(nameBuffer))) {
            const std::string oldName = obj.name;
            const std::string newName = nameBuffer;
            if (oldName != newName) {
                obj.name = newName;
                objectNameChanged = true;
                propagateObjectRenameReferences(oldName, newName, obj.id);
                projectManager.currentProject.hasUnsavedChanges = true;
            }
        }

        ImGui::Text("Type:");
        ImGui::SameLine();
        const char* typeLabel = "Empty";
        if (obj.type == ObjectType::Sprite25D) {
            typeLabel = "2.5D Object";
        } else if (obj.hasRenderer) {
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
        } else if (obj.hasLight2D) {
            switch (obj.light2D.type) {
                case Light2DType::Point: typeLabel = "2D Point Light"; break;
                case Light2DType::Spot: typeLabel = "2D Spot Light"; break;
                case Light2DType::Freeform: typeLabel = "2D Freeform Light"; break;
                case Light2DType::Sprite: typeLabel = "2D Sprite Light"; break;
                case Light2DType::Global: typeLabel = "2D Global Light"; break;
            }
        } else if (obj.hasShadowCaster2D) {
            typeLabel = "2D Shadow Caster";
        } else if (obj.hasCamera) {
            typeLabel = "Camera";
        } else if (obj.hasPostFX) {
            typeLabel = "ModuVolume";
        }
        ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "%s", typeLabel);

        ImGui::Text("ID:");
        ImGui::SameLine();
        ImGui::TextDisabled("%d", obj.id);

        if (ImGui::Checkbox("Enabled##ObjEnabled", &obj.enabled)) {
            objectEnabledChanged = true;
            projectManager.currentProject.hasUnsavedChanges = true;
        }

        ImGui::Text("Layer:");
        ImGui::SameLine();
        int layer = obj.layer;
        ImGui::SetNextItemWidth(120);
        if (ImGui::SliderInt("##Layer", &layer, 0, 31)) {
            obj.layer = layer;
            objectLayerChanged = true;
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
            objectTagChanged = true;
            projectManager.currentProject.hasUnsavedChanges = true;
        }

        ImGui::Spacing();
        if (obj.hasPostFX) {
            ImGui::TextDisabled("Transform is ignored for post-processing nodes.");
        }
        if (obj.type == ObjectType::Sprite25D) {
            ImGui::TextDisabled("2.5D objects use the transform for 3D placement and the UI section for sprite content.");
        } else if (isUIObject(obj)) {
            ImGui::TextDisabled("UI objects use the UI section for positioning.");
        }

        ImGui::Text("Position");
        ImGui::PushItemWidth(-1);
        if (ImGui::DragFloat3("##Position", &obj.position.x, 0.1f)) {
            syncLocalTransform(obj);
            objectTransformChanged = true;
            projectManager.currentProject.hasUnsavedChanges = true;
        }
        ImGui::PopItemWidth();

        ImGui::Text("Rotation");
        ImGui::PushItemWidth(-1);
        if (ImGui::DragFloat3("##Rotation", &obj.rotation.x, 1.0f, -360.0f, 360.0f)) {
            obj.rotation = NormalizeEulerDegrees(obj.rotation);
            syncLocalTransform(obj);
            objectTransformChanged = true;
            projectManager.currentProject.hasUnsavedChanges = true;
        }
        ImGui::PopItemWidth();

        ImGui::Text("Scale");
        ImGui::PushItemWidth(-1);
        if (ImGui::DragFloat3("##Scale", &obj.scale.x, 0.05f, 0.01f, 100.0f)) {
            syncLocalTransform(obj);
            objectTransformChanged = true;
            projectManager.currentProject.hasUnsavedChanges = true;
        }
        ImGui::PopItemWidth();

        if (ImGui::Button("Reset Transform", ImVec2(-1, 0))) {
            obj.position = glm::vec3(0.0f);
            obj.rotation = glm::vec3(0.0f);
            obj.scale = glm::vec3(1.0f);
            syncLocalTransform(obj);
            objectTransformChanged = true;
            projectManager.currentProject.hasUnsavedChanges = true;
        }
    }

    ImGui::Spacing();

    if (isUIObject(obj) && sharedUIObject) {
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

            if (obj.type != ObjectType::Sprite25D) {
                const char* anchors[] = { "Center", "Top Left", "Top Right", "Bottom Left", "Bottom Right" };
                int anchor = static_cast<int>(obj.ui.anchor);
                if (ImGui::Combo("Anchor", &anchor, anchors, IM_ARRAYSIZE(anchors))) {
                    obj.ui.anchor = static_cast<UIAnchor>(anchor);
                    changed = true;
                }

                if (ImGui::DragFloat2("Position (px)", &obj.ui.position.x, 1.0f)) {
                    changed = true;
                }
            } else {
                ImGui::TextDisabled("Anchor and UI position are ignored for projected 2.5D sprites.");
            }

            if (ImGui::DragFloat("Rotation (deg)", &obj.ui.rotation, 0.5f, -360.0f, 360.0f)) {
                glm::vec3 rot(0.0f, 0.0f, obj.ui.rotation);
                rot = NormalizeEulerDegrees(rot);
                obj.ui.rotation = rot.z;
                changed = true;
            }

            glm::vec2 minSize(1.0f, 1.0f);
            if (obj.ui.type == UIElementType::Image || obj.ui.type == UIElementType::Sprite2D) {
                minSize = glm::vec2(0.01f, 0.01f);
            }
            if (ImGui::DragFloat2("Size (px)", &obj.ui.size.x, 1.0f, minSize.x, 4096.0f)) {
                obj.ui.size.x = std::max(minSize.x, obj.ui.size.x);
                obj.ui.size.y = std::max(minSize.y, obj.ui.size.y);
                changed = true;
            }

            if (obj.ui.type == UIElementType::Canvas) {
                if (ImGui::Checkbox("Render In 3D", &obj.ui.renderIn3D)) {
                    changed = true;
                }
                if (ImGui::Checkbox("Mask Children", &obj.ui.maskChildren)) {
                    changed = true;
                }
                if (obj.ui.renderIn3D) {
                    if (ImGui::Checkbox("Face Camera", &obj.faceCamera)) {
                        changed = true;
                    }
                    int size[2] = { obj.ui.renderTargetSize.x, obj.ui.renderTargetSize.y };
                    if (ImGui::DragInt2("Render Target (px)", size, 1.0f, 16, 4096)) {
                        obj.ui.renderTargetSize.x = std::max(16, size[0]);
                        obj.ui.renderTargetSize.y = std::max(16, size[1]);
                        changed = true;
                    }
                    ImGui::TextDisabled("Canvas renders on a 3D quad; use object scale for world size.");
                } else {
                    if (ImGui::Checkbox("Pseudo 3D Enabled", &obj.ui.pseudo3DEnabled)) {
                        changed = true;
                    }
                    if (obj.ui.pseudo3DEnabled) {
                        if (ImGui::Checkbox("Use Offscreen Surface", &obj.ui.pseudo3DUseOffscreenSurface)) {
                            changed = true;
                        }

                        if (ImGui::DragFloat2("Pseudo Panel Size", &obj.ui.pseudo3DPanelSize.x, 1.0f, 0.0f, 4096.0f)) {
                            obj.ui.pseudo3DPanelSize.x = std::max(0.0f, obj.ui.pseudo3DPanelSize.x);
                            obj.ui.pseudo3DPanelSize.y = std::max(0.0f, obj.ui.pseudo3DPanelSize.y);
                            changed = true;
                        }
                        int pseudoRt[2] = { obj.ui.renderTargetSize.x, obj.ui.renderTargetSize.y };
                        if (ImGui::DragInt2("Pseudo RT (px)", pseudoRt, 1.0f, 16, 4096)) {
                            obj.ui.renderTargetSize.x = std::max(16, pseudoRt[0]);
                            obj.ui.renderTargetSize.y = std::max(16, pseudoRt[1]);
                            changed = true;
                        }

                        if (ImGui::DragFloat2("Top Left Offset", &obj.ui.pseudo3DTopLeftOffset.x, 0.25f, -4096.0f, 4096.0f)) changed = true;
                        if (ImGui::DragFloat2("Top Right Offset", &obj.ui.pseudo3DTopRightOffset.x, 0.25f, -4096.0f, 4096.0f)) changed = true;
                        if (ImGui::DragFloat2("Bottom Right Offset", &obj.ui.pseudo3DBottomRightOffset.x, 0.25f, -4096.0f, 4096.0f)) changed = true;
                        if (ImGui::DragFloat2("Bottom Left Offset", &obj.ui.pseudo3DBottomLeftOffset.x, 0.25f, -4096.0f, 4096.0f)) changed = true;

                        if (ImGui::SliderFloat2("Pseudo Pivot", &obj.ui.pseudo3DPivot.x, 0.0f, 1.0f, "%.2f")) {
                            obj.ui.pseudo3DPivot.x = std::clamp(obj.ui.pseudo3DPivot.x, 0.0f, 1.0f);
                            obj.ui.pseudo3DPivot.y = std::clamp(obj.ui.pseudo3DPivot.y, 0.0f, 1.0f);
                            changed = true;
                        }

                        if (ImGui::DragFloat("Perspective Intensity", &obj.ui.pseudo3DPerspectiveIntensity, 0.01f, -2.0f, 2.0f, "%.2f")) changed = true;
                        if (ImGui::DragFloat("Skew Amount", &obj.ui.pseudo3DSkewAmount, 0.01f, -2.0f, 2.0f, "%.2f")) changed = true;
                        if (ImGui::DragFloat("Curvature Amount", &obj.ui.pseudo3DCurvatureAmount, 0.01f, -2.0f, 2.0f, "%.2f")) changed = true;

                        std::string anchorLabel = "None";
                        if (obj.ui.pseudo3DAnchorTargetId >= 0) {
                            if (const SceneObject* anchorObj = findObjectById(obj.ui.pseudo3DAnchorTargetId)) {
                                anchorLabel = anchorObj->name + " (" + std::to_string(anchorObj->id) + ")";
                            }
                        }
                        if (ImGui::BeginCombo("Anchor Target", anchorLabel.c_str())) {
                            if (ImGui::Selectable("None", obj.ui.pseudo3DAnchorTargetId < 0)) {
                                obj.ui.pseudo3DAnchorTargetId = -1;
                                changed = true;
                            }
                            for (const auto& candidate : sceneObjects) {
                                if (candidate.id == obj.id) continue;
                                const std::string label = candidate.name + " (" + std::to_string(candidate.id) + ")";
                                const bool selected = (candidate.id == obj.ui.pseudo3DAnchorTargetId);
                                if (ImGui::Selectable(label.c_str(), selected)) {
                                    obj.ui.pseudo3DAnchorTargetId = candidate.id;
                                    changed = true;
                                }
                                if (selected) ImGui::SetItemDefaultFocus();
                            }
                            ImGui::EndCombo();
                        }

                        if (ImGui::Checkbox("Distance Scaling Enabled", &obj.ui.pseudo3DDistanceScalingEnabled)) changed = true;
                        if (obj.ui.pseudo3DDistanceScalingEnabled) {
                            if (ImGui::DragFloat("Min Distance", &obj.ui.pseudo3DMinDistance, 0.05f, 0.01f, 10000.0f, "%.2f")) {
                                obj.ui.pseudo3DMinDistance = std::max(0.01f, obj.ui.pseudo3DMinDistance);
                                changed = true;
                            }
                            if (ImGui::DragFloat("Max Distance", &obj.ui.pseudo3DMaxDistance, 0.05f, 0.02f, 10000.0f, "%.2f")) {
                                obj.ui.pseudo3DMaxDistance = std::max(obj.ui.pseudo3DMinDistance + 0.01f, obj.ui.pseudo3DMaxDistance);
                                changed = true;
                            }
                            if (ImGui::Checkbox("Perspective Scales With Distance", &obj.ui.pseudo3DAdjustPerspectiveWithDistance)) changed = true;
                        }

                        if (ImGui::DragFloat("Interaction Distance", &obj.ui.pseudo3DInteractionDistance, 0.05f, 0.0f, 10000.0f, "%.2f")) {
                            obj.ui.pseudo3DInteractionDistance = std::max(0.0f, obj.ui.pseudo3DInteractionDistance);
                            changed = true;
                        }
                        ImGui::TextDisabled("Interaction Distance = 0 disables distance gating.");
                        if (ImGui::DragInt("Depth Sort / Draw Order", &obj.ui.pseudo3DDepthSort, 1.0f, -2048, 2048)) changed = true;
                        if (ImGui::Checkbox("Allow Interaction", &obj.ui.pseudo3DAllowInteraction)) changed = true;
                    }
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
                if (obj.ui.type == UIElementType::Text) {
                    char labelBuf[4096] = {};
                    std::snprintf(labelBuf, sizeof(labelBuf), "%s", obj.ui.label.c_str());
                    if (ImGui::InputTextMultiline("Text", labelBuf, sizeof(labelBuf), ImVec2(-FLT_MIN, 96.0f))) {
                        obj.ui.label = labelBuf;
                        changed = true;
                    }
                } else {
                    char labelBuf[128] = {};
                    std::snprintf(labelBuf, sizeof(labelBuf), "%s", obj.ui.label.c_str());
                    if (ImGui::InputText("Label", labelBuf, sizeof(labelBuf))) {
                        obj.ui.label = labelBuf;
                        changed = true;
                    }
                }
            }
            if (obj.ui.type == UIElementType::Text) {
                if (ImGui::DragFloat("Text Size", &obj.ui.textScale, 0.05f, 0.1f, 10.0f, "%.2f")) {
                    obj.ui.textScale = std::max(0.1f, obj.ui.textScale);
                    changed = true;
                }
                if (ImGui::Checkbox("Auto Wrap", &obj.ui.textAutoWrap)) {
                    changed = true;
                }
                const char* hAlignLabels[] = { "Left", "Center", "Right" };
                int hAlignIndex = static_cast<int>(obj.ui.textHAlign);
                if (ImGui::Combo("Horizontal Align", &hAlignIndex, hAlignLabels, IM_ARRAYSIZE(hAlignLabels))) {
                    obj.ui.textHAlign = static_cast<UITextHAlign>(std::clamp(hAlignIndex, 0, 2));
                    changed = true;
                }
                const char* vAlignLabels[] = { "Top", "Middle", "Bottom" };
                int vAlignIndex = static_cast<int>(obj.ui.textVAlign);
                if (ImGui::Combo("Vertical Align", &vAlignIndex, vAlignLabels, IM_ARRAYSIZE(vAlignLabels))) {
                    obj.ui.textVAlign = static_cast<UITextVAlign>(std::clamp(vAlignIndex, 0, 2));
                    changed = true;
                }
                if (ImGui::DragFloat("Effect Speed", &obj.ui.textEffectSpeed, 0.01f, 0.01f, 20.0f, "%.2f")) {
                    obj.ui.textEffectSpeed = std::max(0.01f, obj.ui.textEffectSpeed);
                    changed = true;
                }
                if (ImGui::DragFloat("Effect Intensity", &obj.ui.textEffectIntensity, 0.01f, 0.0f, 10.0f, "%.2f")) {
                    obj.ui.textEffectIntensity = std::max(0.0f, obj.ui.textEffectIntensity);
                    changed = true;
                }
                int textEffectFlags = obj.ui.textEffectFlags;
                bool wave = (textEffectFlags & (1 << 0)) != 0;
                bool shake = (textEffectFlags & (1 << 1)) != 0;
                bool bounce = (textEffectFlags & (1 << 2)) != 0;
                bool rotate = (textEffectFlags & (1 << 3)) != 0;
                bool fade = (textEffectFlags & (1 << 4)) != 0;
                if (ImGui::Checkbox("Wave", &wave)) {
                    if (wave) textEffectFlags |= (1 << 0);
                    else textEffectFlags &= ~(1 << 0);
                    changed = true;
                }
                ImGui::SameLine();
                if (ImGui::Checkbox("Shake", &shake)) {
                    if (shake) textEffectFlags |= (1 << 1);
                    else textEffectFlags &= ~(1 << 1);
                    changed = true;
                }
                ImGui::SameLine();
                if (ImGui::Checkbox("Bounce", &bounce)) {
                    if (bounce) textEffectFlags |= (1 << 2);
                    else textEffectFlags &= ~(1 << 2);
                    changed = true;
                }
                if (ImGui::Checkbox("Rotate", &rotate)) {
                    if (rotate) textEffectFlags |= (1 << 3);
                    else textEffectFlags &= ~(1 << 3);
                    changed = true;
                }
                ImGui::SameLine();
                if (ImGui::Checkbox("Fade", &fade)) {
                    if (fade) textEffectFlags |= (1 << 4);
                    else textEffectFlags &= ~(1 << 4);
                    changed = true;
                }
                obj.ui.textEffectFlags = textEffectFlags;
                const char* textFilterOptions[] = { "Bilinear", "Point" };
                int textFilterIndex = (obj.material.textureFilter == MaterialProperties::TextureFilter::Point) ? 1 : 0;
                if (ImGui::Combo("Text Filter", &textFilterIndex, textFilterOptions, IM_ARRAYSIZE(textFilterOptions))) {
                    obj.material.textureFilter =
                        (textFilterIndex == 1) ? MaterialProperties::TextureFilter::Point
                                               : MaterialProperties::TextureFilter::Bilinear;
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
                    obj.ui.spriteCustomFrames.clear();
                    obj.ui.spriteCustomFrameNames.clear();
                    obj.ui.spriteCustomFramesEnabled = false;
                    obj.ui.spriteSourceWidth = 0;
                    obj.ui.spriteSourceHeight = 0;
                    changed = true;
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Clear##UITexture")) {
                    obj.albedoTexturePath.clear();
                    obj.ui.spriteCustomFrames.clear();
                    obj.ui.spriteCustomFrameNames.clear();
                    obj.ui.spriteCustomFramesEnabled = false;
                    obj.ui.spriteSourceWidth = 0;
                    obj.ui.spriteSourceHeight = 0;
                    obj.ui.spriteSheetFrame = 0;
                    changed = true;
                }
                ImGui::SameLine();
                bool canUseTex = isTextureOrSpriteSheetSelection(fileBrowser.selectedFile);
                ImGui::BeginDisabled(!canUseTex);
                if (ImGui::SmallButton("Use Selection##UITexture")) {
                    if (assignSpriteTextureOrClips(obj, fileBrowser.selectedFile)) {
                        changed = true;
                    }
                }
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::BeginDisabled(obj.albedoTexturePath.empty());
                if (ImGui::SmallButton("Reload Clips##UITexture")) {
                    if (assignSpriteTextureOrClips(obj, fs::path(obj.albedoTexturePath))) {
                        changed = true;
                    }
                }
                ImGui::EndDisabled();
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("FILE_PATH")) {
                        const char* dropped = static_cast<const char*>(payload->Data);
                        if (assignSpriteTextureOrClips(obj, fs::path(dropped))) {
                            changed = true;
                        }
                    }
                    ImGui::EndDragDropTarget();
                }

                if (!obj.albedoTexturePath.empty()) {
                    ImGui::SameLine();
                    if (hasSpritesheetPackage() && ImGui::SmallButton("Import Sheet##UISpriteSheet")) {
                        pendingSpriteSheetPath = obj.albedoTexturePath;
                        std::snprintf(importSpriteSheetName, sizeof(importSpriteSheetName), "%s", obj.name.c_str());
                        importSpriteSheetAsSprite2D = (obj.ui.type == UIElementType::Sprite2D);
                        showImportSpriteSheetDialog = true;
                    }
                }

                if (Texture* previewTex = (!obj.albedoTexturePath.empty()
                        ? renderer.getTexture(obj.albedoTexturePath, MaterialProperties::TextureFilter::Point)
                        : nullptr)) {
                    if (previewTex->GetID()) {
                        ImGui::Spacing();
                        ImGui::TextDisabled("Sprite Preview");
                        std::array<ImVec2, 4> uvQuad = buildSpriteSheetUvs(obj);
                        ImVec2 uvMin(uvQuad[0].x, uvQuad[0].y);
                        ImVec2 uvMax(uvQuad[2].x, uvQuad[2].y);
                        float frameWidth = static_cast<float>(previewTex->GetWidth());
                        float frameHeight = static_cast<float>(previewTex->GetHeight());
                        if (obj.ui.spriteCustomFramesEnabled && !obj.ui.spriteCustomFrames.empty()) {
                            const glm::ivec4 frame = obj.ui.spriteCustomFrames[std::clamp(obj.ui.spriteSheetFrame, 0, static_cast<int>(obj.ui.spriteCustomFrames.size()) - 1)];
                            frameWidth = static_cast<float>(frame.z);
                            frameHeight = static_cast<float>(frame.w);
                        } else if (obj.ui.spriteSheetEnabled) {
                            frameWidth = std::max(1.0f, frameWidth / static_cast<float>(std::max(1, obj.ui.spriteSheetColumns)));
                            frameHeight = std::max(1.0f, frameHeight / static_cast<float>(std::max(1, obj.ui.spriteSheetRows)));
                        }
                        float previewWidth = std::min(ImGui::GetContentRegionAvail().x, 196.0f);
                        float aspect = frameWidth > 0.0f ? (frameHeight / frameWidth) : 1.0f;
                        ImVec2 previewSize(previewWidth, std::max(64.0f, previewWidth * aspect));
                        ImGui::Image((ImTextureID)(intptr_t)previewTex->GetID(), previewSize, uvMin, uvMax);
                    }
                }

                if (hasSpritesheetPackage() &&
                    ImGui::CollapsingHeader("Sprite Sheet", ImGuiTreeNodeFlags_DefaultOpen)) {
                    if (ImGui::Checkbox("Enable Sprite Sheet", &obj.ui.spriteSheetEnabled)) {
                        changed = true;
                    }
                    ImGui::BeginDisabled(!obj.ui.spriteSheetEnabled);
                    const bool usingCustomClips = obj.ui.spriteCustomFramesEnabled && !obj.ui.spriteCustomFrames.empty();
                    if (usingCustomClips) {
                        const int clipCount = static_cast<int>(obj.ui.spriteCustomFrames.size());
                        EnsureSpriteClipNames(obj.ui.spriteCustomFrameNames, obj.ui.spriteCustomFrames.size());
                        ImGui::TextDisabled("Using %d cropped sprite clips.", clipCount);
                        obj.ui.spriteSheetFrame = std::clamp(obj.ui.spriteSheetFrame, 0, clipCount - 1);
                        const char* previewName = obj.ui.spriteCustomFrameNames[obj.ui.spriteSheetFrame].c_str();
                        if (ImGui::BeginCombo("Clip", previewName)) {
                            for (int clipIndex = 0; clipIndex < clipCount; ++clipIndex) {
                                bool selected = (clipIndex == obj.ui.spriteSheetFrame);
                                if (ImGui::Selectable(obj.ui.spriteCustomFrameNames[clipIndex].c_str(), selected)) {
                                    obj.ui.spriteSheetFrame = clipIndex;
                                    changed = true;
                                }
                                if (selected) ImGui::SetItemDefaultFocus();
                            }
                            ImGui::EndCombo();
                        }
                        int clipIndex = obj.ui.spriteSheetFrame;
                        if (ImGui::SliderInt("Clip Index", &clipIndex, 0, clipCount - 1)) {
                            obj.ui.spriteSheetFrame = std::clamp(clipIndex, 0, clipCount - 1);
                            changed = true;
                        }
                    } else {
                        if (ImGui::DragInt("Columns", &obj.ui.spriteSheetColumns, 1.0f, 1, 1024)) {
                            obj.ui.spriteSheetColumns = std::max(1, obj.ui.spriteSheetColumns);
                            changed = true;
                        }
                        if (ImGui::DragInt("Rows", &obj.ui.spriteSheetRows, 1.0f, 1, 1024)) {
                            obj.ui.spriteSheetRows = std::max(1, obj.ui.spriteSheetRows);
                            changed = true;
                        }
                        int frameCount = std::max(1, obj.ui.spriteSheetColumns * obj.ui.spriteSheetRows);
                        if (ImGui::SliderInt("Frame", &obj.ui.spriteSheetFrame, 0, frameCount - 1)) {
                            obj.ui.spriteSheetFrame = std::clamp(obj.ui.spriteSheetFrame, 0, frameCount - 1);
                            changed = true;
                        }
                        if (ImGui::DragFloat("FPS", &obj.ui.spriteSheetFps, 0.1f, 1.0f, 120.0f, "%.1f")) {
                            obj.ui.spriteSheetFps = std::clamp(obj.ui.spriteSheetFps, 1.0f, 120.0f);
                            changed = true;
                        }
                        if (ImGui::Checkbox("Loop", &obj.ui.spriteSheetLoop)) {
                            changed = true;
                        }
                    }
                    ImGui::EndDisabled();
                }

                if (ImGui::CollapsingHeader("9-Slice")) {
                    if (ImGui::Checkbox("Enable 9-Slice", &obj.ui.nineSliceEnabled)) {
                        changed = true;
                    }
                    ImGui::BeginDisabled(!obj.ui.nineSliceEnabled);
                    float border[4] = {
                        obj.ui.nineSliceBorder.x,
                        obj.ui.nineSliceBorder.y,
                        obj.ui.nineSliceBorder.z,
                        obj.ui.nineSliceBorder.w
                    };
                    if (ImGui::DragFloat4("Border L/R/T/B", border, 1.0f, 0.0f, 2048.0f, "%.0f")) {
                        obj.ui.nineSliceBorder = glm::vec4(
                            std::max(0.0f, border[0]),
                            std::max(0.0f, border[1]),
                            std::max(0.0f, border[2]),
                            std::max(0.0f, border[3]));
                        changed = true;
                    }
                    if (ImGui::Checkbox("Tile Edges", &obj.ui.nineSliceTileEdges)) {
                        changed = true;
                    }
                    if (ImGui::Checkbox("Tile Center", &obj.ui.nineSliceTileCenter)) {
                        changed = true;
                    }
                    ImGui::EndDisabled();
                }

                if (ImGui::CollapsingHeader("2D Lighting", ImGuiTreeNodeFlags_DefaultOpen)) {
                    if (ImGui::Checkbox("Receive Lighting", &obj.ui.receiveLighting2D)) {
                        changed = true;
                    }
                    if (ImGui::Checkbox("Force Unlit", &obj.ui.unlitLighting2D)) {
                        changed = true;
                    }
                    if (ImGui::SliderFloat("Emissive", &obj.ui.emissiveLighting2D, 0.0f, 8.0f, "%.2f")) {
                        changed = true;
                    }
                    ImGui::TextDisabled("Nine-slice and masked sprites fall back to the legacy 2D draw path.");
                    const auto routingIt = light2DObjectRoutingReasonsLastFrame.find(obj.id);
                    if (routingIt != light2DObjectRoutingReasonsLastFrame.end()) {
                        ImGui::SeparatorText("Runtime Debug");
                        ImGui::Text("Compositor Ran: %s", light2DCompositorRanLastFrame ? "Yes" : "No");
                        ImGui::Text("Light Buffer: %s", light2DLightBufferHadContentLastFrame ? "Non-empty" : "Empty");
                        ImGui::Text("Active Lights: %d", light2DActiveCountLastFrame);
                        ImGui::Text("Lit Sprite2D: %d", light2DLitSprite2DCountLastFrame);
                        ImGui::Text("Lit UI Images: %d", light2DLitWorldImageCountLastFrame);
                        ImGui::TextWrapped("%s", routingIt->second.c_str());
                    }
                }
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
            uiSectionChanged = true;
            projectManager.currentProject.hasUnsavedChanges = true;
        }
        ImGui::PopStyleColor();
    }

    if (obj.hasCollider && sharedCollider) {
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

            if (ImGui::DragFloat3("Offset", &obj.collider.offset.x, 0.01f, -1000.0f, 1000.0f, "%.3f")) {
                changed = true;
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
            colliderSectionChanged = true;
            projectManager.currentProject.hasUnsavedChanges = true;
        }
        ImGui::PopStyleColor();
    }

    if (obj.hasPlayerController && sharedPlayerController) {
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
            if (obj.hasPlayerController) {
                obj.hasCollider = true;
                obj.collider.type = ColliderType::Capsule;
                obj.collider.convex = true;
                obj.hasRigidbody = true;
                obj.rigidbody.enabled = true;
                obj.rigidbody.useGravity = true;
            }
            playerControllerSectionChanged = true;
            projectManager.currentProject.hasUnsavedChanges = true;
        }
        ImGui::PopStyleColor();
    }

    if (obj.hasRigidbody && sharedRigidbody) {
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
            rigidbodySectionChanged = true;
            projectManager.currentProject.hasUnsavedChanges = true;
        }
        ImGui::PopStyleColor();
    }

    if (obj.hasRigidbody2D && sharedRigidbody2D) {
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
            rigidbody2DSectionChanged = true;
            projectManager.currentProject.hasUnsavedChanges = true;
        }
        ImGui::PopStyleColor();
    }

    if (obj.hasCollider2D && sharedCollider2D) {
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

            if (ImGui::DragFloat2("Offset", &obj.collider2D.offset.x, 0.1f, -10000.0f, 10000.0f, "%.2f")) {
                changed = true;
            }

            ImGui::Unindent(10.0f);
            ImGui::PopID();
        }
        if (removeCollider2D) {
            obj.hasCollider2D = false;
            changed = true;
        }
        if (changed) {
            collider2DSectionChanged = true;
            projectManager.currentProject.hasUnsavedChanges = true;
        }
        ImGui::PopStyleColor();
    }

    if (obj.hasParallaxLayer2D && sharedParallax2D) {
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
            int lowerCount = 0;
            int higherCount = 0;
            for (const auto& other : sceneObjects) {
                if (other.id == obj.id || !other.enabled) continue;
                if (!other.hasParallaxLayer2D || !other.parallaxLayer2D.enabled) continue;
                if (!isUIObject(other)) continue;
                if (other.parallaxLayer2D.order < obj.parallaxLayer2D.order) ++lowerCount;
                if (other.parallaxLayer2D.order > obj.parallaxLayer2D.order) ++higherCount;
            }
            ImGui::TextDisabled("Layer stack: %d behind, %d in front", lowerCount, higherCount);
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
            if (ImGui::Checkbox("Disable Culling", &obj.parallaxLayer2D.disableCulling)) {
                changed = true;
            }
            ImGui::TextDisabled("Keeps this parallax object rendering even when it moves outside the current world overlay.");
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
            parallax2DSectionChanged = true;
            projectManager.currentProject.hasUnsavedChanges = true;
        }
        ImGui::PopStyleColor();
    }

    if (obj.hasAudioSource && sharedAudioSource) {
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
            const bool previewPlaying = !src.clipPath.empty() && audio.isPreviewing(src.clipPath);
            ImGui::TextDisabled("%s", src.clipPath.empty() ? "No clip selected" : fs::path(src.clipPath).filename().string().c_str());

            if (clipPreview) {
                ImGui::TextDisabled("%u channels  |  %u Hz  |  %.2fs",
                    clipPreview->channels,
                    clipPreview->sampleRate,
                    clipPreview->durationSeconds);
            } else {
                ImGui::TextDisabled("Load an audio clip to preview timing and waveform.");
            }

            ImGui::Spacing();
            if (drawAudioPlayerIconButton(
                    "##AudioSourcePlayButton",
                    previewPlaying
                        ? "Resources/Engine-Root/Audio Player/Play Button Toggled On.png"
                        : "Resources/Engine-Root/Audio Player/Play Button Toggled Off.png",
                    "Play",
                    previewPlaying ? "Stop preview" : "Play preview",
                    previewPlaying,
                    src.clipPath.empty(),
                    ImVec2(42.0f, 42.0f),
                    ImVec4(0.92f, 0.55f, 0.30f, 1.0f))) {
                if (previewPlaying) {
                    audio.stopPreview();
                } else {
                    audio.playPreview(src.clipPath, src.volume, src.loop);
                }
            }
            ImGui::SameLine();
            if (drawAudioPlayerIconButton(
                    "##AudioSourceLoopButton",
                    src.loop
                        ? "Resources/Engine-Root/Audio Player/Loop Toggled On.png"
                        : "Resources/Engine-Root/Audio Player/Loop Toggled Off.png",
                    "Loop",
                    src.loop ? "Disable loop" : "Enable loop",
                    src.loop,
                    src.clipPath.empty(),
                    ImVec2(36.0f, 36.0f),
                    ImVec4(0.42f, 0.76f, 1.0f, 1.0f))) {
                src.loop = !src.loop;
                changed = true;
                if (previewPlaying) {
                    audio.setPreviewLoop(src.loop);
                }
            }

            ImVec2 waveSize(ImGui::GetContentRegionAvail().x, 64.0f);
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

            ImGui::Spacing();
            drawAudioTimeReadout(cur, dur);

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
            audioSourceSectionChanged = true;
            projectManager.currentProject.hasUnsavedChanges = true;
        }
        ImGui::PopStyleColor();
    }

    if (obj.hasGroundBakedType && sharedGroundBaked) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.28f, 0.5f, 0.34f, 1.0f));
        bool removeGroundBaked = false;
        bool changed = false;
        auto header = drawComponentHeader("GroundBakedType", "GroundBakedType", &obj.groundBakedType.enabled, true, [&]() {
            if (ImGui::MenuItem("Open AI Pathfinding")) {
                showAIPathfindingWindow = true;
            }
            if (ImGui::MenuItem("Remove")) {
                removeGroundBaked = true;
            }
        });
        if (header.enabledChanged) {
            changed = true;
        }
        if (header.open) {
            ImGui::PushID("GroundBakedType");
            ImGui::Indent(10.0f);

            if (ImGui::Button("Open AI Pathfinding")) {
                showAIPathfindingWindow = true;
            }
            if (ImGui::Checkbox("Include In Bake", &obj.groundBakedType.includeInBake)) {
                changed = true;
            }
            if (ImGui::DragFloat("Area Cost", &obj.groundBakedType.areaCost, 0.05f, 0.1f, 100.0f, "%.2f")) {
                obj.groundBakedType.areaCost = std::clamp(obj.groundBakedType.areaCost, 0.1f, 100.0f);
                changed = true;
            }
            ImGui::TextDisabled("Objects marked here are considered walkable during bake.");

            ImGui::Unindent(10.0f);
            ImGui::PopID();
        }
        if (removeGroundBaked) {
            obj.hasGroundBakedType = false;
            obj.groundBakedType = GroundBakedTypeComponent{};
            changed = true;
        }
        if (changed) {
            groundBakedSectionChanged = true;
            projectManager.currentProject.hasUnsavedChanges = true;
        }
        ImGui::PopStyleColor();
    }

    if (obj.hasObsticleObject && sharedObstacle) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.5f, 0.3f, 0.28f, 1.0f));
        bool removeObstacle = false;
        bool changed = false;
        auto header = drawComponentHeader("ObsticleObject", "ObsticleObject", &obj.obsticleObject.enabled, true, [&]() {
            if (ImGui::MenuItem("Open AI Pathfinding")) {
                showAIPathfindingWindow = true;
            }
            if (ImGui::MenuItem("Remove")) {
                removeObstacle = true;
            }
        });
        if (header.enabledChanged) {
            changed = true;
        }
        if (header.open) {
            ImGui::PushID("ObsticleObject");
            ImGui::Indent(10.0f);

            if (ImGui::Button("Open AI Pathfinding")) {
                showAIPathfindingWindow = true;
            }
            if (ImGui::Checkbox("Carve", &obj.obsticleObject.carve)) {
                changed = true;
            }
            if (ImGui::DragFloat("Padding", &obj.obsticleObject.padding, 0.02f, 0.0f, 10.0f, "%.2f")) {
                obj.obsticleObject.padding = std::clamp(obj.obsticleObject.padding, 0.0f, 10.0f);
                changed = true;
            }
            ImGui::TextDisabled("Obstacle regions are removed from the baked walkable map.");

            ImGui::Unindent(10.0f);
            ImGui::PopID();
        }
        if (removeObstacle) {
            obj.hasObsticleObject = false;
            obj.obsticleObject = ObsticleObjectComponent{};
            changed = true;
        }
        if (changed) {
            obstacleSectionChanged = true;
            projectManager.currentProject.hasUnsavedChanges = true;
        }
        ImGui::PopStyleColor();
    }

    if (obj.hasAIAgent && sharedAgent) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.32f, 0.4f, 0.58f, 1.0f));
        bool removeAgent = false;
        bool changed = false;
        auto header = drawComponentHeader("AI Agent", "AIAgent", &obj.aiAgent.enabled, true, [&]() {
            if (ImGui::MenuItem("Open AI Pathfinding")) {
                showAIPathfindingWindow = true;
                aiPreviewAgentId = obj.id;
            }
            if (ImGui::MenuItem("Remove")) {
                removeAgent = true;
            }
        });
        if (header.enabledChanged) {
            changed = true;
        }
        if (header.open) {
            ImGui::PushID("AIAgent");
            ImGui::Indent(10.0f);

            if (ImGui::Button("Open AI Pathfinding")) {
                showAIPathfindingWindow = true;
                aiPreviewAgentId = obj.id;
            }

            if (ImGui::Checkbox("Use Target Object", &obj.aiAgent.useTargetObject)) {
                changed = true;
            }
            if (obj.aiAgent.useTargetObject) {
                SceneObject* target = findObjectById(obj.aiAgent.targetId);
                ImGui::TextDisabled("Target: %s",
                    (target && target->enabled) ? target->name.c_str() : "<none>");

                SceneObject* selectedTarget = findObjectById(selectedObjectId);
                bool canUseSelection = selectedTarget && selectedTarget->id != obj.id;
                ImGui::BeginDisabled(!canUseSelection);
                if (ImGui::Button("Use Selection as Target")) {
                    obj.aiAgent.targetId = selectedTarget->id;
                    aiPreviewTargetId = selectedTarget->id;
                    changed = true;
                }
                ImGui::EndDisabled();
                ImGui::SameLine();
                if (ImGui::Button("Clear Target")) {
                    obj.aiAgent.targetId = -1;
                    changed = true;
                }
            }

            if (ImGui::DragFloat3("Destination", &obj.aiAgent.destination.x, 0.05f, -10000.0f, 10000.0f, "%.2f")) {
                changed = true;
            }
            if (ImGui::Button("Set Destination To Current")) {
                obj.aiAgent.destination = obj.position;
                changed = true;
            }

            if (ImGui::DragFloat("Speed", &obj.aiAgent.speed, 0.05f, 0.05f, 100.0f, "%.2f")) {
                obj.aiAgent.speed = std::max(0.05f, obj.aiAgent.speed);
                changed = true;
            }
            if (ImGui::DragFloat("Stopping Distance", &obj.aiAgent.stoppingDistance, 0.01f, 0.0f, 25.0f, "%.2f")) {
                obj.aiAgent.stoppingDistance = std::clamp(obj.aiAgent.stoppingDistance, 0.0f, 25.0f);
                changed = true;
            }
            if (ImGui::DragFloat("Repath Interval", &obj.aiAgent.repathInterval, 0.05f, 0.05f, 10.0f, "%.2f")) {
                obj.aiAgent.repathInterval = std::clamp(obj.aiAgent.repathInterval, 0.05f, 10.0f);
                changed = true;
            }
            if (ImGui::Checkbox("Auto Repath", &obj.aiAgent.autoRepath)) {
                changed = true;
            }
            if (ImGui::Checkbox("Align To Path", &obj.aiAgent.alignToPath)) {
                changed = true;
            }
            if (ImGui::Checkbox("Debug Draw Path", &obj.aiAgent.debugDrawPath)) {
                changed = true;
            }

            ImGui::Unindent(10.0f);
            ImGui::PopID();
        }
        if (removeAgent) {
            obj.hasAIAgent = false;
            obj.aiAgent = AIAgentComponent{};
            aiAgentRuntimeStates.erase(obj.id);
            changed = true;
        }
        if (changed) {
            agentSectionChanged = true;
            projectManager.currentProject.hasUnsavedChanges = true;
        }
        ImGui::PopStyleColor();
    }

    if (obj.hasAnimation && sharedAnimation) {
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
            NormalizeAnimationClipSlots(obj.animation);
            if (ImGui::Button("Open Animator")) {
                showAnimationWindow = true;
                animationTargetId = obj.id;
            }
            ImGui::SameLine();
            if (!obj.animation.clips.empty()) {
                int activeIndex = AnimationGetActiveClipIndex(obj.animation);
                const char* preview = (activeIndex >= 0 && activeIndex < static_cast<int>(obj.animation.clips.size()))
                    ? obj.animation.clips[activeIndex].name.c_str()
                    : "<none>";
                if (ImGui::BeginCombo("##AnimActiveClip", preview)) {
                    for (int i = 0; i < static_cast<int>(obj.animation.clips.size()); ++i) {
                        const bool selected = (i == activeIndex);
                        const char* clipName = obj.animation.clips[i].name.empty()
                            ? "<unnamed>"
                            : obj.animation.clips[i].name.c_str();
                        if (ImGui::Selectable(clipName, selected)) {
                            obj.animation.activeClipIndex = i;
                            NormalizeAnimationClipSlots(obj.animation);
                            changed = true;
                        }
                        if (selected) {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }
            } else {
                ImGui::TextDisabled("No clips");
            }

            size_t displayKeyCount = obj.animation.keyframes.size();
            size_t displayTrackCount = obj.animation.tracks.size();
            const std::string activeClipAssetPath = AnimationGetActiveClipAssetPath(obj.animation);
            if (!activeClipAssetPath.empty()) {
                auto resolveClipPath = [&](const std::string& storedPath) -> fs::path {
                    if (storedPath.empty()) return {};
                    fs::path p = storedPath;
                    if (p.is_absolute()) return p;
                    if (projectManager.currentProject.isLoaded && !projectManager.currentProject.projectPath.empty()) {
                        return projectManager.currentProject.projectPath / p;
                    }
                    return p;
                };
                auto gatherClipStats = [](const fs::path& clipPath, size_t& outTracks, size_t& outKeys) -> bool {
                    std::ifstream in(clipPath);
                    if (!in.is_open()) return false;
                    outTracks = 0;
                    outKeys = 0;
                    std::string token;
                    while (in >> token) {
                        if (token == "track") {
                            std::string propertyId;
                            int visible = 1;
                            int locked = 0;
                            in >> std::quoted(propertyId) >> visible >> locked;
                            (void)visible;
                            (void)locked;
                            ++outTracks;
                        } else if (token == "keyCount") {
                            size_t keyCount = 0;
                            in >> keyCount;
                            outKeys += keyCount;
                        } else {
                            std::string discard;
                            std::getline(in, discard);
                        }
                    }
                    return !in.bad();
                };
                const fs::path clipPath = resolveClipPath(activeClipAssetPath);
                size_t clipTrackCount = 0;
                size_t clipKeyCount = 0;
                if (!clipPath.empty() && gatherClipStats(clipPath, clipTrackCount, clipKeyCount)) {
                    displayTrackCount = clipTrackCount;
                    displayKeyCount = clipKeyCount;
                }
            }
            ImGui::TextDisabled("Clips: %zu", obj.animation.clips.size());
            ImGui::TextDisabled("Keyframes: %zu | Tracks: %zu",
                                displayKeyCount,
                                displayTrackCount);

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
            if (ImGui::Checkbox("Play On Awake", &obj.animation.playOnAwake)) {
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
            animationSectionChanged = true;
            projectManager.currentProject.hasUnsavedChanges = true;
        }
        ImGui::PopStyleColor();
    }

    if (obj.hasSkeletalAnimation && sharedSkeletal) {
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
            skeletalSectionChanged = true;
            projectManager.currentProject.hasUnsavedChanges = true;
        }
        ImGui::PopStyleColor();
    }

    if (obj.hasReverbZone && sharedReverb) {
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
            reverbSectionChanged = true;
            projectManager.currentProject.hasUnsavedChanges = true;
        }
        ImGui::PopStyleColor();
    }

    if (obj.hasCamera && sharedCamera) {
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
            bool project2D = isProject2DPipeline();
            if (project2D) {
                ImGui::TextDisabled("2D camera mode is controlled by Project Pipeline.");
            } else {
                if (ImGui::Checkbox("Legacy 2D Camera Override", &obj.camera.use2D)) {
                    changed = true;
                }
            }
            bool cameraUses2D = project2D || obj.camera.use2D;
            if (cameraUses2D) {
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
            cameraSectionChanged = true;
            projectManager.currentProject.hasUnsavedChanges = true;
        }
        ImGui::PopStyleColor();
    }

    if (obj.hasCameraFollow2D && sharedCameraFollow2D) {
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
            cameraFollowSectionChanged = true;
            projectManager.currentProject.hasUnsavedChanges = true;
        }
        ImGui::PopStyleColor();
    }

    if (obj.hasPostFX && sharedPostFX) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.25f, 0.55f, 0.6f, 1.0f));
        bool changed = false;
        bool removePostFx = false;
        auto header = drawComponentHeader("ModuVolume", "PostFX", &obj.postFx.enabled, true, [&]() {
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
            if (ImGui::CollapsingHeader("Volume", ImGuiTreeNodeFlags_DefaultOpen)) {
                if (ImGui::Checkbox("Global Volume", &obj.postFx.isGlobal)) {
                    changed = true;
                }
                if (ImGui::DragFloat("Priority", &obj.postFx.priority, 0.05f, -100.0f, 100.0f, "%.2f")) {
                    changed = true;
                }
                if (ImGui::SliderFloat("Blend Weight", &obj.postFx.blendWeight, 0.0f, 1.0f, "%.2f")) {
                    changed = true;
                }
                if (!obj.postFx.isGlobal) {
                    if (ImGui::DragFloat("Blend Radius", &obj.postFx.blendRadius, 0.1f, 0.1f, 1000.0f, "%.2f")) {
                        obj.postFx.blendRadius = std::max(0.1f, obj.postFx.blendRadius);
                        changed = true;
                    }
                    ImGui::TextDisabled("Local volumes use this object's transform and scale as bounds.");
                }
            }

            if (ImGui::CollapsingHeader("HDR & Tone Mapping", ImGuiTreeNodeFlags_DefaultOpen)) {
                if (ImGui::Checkbox("HDR Enabled", &obj.postFx.hdrEnabled)) {
                    changed = true;
                }
                const char* toneMapperNames[] = { "None", "Reinhard", "ACES" };
                int toneMapper = static_cast<int>(obj.postFx.toneMapper);
                if (ImGui::Combo("Tone Mapper", &toneMapper, toneMapperNames, IM_ARRAYSIZE(toneMapperNames))) {
                    obj.postFx.toneMapper = static_cast<PostFXToneMapper>(toneMapper);
                    changed = true;
                }
                if (ImGui::SliderFloat("White Point", &obj.postFx.whitePoint, 0.25f, 16.0f, "%.2f")) {
                    changed = true;
                }
                if (ImGui::SliderFloat("Output Gamma", &obj.postFx.gamma, 1.0f, 3.0f, "%.2f")) {
                    changed = true;
                }
            }

            if (ImGui::CollapsingHeader("Bloom", ImGuiTreeNodeFlags_DefaultOpen)) {
                if (ImGui::Checkbox("Enabled##Bloom", &obj.postFx.bloomEnabled)) {
                    changed = true;
                }
                ImGui::BeginDisabled(!obj.postFx.bloomEnabled);
                if (ImGui::SliderFloat("Threshold", &obj.postFx.bloomThreshold, 0.0f, 4.0f, "%.2f")) {
                    changed = true;
                }
                if (ImGui::SliderFloat("Soft Knee", &obj.postFx.bloomSoftKnee, 0.0f, 1.0f, "%.2f")) {
                    changed = true;
                }
                if (ImGui::SliderFloat("Intensity##Bloom", &obj.postFx.bloomIntensity, 0.0f, 4.0f, "%.2f")) {
                    changed = true;
                }
                if (ImGui::SliderFloat("Spread", &obj.postFx.bloomRadius, 0.5f, 4.5f, "%.2f")) {
                    changed = true;
                }
                ImGui::EndDisabled();
            }

            if (ImGui::CollapsingHeader("Color", ImGuiTreeNodeFlags_DefaultOpen)) {
                if (ImGui::Checkbox("Enabled##ColorAdjust", &obj.postFx.colorAdjustEnabled)) {
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
            }

            if (ImGui::CollapsingHeader("Motion Blur", ImGuiTreeNodeFlags_DefaultOpen)) {
                if (ImGui::Checkbox("Enabled##MotionBlur", &obj.postFx.motionBlurEnabled)) {
                    changed = true;
                }
                ImGui::BeginDisabled(!obj.postFx.motionBlurEnabled);
                if (ImGui::SliderFloat("Strength", &obj.postFx.motionBlurStrength, 0.0f, 0.95f, "%.2f")) {
                    changed = true;
                }
                if (ImGui::SliderFloat("Threshold", &obj.postFx.motionBlurThreshold, 0.0f, 0.25f, "%.3f")) {
                    changed = true;
                }
                if (ImGui::SliderFloat("Clamp", &obj.postFx.motionBlurClamp, 0.0f, 1.5f, "%.2f")) {
                    changed = true;
                }
                ImGui::EndDisabled();
            }

            if (ImGui::CollapsingHeader("Lens", ImGuiTreeNodeFlags_DefaultOpen)) {
                if (ImGui::Checkbox("Vignette", &obj.postFx.vignetteEnabled)) {
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

                if (ImGui::Checkbox("Chromatic Aberration", &obj.postFx.chromaticAberrationEnabled)) {
                    changed = true;
                }
                ImGui::BeginDisabled(!obj.postFx.chromaticAberrationEnabled);
                if (ImGui::SliderFloat("Fringe Amount", &obj.postFx.chromaticAmount, 0.0f, 0.01f, "%.4f")) {
                    changed = true;
                }
                ImGui::EndDisabled();

                if (ImGui::Checkbox("Sharpen", &obj.postFx.sharpenEnabled)) {
                    changed = true;
                }
                ImGui::BeginDisabled(!obj.postFx.sharpenEnabled);
                if (ImGui::SliderFloat("Sharpen Strength", &obj.postFx.sharpenStrength, 0.0f, 1.0f, "%.2f")) {
                    changed = true;
                }
                ImGui::EndDisabled();
            }

            if (ImGui::CollapsingHeader("Ambient Occlusion", ImGuiTreeNodeFlags_DefaultOpen)) {
                if (ImGui::Checkbox("Enabled##AO", &obj.postFx.ambientOcclusionEnabled)) {
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
            }

            if (ImGui::CollapsingHeader("Profiling", ImGuiTreeNodeFlags_DefaultOpen)) {
                static const Renderer::PostProcessStats zeroPostStats{};
                const Renderer::PostProcessStats& postStats = rendererInitialized
                    ? renderer.getLastViewportPostStats()
                    : zeroPostStats;
                const bool activeLastFrame = (postStats.resolvedVolumeId == obj.id);
                ImGui::Text("Resolved Last Frame: %s", activeLastFrame ? "Yes" : "No");
                if (!postStats.resolvedVolumeName.empty()) {
                    ImGui::Text("Resolved Volume: %s", postStats.resolvedVolumeName.c_str());
                }
                ImGui::Text("Active Volumes: %d", postStats.activeVolumeCount);
                ImGui::Text("Blend: %.2f", activeLastFrame ? postStats.resolvedBlend : 0.0f);
                ImGui::Text("Effects: %d", postStats.activeEffectCount);
                ImGui::Text("Resolve: %.2f ms", postStats.resolveMs);
                ImGui::Text("Bloom Extract: %.2f ms", postStats.bloomExtractMs);
                ImGui::Text("Bloom Blur: %.2f ms", postStats.bloomBlurMs);
                ImGui::Text("Composite: %.2f ms", postStats.compositeMs);
                ImGui::Text("Total: %.2f ms", postStats.totalMs);
                ImGui::TextDisabled("Highest-priority active volume wins; local volumes fade by blend radius.");
                ImGui::TextDisabled("Wireframe/line mode auto-disables post effects.");
            }

            ImGui::Unindent(10.0f);
            ImGui::PopID();
        }
        if (removePostFx) {
            obj.hasPostFX = false;
            UpdateLegacyTypeFromComponents(obj);
            changed = true;
        }
        if (changed) {
            postFxSectionChanged = true;
            projectManager.currentProject.hasUnsavedChanges = true;
        }
        ImGui::PopStyleColor();
    }

    if (obj.hasRenderer && sharedRenderer) {
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
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("FILE_PATH")) {
                        const char* dropped = static_cast<const char*>(payload->Data);
                        std::error_code ec;
                        fs::directory_entry droppedEntry(fs::path(dropped), ec);
                        if (!ec && fileBrowser.isTextureFile(droppedEntry)) {
                            path = dropped;
                            changed = true;
                        }
                    }
                    ImGui::EndDragDropTarget();
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
            glm::vec4 baseColor(obj.material.color, obj.material.alpha);
            if (ImGui::ColorEdit4("Base Color", &baseColor.x)) {
                obj.material.color = glm::vec3(baseColor);
                // Alpha looked cursed here once, but this assignment is fine.
                obj.material.alpha = std::clamp(baseColor.w, 0.0f, 1.0f);
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
            const char* texFilterOptions[] = { "Bilinear", "Point" };
            int texFilterIndex = (obj.material.textureFilter == MaterialProperties::TextureFilter::Point) ? 1 : 0;
            if (ImGui::Combo("Texture Filter", &texFilterIndex, texFilterOptions, IM_ARRAYSIZE(texFilterOptions))) {
                obj.material.textureFilter =
                    (texFilterIndex == 1) ? MaterialProperties::TextureFilter::Point
                                          : MaterialProperties::TextureFilter::Bilinear;
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

            if (obj.renderType == RenderType::Sprite) {
                bool canUseSpriteAsset = isTextureOrSpriteSheetSelection(fileBrowser.selectedFile);
                ImGui::BeginDisabled(!canUseSpriteAsset);
                if (ImGui::SmallButton("Use Selection As Sprite Asset##WorldSpriteAsset")) {
                    if (assignSpriteTextureOrClips(obj, fileBrowser.selectedFile)) {
                        materialChanged = true;
                    }
                }
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::BeginDisabled(obj.albedoTexturePath.empty());
                if (ImGui::SmallButton("Reload Clips##WorldSpriteAsset")) {
                    if (assignSpriteTextureOrClips(obj, fs::path(obj.albedoTexturePath))) {
                        materialChanged = true;
                    }
                }
                ImGui::EndDisabled();

                if (!obj.albedoTexturePath.empty()) {
                    ImGui::SameLine();
                    if (hasSpritesheetPackage() && ImGui::SmallButton("Import Sheet##WorldSpriteSheet")) {
                        pendingSpriteSheetPath = obj.albedoTexturePath;
                        std::snprintf(importSpriteSheetName, sizeof(importSpriteSheetName), "%s", obj.name.c_str());
                        importSpriteSheetAsSprite2D = false;
                        showImportSpriteSheetDialog = true;
                    }
                }

                if (hasSpritesheetPackage() &&
                    ImGui::CollapsingHeader("Sprite Sheet", ImGuiTreeNodeFlags_DefaultOpen)) {
                    if (ImGui::Checkbox("Enable Sprite Sheet", &obj.ui.spriteSheetEnabled)) {
                        materialChanged = true;
                    }
                    ImGui::BeginDisabled(!obj.ui.spriteSheetEnabled);
                    const bool usingCustomClips = obj.ui.spriteCustomFramesEnabled && !obj.ui.spriteCustomFrames.empty();
                    if (usingCustomClips) {
                        const int clipCount = static_cast<int>(obj.ui.spriteCustomFrames.size());
                        EnsureSpriteClipNames(obj.ui.spriteCustomFrameNames, obj.ui.spriteCustomFrames.size());
                        ImGui::TextDisabled("Using %d cropped sprite clips.", clipCount);
                        obj.ui.spriteSheetFrame = std::clamp(obj.ui.spriteSheetFrame, 0, clipCount - 1);
                        const char* previewName = obj.ui.spriteCustomFrameNames[obj.ui.spriteSheetFrame].c_str();
                        if (ImGui::BeginCombo("Clip", previewName)) {
                            for (int clipIndex = 0; clipIndex < clipCount; ++clipIndex) {
                                bool selected = (clipIndex == obj.ui.spriteSheetFrame);
                                if (ImGui::Selectable(obj.ui.spriteCustomFrameNames[clipIndex].c_str(), selected)) {
                                    obj.ui.spriteSheetFrame = clipIndex;
                                    materialChanged = true;
                                }
                                if (selected) ImGui::SetItemDefaultFocus();
                            }
                            ImGui::EndCombo();
                        }
                        int clipIndex = obj.ui.spriteSheetFrame;
                        if (ImGui::SliderInt("Clip Index", &clipIndex, 0, clipCount - 1)) {
                            obj.ui.spriteSheetFrame = std::clamp(clipIndex, 0, clipCount - 1);
                            materialChanged = true;
                        }
                    } else {
                        if (ImGui::DragInt("Columns", &obj.ui.spriteSheetColumns, 1.0f, 1, 1024)) {
                            obj.ui.spriteSheetColumns = std::max(1, obj.ui.spriteSheetColumns);
                            materialChanged = true;
                        }
                        if (ImGui::DragInt("Rows", &obj.ui.spriteSheetRows, 1.0f, 1, 1024)) {
                            obj.ui.spriteSheetRows = std::max(1, obj.ui.spriteSheetRows);
                            materialChanged = true;
                        }
                        int frameCount = std::max(1, obj.ui.spriteSheetColumns * obj.ui.spriteSheetRows);
                        if (ImGui::SliderInt("Frame", &obj.ui.spriteSheetFrame, 0, frameCount - 1)) {
                            obj.ui.spriteSheetFrame = std::clamp(obj.ui.spriteSheetFrame, 0, frameCount - 1);
                            materialChanged = true;
                        }
                        if (ImGui::DragFloat("FPS", &obj.ui.spriteSheetFps, 0.1f, 1.0f, 120.0f, "%.1f")) {
                            obj.ui.spriteSheetFps = std::clamp(obj.ui.spriteSheetFps, 1.0f, 120.0f);
                            materialChanged = true;
                        }
                        if (ImGui::Checkbox("Loop", &obj.ui.spriteSheetLoop)) {
                            materialChanged = true;
                        }
                    }
                    ImGui::EndDisabled();
                }
            }

            ImGui::Spacing();
            ImGui::TextDisabled("Shader");
            const char* shaderPresetOptions[] = { "Custom", "Engine Lit (Default)", "Scrolling UV" };
            MaterialShaderPreset objectPreset = shaderPresetFromPaths(obj.vertexShaderPath, obj.fragmentShaderPath);
            int objectPresetIndex = static_cast<int>(objectPreset);
            if (ImGui::Combo("Shader Type", &objectPresetIndex, shaderPresetOptions, IM_ARRAYSIZE(shaderPresetOptions))) {
                if (applyShaderPreset(static_cast<MaterialShaderPreset>(objectPresetIndex),
                                      obj.vertexShaderPath, obj.fragmentShaderPath))
                {
                    materialChanged = true;
                }
            }
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
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("FILE_PATH")) {
                        const char* dropped = static_cast<const char*>(payload->Data);
                        std::error_code ec;
                        fs::directory_entry droppedEntry(fs::path(dropped), ec);
                        if (!ec && fileBrowser.getFileCategory(droppedEntry) == FileCategory::Shader) {
                            path = dropped;
                            changed = true;
                        }
                    }
                    ImGui::EndDragDropTarget();
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
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("FILE_PATH")) {
                    const char* dropped = static_cast<const char*>(payload->Data);
                    std::error_code ec;
                    fs::directory_entry droppedEntry(fs::path(dropped), ec);
                    if (!ec && fileBrowser.getFileCategory(droppedEntry) == FileCategory::Material) {
                        obj.materialPath = dropped;
                        loadMaterialFromFile(obj);
                        materialChanged = true;
                    }
                }
                ImGui::EndDragDropTarget();
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
            rendererSectionChanged = true;
            projectManager.currentProject.hasUnsavedChanges = true;
        }
    }

    if (obj.hasLight && sharedLight) {
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
            lightSectionChanged = true;
            projectManager.currentProject.hasUnsavedChanges = true;
        }
        ImGui::PopStyleColor();
    }

    if (has2DWorldPackage() && obj.hasLight2D && sharedLight2D) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.62f, 0.54f, 0.18f, 1.0f));
        bool changed = false;
        bool removeLight2D = false;
        auto header = drawComponentHeader("Light 2D", "Light2D", &obj.light2D.enabled, true, [&]() {
            if (ImGui::MenuItem("Remove")) {
                removeLight2D = true;
            }
        });
        if (header.enabledChanged) {
            changed = true;
        }
        if (header.open) {
            ImGui::PushID("Light2D");
            ImGui::Indent(10.0f);

            auto drawLayerMaskEditor = [&](const char* label, bool& targetAllLayers, uint32_t& targetLayerMask) {
                if (ImGui::Checkbox(label, &targetAllLayers)) {
                    changed = true;
                }
                if (!targetAllLayers && ImGui::TreeNode("Target Layers")) {
                    for (int layerIndex = 0; layerIndex < 32; ++layerIndex) {
                        bool enabledLayer = Light2DLayerMaskContains(targetLayerMask, layerIndex);
                        ImGui::PushID(layerIndex);
                        if (ImGui::Checkbox(std::to_string(layerIndex).c_str(), &enabledLayer)) {
                            if (enabledLayer) {
                                targetLayerMask |= Light2DLayerBit(layerIndex);
                            } else {
                                targetLayerMask &= ~Light2DLayerBit(layerIndex);
                            }
                            changed = true;
                        }
                        ImGui::PopID();
                        if ((layerIndex % 4) != 3) {
                            ImGui::SameLine();
                        }
                    }
                    ImGui::TreePop();
                }
            };

            int currentType = static_cast<int>(obj.light2D.type);
            const char* typeLabels[] = { "Point", "Spot", "Freeform", "Sprite", "Global" };
            if (ImGui::Combo("Type", &currentType, typeLabels, IM_ARRAYSIZE(typeLabels))) {
                obj.light2D.type = static_cast<Light2DType>(std::clamp(currentType, 0, 4));
                if (obj.light2D.type == Light2DType::Freeform && obj.light2D.shapePoints.size() < 3) {
                    obj.light2D.shapePoints = {
                        glm::vec2(-2.0f, -1.5f),
                        glm::vec2(2.0f, -1.5f),
                        glm::vec2(2.5f, 1.0f),
                        glm::vec2(0.0f, 2.5f),
                        glm::vec2(-2.5f, 1.0f)
                    };
                }
                lighting2DRenderer.clearPolygonCache(obj.id);
                UpdateLegacyTypeFromComponents(obj);
                changed = true;
            }

            if (ImGui::ColorEdit4("Color", &obj.light2D.color.x)) {
                changed = true;
            }
            if (ImGui::SliderFloat("Intensity", &obj.light2D.intensity, 0.0f, 16.0f, "%.2f")) {
                changed = true;
            }
            if (obj.light2D.type != Light2DType::Global) {
                if (ImGui::DragFloat("Radius", &obj.light2D.radius, 0.05f, 0.0f, 4096.0f, "%.2f")) {
                    obj.light2D.radius = std::max(0.0f, obj.light2D.radius);
                    changed = true;
                }
                if (ImGui::DragFloat("Inner Radius", &obj.light2D.innerRadius, 0.05f, 0.0f, 4096.0f, "%.2f")) {
                    obj.light2D.innerRadius = std::max(0.0f, obj.light2D.innerRadius);
                    obj.light2D.outerRadius = std::max(obj.light2D.outerRadius, obj.light2D.innerRadius);
                    changed = true;
                }
                if (ImGui::DragFloat("Outer Radius", &obj.light2D.outerRadius, 0.05f, obj.light2D.innerRadius, 4096.0f, "%.2f")) {
                    obj.light2D.outerRadius = std::max(obj.light2D.innerRadius, obj.light2D.outerRadius);
                    obj.light2D.radius = std::max(obj.light2D.radius, obj.light2D.outerRadius);
                    changed = true;
                }
                if (ImGui::SliderFloat("Falloff Strength", &obj.light2D.falloffStrength, 0.01f, 8.0f, "%.2f")) {
                    changed = true;
                }
            }

            if (obj.light2D.type == Light2DType::Spot) {
                if (ImGui::SliderFloat("Inner Spot Angle", &obj.light2D.innerSpotAngle, 0.0f, 360.0f, "%.1f")) {
                    obj.light2D.outerSpotAngle = std::max(obj.light2D.outerSpotAngle, obj.light2D.innerSpotAngle);
                    changed = true;
                }
                if (ImGui::SliderFloat("Outer Spot Angle", &obj.light2D.outerSpotAngle, obj.light2D.innerSpotAngle, 360.0f, "%.1f")) {
                    changed = true;
                }
            }

            int blendStyle = std::clamp(obj.light2D.blendStyle, 0, static_cast<int>(light2DBlendStyles.size()) - 1);
            const char* currentBlend = light2DBlendStyles[static_cast<size_t>(blendStyle)].name.c_str();
            if (ImGui::BeginCombo("Blend Style", currentBlend)) {
                for (int i = 0; i < static_cast<int>(light2DBlendStyles.size()); ++i) {
                    bool selected = (i == blendStyle);
                    if (ImGui::Selectable(light2DBlendStyles[static_cast<size_t>(i)].name.c_str(), selected)) {
                        obj.light2D.blendStyle = i;
                        changed = true;
                    }
                    if (selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            const char* overlapLabels[] = { "Additive", "Max", "Alpha Blend" };
            int overlapMode = static_cast<int>(obj.light2D.overlapOperation);
            if (ImGui::Combo("Overlap", &overlapMode, overlapLabels, IM_ARRAYSIZE(overlapLabels))) {
                obj.light2D.overlapOperation = static_cast<Light2DOverlapOperation>(std::clamp(overlapMode, 0, 2));
                changed = true;
            }
            if (ImGui::DragInt("Light Order", &obj.light2D.lightOrder, 1.0f, -4096, 4096)) {
                changed = true;
            }

            drawLayerMaskEditor("Target All Layers", obj.light2D.targetAllLayers, obj.light2D.targetLayerMask);

            if (ImGui::CollapsingHeader("Shadows", ImGuiTreeNodeFlags_DefaultOpen)) {
                if (ImGui::Checkbox("Cast Shadows", &obj.light2D.castsShadows)) {
                    changed = true;
                }
                if (ImGui::SliderFloat("Shadow Strength", &obj.light2D.shadowStrength, 0.0f, 1.0f, "%.2f")) {
                    changed = true;
                }
            }

            if (ImGui::CollapsingHeader("Volumetric")) {
                if (ImGui::Checkbox("Enabled", &obj.light2D.volumetricEnabled)) {
                    changed = true;
                }
                ImGui::TextDisabled("Volumetric accumulation is scaffolded for a later pass.");
            }

            if (ImGui::CollapsingHeader("Normal Maps")) {
                const char* normalQualityLabels[] = { "Disabled", "Fast", "Accurate" };
                int quality = static_cast<int>(obj.light2D.normalMapQuality);
                if (ImGui::Combo("Quality", &quality, normalQualityLabels, IM_ARRAYSIZE(normalQualityLabels))) {
                    obj.light2D.normalMapQuality = static_cast<Light2DNormalMapQuality>(std::clamp(quality, 0, 2));
                    changed = true;
                }
                if (ImGui::DragFloat("Distance", &obj.light2D.normalMapDistance, 0.05f, 0.0f, 64.0f, "%.2f")) {
                    obj.light2D.normalMapDistance = std::max(0.0f, obj.light2D.normalMapDistance);
                    changed = true;
                }
            }

            if (ImGui::CollapsingHeader("Distance Attenuation")) {
                if (ImGui::Checkbox("Use Distance Exponent", &obj.light2D.useDistanceExponent)) {
                    changed = true;
                }
                if (ImGui::SliderFloat("Distance Exponent", &obj.light2D.distanceExponent, 0.1f, 8.0f, "%.2f")) {
                    changed = true;
                }
            }

            if (ImGui::CollapsingHeader("Cookie")) {
                char cookieBuffer[512] = {};
                std::snprintf(cookieBuffer, sizeof(cookieBuffer), "%s", obj.light2D.cookieTexturePath.c_str());
                if (ImGui::InputText("Texture", cookieBuffer, sizeof(cookieBuffer))) {
                    obj.light2D.cookieTexturePath = cookieBuffer;
                    changed = true;
                }
                if (ImGui::DragFloat2("Scale", &obj.light2D.cookieScale.x, 0.01f, 0.01f, 16.0f, "%.2f")) {
                    obj.light2D.cookieScale.x = std::max(0.01f, obj.light2D.cookieScale.x);
                    obj.light2D.cookieScale.y = std::max(0.01f, obj.light2D.cookieScale.y);
                    changed = true;
                }
                if (ImGui::DragFloat("Rotation", &obj.light2D.cookieRotation, 0.5f, -360.0f, 360.0f, "%.1f")) {
                    changed = true;
                }
            }

            if (ImGui::CollapsingHeader("Flicker")) {
                if (ImGui::Checkbox("Enabled", &obj.light2D.flicker.enabled)) {
                    changed = true;
                }
                if (ImGui::SliderFloat("Speed", &obj.light2D.flicker.speed, 0.01f, 64.0f, "%.2f")) {
                    changed = true;
                }
                if (ImGui::SliderFloat("Amount", &obj.light2D.flicker.amount, 0.0f, 1.0f, "%.2f")) {
                    changed = true;
                }
                if (ImGui::DragFloat("Seed", &obj.light2D.flicker.seed, 0.05f, -1000.0f, 1000.0f, "%.2f")) {
                    changed = true;
                }
            }

            if (obj.light2D.type == Light2DType::Freeform || obj.light2D.type == Light2DType::Sprite) {
                if (ImGui::CollapsingHeader("Shape", ImGuiTreeNodeFlags_DefaultOpen)) {
                    if (ImGui::Button(light2DShapeEditMode && light2DShapeEditingObjectId == obj.id
                            ? "Stop Shape Edit"
                            : "Edit Shape")) {
                        if (light2DShapeEditMode && light2DShapeEditingObjectId == obj.id) {
                            light2DShapeEditMode = false;
                            light2DShapeEditingObjectId = -1;
                            light2DShapeEditingPointIndex = -1;
                        } else {
                            light2DShapeEditMode = true;
                            light2DShapeEditingObjectId = obj.id;
                            light2DShapeEditingPointIndex = -1;
                        }
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Add Point")) {
                        if (obj.light2D.shapePoints.empty()) {
                            obj.light2D.shapePoints = {
                                glm::vec2(-1.5f, -1.0f),
                                glm::vec2(1.5f, -1.0f),
                                glm::vec2(0.0f, 1.5f)
                            };
                        } else {
                            obj.light2D.shapePoints.push_back(obj.light2D.shapePoints.back() + glm::vec2(0.5f, 0.5f));
                        }
                        lighting2DRenderer.clearPolygonCache(obj.id);
                        changed = true;
                    }
                    ImGui::SameLine();
                    ImGui::BeginDisabled(obj.light2D.shapePoints.size() <= 3);
                    if (ImGui::Button("Remove Last")) {
                        obj.light2D.shapePoints.pop_back();
                        light2DShapeEditingPointIndex = std::min(light2DShapeEditingPointIndex,
                            static_cast<int>(obj.light2D.shapePoints.size()) - 1);
                        lighting2DRenderer.clearPolygonCache(obj.id);
                        changed = true;
                    }
                    ImGui::EndDisabled();

                    if (ImGui::SliderFloat("Feather", &obj.light2D.freeformFeather, 0.0f, 4.0f, "%.2f")) {
                        changed = true;
                    }
                    if (ImGui::SliderFloat("Edge Falloff", &obj.light2D.freeformEdgeFalloff, 0.1f, 8.0f, "%.2f")) {
                        changed = true;
                    }

                    const Light2DPolygonCache& cache = lighting2DRenderer.updatePolygonCache(obj.id, obj.light2D);
                    if (cache.valid) {
                        ImGui::TextDisabled("Points: %d  Triangles: %d",
                                            static_cast<int>(obj.light2D.shapePoints.size()),
                                            static_cast<int>(cache.indices.size() / 3));
                    } else {
                        ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.5f, 1.0f), "%s",
                                           cache.error.empty() ? "Invalid light shape." : cache.error.c_str());
                    }
                }
            }

            ImGui::Unindent(10.0f);
            ImGui::PopID();
        }
        if (removeLight2D) {
            obj.hasLight2D = false;
            if (light2DShapeEditingObjectId == obj.id) {
                light2DShapeEditMode = false;
                light2DShapeEditingObjectId = -1;
                light2DShapeEditingPointIndex = -1;
            }
            lighting2DRenderer.clearPolygonCache(obj.id);
            UpdateLegacyTypeFromComponents(obj);
            changed = true;
        }
        if (changed) {
            light2DSectionChanged = true;
            projectManager.currentProject.hasUnsavedChanges = true;
        }
        ImGui::PopStyleColor();
    }

    if (obj.hasShadowCaster2D && sharedShadowCaster2D) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.26f, 0.36f, 0.48f, 1.0f));
        bool changed = false;
        bool removeShadowCaster2D = false;
        auto header = drawComponentHeader("Shadow Caster 2D", "ShadowCaster2D", &obj.shadowCaster2D.enabled, true, [&]() {
            if (ImGui::MenuItem("Remove")) {
                removeShadowCaster2D = true;
            }
        });
        if (header.enabledChanged) {
            changed = true;
        }
        if (header.open) {
            ImGui::PushID("ShadowCaster2D");
            ImGui::Indent(10.0f);

            if (ImGui::Checkbox("Self Shadow", &obj.shadowCaster2D.castsSelfShadow)) {
                changed = true;
            }
            if (ImGui::SliderFloat("Strength", &obj.shadowCaster2D.shadowStrength, 0.0f, 1.0f, "%.2f")) {
                changed = true;
            }
            if (ImGui::Checkbox("Target All Layers", &obj.shadowCaster2D.targetAllLayers)) {
                changed = true;
            }
            if (!obj.shadowCaster2D.targetAllLayers && ImGui::TreeNode("Target Layers")) {
                for (int layerIndex = 0; layerIndex < 32; ++layerIndex) {
                    bool enabledLayer = Light2DLayerMaskContains(obj.shadowCaster2D.targetLayerMask, layerIndex);
                    ImGui::PushID(layerIndex);
                    if (ImGui::Checkbox(std::to_string(layerIndex).c_str(), &enabledLayer)) {
                        if (enabledLayer) {
                            obj.shadowCaster2D.targetLayerMask |= Light2DLayerBit(layerIndex);
                        } else {
                            obj.shadowCaster2D.targetLayerMask &= ~Light2DLayerBit(layerIndex);
                        }
                        changed = true;
                    }
                    ImGui::PopID();
                    if ((layerIndex % 4) != 3) {
                        ImGui::SameLine();
                    }
                }
                ImGui::TreePop();
            }

            if (ImGui::Button(light2DShapeEditMode && light2DShapeEditingObjectId == obj.id
                    ? "Stop Shape Edit"
                    : "Edit Shape")) {
                if (light2DShapeEditMode && light2DShapeEditingObjectId == obj.id) {
                    light2DShapeEditMode = false;
                    light2DShapeEditingObjectId = -1;
                    light2DShapeEditingPointIndex = -1;
                } else {
                    light2DShapeEditMode = true;
                    light2DShapeEditingObjectId = obj.id;
                    light2DShapeEditingPointIndex = -1;
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Add Point##ShadowCaster2D")) {
                if (obj.shadowCaster2D.points.empty()) {
                    obj.shadowCaster2D.points = {
                        glm::vec2(-1.0f, -1.0f),
                        glm::vec2(1.0f, -1.0f),
                        glm::vec2(1.0f, 1.0f),
                        glm::vec2(-1.0f, 1.0f)
                    };
                } else {
                    obj.shadowCaster2D.points.push_back(obj.shadowCaster2D.points.back() + glm::vec2(0.4f, 0.4f));
                }
                changed = true;
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(obj.shadowCaster2D.points.size() <= 3);
            if (ImGui::Button("Remove Last##ShadowCaster2D")) {
                obj.shadowCaster2D.points.pop_back();
                light2DShapeEditingPointIndex = std::min(light2DShapeEditingPointIndex,
                    static_cast<int>(obj.shadowCaster2D.points.size()) - 1);
                changed = true;
            }
            ImGui::EndDisabled();

            std::string shadowError;
            if (Light2DValidatePolygon(obj.shadowCaster2D.points, &shadowError)) {
                std::vector<unsigned int> triangles;
                Light2DTriangulatePolygon(obj.shadowCaster2D.points, triangles, nullptr);
                ImGui::TextDisabled("Points: %d  Triangles: %d",
                                    static_cast<int>(obj.shadowCaster2D.points.size()),
                                    static_cast<int>(triangles.size() / 3));
            } else {
                ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.5f, 1.0f), "%s",
                                   shadowError.empty() ? "Invalid shadow caster shape." : shadowError.c_str());
            }

            ImGui::Unindent(10.0f);
            ImGui::PopID();
        }
        if (removeShadowCaster2D) {
            obj.hasShadowCaster2D = false;
            if (light2DShapeEditingObjectId == obj.id) {
                light2DShapeEditMode = false;
                light2DShapeEditingObjectId = -1;
                light2DShapeEditingPointIndex = -1;
            }
            UpdateLegacyTypeFromComponents(obj);
            changed = true;
        }
        if (changed) {
            shadowCaster2DSectionChanged = true;
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
        return ext == ".c" || ext == ".cc" || ext == ".cpp" || ext == ".cxx" || ext == ".moducpp";
    };

    for (size_t i = 0; i < obj.scripts.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        if (multiSelection && i < sharedScriptByIndex.size() && !sharedScriptByIndex[i]) {
            ImGui::PopID();
            continue;
        }
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
                sc.lastBinaryPath.clear();
                sc.lastBinaryVerified = false;
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
                        sc.lastBinaryPath.clear();
                        sc.lastBinaryVerified = false;
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
                    fs::path binary;
                    if (!sc.lastBinaryPath.empty()) {
                        fs::path cachedBinary = sc.lastBinaryPath;
                        if (fs::exists(cachedBinary)) {
                            binary = std::move(cachedBinary);
                        }
                    }
                    if (binary.empty()) {
                        binary = resolveScriptBinary(sc.path);
                    }
                    sc.lastBinaryPath = binary.string();
                    sc.lastBinaryVerified = !binary.empty();
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
                    fs::path assembly;
                    if (!sc.lastBinaryPath.empty()) {
                        fs::path cachedAssembly = sc.lastBinaryPath;
                        if (fs::exists(cachedAssembly)) {
                            assembly = std::move(cachedAssembly);
                        }
                    }
                    if (assembly.empty()) {
                        assembly = resolveManagedAssembly(sc.path);
                    }
                    sc.lastBinaryPath = assembly.string();
                    sc.lastBinaryVerified = !assembly.empty();
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

    if (obj.hasRenderer && sharedRenderer) {
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
    ImGui::BeginDisabled(multiSelection);
    if (ImGui::Button("Add Component", ImVec2(-1, 0))) {
        ImGui::OpenPopup("AddComponentPopup");
    }
    ImGui::EndDisabled();
    if (multiSelection) {
        ImGui::TextDisabled("Add Component is disabled for multi-selection.");
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
        addEntry("AI Pathfinding/GroundBakedType", !obj.hasGroundBakedType && !isUIType, [&]() {
            obj.hasGroundBakedType = true;
            obj.groundBakedType = GroundBakedTypeComponent{};
            showAIPathfindingWindow = true;
            componentChanged = true;
        });
        addEntry("AI Pathfinding/ObsticleObject", !obj.hasObsticleObject && !isUIType, [&]() {
            obj.hasObsticleObject = true;
            obj.obsticleObject = ObsticleObjectComponent{};
            showAIPathfindingWindow = true;
            componentChanged = true;
        });
        addEntry("AI Pathfinding/AI Agent", !obj.hasAIAgent && !isUIType, [&]() {
            obj.hasAIAgent = true;
            obj.aiAgent = AIAgentComponent{};
            obj.aiAgent.destination = obj.position;
            showAIPathfindingWindow = true;
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
        addEntry("Rendering/ModuVolume", !obj.hasPostFX, [&]() {
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
        if (has2DWorldPackage()) {
            addEntry("Lights/2D Point", !obj.hasLight2D, [&]() {
                obj.hasLight2D = true;
                obj.light2D = Light2DComponent{};
                obj.light2D.type = Light2DType::Point;
                obj.light2D.radius = 5.0f;
                obj.light2D.outerRadius = 5.0f;
                UpdateLegacyTypeFromComponents(obj);
                componentChanged = true;
            });
            addEntry("Lights/2D Spot", !obj.hasLight2D, [&]() {
                obj.hasLight2D = true;
                obj.light2D = Light2DComponent{};
                obj.light2D.type = Light2DType::Spot;
                obj.light2D.radius = 7.0f;
                obj.light2D.outerRadius = 7.0f;
                obj.light2D.innerSpotAngle = 22.0f;
                obj.light2D.outerSpotAngle = 48.0f;
                UpdateLegacyTypeFromComponents(obj);
                componentChanged = true;
            });
            addEntry("Lights/2D Freeform", !obj.hasLight2D, [&]() {
                obj.hasLight2D = true;
                obj.light2D = Light2DComponent{};
                obj.light2D.type = Light2DType::Freeform;
                obj.light2D.shapePoints = {
                    glm::vec2(-2.0f, -1.5f),
                    glm::vec2(2.0f, -1.5f),
                    glm::vec2(2.5f, 1.0f),
                    glm::vec2(0.0f, 2.5f),
                    glm::vec2(-2.5f, 1.0f)
                };
                obj.light2D.radius = 4.0f;
                obj.light2D.outerRadius = 4.0f;
                UpdateLegacyTypeFromComponents(obj);
                componentChanged = true;
            });
            addEntry("Lights/2D Global", !obj.hasLight2D, [&]() {
                obj.hasLight2D = true;
                obj.light2D = Light2DComponent{};
                obj.light2D.type = Light2DType::Global;
                obj.light2D.intensity = 0.35f;
                obj.light2D.color = glm::vec4(0.45f, 0.52f, 0.72f, 1.0f);
                UpdateLegacyTypeFromComponents(obj);
                componentChanged = true;
            });
            addEntry("Lights/2D Shadow Caster", !obj.hasShadowCaster2D, [&]() {
                obj.hasShadowCaster2D = true;
                obj.shadowCaster2D = ShadowCaster2DComponent{};
                obj.shadowCaster2D.points = {
                    glm::vec2(-1.0f, -1.0f),
                    glm::vec2(1.0f, -1.0f),
                    glm::vec2(1.0f, 1.0f),
                    glm::vec2(-1.0f, 1.0f)
                };
                UpdateLegacyTypeFromComponents(obj);
                componentChanged = true;
            });
        }
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
            obj.faceCamera = false;
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
        if (has2DWorldPackage()) {
            addEntry("UI/Sprite2D", true, [&]() {
                obj.hasUI = true;
                applyUiDefaults(obj, UIElementType::Sprite2D);
                UpdateLegacyTypeFromComponents(obj);
                componentChanged = true;
            });
        }
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
                    if (ext == ".cpp" || ext == ".c" || ext == ".moducpp" || ext == ".cs") {
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
                sc.lastBinaryPath.clear();
                sc.lastBinaryVerified = false;
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
                sc.lastBinaryVerified = true;
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

    auto forEachSecondarySelected = [&](auto&& fn) {
        for (SceneObject* selectedObj : selectedObjects) {
            if (!selectedObj || selectedObj->id == obj.id) continue;
            fn(*selectedObj);
        }
    };

    if (multiSelection) {
        bool propagated = false;

        if (objectNameChanged || objectEnabledChanged || objectLayerChanged || objectTagChanged || objectTransformChanged) {
            forEachSecondarySelected([&](SceneObject& target) {
                if (objectNameChanged) {
                    const std::string oldTargetName = target.name;
                    target.name = obj.name;
                    propagateObjectRenameReferences(oldTargetName, target.name, target.id);
                }
                if (objectEnabledChanged) {
                    target.enabled = obj.enabled;
                }
                if (objectLayerChanged) {
                    target.layer = obj.layer;
                }
                if (objectTagChanged) {
                    target.tag = obj.tag;
                }
                if (objectTransformChanged) {
                    target.position = obj.position;
                    target.rotation = obj.rotation;
                    target.scale = obj.scale;
                    syncLocalTransform(target);
                }
            });
            propagated = true;
        }

        if (uiSectionChanged && sharedUIObject) {
            forEachSecondarySelected([&](SceneObject& target) {
                target.hasUI = obj.hasUI;
                target.ui = obj.ui;
                target.albedoTexturePath = obj.albedoTexturePath;
                target.material.textureFilter = obj.material.textureFilter;
            });
            propagated = true;
        }
        if (colliderSectionChanged && sharedCollider) {
            forEachSecondarySelected([&](SceneObject& target) {
                target.hasCollider = obj.hasCollider;
                target.collider = obj.collider;
            });
            propagated = true;
        }
        if (playerControllerSectionChanged && sharedPlayerController) {
            forEachSecondarySelected([&](SceneObject& target) {
                target.hasPlayerController = obj.hasPlayerController;
                target.playerController = obj.playerController;
                if (target.hasPlayerController) {
                    target.hasCollider = true;
                    target.collider.type = ColliderType::Capsule;
                    target.collider.convex = true;
                    target.hasRigidbody = true;
                    target.rigidbody.enabled = true;
                    target.rigidbody.useGravity = true;
                }
            });
            propagated = true;
        }
        if (rigidbodySectionChanged && sharedRigidbody) {
            forEachSecondarySelected([&](SceneObject& target) {
                target.hasRigidbody = obj.hasRigidbody;
                target.rigidbody = obj.rigidbody;
            });
            propagated = true;
        }
        if (rigidbody2DSectionChanged && sharedRigidbody2D) {
            forEachSecondarySelected([&](SceneObject& target) {
                target.hasRigidbody2D = obj.hasRigidbody2D;
                target.rigidbody2D = obj.rigidbody2D;
            });
            propagated = true;
        }
        if (collider2DSectionChanged && sharedCollider2D) {
            forEachSecondarySelected([&](SceneObject& target) {
                target.hasCollider2D = obj.hasCollider2D;
                target.collider2D = obj.collider2D;
            });
            propagated = true;
        }
        if (parallax2DSectionChanged && sharedParallax2D) {
            forEachSecondarySelected([&](SceneObject& target) {
                target.hasParallaxLayer2D = obj.hasParallaxLayer2D;
                target.parallaxLayer2D = obj.parallaxLayer2D;
            });
            propagated = true;
        }
        if (audioSourceSectionChanged && sharedAudioSource) {
            forEachSecondarySelected([&](SceneObject& target) {
                target.hasAudioSource = obj.hasAudioSource;
                target.audioSource = obj.audioSource;
            });
            propagated = true;
        }
        if (groundBakedSectionChanged && sharedGroundBaked) {
            forEachSecondarySelected([&](SceneObject& target) {
                target.hasGroundBakedType = obj.hasGroundBakedType;
                target.groundBakedType = obj.groundBakedType;
            });
            propagated = true;
        }
        if (obstacleSectionChanged && sharedObstacle) {
            forEachSecondarySelected([&](SceneObject& target) {
                target.hasObsticleObject = obj.hasObsticleObject;
                target.obsticleObject = obj.obsticleObject;
            });
            propagated = true;
        }
        if (agentSectionChanged && sharedAgent) {
            forEachSecondarySelected([&](SceneObject& target) {
                target.hasAIAgent = obj.hasAIAgent;
                target.aiAgent = obj.aiAgent;
            });
            propagated = true;
        }
        if (animationSectionChanged && sharedAnimation) {
            forEachSecondarySelected([&](SceneObject& target) {
                target.hasAnimation = obj.hasAnimation;
                target.animation = obj.animation;
            });
            propagated = true;
        }
        if (skeletalSectionChanged && sharedSkeletal) {
            forEachSecondarySelected([&](SceneObject& target) {
                target.hasSkeletalAnimation = obj.hasSkeletalAnimation;
                target.skeletal = obj.skeletal;
            });
            propagated = true;
        }
        if (reverbSectionChanged && sharedReverb) {
            forEachSecondarySelected([&](SceneObject& target) {
                target.hasReverbZone = obj.hasReverbZone;
                target.reverbZone = obj.reverbZone;
            });
            propagated = true;
        }
        if (cameraSectionChanged && sharedCamera) {
            forEachSecondarySelected([&](SceneObject& target) {
                target.hasCamera = obj.hasCamera;
                target.camera = obj.camera;
            });
            propagated = true;
        }
        if (cameraFollowSectionChanged && sharedCameraFollow2D) {
            forEachSecondarySelected([&](SceneObject& target) {
                target.hasCameraFollow2D = obj.hasCameraFollow2D;
                target.cameraFollow2D = obj.cameraFollow2D;
            });
            propagated = true;
        }
        if (postFxSectionChanged && sharedPostFX) {
            forEachSecondarySelected([&](SceneObject& target) {
                target.hasPostFX = obj.hasPostFX;
                target.postFx = obj.postFx;
            });
            propagated = true;
        }
        if (rendererSectionChanged && sharedRenderer) {
            forEachSecondarySelected([&](SceneObject& target) {
                target.hasRenderer = obj.hasRenderer;
                target.renderType = obj.renderType;
                target.faceCamera = obj.faceCamera;
                target.meshPath = obj.meshPath;
                target.meshId = obj.meshId;
                target.meshSourceIndex = obj.meshSourceIndex;
                target.material = obj.material;
                target.materialPath = obj.materialPath;
                target.albedoTexturePath = obj.albedoTexturePath;
                target.overlayTexturePath = obj.overlayTexturePath;
                target.normalMapPath = obj.normalMapPath;
                target.vertexShaderPath = obj.vertexShaderPath;
                target.fragmentShaderPath = obj.fragmentShaderPath;
                target.useOverlay = obj.useOverlay;
                target.additionalMaterialPaths = obj.additionalMaterialPaths;
                target.ui.spriteSheetEnabled = obj.ui.spriteSheetEnabled;
                target.ui.spriteSheetColumns = obj.ui.spriteSheetColumns;
                target.ui.spriteSheetRows = obj.ui.spriteSheetRows;
                target.ui.spriteSheetFrame = obj.ui.spriteSheetFrame;
                target.ui.spriteSheetFps = obj.ui.spriteSheetFps;
                target.ui.spriteSheetLoop = obj.ui.spriteSheetLoop;
                target.ui.spriteCustomFramesEnabled = obj.ui.spriteCustomFramesEnabled;
                target.ui.spriteCustomFrames = obj.ui.spriteCustomFrames;
                target.ui.spriteCustomFrameNames = obj.ui.spriteCustomFrameNames;
                target.ui.spriteCustomFrameScales = obj.ui.spriteCustomFrameScales;
                target.ui.spriteSourceWidth = obj.ui.spriteSourceWidth;
                target.ui.spriteSourceHeight = obj.ui.spriteSourceHeight;
                target.ui.nineSliceEnabled = obj.ui.nineSliceEnabled;
                target.ui.nineSliceBorder = obj.ui.nineSliceBorder;
                target.ui.nineSliceTileEdges = obj.ui.nineSliceTileEdges;
                target.ui.nineSliceTileCenter = obj.ui.nineSliceTileCenter;
            });
            propagated = true;
        }
        if (lightSectionChanged && sharedLight) {
            forEachSecondarySelected([&](SceneObject& target) {
                target.hasLight = obj.hasLight;
                target.light = obj.light;
            });
            propagated = true;
        }
        if (light2DSectionChanged && sharedLight2D) {
            forEachSecondarySelected([&](SceneObject& target) {
                target.hasLight2D = obj.hasLight2D;
                target.light2D = obj.light2D;
                lighting2DRenderer.clearPolygonCache(target.id);
            });
            propagated = true;
        }
        if (shadowCaster2DSectionChanged && sharedShadowCaster2D) {
            forEachSecondarySelected([&](SceneObject& target) {
                target.hasShadowCaster2D = obj.hasShadowCaster2D;
                target.shadowCaster2D = obj.shadowCaster2D;
            });
            propagated = true;
        }
        if (scriptsChanged) {
            if (sharedScriptsLayout) {
                forEachSecondarySelected([&](SceneObject& target) {
                    target.scripts = obj.scripts;
                });
                propagated = true;
            } else {
                forEachSecondarySelected([&](SceneObject& target) {
                    for (size_t i = 0; i < sharedScriptByIndex.size() && i < obj.scripts.size(); ++i) {
                        if (!sharedScriptByIndex[i]) continue;
                        const std::string& oldSignature = sharedScriptSignatures[i];
                        const std::string newSignature = scriptSignature(obj.scripts[i]);
                        auto targetIt = std::find_if(target.scripts.begin(), target.scripts.end(),
                            [&](const ScriptComponent& candidate) {
                                const std::string candidateSignature = scriptSignature(candidate);
                                return candidateSignature == oldSignature || candidateSignature == newSignature;
                            });
                        if (targetIt != target.scripts.end()) {
                            *targetIt = obj.scripts[i];
                        }
                    }
                });
                propagated = true;
            }
        }

        if (propagated) {
            forEachSecondarySelected([&](SceneObject& target) {
                UpdateLegacyTypeFromComponents(target);
            });
            projectManager.currentProject.hasUnsavedChanges = true;
        }
    }

    if (scriptsChanged) {
        markRuntimeScriptBindingsDirty();
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
    if (multiSelection && hasMixedComponents) {
        ImGui::Spacing();
        ImGui::TextDisabled("Note: not all components are shown because one or more selected objects have different components/scripts.");
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
    static float consolePopoutAnim = 0.0f;

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
        const bool tabActive = consolePopoutOpen || consolePopoutAnim > 0.02f;
        if (tabActive) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        }
        if (ImGui::Button("Console", ImGui::GetContentRegionAvail())) {
            consolePopoutOpen = !consolePopoutOpen;
        }
        if (tabActive) {
            ImGui::PopStyleColor(3);
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(3);

    const float targetAnim = consolePopoutOpen ? 1.0f : 0.0f;
    const float blend = 1.0f - std::exp(-12.0f * ImGui::GetIO().DeltaTime);
    consolePopoutAnim += (targetAnim - consolePopoutAnim) * blend;
    if (std::fabs(consolePopoutAnim - targetAnim) < 0.001f) {
        consolePopoutAnim = targetAnim;
    }
    const float openAmount = std::clamp(consolePopoutAnim, 0.0f, 1.0f);
    auto withScaledAlpha = [&](ImVec4 color) -> ImVec4 {
        color.w = std::clamp(color.w * openAmount, 0.0f, 1.0f);
        return color;
    };
    auto withScaledAlphaU32 = [&](ImU32 color) -> ImU32 {
        ImVec4 c = ImGui::ColorConvertU32ToFloat4(color);
        c.w = std::clamp(c.w * openAmount, 0.0f, 1.0f);
        return ImGui::ColorConvertFloat4ToU32(c);
    };

    if (!consolePopoutOpen && openAmount <= 0.001f) {
        return;
    }

    ImVec2 miniSize(ImMin(560.0f, anchorMax.x - anchorMin.x - margin * 2.0f),
                    ImMin(320.0f, anchorMax.y - anchorMin.y - tabSize.y - margin * 3.0f));
    if (miniSize.x < 260.0f || miniSize.y < 140.0f) {
        return;
    }

    ImVec2 miniPos(anchorMax.x - miniSize.x - margin, tabPos.y - miniSize.y - 8.0f);
    miniPos.y += (1.0f - openAmount) * 22.0f;
    miniPos.y = ImMax(anchorMin.y + margin, miniPos.y);

    bool keepOpen = consolePopoutOpen;
    bool* openPtr = consolePopoutOpen ? &keepOpen : nullptr;
    ImGuiWindowFlags miniFlags = ImGuiWindowFlags_NoDocking |
                                 ImGuiWindowFlags_NoSavedSettings |
                                 ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_NoCollapse;
    if (openAmount < 0.98f) {
        miniFlags |= ImGuiWindowFlags_NoInputs;
    }
    ImGui::SetNextWindowPos(miniPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(miniSize, ImGuiCond_Always);
    ImGui::SetNextWindowViewport(mainViewport->ID);
    const ImGuiStyle& style = ImGui::GetStyle();
    ImGui::SetNextWindowBgAlpha(style.Colors[ImGuiCol_WindowBg].w * openAmount);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, openAmount);
    ImGui::PushStyleColor(ImGuiCol_Border, withScaledAlpha(style.Colors[ImGuiCol_Border]));
    ImGui::PushStyleColor(ImGuiCol_BorderShadow, withScaledAlpha(style.Colors[ImGuiCol_BorderShadow]));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, withScaledAlpha(style.Colors[ImGuiCol_ChildBg]));
    if (!ImGui::Begin("Console##MiniLogPanel", openPtr, miniFlags)) {
        ImGui::End();
        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar();
        if (openPtr) {
            consolePopoutOpen = keepOpen;
        }
        return;
    }
    if (openPtr) {
        consolePopoutOpen = keepOpen;
    }

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

    Texture* infoLogo = nullptr;
    Texture* warningLogo = nullptr;
    Texture* errorLogo = nullptr;
    Texture* successLogo = nullptr;
    if (rendererInitialized) {
        infoLogo = renderer.getTexture("Resources/Engine-Root/Info Logo.png");
        warningLogo = renderer.getTexture("Resources/Engine-Root/Warning Logo.png");
        errorLogo = renderer.getTexture("Resources/Engine-Root/Error Logo.png");
        successLogo = renderer.getTexture("Resources/Engine-Root/Modu-Logo.png");
    }

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

    auto typeColor = [&](ConsoleMessageType type) -> ImVec4 {
        switch (type) {
            case ConsoleMessageType::Warning: return withScaledAlpha(ImVec4(1.0f, 0.84f, 0.35f, 1.0f));
            case ConsoleMessageType::Error: return withScaledAlpha(ImVec4(1.0f, 0.46f, 0.46f, 1.0f));
            case ConsoleMessageType::Success: return withScaledAlpha(ImVec4(0.50f, 0.95f, 0.55f, 1.0f));
            case ConsoleMessageType::Info:
            default:
                return withScaledAlpha(ImVec4(0.65f, 0.84f, 1.0f, 1.0f));
        }
    };

    auto rowColor = [&](ConsoleMessageType type) -> ImU32 {
        switch (type) {
            case ConsoleMessageType::Warning: return withScaledAlphaU32(IM_COL32(74, 60, 20, 195));
            case ConsoleMessageType::Error: return withScaledAlphaU32(IM_COL32(88, 28, 28, 205));
            case ConsoleMessageType::Success: return withScaledAlphaU32(IM_COL32(20, 70, 30, 190));
            case ConsoleMessageType::Info:
            default:
                return withScaledAlphaU32(IM_COL32(30, 52, 78, 185));
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
    const float contentWidth = ImGui::GetContentRegionAvail().x;
    const uint64_t logFingerprint = computeConsoleLogFingerprint(consoleLog);
    const bool cacheNeedsRefresh =
        gConsolePanelCache.entryCount != consoleLog.size() ||
        gConsolePanelCache.wrapText != consoleWrapText ||
        gConsolePanelCache.iconsAvailable != rendererInitialized ||
        std::abs(gConsolePanelCache.contentWidth - contentWidth) > 0.5f ||
        gConsolePanelCache.fingerprint != logFingerprint;

    if (cacheNeedsRefresh) {
        gConsolePanelCache.entryCount = consoleLog.size();
        gConsolePanelCache.wrapText = consoleWrapText;
        gConsolePanelCache.iconsAvailable = rendererInitialized;
        gConsolePanelCache.contentWidth = contentWidth;
        gConsolePanelCache.fingerprint = logFingerprint;
        gConsolePanelCache.rows.clear();
        gConsolePanelCache.rows.resize(consoleLog.size());

        const float topPad = 4.0f;
        const float bottomPad = 4.0f;
        const float lineGap = 2.0f;
        const float minTextWidth = 32.0f;

        for (size_t i = 0; i < consoleLog.size(); ++i) {
            const ConsoleEntry& log = consoleLog[i];
            ConsoleRowMetrics& row = gConsolePanelCache.rows[i];
            row.header = "[" + log.timestamp + "] " + typeLabel(log.type);

            const bool hasIcon = rendererInitialized;
            const float textLeft = hasIcon ? 36.0f : 10.0f;
            row.textWidth = ImMax(minTextWidth, contentWidth - textLeft - 10.0f);
            row.headerSize = ImGui::CalcTextSize(row.header.c_str(), nullptr, false, FLT_MAX);

            if (consoleWrapText) {
                row.messageSize = ImGui::CalcTextSize(log.message.c_str(), nullptr, false, row.textWidth);
                row.rowWidth = ImMax(contentWidth, textLeft + row.textWidth + 10.0f);
            } else {
                row.messageSize = ImGui::CalcTextSize(log.message.c_str(), nullptr, false, FLT_MAX);
                row.rowWidth = ImMax(contentWidth, textLeft + ImMax(row.headerSize.x, row.messageSize.x) + 10.0f);
                row.textWidth = ImMax(minTextWidth, row.rowWidth - textLeft - 10.0f);
            }

            row.rowHeight = ImMax(26.0f, topPad + ImGui::GetTextLineHeight() + lineGap + row.messageSize.y + bottomPad);
        }
    }

    if (consoleLog.empty()) {
        ImGui::TextDisabled("No console messages yet.");
    } else {
        const float iconSize = 16.0f;
        const float topPad = 4.0f;
        const float lineGap = 2.0f;
        for (size_t i = 0; i < consoleLog.size(); ++i) {
            const ConsoleEntry& log = consoleLog[i];
            const ConsoleRowMetrics& row = gConsolePanelCache.rows[i];
            ImGui::PushID(static_cast<int>(i));

            Texture* icon = typeIcon(log.type);
            const bool hasIcon = icon && icon->GetID();
            float rowWidth = row.rowWidth;
            const float textLeft = hasIcon ? 36.0f : 10.0f;
            const float textWidth = row.textWidth;
            const float rowHeight = row.rowHeight;

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
                               ImVec2(0, 1), ImVec2(1, 0), withScaledAlphaU32(IM_COL32_WHITE));
            }

            const float textX = rowMin.x + textLeft;
            draw->AddText(ImVec2(textX, rowMin.y + topPad),
                          ImGui::ColorConvertFloat4ToU32(typeColor(log.type)),
                          row.header.c_str());
            draw->AddText(ImGui::GetFont(), ImGui::GetFontSize(),
                          ImVec2(textX, rowMin.y + topPad + ImGui::GetTextLineHeight() + lineGap),
                          withScaledAlphaU32(ImGui::GetColorU32(ImGuiCol_Text)),
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
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar();
}

void Engine::renderLatestErrorBar() {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    if (!viewport) return;

    const bool hasMessage = !consoleLog.empty();
    const ConsoleEntry* latest = hasMessage ? &consoleLog.back() : nullptr;
    const ConsoleMessageType latestType = latest ? latest->type : ConsoleMessageType::Info;

    Texture* errorLogo = nullptr;
    Texture* infoLogo = nullptr;
    Texture* warningLogo = nullptr;
    Texture* successLogo = nullptr;
    if (rendererInitialized) {
        errorLogo = renderer.getTexture("Resources/Engine-Root/Error Logo.png");
        infoLogo = renderer.getTexture("Resources/Engine-Root/Info Logo.png");
        warningLogo = renderer.getTexture("Resources/Engine-Root/Warning Logo.png");
        successLogo = renderer.getTexture("Resources/Engine-Root/Modu-Logo.png");
    }

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

    auto typeAccentColor = [](ConsoleMessageType type) -> ImVec4 {
        switch (type) {
            case ConsoleMessageType::Warning: return ImVec4(1.0f, 0.84f, 0.35f, 1.0f);
            case ConsoleMessageType::Error: return ImVec4(1.0f, 0.46f, 0.46f, 1.0f);
            case ConsoleMessageType::Success: return ImVec4(0.50f, 0.95f, 0.55f, 1.0f);
            case ConsoleMessageType::Info:
            default:
                return ImVec4(0.70f, 0.88f, 1.0f, 1.0f);
        }
    };

    auto typeBackgroundColor = [](ConsoleMessageType type) -> ImVec4 {
        switch (type) {
            case ConsoleMessageType::Warning: return ImVec4(0.30f, 0.22f, 0.08f, 0.93f);
            case ConsoleMessageType::Error: return ImVec4(0.30f, 0.10f, 0.10f, 0.93f);
            case ConsoleMessageType::Success: return ImVec4(0.10f, 0.23f, 0.12f, 0.90f);
            case ConsoleMessageType::Info:
            default:
                return ImVec4(0.10f, 0.18f, 0.24f, 0.88f);
        }
    };

    auto typeBorderColor = [](ConsoleMessageType type) -> ImVec4 {
        switch (type) {
            case ConsoleMessageType::Warning: return ImVec4(0.85f, 0.65f, 0.20f, 0.75f);
            case ConsoleMessageType::Error: return ImVec4(0.85f, 0.25f, 0.25f, 0.80f);
            case ConsoleMessageType::Success: return ImVec4(0.25f, 0.70f, 0.35f, 0.70f);
            case ConsoleMessageType::Info:
            default:
                return ImVec4(0.30f, 0.50f, 0.70f, 0.60f);
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

    Texture* icon = typeIcon(latestType);
    ImVec4 accentColor = typeAccentColor(latestType);
    ImVec4 bgColor = typeBackgroundColor(latestType);
    ImVec4 borderColor = typeBorderColor(latestType);

    std::string bodyText;
    if (latest) {
        bodyText = "[" + latest->timestamp + "] " + latest->message;
    } else {
        bodyText = "No console messages yet.";
    }

    const ImGuiWindow* dockHost = ImGui::FindWindowByName("DockSpace");
    ImVec2 hostPos = dockHost ? dockHost->Pos : viewport->WorkPos;
    ImVec2 hostSize = dockHost ? dockHost->Size : viewport->WorkSize;

    const float reserveHeight = getEditorBottomStatusReserveHeight();
    const float barHeight = ImMax(14.0f, reserveHeight - 6.0f);
    const float marginX = 6.0f;
    const float stripTop = hostPos.y + hostSize.y - reserveHeight;

    ImVec2 barMin(hostPos.x + marginX,
                  stripTop + (reserveHeight - barHeight) * 0.5f);
    ImVec2 barMax(hostPos.x + hostSize.x - marginX, barMin.y + barHeight);
    if (barMax.x - barMin.x <= 40.0f || barMax.y - barMin.y <= 8.0f) return;

    ImDrawList* draw = ImGui::GetForegroundDrawList(const_cast<ImGuiViewport*>(viewport));
    if (!draw) return;

    const ImU32 bgU32 = ImGui::ColorConvertFloat4ToU32(bgColor);
    const ImU32 borderU32 = ImGui::ColorConvertFloat4ToU32(borderColor);
    const ImU32 accentU32 = ImGui::ColorConvertFloat4ToU32(accentColor);
    const ImU32 textU32 = ImGui::GetColorU32(ImGuiCol_Text);

    draw->AddRectFilled(barMin, barMax, bgU32, 5.0f);
    draw->AddRect(barMin, barMax, borderU32, 5.0f, 0, 1.0f);

    float cursorX = barMin.x + 6.0f;
    const float fontY = barMin.y + (barHeight - ImGui::GetFontSize()) * 0.5f;

    if (icon && icon->GetID()) {
        const float iconSize = ImClamp(barHeight - 4.0f, 10.0f, 14.0f);
        const ImVec2 iconMin(cursorX, barMin.y + (barHeight - iconSize) * 0.5f);
        const ImVec2 iconMax(iconMin.x + iconSize, iconMin.y + iconSize);
        draw->AddImage((ImTextureID)(intptr_t)icon->GetID(), iconMin, iconMax,
                       ImVec2(0, 1), ImVec2(1, 0), IM_COL32_WHITE);
        cursorX += iconSize + 5.0f;
    }

    const char* label = latest ? typeLabel(latestType) : "Status";
    std::string labelText = std::string(label) + ":";
    draw->AddText(ImVec2(cursorX, fontY), accentU32, labelText.c_str());
    cursorX += ImGui::CalcTextSize(labelText.c_str()).x + 6.0f;

    draw->PushClipRect(ImVec2(cursorX, barMin.y + 1.0f), ImVec2(barMax.x - 6.0f, barMax.y - 1.0f), true);
    draw->AddText(ImVec2(cursorX, fontY), textU32, bodyText.c_str());
    draw->PopClipRect();
}

#pragma endregion

#pragma region Mesh Builder Panel
void Engine::renderMeshBuilderPanel() {
    if (!hasMeshBuilderPackage()) {
        showMeshBuilder = false;
        return;
    }
    ImGui::Begin("Mesh Builder (Legacy)", &showMeshBuilder);
    ImGui::TextDisabled("Primary workflow moved to the viewport toolbar RMesh edit mode.");
    ImGui::Separator();

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

    if (showImportSpriteSheetDialog && hasSpritesheetPackage()) {
        ImGuiIO& io = ImGui::GetIO();
        ImVec2 center = ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(460, 260), ImGuiCond_Appearing);

        if (ImGui::Begin("Import Sprite Sheet", &showImportSpriteSheetDialog,
                        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoDocking)) {
            ImGui::Text("File: %s", fs::path(pendingSpriteSheetPath).filename().string().c_str());
            ImGui::TextDisabled("%s", pendingSpriteSheetPath.c_str());
            int texW = 0;
            int texH = 0;
            if (!pendingSpriteSheetPath.empty()) {
                if (Texture* tex = renderer.getTexture(pendingSpriteSheetPath)) {
                    texW = tex->GetWidth();
                    texH = tex->GetHeight();
                }
            }
            if (texW > 0 && texH > 0) {
                ImGui::TextDisabled("Texture Size: %d x %d", texW, texH);
            }

            ImGui::Spacing();
            ImGui::Text("Object Name:");
            ImGui::SetNextItemWidth(-1);
            ImGui::InputText("##ImportSpriteSheetName", importSpriteSheetName, sizeof(importSpriteSheetName));

            ImGui::Spacing();
            ImGui::DragInt("Columns", &importSpriteSheetColumns, 1.0f, 1, 1024);
            ImGui::DragInt("Rows", &importSpriteSheetRows, 1.0f, 1, 1024);
            importSpriteSheetColumns = std::max(1, importSpriteSheetColumns);
            importSpriteSheetRows = std::max(1, importSpriteSheetRows);
            ImGui::DragFloat("FPS", &importSpriteSheetFps, 0.1f, 1.0f, 120.0f, "%.1f");
            importSpriteSheetFps = std::clamp(importSpriteSheetFps, 1.0f, 120.0f);
            if (!has2DWorldPackage()) {
                importSpriteSheetAsSprite2D = false;
                ImGui::BeginDisabled();
                ImGui::Checkbox("Create Sprite2D", &importSpriteSheetAsSprite2D);
                ImGui::EndDisabled();
            } else {
                ImGui::Checkbox("Create Sprite2D", &importSpriteSheetAsSprite2D);
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            float buttonWidth = 80;
            ImGui::SetCursorPosX(ImGui::GetWindowWidth() - buttonWidth * 2 - 20);

            if (ImGui::Button("Cancel", ImVec2(buttonWidth, 0))) {
                showImportSpriteSheetDialog = false;
                pendingSpriteSheetPath.clear();
            }
            ImGui::SameLine();

            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 0.3f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.6f, 0.4f, 1.0f));
            if (ImGui::Button("Import", ImVec2(buttonWidth, 0))) {
                std::string baseName = importSpriteSheetName;
                if (baseName.empty()) {
                    baseName = fs::path(pendingSpriteSheetPath).stem().string();
                }

                ObjectType type = importSpriteSheetAsSprite2D ? ObjectType::Sprite2D : ObjectType::UIImage;
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
                if (!sceneObjects.empty()) {
                    SceneObject& created = sceneObjects.back();
                    created.albedoTexturePath = pendingSpriteSheetPath;
                    created.material.textureFilter = MaterialProperties::TextureFilter::Point;
                    created.ui.spriteSheetEnabled = true;
                    created.ui.spriteSheetColumns = std::max(1, importSpriteSheetColumns);
                    created.ui.spriteSheetRows = std::max(1, importSpriteSheetRows);
                    created.ui.spriteSheetFrame = 0;
                    created.ui.spriteSheetFps = importSpriteSheetFps;
                    created.ui.spriteSheetLoop = true;
                    created.ui.spriteCustomFramesEnabled = false;
                    created.ui.spriteSourceWidth = texW;
                    created.ui.spriteSourceHeight = texH;
                    created.ui.spriteCustomFrames.clear();
                    if (texW > 0 && texH > 0) {
                        created.ui.size.x = std::max(1.0f, static_cast<float>(texW) / static_cast<float>(created.ui.spriteSheetColumns));
                        created.ui.size.y = std::max(1.0f, static_cast<float>(texH) / static_cast<float>(created.ui.spriteSheetRows));
                    }
                    if (canvasId >= 0) {
                        setParent(created.id, canvasId);
                    }
                    projectManager.currentProject.hasUnsavedChanges = true;
                    addConsoleMessage("Imported sprite sheet: " + pendingSpriteSheetPath, ConsoleMessageType::Success);
                }

                showImportSpriteSheetDialog = false;
                pendingSpriteSheetPath.clear();
            }
            ImGui::PopStyleColor(2);
        }
        ImGui::End();
    }
}
