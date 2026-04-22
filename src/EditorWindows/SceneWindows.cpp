#include "Engine.h"
#include "MaterialAssetUtils.h"
#include "ModelLoader.h"
#include "../SpritesheetFormat.h"
#include "imgui.h"
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
#include <future>
#include <chrono>
#include <future>

#ifdef _WIN32
#include <shlobj.h>
#endif

namespace ImGui {
    bool BufferingBar(const char* label, float value, const ImVec2& size_arg, const ImU32& bg_col, const ImU32& fg_col);
}

#pragma region Hierarchy Helpers
namespace {
    bool IsNativeBinaryPath(const fs::path& path) {
        const std::string ext = path.extension().string();
        return ext == ".so" || ext == ".dll" || ext == ".dylib";
    }

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

    bool MoveInspectorComponentBefore(SceneObject& obj,
                                      const std::string& movingKey,
                                      const std::string& targetKey) {
        EnsureInspectorComponentMetadata(obj);
        if (movingKey.empty() || targetKey.empty() || movingKey == targetKey) {
            return false;
        }

        auto movingIt = std::find(obj.inspectorComponentOrder.begin(), obj.inspectorComponentOrder.end(), movingKey);
        auto targetIt = std::find(obj.inspectorComponentOrder.begin(), obj.inspectorComponentOrder.end(), targetKey);
        if (movingIt == obj.inspectorComponentOrder.end() || targetIt == obj.inspectorComponentOrder.end()) {
            return false;
        }

        const std::string key = *movingIt;
        const ptrdiff_t targetIndex = std::distance(obj.inspectorComponentOrder.begin(), targetIt);
        obj.inspectorComponentOrder.erase(movingIt);
        auto insertIt = obj.inspectorComponentOrder.begin() +
            std::clamp<ptrdiff_t>(targetIndex, 0, static_cast<ptrdiff_t>(obj.inspectorComponentOrder.size()));
        obj.inspectorComponentOrder.insert(insertIt, key);
        return true;
    }

    bool MoveInspectorComponentByOffset(SceneObject& obj,
                                        const std::string& key,
                                        int offset) {
        EnsureInspectorComponentMetadata(obj);
        auto it = std::find(obj.inspectorComponentOrder.begin(), obj.inspectorComponentOrder.end(), key);
        if (it == obj.inspectorComponentOrder.end() || offset == 0) {
            return false;
        }

        const ptrdiff_t index = std::distance(obj.inspectorComponentOrder.begin(), it);
        const ptrdiff_t targetIndex = std::clamp<ptrdiff_t>(
            index + offset,
            0,
            static_cast<ptrdiff_t>(obj.inspectorComponentOrder.size()) - 1);
        if (targetIndex == index) {
            return false;
        }

        const std::string movedKey = *it;
        obj.inspectorComponentOrder.erase(it);
        obj.inspectorComponentOrder.insert(obj.inspectorComponentOrder.begin() + targetIndex, movedKey);
        return true;
    }

    bool MoveInspectorComponentToEdge(SceneObject& obj,
                                      const std::string& key,
                                      bool toTop) {
        EnsureInspectorComponentMetadata(obj);
        auto it = std::find(obj.inspectorComponentOrder.begin(), obj.inspectorComponentOrder.end(), key);
        if (it == obj.inspectorComponentOrder.end()) {
            return false;
        }

        const ptrdiff_t index = std::distance(obj.inspectorComponentOrder.begin(), it);
        if ((toTop && index == 0) ||
            (!toTop && index == static_cast<ptrdiff_t>(obj.inspectorComponentOrder.size()) - 1)) {
            return false;
        }

        const std::string movedKey = *it;
        obj.inspectorComponentOrder.erase(it);
        if (toTop) {
            obj.inspectorComponentOrder.insert(obj.inspectorComponentOrder.begin(), movedKey);
        } else {
            obj.inspectorComponentOrder.push_back(movedKey);
        }
        return true;
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
        ImVec4 trunkCol(base.x, base.y, base.z, 0.34f);
        ImVec4 branchCol(
            std::min(1.0f, base.x + 0.10f),
            std::min(1.0f, base.y + 0.10f),
            std::min(1.0f, base.z + 0.10f),
            0.62f);
        ImU32 trunkColor = ImGui::GetColorU32(trunkCol);
        ImU32 lineColor = ImGui::GetColorU32(branchCol);
        float indent = style.IndentSpacing;
        float rowTop = itemMin.y;
        float rowBottom = itemMax.y;
        float rowMid = (rowTop + rowBottom) * 0.5f;
        float baseX = itemMin.x - indent * depth;
        float lineThickness = 1.35f;
        float cornerRadius = std::min(indent * 0.28f, std::max(3.5f, (rowBottom - rowTop) * 0.22f));
        float branchEndX = itemMin.x + 4.0f;

        for (int i = 0; i < depth && i < static_cast<int>(ancestorHasNext.size()); ++i) {
            if (ancestorHasNext[i]) {
                float x = baseX + indent * (i + 0.5f);
                drawList->AddLine(ImVec2(x, rowTop - 1.0f), ImVec2(x, rowBottom + 1.0f), trunkColor, 1.0f);
            }
        }

        float connectorX = baseX + indent * (depth - 0.5f);
        float branchStartY = rowMid - cornerRadius;
        float vertEnd = isLast ? branchStartY : rowBottom + 1.0f;
        drawList->AddLine(ImVec2(connectorX, rowTop - 1.0f), ImVec2(connectorX, vertEnd), lineColor, lineThickness);

        ImVec2 curveStart(connectorX, branchStartY);
        ImVec2 curveEnd(connectorX + cornerRadius, rowMid);
        ImVec2 curveCp1(connectorX, branchStartY + cornerRadius * 0.55f);
        ImVec2 curveCp2(connectorX + cornerRadius * 0.55f, rowMid);
        drawList->AddBezierCubic(curveStart, curveCp1, curveCp2, curveEnd, lineColor, lineThickness);
        drawList->AddLine(curveEnd, ImVec2(branchEndX, rowMid), lineColor, lineThickness);

        float capRadius = 1.5f;
        drawList->AddCircleFilled(ImVec2(branchEndX, rowMid), capRadius, lineColor, 10);
        drawList->AddCircleFilled(ImVec2(connectorX, rowTop + 1.0f), 1.1f, trunkColor, 8);
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
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 8.0f));
    ImGui::Begin("Hierarchy", &showHierarchy);
    ImGui::PopStyleVar();

    static char searchBuffer[128] = "";
    float animSpeed = 0.0f;
    if (uiAnimationMode == UIAnimationMode::Fluid) {
        animSpeed = 8.0f;
    } else if (uiAnimationMode == UIAnimationMode::Snappy) {
        animSpeed = 18.0f;
    }
    float animStep = (uiAnimationMode == UIAnimationMode::Off) ? 1.0f
        : (1.0f - std::exp(-animSpeed * ImGui::GetIO().DeltaTime));

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 8.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 5.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 5.0f));
    ImGui::BeginChild("HierarchyHeader", ImVec2(-1.0f, 60.0f), false, ImGuiWindowFlags_NoScrollbar);

    const float headerButtonSize = ImGui::GetFrameHeight();
    if (ImGui::Button("+", ImVec2(headerButtonSize, headerButtonSize))) {
        ImGui::OpenPopup("HierarchyCreatePopup");
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##Search", "Search...", searchBuffer, sizeof(searchBuffer));

    if (ImGui::BeginPopup("HierarchyCreatePopup")) {
        if (ImGui::MenuItem("Empty")) addObject(ObjectType::Empty, "Empty");
        ImGui::Separator();
        if (ImGui::BeginMenu("Primitives")) {
            if (ImGui::MenuItem("Cube")) addObject(ObjectType::Cube, "Cube");
            if (ImGui::MenuItem("Sphere")) addObject(ObjectType::Sphere, "Sphere");
            if (ImGui::MenuItem("Capsule")) addObject(ObjectType::Capsule, "Capsule");
            if (ImGui::MenuItem("Plane")) addObject(ObjectType::Plane, "Plane");
            if (ImGui::MenuItem("Torus")) addObject(ObjectType::Torus, "Torus");
            if (ImGui::MenuItem("Sprite (Quad)")) addObject(ObjectType::Sprite, "Sprite");
            if (ImGui::MenuItem("2.5D Sprite")) addObject(ObjectType::Sprite25D, "2.5D Sprite");
            if (ImGui::MenuItem("Mirror")) addObject(ObjectType::Mirror, "Mirror");
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("MMesh")) {
            if (ImGui::MenuItem("Cube")) createMMeshPrimitive("Cube");
            if (ImGui::MenuItem("Sphere")) createMMeshPrimitive("Sphere");
            if (ImGui::MenuItem("Plane")) createMMeshPrimitive("Plane");
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Lights")) {
            if (ImGui::MenuItem("Camera")) addObject(ObjectType::Camera, "Camera");
            if (ImGui::MenuItem("Directional Light")) addObject(ObjectType::DirectionalLight, "Directional Light");
            if (ImGui::MenuItem("Point Light")) addObject(ObjectType::PointLight, "Point Light");
            if (ImGui::MenuItem("Spot Light")) addObject(ObjectType::SpotLight, "Spot Light");
            if (ImGui::MenuItem("Area Light")) addObject(ObjectType::AreaLight, "Area Light");
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("ModuVolume")) addObject(ObjectType::PostFXNode, "ModuVolume");
        if (ImGui::MenuItem("Canvas")) addObject(ObjectType::Canvas, "Canvas");
        ImGui::EndPopup();
    }

    ImGui::Spacing();
    ImGui::Checkbox("Texture Preview", &hierarchyShowTexturePreview);
    ImGui::EndChild();
    ImGui::PopStyleVar(3);

    ImGui::Spacing();

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

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 8.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 3.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 3.0f));
    ImGui::BeginChild("HierarchyList", ImVec2(-1.0f, 0.0f), false);

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
                if (ImGui::MenuItem("2.5D Sprite")) addObject(ObjectType::Sprite25D, "2.5D Sprite");
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

            if (ImGui::BeginMenu("MMesh"))
            {
                if (ImGui::BeginMenu("Primitives"))
                {
                    if (ImGui::MenuItem("Cube"))   createMMeshPrimitive("Cube");
                    if (ImGui::MenuItem("Sphere")) createMMeshPrimitive("Sphere");
                    if (ImGui::MenuItem("Plane"))  createMMeshPrimitive("Plane");
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
    ImGui::PopStyleVar(3);

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
    bool nodeOpen = ImGui::TreeNodeEx((void*)(intptr_t)obj.id, flags, "%s", "");

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

    const ImVec2 leftDragDelta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
    const float leftDragDistanceSq = leftDragDelta.x * leftDragDelta.x + leftDragDelta.y * leftDragDelta.y;
    const float leftDragThreshold = ImGui::GetIO().MouseDragThreshold;
    const bool hierarchyRowReleasedAsClick =
        ImGui::IsItemHovered()
        && ImGui::IsMouseReleased(ImGuiMouseButton_Left)
        && leftDragDistanceSq <= leftDragThreshold * leftDragThreshold
        && !ImGui::IsItemToggledOpen();

    if (hierarchyRowReleasedAsClick) {
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
                        playEditorFeedbackPreview("Resources/Sounds/Drag Script Assign Check Successful.mp3", 0.95f, false, EditorFeedbackSoundCategory::Other);
                        const std::string targetName = obj.name.empty() ? "Object" : obj.name;
                        showEditorToast("Script assigned to " + targetName + " Successful.",
                                        ConsoleMessageType::Success,
                                        1.65);
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

    std::vector<SceneObject*> visibleChildren;
    if (hasChildren) {
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
    }

    const bool shouldAnimateChildren = !visibleChildren.empty() && (nodeOpen || openT > 0.001f);
    if (shouldAnimateChildren) {
        const float revealT = std::clamp(openT, 0.0f, 1.0f);
        const float revealHeightT = std::clamp((revealT - 0.045f) / 0.955f, 0.0f, 1.0f);
        const float revealAlphaT = std::clamp((revealT - 0.11f) / 0.89f, 0.0f, 1.0f);
        const float easedReveal = revealHeightT * revealHeightT * (3.0f - 2.0f * revealHeightT);
        const float easedAlpha = revealAlphaT * revealAlphaT * (3.0f - 2.0f * revealAlphaT);
        const bool treePushed = nodeOpen;
        if (!treePushed) {
            ImGui::Indent();
        }

        const ImVec2 layoutCursor = ImGui::GetCursorPos();
        const ImVec2 renderCursor = ImGui::GetCursorScreenPos();
        const float estimatedHeight = std::max(lineHeight * 0.9f,
                                               lineHeight * static_cast<float>(visibleChildren.size()) * 1.1f);
        const float cachedHeight = std::max(animState.contentExtent, estimatedHeight);
        const float reservedHeight = cachedHeight * easedReveal;
        const float slideOffset = (1.0f - easedReveal) * std::min(std::max(lineHeight * 0.9f, cachedHeight * 0.18f),
                                                                  lineHeight * 3.0f);
        const ImVec2 clipMin(renderCursor.x - 24.0f, itemMax.y - 1.0f);
        const ImVec2 clipMax(ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x + 24.0f,
                             renderCursor.y + reservedHeight + lineHeight);

        if (reservedHeight > 0.75f && easedAlpha > 0.001f) {
            ImGui::PushClipRect(clipMin, clipMax, true);
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, easedAlpha);
            ImGui::SetCursorScreenPos(ImVec2(renderCursor.x, renderCursor.y - slideOffset));
            ImGui::BeginGroup();

            ancestorHasNext.push_back(!isLast);
            for (size_t i = 0; i < visibleChildren.size(); ++i) {
                bool childLast = (i + 1 == visibleChildren.size());
                renderObjectNode(*visibleChildren[i], filter, ancestorHasNext, childLast, depth + 1, animStep);
            }
            ancestorHasNext.pop_back();

            ImGui::EndGroup();
            const ImVec2 renderedMin = ImGui::GetItemRectMin();
            const ImVec2 renderedMax = ImGui::GetItemRectMax();
            const float renderedHeight = std::max(0.0f, renderedMax.y - renderedMin.y);
            if (renderedHeight > 0.0f) {
                if (uiAnimationMode == UIAnimationMode::Off || animState.contentExtent <= 0.0f) {
                    animState.contentExtent = renderedHeight;
                } else {
                    animState.contentExtent += (renderedHeight - animState.contentExtent) * std::min(1.0f, animStep * 0.85f);
                }
            }
            ImGui::PopStyleVar();
            ImGui::PopClipRect();
        }

        ImGui::SetCursorPos(layoutCursor);
        ImGui::Dummy(ImVec2(0.0f, reservedHeight));

        if (treePushed) {
            ImGui::TreePop();
        } else {
            ImGui::Unindent();
        }
    } else if (nodeOpen) {
        ImGui::TreePop();
    }
}
#pragma endregion

#pragma region Inspector Panel
void Engine::renderInspectorPanel() {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 6.0f));
    ImGui::Begin("Inspector", &showInspector);
    ImGui::PopStyleVar();
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(5.0f, 3.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 4.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(4.0f, 2.0f));

    if (deferInspectorRefresh) {
        deferInspectorRefresh = false;
        ImGui::TextDisabled("Refreshing inspector after play mode transition...");
        ImGui::PopStyleVar(3);
        ImGui::End();
        return;
    }

    fs::path selectedMaterialPath;
    bool browserHasMaterial = false;
    fs::path selectedAudioPath;
    bool browserHasAudio = false;
    const AudioClipPreview* selectedAudioPreview = nullptr;
    fs::path selectedTexturePath;
    bool browserHasTexture = false;
    static std::unordered_map<int, int> selectedRendererMaterialSlots;
    static std::string slotMaterialInspectorPath;
    static bool slotMaterialInspectorValid = false;
    static MaterialProperties slotInspectedMaterial;
    static std::string slotInspectedAlbedo;
    static std::string slotInspectedOverlay;
    static std::string slotInspectedNormal;
    static std::string slotInspectedShaderPack;
    static std::string slotInspectedVertShader;
    static std::string slotInspectedFragShader;
    static bool slotInspectedUseOverlay = false;
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
                    &inspectedShaderPack,
                    &inspectedVertShader,
                    &inspectedFragShader
                );
                inspectedMaterialPath = selectedMaterialPath.string();
            }
        } else {
            inspectedMaterialPath.clear();
            inspectedMaterialValid = false;
            inspectedShaderPack.clear();
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
        inspectedShaderPack.clear();
    }

    if (browserHasAudio) {
        std::string selectedAudio = selectedAudioPath.string();
        if (selectedAudio != audioPreviewSelectedPath) {
            audioPreviewSelectedPath = selectedAudio;
            if (audioPreviewAutoPlay) {
                audioPreviewBaseVolume = 1.0f;
                audioPreviewContext = AudioPreviewContext::AssetBrowser;
                audio.playPreview(selectedAudio, audioPreviewBaseVolume * audioPreviewVolume, audioPreviewLoop);
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

    // Resolve an inspector UI icon (OpenGL or Vulkan, no Y-flip needed for UI icons).
    auto resolveInspectorIcon = [&](const char* iconPath) -> ImTextureID {
        if (!iconPath || !*iconPath) return static_cast<ImTextureID>(0);
        if (rendererInitialized) {
            if (Texture* icon = renderer.getTexture(iconPath, MaterialProperties::TextureFilter::Bilinear);
                icon && icon->GetID()) {
                return static_cast<ImTextureID>(icon->GetID());
            }
        }
        if (hasVulkanUiImages && vulkanRenderer) {
            ImTextureID icon = vulkanRenderer->getOrCreateUIImage(iconPath);
            if (icon != static_cast<ImTextureID>(0)) return icon;
        }
        return static_cast<ImTextureID>(0);
    };

    const ImTextureID iconGameObject  = resolveInspectorIcon("Resources/Engine-Root/Inspector/GameObject Icon.png");
    const ImTextureID iconTransform   = resolveInspectorIcon("Resources/Engine-Root/Inspector/Transform Component Icon.png");
    const ImTextureID iconScript      = resolveInspectorIcon("Resources/Engine-Root/Inspector/Script Icon.png");
    const ImTextureID iconActionsMenu = resolveInspectorIcon("Resources/Engine-Root/Inspector/Tab Area/Actions (... menu).png");
    const ImTextureID iconTextureSelect = resolveInspectorIcon(
        "Resources/Engine-Root/Inspector/Materials and texturing/Select Texture list icon.png");
    const ImTextureID iconColorPicker = resolveInspectorIcon(
        "Resources/Engine-Root/Inspector/Materials and texturing/Color Picker Icon.png");
    (void)iconTransform;

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

    auto fitLabelToWidth = [&](const std::string& text, float maxWidth) {
        const float safeWidth = std::max(12.0f, maxWidth);
        if (ImGui::CalcTextSize(text.c_str()).x <= safeWidth) {
            return text;
        }

        constexpr const char* kEllipsis = "...";
        std::string clipped = text;
        while (!clipped.empty()) {
            std::string candidate = clipped + kEllipsis;
            if (ImGui::CalcTextSize(candidate.c_str()).x <= safeWidth) {
                return candidate;
            }
            clipped.pop_back();
        }
        return std::string(kEllipsis);
    };

    auto drawMaterialInlineLabel = [&](const char* label, float width = 126.0f) {
        ImGui::AlignTextToFramePadding();

        const float rowHeight = std::max(20.0f, ImGui::GetFrameHeight());
        const ImVec2 labelMin = ImGui::GetCursorScreenPos();

        ImGui::Dummy(ImVec2(width, rowHeight));

        const std::string clippedLabel = fitLabelToWidth(label, std::max(12.0f, width - 6.0f));
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        drawList->PushClipRect(labelMin, ImVec2(labelMin.x + width, labelMin.y + rowHeight), true);
        drawList->AddText(
            ImVec2(labelMin.x, labelMin.y + std::max(0.0f, (rowHeight - ImGui::GetTextLineHeight()) * 0.5f)),
            ImGui::GetColorU32(ImGuiCol_TextDisabled),
            clippedLabel.c_str()
        );
        drawList->PopClipRect();

        if (clippedLabel != label && ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", label);
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

    auto stopClipPreview = [&]() {
        audio.stopPreview();
        audioPreviewBaseVolume = 1.0f;
        audioPreviewContext = AudioPreviewContext::None;
    };

    auto beginClipPreview = [&](const std::string& path, float baseVolume, bool loop, AudioPreviewContext context) {
        audioPreviewBaseVolume = std::max(0.0f, baseVolume);
        audioPreviewContext = context;
        return audio.playPreview(path, audioPreviewBaseVolume * audioPreviewVolume, loop);
    };

    auto syncClipPreviewVolume = [&](AudioPreviewContext context, float baseVolume) {
        if (audioPreviewContext != context) {
            return;
        }
        audioPreviewBaseVolume = std::max(0.0f, baseVolume);
        audio.setPreviewVolume(audioPreviewBaseVolume * audioPreviewVolume);
    };

    auto drawAudioPreviewVolumeControl = [&](const char* id, AudioPreviewContext context, float baseVolume) {
        AudioPlayerUiIcon icon = resolveAudioPlayerIcon("Resources/Engine-Root/Audio Player/Audio Icon.png");
        if (icon.id != static_cast<ImTextureID>(0)) {
            const ImVec2 uvMin = icon.flipY ? ImVec2(0.0f, 1.0f) : ImVec2(0.0f, 0.0f);
            const ImVec2 uvMax = icon.flipY ? ImVec2(1.0f, 0.0f) : ImVec2(1.0f, 1.0f);
            ImGui::Image(icon.id, ImVec2(18.0f, 18.0f), uvMin, uvMax);
        } else {
            ImGui::TextDisabled("Vol");
        }
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("Preview Volume");
        ImGui::SetNextItemWidth(std::max(140.0f, ImGui::GetContentRegionAvail().x));
        if (ImGui::SliderFloat(id, &audioPreviewVolume, 0.0f, 2.0f, "%.2fx")) {
            audioPreviewVolume = std::clamp(audioPreviewVolume, 0.0f, 2.0f);
            saveEditorUserSettings();
            syncClipPreviewVolume(context, baseVolume);
        }
    };

    auto drawFileReferenceSlot = [&](const char* label,
                                     const char* id,
                                     std::string& path,
                                     FileCategory expectedCategory,
                                     const char* noneLabel) -> bool {
        bool changed = false;
        const std::string display = path.empty() ? std::string(noneLabel) : fs::path(path).filename().string();
        char displayBuf[512] = {};
        std::snprintf(displayBuf, sizeof(displayBuf), "%s", display.c_str());
        ImGui::PushID(id);

        ImGui::TextDisabled("%s", label);
        ImGui::SetNextItemWidth(-146.0f);
        ImGui::InputText("##SlotValue", displayBuf, sizeof(displayBuf), ImGuiInputTextFlags_ReadOnly);
        if (ImGui::IsItemHovered()) {
            if (path.empty()) {
                ImGui::SetTooltip("Drag a matching asset here.");
            } else {
                ImGui::SetTooltip("%s", path.c_str());
            }
        }
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("FILE_PATH")) {
                const char* droppedPath = static_cast<const char*>(payload->Data);
                if (droppedPath && *droppedPath) {
                    std::error_code ec;
                    fs::directory_entry entry(droppedPath, ec);
                    if (!ec && fileBrowser.getFileCategory(entry) == expectedCategory) {
                        path = entry.path().string();
                        changed = true;
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }

        ImGui::SameLine();
        bool selectionMatches = false;
        if (!fileBrowser.selectedFile.empty() && fs::exists(fileBrowser.selectedFile)) {
            selectionMatches = fileBrowser.getFileCategory(fs::directory_entry(fileBrowser.selectedFile)) == expectedCategory;
        }
        ImGui::BeginDisabled(!selectionMatches);
        if (ImGui::SmallButton("Use Selection")) {
            path = fileBrowser.selectedFile.string();
            changed = true;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(path.empty());
        if (ImGui::SmallButton("Clear")) {
            path.clear();
            changed = true;
        }
        ImGui::EndDisabled();
        ImGui::PopID();
        return changed;
    };

    auto drawSceneObjectReferenceSlot = [&](const char* label,
                                            const char* id,
                                            int& targetId,
                                            int disallowId,
                                            const char* noneLabel) -> bool {
        bool changed = false;
        ImGui::PushID(id);
        std::string display = noneLabel;
        if (targetId >= 0) {
            if (SceneObject* current = findObjectById(targetId)) {
                display = current->name + " (" + std::to_string(current->id) + ")";
            } else {
                targetId = -1;
            }
        }

        if (ImGui::BeginCombo(label, display.c_str())) {
            if (ImGui::Selectable(noneLabel, targetId < 0)) {
                targetId = -1;
                changed = true;
            }
            for (const auto& candidate : sceneObjects) {
                if (candidate.id == disallowId) continue;
                const std::string option = candidate.name + " (" + std::to_string(candidate.id) + ")";
                const bool selected = candidate.id == targetId;
                if (ImGui::Selectable(option.c_str(), selected)) {
                    targetId = candidate.id;
                    changed = true;
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCENE_OBJECT")) {
                if (payload->DataSize == sizeof(int)) {
                    int droppedId = *static_cast<const int*>(payload->Data);
                    if (droppedId != disallowId) {
                        targetId = droppedId;
                        changed = true;
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }
        ImGui::SameLine();
        SceneObject* selectedTarget = findObjectById(selectedObjectId);
        const bool canUseSelected = selectedTarget && selectedTarget->id != disallowId;
        ImGui::BeginDisabled(!canUseSelected);
        if (ImGui::SmallButton("Use Selected")) {
            targetId = selectedTarget->id;
            changed = true;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(targetId < 0);
        if (ImGui::SmallButton("Clear")) {
            targetId = -1;
            changed = true;
        }
        ImGui::EndDisabled();
        ImGui::PopID();
        return changed;
    };

    auto renderMaterialPreviewTexture = [&](const MaterialProperties& material,
                                            const std::string& albedoPath,
                                            const std::string& overlayPath,
                                            const std::string& normalPath,
                                            bool useOverlay,
                                            const std::string& vertShaderPath,
                                            const std::string& fragShaderPath,
                                            int targetWidth,
                                            int targetHeight,
                                            int previewSlot,
                                            const glm::vec2& orbitAngles) {
        static const std::string kPreviewWhiteTexture = "Resources/Textures/editor_preview_white.ppm";

        Camera previewCamera;
        previewCamera.position = glm::vec3(0.0f, 0.18f, 2.9f);
        previewCamera.front = glm::normalize(glm::vec3(0.0f, -0.06f, -1.0f));
        previewCamera.up = glm::vec3(0.0f, 1.0f, 0.0f);

        std::vector<SceneObject> previewScene;
        previewScene.reserve(4);

        SceneObject keyLight("MatPreviewKey", ObjectType::PointLight, -9201);
        keyLight.hasLight = true;
        keyLight.position = glm::vec3(1.5f, 1.6f, 2.0f);
        keyLight.light.type = LightType::Point;
        keyLight.light.color = glm::vec3(1.0f, 0.98f, 0.94f);
        keyLight.light.intensity = 2.8f;
        keyLight.light.range = 8.0f;
        previewScene.push_back(keyLight);

        SceneObject fillLight("MatPreviewFill", ObjectType::PointLight, -9202);
        fillLight.hasLight = true;
        fillLight.position = glm::vec3(-1.7f, 0.5f, 1.4f);
        fillLight.light.type = LightType::Point;
        fillLight.light.color = glm::vec3(0.72f, 0.79f, 1.0f);
        fillLight.light.intensity = 1.0f;
        fillLight.light.range = 8.0f;
        previewScene.push_back(fillLight);

        SceneObject rimLight("MatPreviewRim", ObjectType::DirectionalLight, -9203);
        rimLight.hasLight = true;
        rimLight.light.type = LightType::Directional;
        rimLight.light.color = glm::vec3(0.96f, 0.98f, 1.0f);
        rimLight.light.intensity = 0.55f;
        rimLight.rotation = glm::vec3(24.0f, 208.0f, 0.0f);
        previewScene.push_back(rimLight);

        SceneObject previewSphere("MatPreviewSphere", ObjectType::Sphere, -9204);
        previewSphere.hasRenderer = true;
        previewSphere.renderType = RenderType::Sphere;
        previewSphere.position = glm::vec3(0.0f, 0.0f, 0.0f);
        previewSphere.rotation = glm::vec3(std::clamp(-orbitAngles.y, -55.0f, 55.0f), orbitAngles.x - 18.0f, 0.0f);
        previewSphere.scale = glm::vec3(1.45f);
        previewSphere.material = material;
        previewSphere.albedoTexturePath = albedoPath.empty() ? kPreviewWhiteTexture : albedoPath;
        previewSphere.overlayTexturePath = overlayPath;
        previewSphere.normalMapPath = normalPath;
        previewSphere.useOverlay = useOverlay;
        previewSphere.vertexShaderPath = vertShaderPath;
        previewSphere.fragmentShaderPath = fragShaderPath;
        previewScene.push_back(previewSphere);

        return renderer.renderScenePreview(
            previewCamera,
            previewScene,
            targetWidth,
            targetHeight,
            32.0f,
            0.1f,
            20.0f,
            false,
            previewSlot,
            true
        );
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
        static std::unordered_map<ImGuiID, ImVec2> previewOrbitById;

        ImGui::PushID(idSuffix);
        const ImGuiID previewId = ImGui::GetID("MaterialPreviewOrbit");
        ImVec2& orbit = previewOrbitById[previewId];

        const float availableWidth = ImGui::GetContentRegionAvail().x;
        const float previewWidth = std::clamp(212.0f * previewScale, 132.0f, std::max(132.0f, availableWidth));
        const float previewHeight = std::clamp(previewWidth * 0.76f, 126.0f, 240.0f);
        const int targetWidth = std::max(96, static_cast<int>(previewWidth));
        const int targetHeight = std::max(96, static_cast<int>(previewHeight));

        const unsigned int previewTexture = renderMaterialPreviewTexture(
            material,
            albedoPath,
            overlayPath,
            normalPath,
            useOverlay,
            vertShaderPath,
            fragShaderPath,
            targetWidth,
            targetHeight,
            previewSlot,
            glm::vec2(orbit.x, orbit.y)
        );

        const ImVec2 imageSize(previewWidth, previewHeight);
        const float padX = std::max(0.0f, availableWidth - previewWidth);
        if (padX > 1.0f) {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + padX * 0.5f);
        }

        const ImVec2 imagePos = ImGui::GetCursorScreenPos();
        if (previewTexture != 0) {
            ImGui::Image((ImTextureID)(intptr_t)previewTexture, imageSize, ImVec2(0, 1), ImVec2(1, 0));
            ImGui::SetCursorScreenPos(imagePos);
            ImGui::InvisibleButton("##PreviewDragArea", imageSize);
            if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                const ImVec2 dragDelta = ImGui::GetIO().MouseDelta;
                orbit.x += dragDelta.x * 0.6f;
                orbit.y = std::clamp(orbit.y - dragDelta.y * 0.45f, -55.0f, 55.0f);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Drag to rotate preview");
            }
        } else {
            ImGui::Dummy(imageSize);
            ImGui::GetWindowDrawList()->AddText(ImVec2(imagePos.x + 12.0f, imagePos.y + 12.0f),
                                                IM_COL32(255, 140, 140, 255), "Preview unavailable");
        }

        ImGui::PopID();
    };

    static float assetMaterialPreviewScale = 1.0f;
    static float objectMaterialPreviewScale = 1.0f;

    struct ComponentHeaderState {
        bool open = false;
        bool enabledChanged = false;
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

    auto applyShaderPreset = [&](MaterialShaderPreset preset,
                                 std::string& shaderPack,
                                 std::string& vert,
                                 std::string& frag) {
        std::string nextVert = vert;
        std::string nextFrag = frag;
        std::string nextPack = shaderPack;
        if (preset == MaterialShaderPreset::EngineLit) {
            nextPack.clear();
            nextVert.clear();
            nextFrag.clear();
        } else if (preset == MaterialShaderPreset::ScrollingUV) {
            auto scrolling = resolveScrollingShaderPaths();
            nextPack.clear();
            nextVert = scrolling.first;
            nextFrag = scrolling.second;
        }
        bool changed = (nextPack != shaderPack) || (nextVert != vert) || (nextFrag != frag);
        if (changed) {
            shaderPack = std::move(nextPack);
            vert = std::move(nextVert);
            frag = std::move(nextFrag);
        }
        return changed;
    };

    auto resolveEditorAssetPath = [&](const std::string& rawPath) -> fs::path {
        if (rawPath.empty()) {
            return {};
        }

        std::error_code ec;
        fs::path candidate(rawPath);
        if (fs::exists(candidate, ec) && !ec) {
            return candidate;
        }

        if (projectManager.currentProject.isLoaded && !projectManager.currentProject.projectPath.empty()) {
            ec.clear();
            fs::path projectCandidate = projectManager.currentProject.projectPath / candidate;
            if (fs::exists(projectCandidate, ec) && !ec) {
                return projectCandidate.lexically_normal();
            }
        }

        return candidate;
    };

    auto revealAssetInProject = [&](const std::string& rawPath) {
        const fs::path resolved = resolveEditorAssetPath(rawPath);
        if (resolved.empty()) {
            return;
        }
        std::error_code ec;
        if (fs::exists(resolved, ec) && !ec) {
            fileBrowser.navigateTo(resolved.parent_path());
            fileBrowser.selectedFile = resolved;
        }
    };

    auto assetDisplayName = [&](const std::string& rawPath, const char* emptyLabel) {
        if (rawPath.empty()) {
            return std::string(emptyLabel);
        }

        const fs::path resolved = resolveEditorAssetPath(rawPath);
        const std::string stem = resolved.stem().string();
        if (!stem.empty()) {
            return stem;
        }

        const std::string fileName = resolved.filename().string();
        if (!fileName.empty()) {
            return fileName;
        }

        return std::string(emptyLabel);
    };

    auto assetHintLabel = [&](const std::string& rawPath, const char* emptyLabel) {
        if (rawPath.empty()) {
            return std::string(emptyLabel);
        }

        const fs::path resolved = resolveEditorAssetPath(rawPath);
        const std::string parentName = resolved.parent_path().filename().string();
        return parentName.empty() ? std::string("Assigned asset") : parentName;
    };

    auto shaderDisplayName = [&](const std::string& shaderPackPath,
                                 const std::string& vertShaderPath,
                                 const std::string& fragShaderPath,
                                 bool preferDefaultEngineShader = true) {
        if (!shaderPackPath.empty()) {
            return assetDisplayName(shaderPackPath, "Shader Pack");
        }

        const MaterialShaderPreset preset = shaderPresetFromPaths(vertShaderPath, fragShaderPath);
        if (preset == MaterialShaderPreset::ScrollingUV) {
            return std::string("Scrolling UV");
        }
        if (preset == MaterialShaderPreset::EngineLit && preferDefaultEngineShader) {
            return std::string("Engine Lit");
        }

        const std::string shaderPath = !fragShaderPath.empty() ? fragShaderPath : vertShaderPath;
        if (!shaderPath.empty()) {
            return assetDisplayName(shaderPath, "Legacy Custom Shader");
        }

        return preferDefaultEngineShader ? std::string("Engine Lit") : std::string("Default");
    };

    auto drawInspectorIconButton = [&](const char* id,
                                       ImTextureID icon,
                                       const char* fallbackLabel,
                                       const char* tooltip,
                                       bool flipY = false) {
        bool clicked = false;
        const float buttonSize = std::max(18.0f, ImGui::GetFrameHeight() - 2.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.0f, 2.0f));
        if (icon != static_cast<ImTextureID>(0)) {
            const ImVec2 uvMin = flipY ? ImVec2(0, 1) : ImVec2(0, 0);
            const ImVec2 uvMax = flipY ? ImVec2(1, 0) : ImVec2(1, 1);
            clicked = ImGui::ImageButton(id, icon, ImVec2(buttonSize, buttonSize), uvMin, uvMax);
        } else {
            clicked = ImGui::SmallButton(fallbackLabel);
        }
        ImGui::PopStyleVar();
        if (tooltip && ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", tooltip);
        }
        return clicked;
    };

    auto drawTransparencyAwareBackground = [&](ImDrawList* drawList,
                                               const ImVec2& minPos,
                                               const ImVec2& maxPos,
                                               float rounding,
                                               float cellSize = 10.0f) {
        drawList->AddRectFilled(minPos, maxPos, IM_COL32(34, 37, 44, 120), rounding);

        const ImU32 lightCell = IM_COL32(78, 84, 96, 90);
        const ImU32 darkCell = IM_COL32(52, 57, 68, 90);
        const int cols = std::max(1, static_cast<int>(std::ceil((maxPos.x - minPos.x) / cellSize)));
        const int rows = std::max(1, static_cast<int>(std::ceil((maxPos.y - minPos.y) / cellSize)));
        for (int y = 0; y < rows; ++y) {
            for (int x = 0; x < cols; ++x) {
                const float x0 = minPos.x + x * cellSize;
                const float y0 = minPos.y + y * cellSize;
                const float x1 = std::min(maxPos.x, x0 + cellSize);
                const float y1 = std::min(maxPos.y, y0 + cellSize);
                drawList->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1),
                                        ((x + y) & 1) == 0 ? lightCell : darkCell, 0.0f);
            }
        }

        drawList->AddRect(minPos, maxPos, IM_COL32(92, 100, 116, 120), rounding, 0, 1.0f);
    };

    auto matchesPopupFilter = [](const std::string& text, const std::string& filter) {
        if (filter.empty()) {
            return true;
        }

        const std::string filterLower = MaterialAssetLowercase(filter);
        return MaterialAssetLowercase(text).find(filterLower) != std::string::npos;
    };

    auto collectAssetsInDirectory = [&](const fs::path& root,
                                        const std::function<bool(const fs::directory_entry&)>& predicate) {
        std::vector<fs::path> matches;
        if (root.empty()) {
            return matches;
        }

        std::error_code ec;
        if (!fs::exists(root, ec) || ec) {
            return matches;
        }

        for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
             it != end; it.increment(ec)) {
            if (ec) {
                ec.clear();
                continue;
            }

            const fs::directory_entry& entry = *it;
            if (!entry.is_regular_file(ec) || ec) {
                ec.clear();
                continue;
            }

            if (predicate(entry)) {
                matches.push_back(entry.path());
            }
        }

        std::sort(matches.begin(), matches.end(), [](const fs::path& a, const fs::path& b) {
            return a.filename().string() < b.filename().string();
        });
        matches.erase(std::unique(matches.begin(), matches.end()), matches.end());
        return matches;
    };

    auto collectProjectTextureAssets = [&]() {
        fs::path root = projectManager.currentProject.isLoaded
            ? projectManager.currentProject.assetsPath
            : fileBrowser.projectRoot;
        return collectAssetsInDirectory(root, [&](const fs::directory_entry& entry) {
            return fileBrowser.isTextureFile(entry);
        });
    };

    auto collectProjectShaderPackAssets = [&]() {
        fs::path root = projectManager.currentProject.isLoaded
            ? projectManager.currentProject.assetsPath
            : fileBrowser.projectRoot;
        return collectAssetsInDirectory(root, [&](const fs::directory_entry& entry) {
            return IsShaderPackFile(entry.path());
        });
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

    auto drawMaterialTextureField = [&](const char* label,
                                        const char* idSuffix,
                                        std::string& path) {
        static std::unordered_map<std::string, std::string> popupFilters;

        bool changed = false;
        ImGui::PushID(idSuffix);

        const float rowHeight = std::max(20.0f, ImGui::GetFrameHeight());
        const float thumbSize = rowHeight;
        const float compactButtonSize = rowHeight;
        const float itemSpacingX = ImGui::GetStyle().ItemSpacing.x;
        const std::string popupId = std::string("##TexturePicker_") + idSuffix;
        const std::string displayName = assetDisplayName(path, "None");
        const fs::path resolvedPath = resolveEditorAssetPath(path);

        drawMaterialInlineLabel(label);
        ImGui::SameLine();

        Texture* thumbTexture = nullptr;
        if (!resolvedPath.empty()) {
            thumbTexture = renderer.getTexture(resolvedPath.string(), MaterialProperties::TextureFilter::Bilinear);
        }

        const ImVec2 thumbScreenPos = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##Thumb", ImVec2(thumbSize, thumbSize));
        ImDrawList* thumbDrawList = ImGui::GetWindowDrawList();
        const ImVec2 thumbMin = thumbScreenPos;
        const ImVec2 thumbMax = ImVec2(thumbScreenPos.x + thumbSize, thumbScreenPos.y + thumbSize);
        drawTransparencyAwareBackground(thumbDrawList, thumbMin, thumbMax, 4.0f, 6.0f);
        if (thumbTexture && thumbTexture->GetID()) {
            const float inset = 1.0f;
            thumbDrawList->AddImage(
                (ImTextureID)(intptr_t)thumbTexture->GetID(),
                ImVec2(thumbMin.x + inset, thumbMin.y + inset),
                ImVec2(thumbMax.x - inset, thumbMax.y - inset),
                ImVec2(0, 1),
                ImVec2(1, 0)
            );
        } else {
            const ImVec2 center((thumbMin.x + thumbMax.x) * 0.5f, (thumbMin.y + thumbMax.y) * 0.5f);
            thumbDrawList->AddLine(ImVec2(center.x - 5.0f, center.y), ImVec2(center.x + 5.0f, center.y),
                                   IM_COL32(184, 190, 204, 180), 1.5f);
            thumbDrawList->AddLine(ImVec2(center.x, center.y - 5.0f), ImVec2(center.x, center.y + 5.0f),
                                   IM_COL32(184, 190, 204, 180), 1.5f);
        }
        if (!path.empty() && ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
            revealAssetInProject(path);
        }
        if (!path.empty() && ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Reveal in Project\n%s", path.c_str());
        }

        ImGui::SameLine();
        const float controlsWidth = compactButtonSize * 2.0f + itemSpacingX * 2.0f;
        const float fieldWidth = std::max(120.0f, ImGui::GetContentRegionAvail().x - controlsWidth);
        const ImVec2 fieldMin = ImGui::GetCursorScreenPos();
        if (ImGui::Button("##TextureField", ImVec2(fieldWidth, rowHeight))) {
            ImGui::OpenPopup(popupId.c_str());
        }
        const ImVec2 fieldMax(fieldMin.x + fieldWidth, fieldMin.y + rowHeight);
        const std::string fieldText = fitLabelToWidth(path.empty() ? std::string("Empty") : displayName, fieldWidth - 14.0f);
        ImDrawList* fieldDrawList = ImGui::GetWindowDrawList();
        fieldDrawList->PushClipRect(ImVec2(fieldMin.x + 6.0f, fieldMin.y),
                                    ImVec2(fieldMax.x - 6.0f, fieldMax.y),
                                    true);
        fieldDrawList->AddText(
            ImVec2(fieldMin.x + 6.0f, fieldMin.y + std::max(0.0f, (rowHeight - ImGui::GetTextLineHeight()) * 0.5f)),
            ImGui::GetColorU32(ImGuiCol_Text),
            fieldText.c_str()
        );
        fieldDrawList->PopClipRect();
        if (ImGui::IsItemHovered()) {
            if (path.empty()) {
                ImGui::SetTooltip("%s\nDrag an image here or choose one from the asset list.",
                                  assetHintLabel(path, "No texture assigned").c_str());
            } else {
                ImGui::SetTooltip("%s", path.c_str());
            }
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
        if (drawInspectorIconButton("##PickAsset", iconTextureSelect, "...", "Choose texture asset")) {
            ImGui::OpenPopup(popupId.c_str());
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(path.empty());
        if (ImGui::Button("X", ImVec2(compactButtonSize, rowHeight))) {
            path.clear();
            changed = true;
        }
        ImGui::EndDisabled();

        if (ImGui::BeginPopup(popupId.c_str())) {
            std::string& filter = popupFilters[popupId];
            char searchBuf[128] = {};
            std::snprintf(searchBuf, sizeof(searchBuf), "%s", filter.c_str());
            if (ImGui::InputTextWithHint("##Filter", "Filter textures", searchBuf, sizeof(searchBuf))) {
                filter = searchBuf;
            }

            const bool canUseSelected = isTextureOrSpriteSheetSelection(fileBrowser.selectedFile);
            if (canUseSelected && ImGui::Selectable("Use Selected Asset")) {
                path = ResolveSpriteSheetImagePath(fileBrowser.selectedFile).string();
                changed = true;
                ImGui::CloseCurrentPopup();
            }

            ImGui::Separator();
            ImGui::BeginChild("##TextureList", ImVec2(320.0f, 180.0f), false);
            for (const fs::path& assetPath : collectProjectTextureAssets()) {
                const std::string assetName = assetDisplayName(assetPath.string(), "Texture");
                const std::string assetInfo = assetHintLabel(assetPath.string(), "Texture");
                if (!matchesPopupFilter(assetName + " " + assetInfo, filter)) {
                    continue;
                }

                const bool selected = resolveEditorAssetPath(path) == assetPath;
                if (ImGui::Selectable((assetName + "##" + assetPath.string()).c_str(), selected)) {
                    path = assetPath.string();
                    changed = true;
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", assetPath.string().c_str());
                }
            }
            ImGui::EndChild();
            ImGui::EndPopup();
        }

        ImGui::PopID();
        return changed;
    };

    auto drawMaterialShaderPackField = [&](const char* label,
                                           const char* idSuffix,
                                           std::string& shaderPackPath,
                                           std::string& vertShaderPath,
                                           std::string& fragShaderPath,
                                           const MaterialProperties& material,
                                           const std::string& albedoPath,
                                           const std::string& overlayPath,
                                           const std::string& normalPath,
                                           bool useOverlay,
                                           int previewSlot) {
        static std::unordered_map<std::string, std::string> popupFilters;
        (void)material;
        (void)albedoPath;
        (void)overlayPath;
        (void)normalPath;
        (void)useOverlay;
        (void)previewSlot;

        bool changed = false;
        ImGui::PushID(idSuffix);
        const std::string popupId = std::string("##ShaderPackPicker_") + idSuffix;
        const float rowHeight = std::max(20.0f, ImGui::GetFrameHeight());
        const float compactButtonSize = rowHeight;
        const float itemSpacingX = ImGui::GetStyle().ItemSpacing.x;

        auto assignShaderPack = [&](const std::string& packPath) {
            ShaderPackAssetData packData;
            if (!ReadShaderPackFile(packPath, packData)) {
                return false;
            }
            shaderPackPath = packPath;
            vertShaderPath = packData.vertexShaderPath;
            fragShaderPath = packData.fragmentShaderPath;
            return true;
        };

        std::string selectionLabel = "Engine Lit";
        std::string selectionHint = "Default engine shader";
        if (!shaderPackPath.empty()) {
            selectionLabel = assetDisplayName(shaderPackPath, "Shader Pack");
            selectionHint = "Shader Pack";
        } else {
            MaterialShaderPreset preset = shaderPresetFromPaths(vertShaderPath, fragShaderPath);
            if (preset == MaterialShaderPreset::ScrollingUV) {
                selectionLabel = "Scrolling UV";
                selectionHint = "Default engine shader";
            } else if (preset == MaterialShaderPreset::Custom && (!vertShaderPath.empty() || !fragShaderPath.empty())) {
                selectionLabel = "Legacy Custom Shader";
                selectionHint = "Using internal shader paths";
            }
        }

        drawMaterialInlineLabel(label);
        ImGui::SameLine();

        const float controlsWidth = compactButtonSize * 2.0f + itemSpacingX * 2.0f;
        const float fieldWidth = std::max(140.0f, ImGui::GetContentRegionAvail().x - controlsWidth);
        const ImVec2 fieldMin = ImGui::GetCursorScreenPos();
        if (ImGui::Button("##ShaderPack", ImVec2(fieldWidth, rowHeight))) {
            ImGui::OpenPopup(popupId.c_str());
        }
        const ImVec2 fieldMax(fieldMin.x + fieldWidth, fieldMin.y + rowHeight);
        ImDrawList* fieldDrawList = ImGui::GetWindowDrawList();
        const ImU32 textColor = ImGui::GetColorU32(ImGuiCol_Text);
        const std::string fieldText = fitLabelToWidth(selectionLabel, fieldWidth - 14.0f);
        const ImVec2 textMin(fieldMin.x + 6.0f, fieldMin.y);
        const ImVec2 textMax(fieldMax.x - 8.0f, fieldMax.y);
        fieldDrawList->PushClipRect(textMin, textMax, true);
        fieldDrawList->AddText(
            ImVec2(textMin.x, fieldMin.y + std::max(0.0f, (rowHeight - ImGui::GetTextLineHeight()) * 0.5f)),
            textColor,
            fieldText.c_str()
        );
        fieldDrawList->PopClipRect();
        if (ImGui::IsItemHovered()) {
            if (!shaderPackPath.empty()) {
                ImGui::SetTooltip("%s", shaderPackPath.c_str());
            } else if (!vertShaderPath.empty() || !fragShaderPath.empty()) {
                ImGui::SetTooltip("%s\nVertex: %s\nFragment: %s", selectionHint.c_str(),
                                  vertShaderPath.c_str(), fragShaderPath.c_str());
            } else {
                ImGui::SetTooltip("%s", selectionHint.c_str());
            }
        }
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("FILE_PATH")) {
                const char* dropped = static_cast<const char*>(payload->Data);
                const fs::path droppedPath(dropped ? dropped : "");
                if (IsShaderPackFile(droppedPath) && assignShaderPack(droppedPath.string())) {
                    changed = true;
                }
            }
            ImGui::EndDragDropTarget();
        }
        ImGui::SameLine();
        if (drawInspectorIconButton("##PickShaderPack", iconTextureSelect, "...", "Choose shader pack")) {
            ImGui::OpenPopup(popupId.c_str());
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(shaderPackPath.empty() && vertShaderPath.empty() && fragShaderPath.empty());
        if (ImGui::Button("X", ImVec2(compactButtonSize, rowHeight))) {
            shaderPackPath.clear();
            vertShaderPath.clear();
            fragShaderPath.clear();
            changed = true;
        }
        ImGui::EndDisabled();

        if (ImGui::BeginPopup(popupId.c_str())) {
            std::string& filter = popupFilters[popupId];
            char searchBuf[128] = {};
            std::snprintf(searchBuf, sizeof(searchBuf), "%s", filter.c_str());
            if (ImGui::InputTextWithHint("##Filter", "Filter shader packs", searchBuf, sizeof(searchBuf))) {
                filter = searchBuf;
            }

            if (matchesPopupFilter("Engine Lit", filter) &&
                ImGui::Selectable("Engine Lit (Default)", shaderPresetFromPaths(vertShaderPath, fragShaderPath) == MaterialShaderPreset::EngineLit &&
                                                          shaderPackPath.empty())) {
                changed |= applyShaderPreset(MaterialShaderPreset::EngineLit, shaderPackPath, vertShaderPath, fragShaderPath);
                ImGui::CloseCurrentPopup();
            }
            if (matchesPopupFilter("Scrolling UV", filter) &&
                ImGui::Selectable("Scrolling UV", shaderPresetFromPaths(vertShaderPath, fragShaderPath) == MaterialShaderPreset::ScrollingUV &&
                                                shaderPackPath.empty())) {
                changed |= applyShaderPreset(MaterialShaderPreset::ScrollingUV, shaderPackPath, vertShaderPath, fragShaderPath);
                ImGui::CloseCurrentPopup();
            }

            const bool selectedIsPack = IsShaderPackFile(fileBrowser.selectedFile);
            if (selectedIsPack) {
                ImGui::Separator();
                if (ImGui::Selectable("Use Selected Shader Pack")) {
                    if (assignShaderPack(fileBrowser.selectedFile.string())) {
                        changed = true;
                        ImGui::CloseCurrentPopup();
                    }
                }
            }

            ImGui::Separator();
            ImGui::BeginChild("##ShaderPackList", ImVec2(320.0f, 180.0f), false);
            for (const fs::path& assetPath : collectProjectShaderPackAssets()) {
                const std::string assetName = assetDisplayName(assetPath.string(), "Shader Pack");
                const std::string assetInfo = assetHintLabel(assetPath.string(), "Shader Pack");
                if (!matchesPopupFilter(assetName + " " + assetInfo, filter)) {
                    continue;
                }

                ShaderPackAssetData packData;
                if (!ReadShaderPackFile(assetPath.string(), packData)) {
                    ImGui::BeginDisabled();
                    ImGui::Selectable((assetName + " (invalid)##" + assetPath.string()).c_str(), false);
                    ImGui::EndDisabled();
                    continue;
                }

                const bool selected = resolveEditorAssetPath(shaderPackPath) == assetPath;
                if (ImGui::Selectable((assetName + "##" + assetPath.string()).c_str(), selected)) {
                    if (assignShaderPack(assetPath.string())) {
                        changed = true;
                        ImGui::CloseCurrentPopup();
                    }
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", assetPath.string().c_str());
                }
            }
            ImGui::EndChild();
            ImGui::EndPopup();
        }

        ImGui::PopID();
        return changed;
    };
    (void)drawMaterialShaderPackField;

    auto drawMaterialShaderSelectorInline = [&](const char* idSuffix,
                                                std::string& shaderPackPath,
                                                std::string& vertShaderPath,
                                                std::string& fragShaderPath,
                                                float width,
                                                float height) {
        static std::unordered_map<std::string, std::string> popupFilters;

        bool changed = false;
        ImGui::PushID(idSuffix);
        const std::string popupId = std::string("##HeaderShaderPackPicker_") + idSuffix;

        auto assignShaderPack = [&](const std::string& packPath) {
            ShaderPackAssetData packData;
            if (!ReadShaderPackFile(packPath, packData)) {
                return false;
            }
            shaderPackPath = packPath;
            vertShaderPath = packData.vertexShaderPath;
            fragShaderPath = packData.fragmentShaderPath;
            return true;
        };

        const std::string selectionLabel = shaderDisplayName(shaderPackPath, vertShaderPath, fragShaderPath, false);
        const float arrowRegionWidth = std::min(18.0f, std::max(12.0f, height - 2.0f));
        const std::string clippedLabel = fitLabelToWidth(selectionLabel, width - arrowRegionWidth - 14.0f);
        const ImVec2 fieldMin = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("##HeaderShaderSelector", ImVec2(width, height))) {
            ImGui::OpenPopup(popupId.c_str());
        }
        const ImVec2 fieldMax(fieldMin.x + width, fieldMin.y + height);
        ImDrawList* fieldDrawList = ImGui::GetWindowDrawList();
        const bool hovered = ImGui::IsItemHovered();
        const bool held = ImGui::IsItemActive();
        const ImU32 frameColor = ImGui::GetColorU32(
            held ? ImGuiCol_FrameBgActive : (hovered ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg));
        const ImU32 borderColor = ImGui::GetColorU32(ImGuiCol_Border);
        fieldDrawList->AddRectFilled(fieldMin, fieldMax, frameColor, 4.0f);
        fieldDrawList->AddRect(fieldMin, fieldMax, borderColor, 4.0f);
        fieldDrawList->PushClipRect(ImVec2(fieldMin.x + 6.0f, fieldMin.y),
                                    ImVec2(fieldMax.x - arrowRegionWidth - 6.0f, fieldMax.y),
                                    true);
        fieldDrawList->AddText(
            ImVec2(fieldMin.x + 6.0f, fieldMin.y + std::max(0.0f, (height - ImGui::GetTextLineHeight()) * 0.5f)),
            ImGui::GetColorU32(ImGuiCol_TextDisabled),
            clippedLabel.c_str()
        );
        fieldDrawList->PopClipRect();
        const float arrowCenterX = fieldMax.x - arrowRegionWidth * 0.5f - 2.0f;
        const float arrowCenterY = fieldMin.y + height * 0.5f + 1.0f;
        fieldDrawList->AddTriangleFilled(
            ImVec2(arrowCenterX - 4.0f, arrowCenterY - 2.0f),
            ImVec2(arrowCenterX + 4.0f, arrowCenterY - 2.0f),
            ImVec2(arrowCenterX, arrowCenterY + 2.5f),
            ImGui::GetColorU32(ImGuiCol_TextDisabled)
        );
        if (ImGui::IsItemHovered()) {
            if (!shaderPackPath.empty()) {
                ImGui::SetTooltip("%s", shaderPackPath.c_str());
            } else if (!vertShaderPath.empty() || !fragShaderPath.empty()) {
                ImGui::SetTooltip("Vertex: %s\nFragment: %s", vertShaderPath.c_str(), fragShaderPath.c_str());
            } else {
                ImGui::SetTooltip("Choose shader");
            }
        }
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("FILE_PATH")) {
                const char* dropped = static_cast<const char*>(payload->Data);
                const fs::path droppedPath(dropped ? dropped : "");
                if (IsShaderPackFile(droppedPath) && assignShaderPack(droppedPath.string())) {
                    changed = true;
                }
            }
            ImGui::EndDragDropTarget();
        }

        if (ImGui::BeginPopup(popupId.c_str())) {
            std::string& filter = popupFilters[popupId];
            char searchBuf[128] = {};
            std::snprintf(searchBuf, sizeof(searchBuf), "%s", filter.c_str());
            if (ImGui::InputTextWithHint("##Filter", "Filter shader packs", searchBuf, sizeof(searchBuf))) {
                filter = searchBuf;
            }

            if (matchesPopupFilter("Engine Lit", filter) &&
                ImGui::Selectable("Engine Lit (Default)",
                                  shaderPresetFromPaths(vertShaderPath, fragShaderPath) == MaterialShaderPreset::EngineLit &&
                                  shaderPackPath.empty())) {
                changed |= applyShaderPreset(MaterialShaderPreset::EngineLit, shaderPackPath, vertShaderPath, fragShaderPath);
                ImGui::CloseCurrentPopup();
            }
            if (matchesPopupFilter("Scrolling UV", filter) &&
                ImGui::Selectable("Scrolling UV",
                                  shaderPresetFromPaths(vertShaderPath, fragShaderPath) == MaterialShaderPreset::ScrollingUV &&
                                  shaderPackPath.empty())) {
                changed |= applyShaderPreset(MaterialShaderPreset::ScrollingUV, shaderPackPath, vertShaderPath, fragShaderPath);
                ImGui::CloseCurrentPopup();
            }

            const bool selectedIsPack = IsShaderPackFile(fileBrowser.selectedFile);
            if (selectedIsPack) {
                ImGui::Separator();
                if (ImGui::Selectable("Use Selected Shader Pack")) {
                    if (assignShaderPack(fileBrowser.selectedFile.string())) {
                        changed = true;
                        ImGui::CloseCurrentPopup();
                    }
                }
            }

            ImGui::Separator();
            ImGui::BeginChild("##HeaderShaderPackList", ImVec2(320.0f, 180.0f), false);
            for (const fs::path& assetPath : collectProjectShaderPackAssets()) {
                const std::string assetName = assetDisplayName(assetPath.string(), "Shader Pack");
                const std::string assetInfo = assetHintLabel(assetPath.string(), "Shader Pack");
                if (!matchesPopupFilter(assetName + " " + assetInfo, filter)) {
                    continue;
                }

                ShaderPackAssetData packData;
                if (!ReadShaderPackFile(assetPath.string(), packData)) {
                    ImGui::BeginDisabled();
                    ImGui::Selectable((assetName + " (invalid)##" + assetPath.string()).c_str(), false);
                    ImGui::EndDisabled();
                    continue;
                }

                const bool selected = resolveEditorAssetPath(shaderPackPath) == assetPath;
                if (ImGui::Selectable((assetName + "##" + assetPath.string()).c_str(), selected)) {
                    if (assignShaderPack(assetPath.string())) {
                        changed = true;
                        ImGui::CloseCurrentPopup();
                    }
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", assetPath.string().c_str());
                }
            }
            ImGui::EndChild();
            ImGui::EndPopup();
        }

        ImGui::PopID();
        return changed;
    };

    auto drawMaterialSectionHeader = [&](const char* idSuffix,
                                         const std::string& materialName,
                                         std::string& shaderPackPath,
                                         std::string& vertShaderPath,
                                         std::string& fragShaderPath,
                                         const MaterialProperties& material,
                                         const std::string& albedoPath,
                                         const std::string& overlayPath,
                                         const std::string& normalPath,
                                         bool useOverlay,
                                         bool shaderEditable,
                                         int previewSlot) {
        ImGui::PushID(idSuffix);
        ImGuiStyle& style = ImGui::GetStyle();
        ImGui::SetNextItemAllowOverlap();
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                            ImVec2(style.FramePadding.x, std::max(style.FramePadding.y, 11.0f)));
        const bool open = ImGui::CollapsingHeader("##MaterialHeader",
                                                  ImGuiTreeNodeFlags_DefaultOpen |
                                                  ImGuiTreeNodeFlags_AllowOverlap |
                                                  ImGuiTreeNodeFlags_SpanAvailWidth);
        ImGui::PopStyleVar();

        const ImVec2 headerMin = ImGui::GetItemRectMin();
        const ImVec2 headerMax = ImGui::GetItemRectMax();
        const float headerHeight = headerMax.y - headerMin.y;
        const float previewSize = std::clamp(headerHeight - 10.0f, 20.0f, 30.0f);
        const float arrowWidth = headerHeight;
        const float previewX = headerMin.x + arrowWidth + 2.0f;
        const float previewY = headerMin.y + (headerHeight - previewSize) * 0.5f;
        const float textX = previewX + previewSize + 8.0f;
        const float textRight = headerMax.x - style.FramePadding.x - 6.0f;
        const float textWidth = std::max(24.0f, textRight - textX);
        const std::string primaryText = fitLabelToWidth(materialName.empty() ? std::string("Material") : materialName,
                                                        textWidth);
        const float lineHeight = ImGui::GetTextLineHeight();
        const float lineSpacing = 1.0f;
        const float shaderFieldHeight = std::max(16.0f, lineHeight - 1.0f);
        const float textBlockHeight = lineHeight + lineSpacing + shaderFieldHeight;
        const float textTop = headerMin.y + std::max(0.0f, (headerHeight - textBlockHeight) * 0.5f);
        const float shaderFieldY = textTop + lineHeight + lineSpacing;

        const unsigned int headerPreviewTexture = renderMaterialPreviewTexture(
            material,
            albedoPath,
            overlayPath,
            normalPath,
            useOverlay,
            vertShaderPath,
            fragShaderPath,
            64,
            64,
            previewSlot,
            glm::vec2(12.0f, -8.0f)
        );

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        if (headerPreviewTexture != 0) {
            drawList->PushClipRect(ImVec2(previewX, previewY),
                                   ImVec2(previewX + previewSize, previewY + previewSize),
                                   true);
            drawList->AddImage(
                (ImTextureID)(intptr_t)headerPreviewTexture,
                ImVec2(previewX, previewY),
                ImVec2(previewX + previewSize, previewY + previewSize),
                ImVec2(0, 1),
                ImVec2(1, 0)
            );
            drawList->PopClipRect();
        }

        drawList->PushClipRect(ImVec2(textX, textTop),
                               ImVec2(textRight, textTop + lineHeight),
                               true);
        drawList->AddText(ImVec2(textX, textTop),
                          ImGui::GetColorU32(ImGuiCol_Text),
                          primaryText.c_str());
        drawList->PopClipRect();

        bool changed = false;
        ImGui::SetCursorScreenPos(ImVec2(textX, shaderFieldY));
        if (shaderEditable) {
            changed |= drawMaterialShaderSelectorInline("HeaderShaderSelector",
                                                        shaderPackPath,
                                                        vertShaderPath,
                                                        fragShaderPath,
                                                        textWidth,
                                                        shaderFieldHeight);
        } else {
            ImGui::BeginDisabled();
            ImGui::Button(shaderDisplayName(shaderPackPath, vertShaderPath, fragShaderPath, false).c_str(),
                          ImVec2(textWidth, shaderFieldHeight));
            ImGui::EndDisabled();
        }

        ImGui::PopID();
        return std::pair<bool, bool>{open, changed};
    };

    auto renderMaterialEditorBody = [&](const char* idPrefix,
                                        MaterialProperties& materialValue,
                                        std::string& albedoPath,
                                        std::string& overlayPath,
                                        std::string& normalPath,
                                        bool& useOverlayValue,
                                        std::string& shaderPackPath,
                                        std::string& vertShaderPath,
                                        std::string& fragShaderPath,
                                        SceneObject* spriteTarget,
                                        float& previewScale,
                                        int previewSlot) {
        (void)shaderPackPath;
        bool changed = false;
        ImGui::PushID(idPrefix);

        glm::vec4 baseColor(materialValue.color, materialValue.alpha);
        if (materialColorSamplerHasResult && materialColorSamplerTargetId == idPrefix) {
            baseColor.x = materialColorSamplerResult.x;
            baseColor.y = materialColorSamplerResult.y;
            baseColor.z = materialColorSamplerResult.z;
            materialValue.color = glm::vec3(baseColor);
            materialColorSamplerHasResult = false;
            materialColorSamplerTargetId.clear();
            changed = true;
        }
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("Color");
        ImGui::SameLine(98.0f);
        if (ImGui::ColorButton("##BaseColorSwatch",
                               ImVec4(baseColor.x, baseColor.y, baseColor.z, baseColor.w),
                               ImGuiColorEditFlags_AlphaPreviewHalf,
                               ImVec2(std::max(20.0f, ImGui::GetFrameHeight()), ImGui::GetFrameHeight()))) {
            ImGui::OpenPopup("##BaseColorPicker");
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(usingVulkan());
        if (drawInspectorIconButton("##BaseColorSamplerButton", iconColorPicker, "Pick", "Sample base color from screen", true)) {
            materialColorSamplerActive = true;
            materialColorSamplerAwaitMouseRelease = true;
            materialColorSamplerHasResult = false;
            materialColorSamplerTargetId = idPrefix;
        }
        ImGui::EndDisabled();
        if (ImGui::BeginPopup("##BaseColorPicker")) {
            if (ImGui::ColorPicker4("##BaseColorPickerValue", &baseColor.x,
                                    ImGuiColorEditFlags_PickerHueBar |
                                    ImGuiColorEditFlags_DisplayRGB |
                                    ImGuiColorEditFlags_InputRGB |
                                    ImGuiColorEditFlags_AlphaBar |
                                    ImGuiColorEditFlags_AlphaPreviewHalf)) {
                materialValue.color = glm::vec3(baseColor);
                materialValue.alpha = std::clamp(baseColor.w, 0.0f, 1.0f);
                changed = true;
            }
            ImGui::EndPopup();
        }
        if (materialColorSamplerActive && materialColorSamplerTargetId == idPrefix) {
            ImGui::SameLine();
            ImGui::TextDisabled("Sampling...");
        }

        float metallic = materialValue.specularStrength;
        if (ImGui::SliderFloat("Metallic", &metallic, 0.0f, 1.0f)) {
            materialValue.specularStrength = metallic;
            changed = true;
        }

        float smoothness = materialValue.shininess / 256.0f;
        if (ImGui::SliderFloat("Smoothness", &smoothness, 0.0f, 1.0f)) {
            smoothness = std::clamp(smoothness, 0.0f, 1.0f);
            materialValue.shininess = smoothness * 256.0f;
            changed = true;
        }

        if (ImGui::SliderFloat("Ambient Light", &materialValue.ambientStrength, 0.0f, 1.0f)) {
            changed = true;
        }

        ImGui::Spacing();
        ImGui::SeparatorText("Texture Maps");
        changed |= drawMaterialTextureField("Base Map", (std::string(idPrefix) + "Albedo").c_str(), albedoPath);
        changed |= drawMaterialTextureField("Normal Map", (std::string(idPrefix) + "Normal").c_str(), normalPath);
        const char* texFilterOptions[] = { "Bilinear", "Point" };
        int texFilterIndex =
            (materialValue.textureFilter == MaterialProperties::TextureFilter::Point) ? 1 : 0;
        drawMaterialInlineLabel("Filter");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::Combo("##TextureFilter", &texFilterIndex, texFilterOptions, IM_ARRAYSIZE(texFilterOptions))) {
            materialValue.textureFilter =
                (texFilterIndex == 1) ? MaterialProperties::TextureFilter::Point
                                      : MaterialProperties::TextureFilter::Bilinear;
            changed = true;
        }
        drawMaterialInlineLabel("UV Tiling");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::DragFloat2("##UVTiling",
                              &materialValue.uvTiling.x,
                              0.01f,
                              0.0f,
                              0.0f,
                              "%.2f"))
        {
            materialValue.uvTiling.x = std::max(0.0001f, materialValue.uvTiling.x);
            materialValue.uvTiling.y = std::max(0.0001f, materialValue.uvTiling.y);
            changed = true;
        }
        drawMaterialInlineLabel("UV Offset");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::DragFloat2("##UVOffset",
                              &materialValue.uvOffset.x,
                              0.01f,
                              0.0f,
                              0.0f,
                              "%.2f"))
        {
            changed = true;
        }
        ImGui::SeparatorText("Detail Maps");
        changed |= drawMaterialTextureField("Detail Map", (std::string(idPrefix) + "Overlay").c_str(), overlayPath);
        drawMaterialInlineLabel("Detail Mix");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::SliderFloat("##DetailMix", &materialValue.textureMix, 0.0f, 1.0f)) {
            changed = true;
        }
        useOverlayValue = !overlayPath.empty();

        if (spriteTarget && spriteTarget->renderType == RenderType::Sprite) {
            const bool canUseSpriteAsset = isTextureOrSpriteSheetSelection(fileBrowser.selectedFile);
            ImGui::BeginDisabled(!canUseSpriteAsset);
            if (ImGui::SmallButton((std::string("Use Selection As Sprite Asset##") + idPrefix).c_str())) {
                if (assignSpriteTextureOrClips(*spriteTarget, fileBrowser.selectedFile)) {
                    albedoPath = spriteTarget->albedoTexturePath;
                    changed = true;
                }
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(albedoPath.empty());
            if (ImGui::SmallButton((std::string("Reload Clips##") + idPrefix).c_str())) {
                if (assignSpriteTextureOrClips(*spriteTarget, fs::path(albedoPath))) {
                    albedoPath = spriteTarget->albedoTexturePath;
                    changed = true;
                }
            }
            ImGui::EndDisabled();

            if (!albedoPath.empty()) {
                ImGui::SameLine();
                if (hasSpritesheetPackage() &&
                    ImGui::SmallButton((std::string("Import Sheet##") + idPrefix).c_str())) {
                    pendingSpriteSheetPath = albedoPath;
                    std::snprintf(importSpriteSheetName, sizeof(importSpriteSheetName), "%s",
                                  spriteTarget->name.c_str());
                    importSpriteSheetTarget = isProject25DPipeline()
                        ? SpriteSheetImportTarget::Sprite25D
                        : SpriteSheetImportTarget::UIImage;
                    showImportSpriteSheetDialog = true;
                }
            }

            if (hasSpritesheetPackage() &&
                ImGui::CollapsingHeader((std::string("Sprite Sheet##") + idPrefix).c_str(),
                                        ImGuiTreeNodeFlags_DefaultOpen)) {
                if (ImGui::Checkbox("Enable Sprite Sheet", &spriteTarget->ui.spriteSheetEnabled)) {
                    changed = true;
                }
                ImGui::BeginDisabled(!spriteTarget->ui.spriteSheetEnabled);
                const bool usingCustomClips = spriteTarget->ui.spriteCustomFramesEnabled &&
                                              !spriteTarget->ui.spriteCustomFrames.empty();
                if (usingCustomClips) {
                    const int clipCount = static_cast<int>(spriteTarget->ui.spriteCustomFrames.size());
                    EnsureSpriteClipNames(spriteTarget->ui.spriteCustomFrameNames,
                                          spriteTarget->ui.spriteCustomFrames.size());
                    ImGui::TextDisabled("Using %d cropped sprite clips.", clipCount);
                    spriteTarget->ui.spriteSheetFrame =
                        std::clamp(spriteTarget->ui.spriteSheetFrame, 0, clipCount - 1);
                    const char* previewName =
                        spriteTarget->ui.spriteCustomFrameNames[spriteTarget->ui.spriteSheetFrame].c_str();
                    if (ImGui::BeginCombo("Clip", previewName)) {
                        for (int clipIndex = 0; clipIndex < clipCount; ++clipIndex) {
                            const bool selected = (clipIndex == spriteTarget->ui.spriteSheetFrame);
                            if (ImGui::Selectable(
                                    spriteTarget->ui.spriteCustomFrameNames[clipIndex].c_str(), selected)) {
                                spriteTarget->ui.spriteSheetFrame = clipIndex;
                                changed = true;
                            }
                            if (selected) ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                    int clipIndex = spriteTarget->ui.spriteSheetFrame;
                    if (ImGui::SliderInt("Clip Index", &clipIndex, 0, clipCount - 1)) {
                        spriteTarget->ui.spriteSheetFrame = std::clamp(clipIndex, 0, clipCount - 1);
                        changed = true;
                    }
                } else {
                    if (ImGui::DragInt("Columns", &spriteTarget->ui.spriteSheetColumns, 1.0f, 1, 1024)) {
                        spriteTarget->ui.spriteSheetColumns = std::max(1, spriteTarget->ui.spriteSheetColumns);
                        changed = true;
                    }
                    if (ImGui::DragInt("Rows", &spriteTarget->ui.spriteSheetRows, 1.0f, 1, 1024)) {
                        spriteTarget->ui.spriteSheetRows = std::max(1, spriteTarget->ui.spriteSheetRows);
                        changed = true;
                    }
                    const int frameCount =
                        std::max(1, spriteTarget->ui.spriteSheetColumns * spriteTarget->ui.spriteSheetRows);
                    if (ImGui::SliderInt("Frame", &spriteTarget->ui.spriteSheetFrame, 0, frameCount - 1)) {
                        spriteTarget->ui.spriteSheetFrame =
                            std::clamp(spriteTarget->ui.spriteSheetFrame, 0, frameCount - 1);
                        changed = true;
                    }
                    if (ImGui::DragFloat("FPS", &spriteTarget->ui.spriteSheetFps, 0.1f, 1.0f, 120.0f, "%.1f")) {
                        spriteTarget->ui.spriteSheetFps =
                            std::clamp(spriteTarget->ui.spriteSheetFps, 1.0f, 120.0f);
                        changed = true;
                    }
                    if (ImGui::Checkbox("Loop", &spriteTarget->ui.spriteSheetLoop)) {
                        changed = true;
                    }
                }
                ImGui::EndDisabled();
            }
        }

        ImGui::Spacing();
        ImGui::SeparatorText("Preview");
        drawMaterialPreview(
            (std::string(idPrefix) + "Preview").c_str(),
            materialValue,
            albedoPath,
            overlayPath,
            normalPath,
            useOverlayValue,
            vertShaderPath,
            fragShaderPath,
            previewScale,
            previewSlot
        );

        ImGui::Spacing();
        ImGui::BeginDisabled(vertShaderPath.empty() && fragShaderPath.empty());
        if (ImGui::Button("Reload Shader")) {
            renderer.forceReloadShader(vertShaderPath, fragShaderPath);
        }
        ImGui::EndDisabled();

        ImGui::PopID();
        return changed;
    };

    auto renderMaterialAssetPanel = [&](const char* headerTitle, bool allowApply) {
        if (!browserHasMaterial) return;

        ImGui::SeparatorText(headerTitle);
        if (!inspectedMaterialValid) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Failed to read material file.");
        } else {
            ImGui::TextDisabled("%s", selectedMaterialPath.filename().string().c_str());
            ImGui::TextColored(ImVec4(0.7f, 0.85f, 1.0f, 1.0f), "%s", selectedMaterialPath.string().c_str());
            ImGui::Spacing();

            const bool matChanged = renderMaterialEditorBody(
                "AssetMaterial",
                inspectedMaterial,
                inspectedAlbedo,
                inspectedOverlay,
                inspectedNormal,
                inspectedUseOverlay,
                inspectedShaderPack,
                inspectedVertShader,
                inspectedFragShader,
                nullptr,
                assetMaterialPreviewScale,
                1001
            );

            if (ImGui::Button("Reload")) {
                inspectedMaterialValid = loadMaterialData(
                    selectedMaterialPath.string(),
                    inspectedMaterial,
                    inspectedAlbedo,
                    inspectedOverlay,
                    inspectedNormal,
                    inspectedUseOverlay,
                    &inspectedShaderPack,
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
                        inspectedShaderPack,
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
                        target->shaderPackPath = inspectedShaderPack;
                        target->vertexShaderPath = inspectedVertShader;
                        target->fragmentShaderPath = inspectedFragShader;
                        projectManager.currentProject.hasUnsavedChanges = true;
                        addConsoleMessage("Applied material to " + target->name, ConsoleMessageType::Success);
                    }
                }
                ImGui::EndDisabled();
            }

            if (matChanged) {
                inspectedMaterialValid = true;
            }
        }
    };

    auto renderAudioAssetPanel = [&](const char* headerTitle, SceneObject* target) {
        if (!browserHasAudio) return;

        ImGui::SeparatorText(headerTitle);
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
                stopClipPreview();
            } else {
                beginClipPreview(selectedAudioPath.string(), 1.0f, audioPreviewLoop, AudioPreviewContext::AssetBrowser);
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
                beginClipPreview(selectedAudioPath.string(), 1.0f, audioPreviewLoop, AudioPreviewContext::AssetBrowser);
            }
        }

        drawAudioPreviewVolumeControl("##AudioAssetPreviewVolume", AudioPreviewContext::AssetBrowser, 1.0f);

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
    };

    auto renderTextureAssetPanel = [&](const char* headerTitle) {
        if (!browserHasTexture) return;

        ImGui::SeparatorText(headerTitle);

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
    };

    const bool assetPreviewSuppressed = isPlaying || specMode || testMode || playerMode;
    if (selectedObjectIds.empty()) {
        if (assetPreviewSuppressed && (browserHasMaterial || browserHasAudio || browserHasTexture)) {
            ImGui::TextDisabled("Asset previews are disabled while the scene is running.");
            ImGui::Spacing();
            ImGui::TextDisabled("Select an object to inspect components, or stop playback to edit assets.");
        } else if (browserHasMaterial) {
            renderMaterialAssetPanel("Material Asset", true);
        } else if (browserHasAudio) {
            renderAudioAssetPanel("Audio Clip", nullptr);
        } else if (browserHasTexture) {
            renderTextureAssetPanel("Texture");
        } else {
            ImGui::TextDisabled("No object selected");
        }
        ImGui::PopStyleVar(3);
        ImGui::End();
        return;
    }

    int primaryId = selectedObjectId;
    auto it = std::find_if(sceneObjects.begin(), sceneObjects.end(),
        [primaryId](const SceneObject& obj) { return obj.id == primaryId; });

    if (it == sceneObjects.end()) {
        ImGui::TextDisabled("Object not found");
        ImGui::PopStyleVar(3);
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
    const bool sharedVideoPlayer = allSelected([](const SceneObject& candidate) { return candidate.hasVideoPlayer; });
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
        hasMixedSelected([](const SceneObject& candidate) { return candidate.hasVideoPlayer; }) ||
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
    bool videoPlayerSectionChanged = false;
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
    bool scriptsChanged = false;
    int scriptToRemove = -1;
    bool inspectorOrderChanged = false;
    EnsureInspectorComponentMetadata(obj);

    auto drawComponentHeader = [&](const char* label,
                                   const char* id,
                                   const std::string& reorderKey,
                                   bool* enabled,
                                   bool defaultOpen,
                                   const std::function<void()>& menuFn,
                                   ImTextureID iconTex = static_cast<ImTextureID>(0)) -> ComponentHeaderState {
        ComponentHeaderState state;
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (defaultOpen) {
            flags |= ImGuiTreeNodeFlags_DefaultOpen;
        }

        std::string headerId = std::string(label) + "##" + id;
        ImGui::SetNextItemAllowOverlap();
        ImGuiStyle& style = ImGui::GetStyle();
        state.open = ImGui::CollapsingHeader(headerId.c_str(), flags);

        const ImVec2 headerMin = ImGui::GetItemRectMin();
        const ImVec2 headerMax = ImGui::GetItemRectMax();
        const ImVec2 cursorAfter = ImGui::GetCursorScreenPos();
        const float headerHeight = headerMax.y - headerMin.y;
        const float controlSize = ImGui::GetFrameHeight();
        float right = headerMax.x - style.FramePadding.x;

        if (!reorderKey.empty()) {
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoHoldToOpenOthers)) {
                ImGui::SetDragDropPayload("INSPECTOR_COMPONENT", reorderKey.c_str(), reorderKey.size() + 1);
                ImGui::TextUnformatted(label);
                ImGui::EndDragDropSource();
            }
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("INSPECTOR_COMPONENT")) {
                    const char* payloadKey = static_cast<const char*>(payload->Data);
                    if (payloadKey && MoveInspectorComponentBefore(obj, payloadKey, reorderKey)) {
                        inspectorOrderChanged = true;
                        projectManager.currentProject.hasUnsavedChanges = true;
                    }
                }
                ImGui::EndDragDropTarget();
            }
        }

        ImGui::PushID(id);
        if (menuFn) {
            const ImVec2 menuPos(right - controlSize, headerMin.y + (headerHeight - controlSize) * 0.5f);
            ImGui::SetCursorScreenPos(menuPos);
            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 1.0f, 1.0f, 0.15f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(1.0f, 1.0f, 1.0f, 0.25f));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
            bool menuClicked = false;
            if (iconActionsMenu != static_cast<ImTextureID>(0)) {
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(1.0f, 1.0f));
                menuClicked = ImGui::ImageButton("##menu", iconActionsMenu, ImVec2(controlSize - 2.0f, controlSize - 2.0f));
                ImGui::PopStyleVar();
            } else {
                menuClicked = ImGui::Button("...", ImVec2(controlSize, controlSize));
            }
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(3);
            if (menuClicked) ImGui::OpenPopup("ComponentMenu");
            if (ImGui::BeginPopup("ComponentMenu")) {
                menuFn();
                ImGui::EndPopup();
            }
            right = menuPos.x - style.ItemSpacing.x;
        }
        if (enabled) {
            const ImVec2 checkPos(right - controlSize, headerMin.y + (headerHeight - controlSize) * 0.5f);
            ImGui::SetCursorScreenPos(checkPos);
            if (ImGui::Checkbox("##Enabled", enabled)) {
                state.enabledChanged = true;
            }
        }
        ImGui::PopID();

        // Draw component icon after the collapse arrow, before the label text.
        // Icon is sized to headerHeight - 6 so it sits with a visible gap before the label.
        // The label is placed by ImGui at ~arrowWidth + FramePadding.x from the left edge,
        // so keeping iconSize smaller than that gap prevents overlap.
        // Okay yeah, i completely forgot to do that when redesigning it lmfao.
        if (iconTex != static_cast<ImTextureID>(0)) {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const float iconSize = headerHeight - 9.0f;
            const float arrowWidth = headerHeight;
            const float iconX = headerMin.x + arrowWidth + 2.0f;
            const float iconY = headerMin.y + (headerHeight - iconSize) * 0.5f;
            dl->AddImage(iconTex,
                ImVec2(iconX, iconY),
                ImVec2(iconX + iconSize, iconY + iconSize),
                ImVec2(0, 0), ImVec2(1, 1),
                IM_COL32(255, 255, 255, 210));
        }

        ImGui::SetCursorScreenPos(cursorAfter);
        return state;
    };

    enum class InspectorClipboardKind {
        None,
        UI,
        Collider,
        PlayerController,
        Rigidbody3D,
        Rigidbody2D,
        Collider2D,
        Parallax2D,
        AudioSource,
        GroundBaked,
        Obstacle,
        AIAgent,
        Animation,
        Skeletal,
        ReverbZone,
        Camera,
        CameraFollow2D,
        PostFX,
        Renderer,
        Light,
        Light2D,
        ShadowCaster2D,
        Script
    };

    struct InspectorUiClipboardData {
        UIElementComponent ui;
        std::string albedoTexturePath;
        MaterialProperties material;
    };

    struct InspectorRendererClipboardData {
        bool faceCamera = false;
        RenderType renderType = RenderType::None;
        std::string meshPath;
        int meshSourceIndex = -1;
        MaterialProperties material;
        std::string materialPath;
        std::string albedoTexturePath;
        std::string overlayTexturePath;
        std::string normalMapPath;
        std::string shaderPackPath;
        std::string vertexShaderPath;
        std::string fragmentShaderPath;
        bool useOverlay = false;
        std::vector<std::string> additionalMaterialPaths;
        UIElementComponent ui;
    };

    struct InspectorClipboard {
        InspectorClipboardKind kind = InspectorClipboardKind::None;
        InspectorUiClipboardData ui;
        ColliderComponent collider;
        PlayerControllerComponent playerController;
        RigidbodyComponent rigidbody;
        Rigidbody2DComponent rigidbody2D;
        Collider2DComponent collider2D;
        ParallaxLayer2DComponent parallax2D;
        AudioSourceComponent audioSource;
        GroundBakedTypeComponent groundBaked;
        ObsticleObjectComponent obstacle;
        AIAgentComponent aiAgent;
        AnimationComponent animation;
        SkeletalAnimationComponent skeletal;
        ReverbZoneComponent reverbZone;
        CameraComponent camera;
        CameraFollow2DComponent cameraFollow2D;
        PostFXSettings postFx;
        InspectorRendererClipboardData renderer;
        LightComponent light;
        Light2DComponent light2D;
        ShadowCaster2DComponent shadowCaster2D;
        ScriptComponent script;
    };

    static InspectorClipboard inspectorClipboard;

    auto markInspectorOrderChanged = [&]() {
        inspectorOrderChanged = true;
        projectManager.currentProject.hasUnsavedChanges = true;
    };

    auto drawClipboardMenus = [&](const char* copyLabel,
                                  const char* pasteNewLabel,
                                  const char* pasteOverrideLabel,
                                  bool canPaste,
                                  const auto& onCopy,
                                  const auto& onPasteNew,
                                  const auto& onPasteOverride) {
        if (ImGui::BeginMenu("Copy")) {
            if (ImGui::MenuItem(copyLabel)) {
                onCopy();
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Paste")) {
            if (!canPaste) ImGui::BeginDisabled();
            if (ImGui::MenuItem(pasteNewLabel)) {
                onPasteNew();
            }
            if (ImGui::MenuItem(pasteOverrideLabel)) {
                onPasteOverride();
            }
            if (!canPaste) ImGui::EndDisabled();
            ImGui::EndMenu();
        }
    };

    auto drawReorderMenuItems = [&](const std::string& key) {
        EnsureInspectorComponentMetadata(obj);
        const auto it = std::find(obj.inspectorComponentOrder.begin(), obj.inspectorComponentOrder.end(), key);
        const bool hasEntry = it != obj.inspectorComponentOrder.end();
        const ptrdiff_t index = hasEntry ? std::distance(obj.inspectorComponentOrder.begin(), it) : -1;
        const ptrdiff_t lastIndex = static_cast<ptrdiff_t>(obj.inspectorComponentOrder.size()) - 1;

        if (ImGui::MenuItem("Find Node Reference in Scene")) {
            setPrimarySelection(obj.id, false);
        }
        if (ImGui::MenuItem("Move Component Up", nullptr, false, hasEntry && index > 0)) {
            if (MoveInspectorComponentByOffset(obj, key, -1)) {
                markInspectorOrderChanged();
            }
        }
        if (ImGui::MenuItem("Move Component Down", nullptr, false, hasEntry && index >= 0 && index < lastIndex)) {
            if (MoveInspectorComponentByOffset(obj, key, 1)) {
                markInspectorOrderChanged();
            }
        }
        if (ImGui::MenuItem("Move Component To Top", nullptr, false, hasEntry && index > 0)) {
            if (MoveInspectorComponentToEdge(obj, key, true)) {
                markInspectorOrderChanged();
            }
        }
        if (ImGui::MenuItem("Move Component To Bottom", nullptr, false, hasEntry && index >= 0 && index < lastIndex)) {
            if (MoveInspectorComponentToEdge(obj, key, false)) {
                markInspectorOrderChanged();
            }
        }
    };

    auto insertInspectorKeyAfter = [&](const std::string& anchorKey, const std::string& newKey) {
        EnsureInspectorComponentMetadata(obj);
        auto anchorIt = std::find(obj.inspectorComponentOrder.begin(), obj.inspectorComponentOrder.end(), anchorKey);
        auto existingIt = std::find(obj.inspectorComponentOrder.begin(), obj.inspectorComponentOrder.end(), newKey);
        if (existingIt != obj.inspectorComponentOrder.end()) {
            obj.inspectorComponentOrder.erase(existingIt);
        }
        if (anchorIt == obj.inspectorComponentOrder.end()) {
            obj.inspectorComponentOrder.push_back(newKey);
        } else {
            obj.inspectorComponentOrder.insert(anchorIt + 1, newKey);
        }
        markInspectorOrderChanged();
    };

    auto drawStandardComponentMenu = [&](const std::string& key,
                                         const char* copyLabel,
                                         const char* pasteNewLabel,
                                         const char* pasteOverrideLabel,
                                         bool canPaste,
                                         const auto& onReset,
                                         const auto& onCopy,
                                         const auto& onPasteNew,
                                         const auto& onPasteOverride,
                                         bool& removeFlag) {
        drawReorderMenuItems(key);
        ImGui::Separator();
        if (ImGui::MenuItem("Reset Component Values")) {
            onReset();
        }
        drawClipboardMenus(copyLabel, pasteNewLabel, pasteOverrideLabel, canPaste, onCopy, onPasteNew, onPasteOverride);
        ImGui::Separator();
        if (ImGui::MenuItem("Remove Component")) {
            removeFlag = true;
        }
    };

    auto reloadRendererMeshAsset = [&](SceneObject& target) {
        target.meshId = -1;
        if (!target.hasRenderer || target.meshPath.empty()) {
            return;
        }

        if (target.renderType == RenderType::OBJMesh) {
            std::string err;
            target.meshId = g_objLoader.loadOBJ(target.meshPath, err);
            return;
        }

        if (target.renderType != RenderType::Model) {
            return;
        }

        ModelSceneData sceneData;
        std::string err;
        if (getModelLoader().loadModelScene(target.meshPath, sceneData, err)) {
            int sourceIndex = target.meshSourceIndex;
            if (sourceIndex < 0 || sourceIndex >= static_cast<int>(sceneData.meshIndices.size())) {
                sourceIndex = 0;
            }
            if (!sceneData.meshIndices.empty() &&
                sourceIndex >= 0 &&
                sourceIndex < static_cast<int>(sceneData.meshIndices.size())) {
                target.meshId = sceneData.meshIndices[static_cast<size_t>(sourceIndex)];
            }
            return;
        }

        ModelLoadResult result = getModelLoader().loadModel(target.meshPath);
        if (result.success) {
            target.meshId = result.meshIndex;
        }
    };

    auto assignRendererMeshAsset = [&](SceneObject& target, const fs::path& sourcePath) {
        if (sourcePath.empty() || !fs::exists(sourcePath)) {
            return false;
        }

        std::error_code ec;
        fs::directory_entry entry(sourcePath, ec);
        if (ec || !fileBrowser.isModelFile(entry)) {
            return false;
        }

        std::string ext = sourcePath.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        target.hasRenderer = true;
        target.meshPath = sourcePath.string();
        target.meshSourceIndex = -1;
        target.renderType = (ext == ".obj") ? RenderType::OBJMesh : RenderType::Model;
        reloadRendererMeshAsset(target);
        UpdateLegacyTypeFromComponents(target);
        return true;
    };

    auto copyRendererClipboard = [&](const SceneObject& source) {
        inspectorClipboard.kind = InspectorClipboardKind::Renderer;
        inspectorClipboard.renderer.faceCamera = source.faceCamera;
        inspectorClipboard.renderer.renderType = source.renderType;
        inspectorClipboard.renderer.meshPath = source.meshPath;
        inspectorClipboard.renderer.meshSourceIndex = source.meshSourceIndex;
        inspectorClipboard.renderer.material = source.material;
        inspectorClipboard.renderer.materialPath = source.materialPath;
        inspectorClipboard.renderer.albedoTexturePath = source.albedoTexturePath;
        inspectorClipboard.renderer.overlayTexturePath = source.overlayTexturePath;
        inspectorClipboard.renderer.normalMapPath = source.normalMapPath;
        inspectorClipboard.renderer.shaderPackPath = source.shaderPackPath;
        inspectorClipboard.renderer.vertexShaderPath = source.vertexShaderPath;
        inspectorClipboard.renderer.fragmentShaderPath = source.fragmentShaderPath;
        inspectorClipboard.renderer.useOverlay = source.useOverlay;
        inspectorClipboard.renderer.additionalMaterialPaths = source.additionalMaterialPaths;
        inspectorClipboard.renderer.ui = source.ui;
    };

    auto applyRendererClipboard = [&](SceneObject& target) {
        target.hasRenderer = true;
        target.faceCamera = inspectorClipboard.renderer.faceCamera;
        target.renderType = inspectorClipboard.renderer.renderType;
        target.meshPath = inspectorClipboard.renderer.meshPath;
        target.meshSourceIndex = inspectorClipboard.renderer.meshSourceIndex;
        target.material = inspectorClipboard.renderer.material;
        target.materialPath = inspectorClipboard.renderer.materialPath;
        target.albedoTexturePath = inspectorClipboard.renderer.albedoTexturePath;
        target.overlayTexturePath = inspectorClipboard.renderer.overlayTexturePath;
        target.normalMapPath = inspectorClipboard.renderer.normalMapPath;
        target.shaderPackPath = inspectorClipboard.renderer.shaderPackPath;
        target.vertexShaderPath = inspectorClipboard.renderer.vertexShaderPath;
        target.fragmentShaderPath = inspectorClipboard.renderer.fragmentShaderPath;
        target.useOverlay = inspectorClipboard.renderer.useOverlay;
        target.additionalMaterialPaths = inspectorClipboard.renderer.additionalMaterialPaths;
        target.ui.spriteSheetEnabled = inspectorClipboard.renderer.ui.spriteSheetEnabled;
        target.ui.spriteSheetColumns = inspectorClipboard.renderer.ui.spriteSheetColumns;
        target.ui.spriteSheetRows = inspectorClipboard.renderer.ui.spriteSheetRows;
        target.ui.spriteSheetFrame = inspectorClipboard.renderer.ui.spriteSheetFrame;
        target.ui.spriteSheetFps = inspectorClipboard.renderer.ui.spriteSheetFps;
        target.ui.spriteSheetLoop = inspectorClipboard.renderer.ui.spriteSheetLoop;
        target.ui.spriteCustomFramesEnabled = inspectorClipboard.renderer.ui.spriteCustomFramesEnabled;
        target.ui.spriteSourceWidth = inspectorClipboard.renderer.ui.spriteSourceWidth;
        target.ui.spriteSourceHeight = inspectorClipboard.renderer.ui.spriteSourceHeight;
        target.ui.spriteCustomFrames = inspectorClipboard.renderer.ui.spriteCustomFrames;
        target.ui.spriteCustomFrameNames = inspectorClipboard.renderer.ui.spriteCustomFrameNames;
        target.ui.spriteCustomFrameScales = inspectorClipboard.renderer.ui.spriteCustomFrameScales;
        target.ui.nineSliceEnabled = inspectorClipboard.renderer.ui.nineSliceEnabled;
        target.ui.nineSliceBorder = inspectorClipboard.renderer.ui.nineSliceBorder;
        target.ui.nineSliceTileEdges = inspectorClipboard.renderer.ui.nineSliceTileEdges;
        target.ui.nineSliceTileCenter = inspectorClipboard.renderer.ui.nineSliceTileCenter;
        reloadRendererMeshAsset(target);
        UpdateLegacyTypeFromComponents(target);
    };

    auto resetRendererComponent = [&](SceneObject& target) {
        const RenderType preservedType = target.renderType;
        const std::string preservedMeshPath = target.meshPath;
        const int preservedMeshSourceIndex = target.meshSourceIndex;
        const int preservedMeshId = target.meshId;
        const UIElementComponent defaultUi;

        target.hasRenderer = true;
        target.faceCamera = false;
        target.renderType = preservedType;
        target.meshPath = preservedMeshPath;
        target.meshSourceIndex = preservedMeshSourceIndex;
        target.meshId = preservedMeshId;
        target.material = MaterialProperties{};
        target.materialPath.clear();
        target.albedoTexturePath.clear();
        target.overlayTexturePath.clear();
        target.normalMapPath.clear();
        target.shaderPackPath.clear();
        target.vertexShaderPath.clear();
        target.fragmentShaderPath.clear();
        target.useOverlay = false;
        target.additionalMaterialPaths.clear();
        target.ui.spriteSheetEnabled = defaultUi.spriteSheetEnabled;
        target.ui.spriteSheetColumns = defaultUi.spriteSheetColumns;
        target.ui.spriteSheetRows = defaultUi.spriteSheetRows;
        target.ui.spriteSheetFrame = defaultUi.spriteSheetFrame;
        target.ui.spriteSheetFps = defaultUi.spriteSheetFps;
        target.ui.spriteSheetLoop = defaultUi.spriteSheetLoop;
        target.ui.spriteCustomFramesEnabled = defaultUi.spriteCustomFramesEnabled;
        target.ui.spriteSourceWidth = defaultUi.spriteSourceWidth;
        target.ui.spriteSourceHeight = defaultUi.spriteSourceHeight;
        target.ui.spriteCustomFrames.clear();
        target.ui.spriteCustomFrameNames.clear();
        target.ui.spriteCustomFrameScales.clear();
        target.ui.nineSliceEnabled = defaultUi.nineSliceEnabled;
        target.ui.nineSliceBorder = defaultUi.nineSliceBorder;
        target.ui.nineSliceTileEdges = defaultUi.nineSliceTileEdges;
        target.ui.nineSliceTileCenter = defaultUi.nineSliceTileCenter;

        if (preservedType == RenderType::Sprite) {
            target.material.ambientStrength = 1.0f;
        } else if (preservedType == RenderType::Mirror) {
            target.useOverlay = true;
            target.material.textureMix = 1.0f;
            target.material.color = glm::vec3(1.0f);
        }

        UpdateLegacyTypeFromComponents(target);
    };

    const bool runtimeSceneEditingLocked = isPlaying || specMode || testMode || playerMode;

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(5.0f, 3.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 4.0f));

    char nameBuffer[128];
    strncpy(nameBuffer, obj.name.c_str(), sizeof(nameBuffer));
    nameBuffer[sizeof(nameBuffer) - 1] = '\0';

    bool nameHovered = false;

    if (ImGui::BeginTable("##ObjectMetaTopTable", 4, ImGuiTableFlags_SizingStretchProp))
    {
        const float objIconSize = ImGui::GetFrameHeight();
        ImGui::TableSetupColumn("IconColumn",      ImGuiTableColumnFlags_WidthFixed, objIconSize);
        ImGui::TableSetupColumn("EnableColumn",    ImGuiTableColumnFlags_WidthFixed, 18.0f);
        ImGui::TableSetupColumn("NameColumn",      ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("InvariableColumn",ImGuiTableColumnFlags_WidthFixed, 110.0f);

        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        if (iconGameObject != static_cast<ImTextureID>(0)) {
            ImGui::Image(iconGameObject, ImVec2(objIconSize, objIconSize));
        } else {
            ImGui::Dummy(ImVec2(objIconSize, objIconSize));
        }

        ImGui::TableSetColumnIndex(1);
        if (ImGui::Checkbox("##Enabled", &obj.enabled))
        {
            objectEnabledChanged = true;
            projectManager.currentProject.hasUnsavedChanges = true;
        }

        ImGui::TableSetColumnIndex(2);
        ImGui::BeginDisabled(runtimeSceneEditingLocked);
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("##Name", nameBuffer, sizeof(nameBuffer)))
        {
            const std::string oldName = obj.name;
            const std::string newName = nameBuffer;
            if (oldName != newName)
            {
                obj.name = newName;
                objectNameChanged = true;
                propagateObjectRenameReferences(oldName, newName, obj.id);
                projectManager.currentProject.hasUnsavedChanges = true;
            }
        }
        nameHovered = ImGui::IsItemHovered();
        ImGui::EndDisabled();

        ImGui::TableSetColumnIndex(3);
        if (ImGui::Checkbox("Invariable", &obj.IsInvariable))
        {
            projectManager.currentProject.hasUnsavedChanges = true;
        }

        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        {
            ImGui::BeginTooltip();
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 24.0f);
            ImGui::TextUnformatted("Invariable objects are locked from physical transform changes and direct scene manipulation. Scripts may still reference them, and material-based properties can still update.");
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }

        ImGui::EndTable();
    }

    if (runtimeSceneEditingLocked && nameHovered)
    {
        ImGui::SetTooltip("Object renaming is disabled while the scene is running.");
    }

    char tagBuf[64] = {};
    std::snprintf(tagBuf, sizeof(tagBuf), "%s", obj.tag.c_str());

    if (ImGui::BeginTable("##ObjectMetaBottomTable", 2, ImGuiTableFlags_SizingStretchProp))
    {
        ImGui::TableSetupColumn("TagColumn", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("LayerColumn", ImGuiTableColumnFlags_WidthStretch, 1.0f);

        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        ImGui::TextDisabled("Tag");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("##Tag", tagBuf, sizeof(tagBuf)))
        {
            obj.tag = tagBuf;
            objectTagChanged = true;
            projectManager.currentProject.hasUnsavedChanges = true;
        }

        ImGui::TableSetColumnIndex(1);
        ImGui::TextDisabled("Layer");
        ImGui::SameLine();
        int layer = obj.layer;
        ImGui::SetNextItemWidth(-1);
        if (ImGui::SliderInt("##Layer", &layer, 0, 31, "%d"))
        {
            obj.layer = layer;
            objectLayerChanged = true;
            projectManager.currentProject.hasUnsavedChanges = true;
        }

        ImGui::EndTable();
        /*{
            const float iconSz = ImGui::GetFrameHeight();
            if (iconTransform != static_cast<ImTextureID>(0)) {
                ImGui::Image(iconTransform, ImVec2(iconSz, iconSz));
                ImGui::SameLine(0.0f, 4.0f);
            }
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("Transform");
            ImGui::Separator();
        }*/

        if (ImGui::BeginTable("##TransformTable", 2, ImGuiTableFlags_SizingStretchProp))
        {
            ImGui::TableSetupColumn("LabelColumn", ImGuiTableColumnFlags_WidthFixed, 62.0f);
            ImGui::TableSetupColumn("ValueColumn", ImGuiTableColumnFlags_WidthStretch, 1.0f);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("Position");

            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-1);
            if (ImGui::DragFloat3("##Position", &obj.position.x, 0.1f))
            {
                syncLocalTransform(obj);
                objectTransformChanged = true;
                projectManager.currentProject.hasUnsavedChanges = true;
            }

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("Rotation");

            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-1);
            if (ImGui::DragFloat3("##Rotation", &obj.rotation.x, 1.0f, -360.0f, 360.0f))
            {
                obj.rotation = NormalizeEulerDegrees(obj.rotation);
                syncLocalTransform(obj);
                objectTransformChanged = true;
                projectManager.currentProject.hasUnsavedChanges = true;
            }

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("Scale");

            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-1);
            if (ImGui::DragFloat3("##Scale", &obj.scale.x, 0.05f, 0.01f, 100.0f))
            {
                syncLocalTransform(obj);
                objectTransformChanged = true;
                projectManager.currentProject.hasUnsavedChanges = true;
            }
            ImGui::EndTable();
        }
    }

    ImGui::PopStyleVar(2);

    // Inspector field layout helpers
    // Label col = 40% of available width, value col = 60% — scales with panel width, no fixed clipping.
    auto beginCompFields = [](const char* id) -> bool {
        if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_NoPadOuterX))
            return false;
        ImGui::TableSetupColumn("##L", ImGuiTableColumnFlags_WidthStretch, 40.0f);
        ImGui::TableSetupColumn("##V", ImGuiTableColumnFlags_WidthStretch, 60.0f);
        return true;
    };
    auto endCompFields = []() { ImGui::EndTable(); };
    auto fieldRow = [](const char* label) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0); ImGui::AlignTextToFramePadding(); ImGui::TextDisabled("%s", label);
        ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-1.0f);
    };
    // Unity-style: label in col 0, checkbox left-aligned in col 1.
    auto boolRow = [](const char* label, bool* val) -> bool {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", label);
        ImGui::TableSetColumnIndex(1);
        ImGui::PushID(static_cast<const void*>(val));
        bool r = ImGui::Checkbox("##chk", val);
        ImGui::PopID();
        return r;
    };

    auto HorizontalBoolRow = [](const char* label,
                                const char* labelA, bool* valA,
                                const char* labelB, bool* valB) -> bool
    {
        bool changed = false;

        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", label);

        ImGui::TableSetColumnIndex(1);

        ImGui::PushID(label);

        if (ImGui::Checkbox(labelA, valA))
            changed = true;

        ImGui::SameLine();

        if (ImGui::Checkbox(labelB, valB))
            changed = true;

        ImGui::PopID();

        return changed;
    };

    // Note/hint text indented under the value column, wraps within its cell width.
    auto noteRow = [](const char* text) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(1);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", text);
        ImGui::PopTextWrapPos();
    };

    const std::vector<std::string> inspectorComponentOrder = obj.inspectorComponentOrder;
    for (const std::string& inspectorComponentKey : inspectorComponentOrder) {
    if (inspectorComponentKey == "ui" && isUIObject(obj) && sharedUIObject) {
        ImGui::Dummy(ImVec2(0.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.25f, 0.45f, 0.65f, 1.0f));
        bool changed = false;
        bool removeUi = false;
        auto header = drawComponentHeader("UI", "UI", "ui", nullptr, true, [&]() {
            drawStandardComponentMenu(
                "ui",
                "Copy Component Values",
                "Paste Component Values as New",
                "Paste Component Values as Value Overrides",
                inspectorClipboard.kind == InspectorClipboardKind::UI,
                [&]() {
                    const UIElementType type = obj.ui.type;
                    obj.ui = UIElementComponent{};
                    obj.ui.type = type;
                    obj.albedoTexturePath.clear();
                    obj.material = MaterialProperties{};
                    changed = true;
                },
                [&]() {
                    inspectorClipboard.kind = InspectorClipboardKind::UI;
                    inspectorClipboard.ui.ui = obj.ui;
                    inspectorClipboard.ui.albedoTexturePath = obj.albedoTexturePath;
                    inspectorClipboard.ui.material = obj.material;
                },
                [&]() {
                    obj.hasUI = true;
                    obj.ui = inspectorClipboard.ui.ui;
                    obj.albedoTexturePath = inspectorClipboard.ui.albedoTexturePath;
                    obj.material = inspectorClipboard.ui.material;
                    changed = true;
                },
                [&]() {
                    obj.ui = inspectorClipboard.ui.ui;
                    obj.albedoTexturePath = inspectorClipboard.ui.albedoTexturePath;
                    obj.material = inspectorClipboard.ui.material;
                    changed = true;
                },
                removeUi);
        });
        if (header.open) {
            ImGui::PushID("UI");

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
                const auto& fontCatalog = getUIFontCatalog();
                std::string currentFontLabel = "Use Editor Style Font";
                const int currentFontIndex = findUIFontCatalogIndex(obj.ui.textFont);
                if (!obj.ui.textFont.empty()) {
                    if (currentFontIndex >= 0) {
                        currentFontLabel = fontCatalog[static_cast<size_t>(currentFontIndex)].label;
                    } else {
                        currentFontLabel = obj.ui.textFont;
                    }
                }
                if (ImGui::BeginCombo("Text Font", currentFontLabel.c_str())) {
                    const bool useEditorFont = obj.ui.textFont.empty();
                    if (ImGui::Selectable("Use Editor Style Font", useEditorFont)) {
                        obj.ui.textFont.clear();
                        changed = true;
                    }
                    if (useEditorFont) ImGui::SetItemDefaultFocus();
                    for (size_t i = 0; i < fontCatalog.size(); ++i) {
                        const bool selected = obj.ui.textFont == fontCatalog[i].id;
                        if (ImGui::Selectable(fontCatalog[i].label.c_str(), selected)) {
                            obj.ui.textFont = fontCatalog[i].id;
                            changed = true;
                        }
                        if (selected) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
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
                        importSpriteSheetTarget = (obj.type == ObjectType::Sprite25D)
                            ? SpriteSheetImportTarget::Sprite25D
                            : ((obj.ui.type == UIElementType::Sprite2D)
                                ? SpriteSheetImportTarget::Sprite2D
                                : SpriteSheetImportTarget::UIImage);
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
                    ImGui::SameLine();
                    if (ImGui::Checkbox("Force Unlit", &obj.ui.unlitLighting2D)) {
                        changed = true;
                    }
                    if (ImGui::SliderFloat("Emissive", &obj.ui.emissiveLighting2D, 0.0f, 8.0f, "%.2f")) {
                        changed = true;
                    }
                    const auto routingIt = light2DObjectRoutingReasonsLastFrame.find(obj.id);
                    /*if (routingIt != light2DObjectRoutingReasonsLastFrame.end()) {
                        ImGui::SeparatorText("Runtime Debug");
                        ImGui::Text("Compositor Ran: %s", light2DCompositorRanLastFrame ? "Yes" : "No");
                        ImGui::Text("Light Buffer: %s", light2DLightBufferHadContentLastFrame ? "Non-empty" : "Empty");
                        ImGui::Text("Active Lights: %d", light2DActiveCountLastFrame);
                        ImGui::Text("Lit Sprite2D: %d", light2DLitSprite2DCountLastFrame);
                        ImGui::Text("Lit UI Images: %d", light2DLitWorldImageCountLastFrame);
                        ImGui::TextWrapped("%s", routingIt->second.c_str());
                    }*/
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

    if (inspectorComponentKey == "collider" && obj.hasCollider && sharedCollider) {
        ImGui::Dummy(ImVec2(0.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.35f, 0.5f, 0.35f, 1.0f));
        bool removeCollider = false;
        bool changed = false;
        auto header = drawComponentHeader("Collider", "Collider", "collider", &obj.collider.enabled, true, [&]() {
            drawStandardComponentMenu(
                "collider",
                "Copy Component Values",
                "Paste Component Values as New",
                "Paste Component Values as Value Overrides",
                inspectorClipboard.kind == InspectorClipboardKind::Collider,
                [&]() { obj.collider = ColliderComponent{}; changed = true; },
                [&]() { inspectorClipboard.kind = InspectorClipboardKind::Collider; inspectorClipboard.collider = obj.collider; },
                [&]() { obj.hasCollider = true; obj.collider = inspectorClipboard.collider; changed = true; },
                [&]() { obj.collider = inspectorClipboard.collider; changed = true; },
                removeCollider);
        });
        if (header.enabledChanged) {
            changed = true;
        }
        if (header.open) {
            ImGui::PushID("Collider");

            if (beginCompFields("##Fields_Collider")) {
                const char* colliderTypes[] = { "Box", "Mesh", "Convex Mesh", "Capsule" };
                int colliderType = static_cast<int>(obj.collider.type);
                fieldRow("Type");
                if (ImGui::Combo("##Type", &colliderType, colliderTypes, IM_ARRAYSIZE(colliderTypes))) {
                    obj.collider.type = static_cast<ColliderType>(colliderType);
                    if (obj.collider.type == ColliderType::Mesh) {
                        obj.collider.convex = false;
                    } else if (obj.collider.type == ColliderType::ConvexMesh) {
                        obj.collider.convex = true;
                    }
                    changed = true;
                }

                if (obj.collider.type == ColliderType::Box) {
                    fieldRow("Box Size");
                    if (ImGui::DragFloat3("##BoxSize", &obj.collider.boxSize.x, 0.01f, 0.01f, 1000.0f, "%.3f")) {
                        obj.collider.boxSize.x = std::max(0.01f, obj.collider.boxSize.x);
                        obj.collider.boxSize.y = std::max(0.01f, obj.collider.boxSize.y);
                        obj.collider.boxSize.z = std::max(0.01f, obj.collider.boxSize.z);
                        changed = true;
                    }
                    ImGui::TableNextRow(); ImGui::TableSetColumnIndex(1);
                    if (ImGui::SmallButton("Match Object Scale")) {
                        obj.collider.boxSize = glm::max(obj.scale, glm::vec3(0.01f));
                        changed = true;
                    }
                } else if (obj.collider.type == ColliderType::Capsule) {
                    float radius = std::max(0.05f, std::max(obj.collider.boxSize.x, obj.collider.boxSize.z) * 0.5f);
                    float height = std::max(0.1f, obj.collider.boxSize.y);
                    fieldRow("Radius");
                    if (ImGui::DragFloat("##Radius", &radius, 0.01f, 0.05f, 5.0f, "%.3f")) {
                        obj.collider.boxSize.x = obj.collider.boxSize.z = radius * 2.0f;
                        changed = true;
                    }
                    fieldRow("Height");
                    if (ImGui::DragFloat("##Height", &height, 0.01f, 0.1f, 10.0f, "%.3f")) {
                        obj.collider.boxSize.y = height;
                        changed = true;
                    }
                    noteRow("Capsule aligned to Y axis.");
                }

                fieldRow("Offset");
                if (ImGui::DragFloat3("##Offset", &obj.collider.offset.x, 0.01f, -1000.0f, 1000.0f, "%.3f")) {
                    changed = true;
                }

                endCompFields();
            }

            ImGui::SeparatorText("Surface");
            if (beginCompFields("##Fields_ColliderSurface")) {
                fieldRow("Static Friction");
                if (ImGui::DragFloat("##StaticFriction", &obj.collider.staticFriction, 0.01f, 0.0f, 4.0f, "%.2f")) {
                    obj.collider.staticFriction = std::clamp(obj.collider.staticFriction, 0.0f, 4.0f);
                    changed = true;
                }
                fieldRow("Dyn Friction");
                if (ImGui::DragFloat("##DynFriction", &obj.collider.dynamicFriction, 0.01f, 0.0f, 4.0f, "%.2f")) {
                    obj.collider.dynamicFriction = std::clamp(obj.collider.dynamicFriction, 0.0f, 4.0f);
                    changed = true;
                }
                fieldRow("Restitution");
                if (ImGui::DragFloat("##Restitution", &obj.collider.restitution, 0.01f, 0.0f, 1.0f, "%.2f")) {
                    obj.collider.restitution = std::clamp(obj.collider.restitution, 0.0f, 1.0f);
                    changed = true;
                }
                endCompFields();
            }

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

    if (inspectorComponentKey == "player_controller" && obj.hasPlayerController && sharedPlayerController) {
        ImGui::Dummy(ImVec2(0.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.35f, 0.45f, 0.7f, 1.0f));
        bool removePlayerController = false;
        bool changed = false;
        auto header = drawComponentHeader("Player Controller", "PlayerController", "player_controller", &obj.playerController.enabled, true, [&]() {
            drawStandardComponentMenu(
                "player_controller",
                "Copy Component Values",
                "Paste Component Values as New",
                "Paste Component Values as Value Overrides",
                inspectorClipboard.kind == InspectorClipboardKind::PlayerController,
                [&]() { obj.playerController = PlayerControllerComponent{}; changed = true; },
                [&]() { inspectorClipboard.kind = InspectorClipboardKind::PlayerController; inspectorClipboard.playerController = obj.playerController; },
                [&]() { obj.hasPlayerController = true; obj.playerController = inspectorClipboard.playerController; changed = true; },
                [&]() { obj.playerController = inspectorClipboard.playerController; changed = true; },
                removePlayerController);
        });
        if (header.enabledChanged) {
            changed = true;
        }
        if (header.open) {
            ImGui::PushID("PlayerController");
            if (beginCompFields("##Fields_PlayerController")) {
                fieldRow("Move Speed");
                if (ImGui::DragFloat("##MoveSpeed", &obj.playerController.moveSpeed, 0.1f, 0.1f, 100.0f, "%.2f")) {
                    obj.playerController.moveSpeed = std::max(0.1f, obj.playerController.moveSpeed);
                    obj.playerController.runSpeed = std::max(obj.playerController.moveSpeed, obj.playerController.runSpeed);
                    changed = true;
                }
                fieldRow("Run Speed");
                if (ImGui::DragFloat("##RunSpeed", &obj.playerController.runSpeed, 0.1f, 0.1f, 140.0f, "%.2f")) {
                    obj.playerController.runSpeed = std::max(obj.playerController.moveSpeed, obj.playerController.runSpeed);
                    changed = true;
                }
                fieldRow("Look Sens.");
                if (ImGui::DragFloat("##LookSensitivity", &obj.playerController.lookSensitivity, 0.01f, 0.01f, 2.0f, "%.2f")) {
                    obj.playerController.lookSensitivity = std::clamp(obj.playerController.lookSensitivity, 0.01f, 2.0f);
                    changed = true;
                }
                fieldRow("Ground Accel");
                if (ImGui::DragFloat("##GroundAccel", &obj.playerController.groundAcceleration, 0.1f, 0.0f, 200.0f, "%.2f")) {
                    obj.playerController.groundAcceleration = std::clamp(obj.playerController.groundAcceleration, 0.0f, 200.0f);
                    changed = true;
                }
                fieldRow("Air Accel");
                if (ImGui::DragFloat("##AirAccel", &obj.playerController.airAcceleration, 0.1f, 0.0f, 200.0f, "%.2f")) {
                    obj.playerController.airAcceleration = std::clamp(obj.playerController.airAcceleration, 0.0f, 200.0f);
                    changed = true;
                }
                fieldRow("Braking");
                if (ImGui::DragFloat("##Braking", &obj.playerController.braking, 0.1f, 0.0f, 200.0f, "%.2f")) {
                    obj.playerController.braking = std::clamp(obj.playerController.braking, 0.0f, 200.0f);
                    changed = true;
                }
                fieldRow("Min Surf Ctrl");
                if (ImGui::DragFloat("##MinSurfaceControl", &obj.playerController.minSurfaceControl, 0.01f, 0.0f, 1.0f, "%.2f")) {
                    obj.playerController.minSurfaceControl = std::clamp(obj.playerController.minSurfaceControl, 0.0f, 1.0f);
                    changed = true;
                }
                fieldRow("Slide Gravity");
                if (ImGui::DragFloat("##SlideGravity", &obj.playerController.slideGravity, 0.1f, 0.0f, 120.0f, "%.2f")) {
                    obj.playerController.slideGravity = std::clamp(obj.playerController.slideGravity, 0.0f, 120.0f);
                    changed = true;
                }
                fieldRow("Platform Carry");
                if (ImGui::DragFloat("##PlatformCarry", &obj.playerController.platformCarry, 0.01f, 0.0f, 3.0f, "%.2f")) {
                    obj.playerController.platformCarry = std::clamp(obj.playerController.platformCarry, 0.0f, 3.0f);
                    changed = true;
                }
                fieldRow("Height");
                if (ImGui::DragFloat("##Height", &obj.playerController.height, 0.01f, 0.5f, 3.0f, "%.2f")) {
                    obj.playerController.height = std::clamp(obj.playerController.height, 0.5f, 3.0f);
                    obj.scale.y = obj.playerController.height;
                    obj.collider.boxSize.y = obj.playerController.height;
                    changed = true;
                }
                fieldRow("Radius");
                if (ImGui::DragFloat("##Radius", &obj.playerController.radius, 0.01f, 0.2f, 1.2f, "%.2f")) {
                    obj.playerController.radius = std::clamp(obj.playerController.radius, 0.2f, 1.2f);
                    obj.scale.x = obj.scale.z = obj.playerController.radius * 2.0f;
                    obj.collider.boxSize.x = obj.collider.boxSize.z = obj.playerController.radius * 2.0f;
                    changed = true;
                }
                fieldRow("Jump Strength");
                if (ImGui::DragFloat("##JumpStrength", &obj.playerController.jumpStrength, 0.1f, 0.1f, 30.0f, "%.1f")) {
                    obj.playerController.jumpStrength = std::max(0.1f, obj.playerController.jumpStrength);
                    changed = true;
                }
                endCompFields();
            }
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
                obj.rigidbody.lockRotationX = true;
                obj.rigidbody.lockRotationY = false;
                obj.rigidbody.lockRotationZ = true;
            }
            playerControllerSectionChanged = true;
            projectManager.currentProject.hasUnsavedChanges = true;
        }
        ImGui::PopStyleColor();
    }

    if (inspectorComponentKey == "rigidbody3d" && obj.hasRigidbody && sharedRigidbody) {
        ImGui::Dummy(ImVec2(0.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.45f, 0.45f, 0.25f, 1.0f));
        bool removeRigidbody = false;
        bool changed = false;
        auto header = drawComponentHeader("Rigidbody3D", "Rigidbody3D", "rigidbody3d", &obj.rigidbody.enabled, true, [&]() {
            drawStandardComponentMenu(
                "rigidbody3d",
                "Copy Component Values",
                "Paste Component Values as New",
                "Paste Component Values as Value Overrides",
                inspectorClipboard.kind == InspectorClipboardKind::Rigidbody3D,
                [&]() { obj.rigidbody = RigidbodyComponent{}; changed = true; },
                [&]() { inspectorClipboard.kind = InspectorClipboardKind::Rigidbody3D; inspectorClipboard.rigidbody = obj.rigidbody; },
                [&]() { obj.hasRigidbody = true; obj.rigidbody = inspectorClipboard.rigidbody; changed = true; },
                [&]() { obj.rigidbody = inspectorClipboard.rigidbody; changed = true; },
                removeRigidbody);
        });
        if (header.enabledChanged) {
            changed = true;
        }
        if (header.open) {
            ImGui::PushID("Rigidbody3D");
            if (beginCompFields("##Fields_Rigidbody3D")) {
                noteRow("Collider required for physics.");
                if (UsesUIOnly2DPhysics(obj)) {
                    noteRow("Rigidbody3D is for 3D objects (use Rigidbody2D for UI/canvas).");
                }
                const float massUnitScale = std::max(0.000001f,
                    ProjectMassUnitToKilograms(projectManager.currentProject.physicsSettings.massUnit));
                const float minDisplayMass = std::max(0.0001f, 0.01f / massUnitScale);
                const std::string massLabel = std::string("Mass (") +
                    ProjectMassUnitSuffix(projectManager.currentProject.physicsSettings.massUnit) + ")";
                fieldRow(massLabel.c_str());
                if (ImGui::DragFloat("##Mass", &obj.rigidbody.mass, minDisplayMass * 0.05f, minDisplayMass, 1000000.0f, "%.3f")) {
                    obj.rigidbody.mass = std::max(minDisplayMass, obj.rigidbody.mass);
                    changed = true;
                }
                if (boolRow("Custom Center Of Mass", &obj.rigidbody.useCustomCenterOfMass)) { changed = true; }
                if (obj.rigidbody.useCustomCenterOfMass) {
                    fieldRow("Center Of Mass");
                    if (ImGui::DragFloat3("##CenterOfMass", &obj.rigidbody.centerOfMass.x, 0.01f, -1000.0f, 1000.0f, "%.3f")) {
                        changed = true;
                    }
                }
                if (boolRow("Use Gravity", &obj.rigidbody.useGravity)) { changed = true; }
                if (boolRow("Kinematic", &obj.rigidbody.isKinematic)) { changed = true; }
                fieldRow("Linear Damp");
                if (ImGui::DragFloat("##LinearDamping", &obj.rigidbody.linearDamping, 0.01f, 0.0f, 10.0f)) {
                    obj.rigidbody.linearDamping = std::clamp(obj.rigidbody.linearDamping, 0.0f, 10.0f);
                    changed = true;
                }
                fieldRow("Angular Damp");
                if (ImGui::DragFloat("##AngularDamping", &obj.rigidbody.angularDamping, 0.01f, 0.0f, 10.0f)) {
                    obj.rigidbody.angularDamping = std::clamp(obj.rigidbody.angularDamping, 0.0f, 10.0f);
                    changed = true;
                }
                endCompFields();
            }
            if (ImGui::CollapsingHeader("Rotation Constraints", ImGuiTreeNodeFlags_DefaultOpen)) {
                if (beginCompFields("##Fields_Rb3DConstraints")) {
                    if (boolRow("Lock X", &obj.rigidbody.lockRotationX)) { changed = true; }
                    if (boolRow("Lock Y", &obj.rigidbody.lockRotationY)) { changed = true; }
                    if (boolRow("Lock Z", &obj.rigidbody.lockRotationZ)) { changed = true; }
                    endCompFields();
                }
            }
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

    if (inspectorComponentKey == "rigidbody2d" && obj.hasRigidbody2D && sharedRigidbody2D) {
        ImGui::Dummy(ImVec2(0.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.35f, 0.55f, 0.45f, 1.0f));
        bool removeRigidbody2D = false;
        bool changed = false;
        auto header = drawComponentHeader("Rigidbody2D", "Rigidbody2D", "rigidbody2d", &obj.rigidbody2D.enabled, true, [&]() {
            drawStandardComponentMenu(
                "rigidbody2d",
                "Copy Component Values",
                "Paste Component Values as New",
                "Paste Component Values as Value Overrides",
                inspectorClipboard.kind == InspectorClipboardKind::Rigidbody2D,
                [&]() { obj.rigidbody2D = Rigidbody2DComponent{}; changed = true; },
                [&]() { inspectorClipboard.kind = InspectorClipboardKind::Rigidbody2D; inspectorClipboard.rigidbody2D = obj.rigidbody2D; },
                [&]() { obj.hasRigidbody2D = true; obj.rigidbody2D = inspectorClipboard.rigidbody2D; changed = true; },
                [&]() { obj.rigidbody2D = inspectorClipboard.rigidbody2D; changed = true; },
                removeRigidbody2D);
        });
        if (header.enabledChanged) {
            changed = true;
        }
        if (header.open) {
            ImGui::PushID("Rigidbody2D");
            if (beginCompFields("##Fields_Rigidbody2D")) {
                if (!UsesUIOnly2DPhysics(obj)) {
                    noteRow("Rigidbody2D is for UI/canvas objects only.");
                }
                if (boolRow("Use Gravity", &obj.rigidbody2D.useGravity)) { changed = true; }
                fieldRow("Gravity Scale");
                if (ImGui::DragFloat("##GravityScale", &obj.rigidbody2D.gravityScale, 0.05f, 0.0f, 10.0f, "%.2f")) {
                    obj.rigidbody2D.gravityScale = std::max(0.0f, obj.rigidbody2D.gravityScale);
                    changed = true;
                }
                fieldRow("Linear Damp");
                if (ImGui::DragFloat("##LinearDamping", &obj.rigidbody2D.linearDamping, 0.01f, 0.0f, 10.0f)) {
                    obj.rigidbody2D.linearDamping = std::clamp(obj.rigidbody2D.linearDamping, 0.0f, 10.0f);
                    changed = true;
                }
                fieldRow("Velocity");
                if (ImGui::DragFloat2("##Velocity", &obj.rigidbody2D.velocity.x, 0.1f)) {
                    changed = true;
                }
                endCompFields();
            }
            if (ImGui::CollapsingHeader("Rotation Constraints", ImGuiTreeNodeFlags_DefaultOpen)) {
                if (beginCompFields("##Fields_Rb2DConstraints")) {
                    noteRow("Locks the object's Z-axis rotation.");
                    if (boolRow("Lock Rotation", &obj.rigidbody2D.lockRotation)) { changed = true; }
                    endCompFields();
                }
            }
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

    if (inspectorComponentKey == "collider2d" && obj.hasCollider2D && sharedCollider2D) {
        ImGui::Dummy(ImVec2(0.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.35f, 0.5f, 0.65f, 1.0f));
        bool removeCollider2D = false;
        bool changed = false;
        auto header = drawComponentHeader("Collider2D", "Collider2D", "collider2d", &obj.collider2D.enabled, true, [&]() {
            drawStandardComponentMenu(
                "collider2d",
                "Copy Component Values",
                "Paste Component Values as New",
                "Paste Component Values as Value Overrides",
                inspectorClipboard.kind == InspectorClipboardKind::Collider2D,
                [&]() { obj.collider2D = Collider2DComponent{}; changed = true; },
                [&]() { inspectorClipboard.kind = InspectorClipboardKind::Collider2D; inspectorClipboard.collider2D = obj.collider2D; },
                [&]() { obj.hasCollider2D = true; obj.collider2D = inspectorClipboard.collider2D; changed = true; },
                [&]() { obj.collider2D = inspectorClipboard.collider2D; changed = true; },
                removeCollider2D);
        });
        if (header.enabledChanged) {
            changed = true;
        }
        if (header.open) {
            ImGui::PushID("Collider2D");

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

            if (beginCompFields("##Fields_Collider2D")) {
                if (!UsesUIOnly2DPhysics(obj)) {
                    noteRow("Collider2D is for UI/canvas objects only.");
                }
                const char* colliderTypes[] = { "Box", "Polygon", "Edge" };
                int colliderType = static_cast<int>(obj.collider2D.type);
                fieldRow("Type");
                if (ImGui::Combo("##Type", &colliderType, colliderTypes, IM_ARRAYSIZE(colliderTypes))) {
                    obj.collider2D.type = static_cast<Collider2DType>(colliderType);
                    if (obj.collider2D.type == Collider2DType::Polygon) {
                        obj.collider2D.closed = true;
                    } else if (obj.collider2D.type == Collider2DType::Edge) {
                        obj.collider2D.closed = false;
                    }
                    changed = true;
                }

                if (obj.collider2D.type == Collider2DType::Box) {
                    fieldRow("Box Size");
                    if (ImGui::DragFloat2("##BoxSize", &obj.collider2D.boxSize.x, 0.1f, 0.01f, 10000.0f, "%.2f")) {
                        obj.collider2D.boxSize.x = std::max(0.01f, obj.collider2D.boxSize.x);
                        obj.collider2D.boxSize.y = std::max(0.01f, obj.collider2D.boxSize.y);
                        changed = true;
                    }
                    ImGui::TableNextRow(); ImGui::TableSetColumnIndex(1);
                    if (ImGui::SmallButton("Match UI Size")) {
                        obj.collider2D.boxSize = glm::max(obj.ui.size, glm::vec2(1.0f));
                        changed = true;
                    }
                    fieldRow("Offset");
                    if (ImGui::DragFloat2("##Offset", &obj.collider2D.offset.x, 0.1f, -10000.0f, 10000.0f, "%.2f")) {
                        changed = true;
                    }
                    endCompFields();
                } else if (obj.collider2D.type == Collider2DType::Polygon) {
                    ensureHexagon(obj.collider2D, glm::max(obj.ui.size, glm::vec2(1.0f)));
                    noteRow("Points (local space)");
                    endCompFields();
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
                    if (beginCompFields("##Fields_Collider2DOffsetPoly")) {
                        fieldRow("Offset");
                        if (ImGui::DragFloat2("##Offset", &obj.collider2D.offset.x, 0.1f, -10000.0f, 10000.0f, "%.2f")) {
                            changed = true;
                        }
                        endCompFields();
                    }
                } else if (obj.collider2D.type == Collider2DType::Edge) {
                    ensureEdge(obj.collider2D, glm::max(obj.ui.size, glm::vec2(1.0f)));
                    if (boolRow("Closed Loop", &obj.collider2D.closed)) { changed = true; }
                    fieldRow("Thickness");
                    if (ImGui::DragFloat("##Thickness", &obj.collider2D.edgeThickness, 0.01f, 0.01f, 10.0f, "%.2f")) {
                        obj.collider2D.edgeThickness = std::max(0.01f, obj.collider2D.edgeThickness);
                        changed = true;
                    }
                    noteRow("Points (local space)");
                    endCompFields();
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
                    if (beginCompFields("##Fields_Collider2DOffsetEdge")) {
                        fieldRow("Offset");
                        if (ImGui::DragFloat2("##Offset", &obj.collider2D.offset.x, 0.1f, -10000.0f, 10000.0f, "%.2f")) {
                            changed = true;
                        }
                        endCompFields();
                    }
                }
            }
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

    if (inspectorComponentKey == "parallax2d" && obj.hasParallaxLayer2D && sharedParallax2D) {
        ImGui::Dummy(ImVec2(0.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.28f, 0.45f, 0.6f, 1.0f));
        bool removeParallax = false;
        bool changed = false;
        auto header = drawComponentHeader("Parallax Layer 2D", "ParallaxLayer2D", "parallax2d", &obj.parallaxLayer2D.enabled, true, [&]() {
            drawStandardComponentMenu(
                "parallax2d",
                "Copy Component Values",
                "Paste Component Values as New",
                "Paste Component Values as Value Overrides",
                inspectorClipboard.kind == InspectorClipboardKind::Parallax2D,
                [&]() { obj.parallaxLayer2D = ParallaxLayer2DComponent{}; changed = true; },
                [&]() { inspectorClipboard.kind = InspectorClipboardKind::Parallax2D; inspectorClipboard.parallax2D = obj.parallaxLayer2D; },
                [&]() { obj.hasParallaxLayer2D = true; obj.parallaxLayer2D = inspectorClipboard.parallax2D; changed = true; },
                [&]() { obj.parallaxLayer2D = inspectorClipboard.parallax2D; changed = true; },
                removeParallax);
        });
        if (header.enabledChanged) {
            changed = true;
        }
        if (header.open) {
            ImGui::PushID("ParallaxLayer2D");
            if (beginCompFields("##Fields_Parallax2D")) {
                if (!isUIObject(obj)) {
                    noteRow("Parallax layers are for UI world objects.");
                }
                fieldRow("Order");
                if (ImGui::DragInt("##Order", &obj.parallaxLayer2D.order, 1.0f)) {
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
                ImGui::TableNextRow(); ImGui::TableSetColumnIndex(1);
                ImGui::TextDisabled("Layer stack: %d behind, %d in front", lowerCount, higherCount);
                fieldRow("Parallax Factor");
                if (ImGui::DragFloat("##ParallaxFactor", &obj.parallaxLayer2D.factor, 0.01f, 0.0f, 1.0f, "%.2f")) {
                    obj.parallaxLayer2D.factor = std::clamp(obj.parallaxLayer2D.factor, 0.0f, 1.0f);
                    changed = true;
                }
                if (HorizontalBoolRow("Repeat", "X", &obj.parallaxLayer2D.repeatX, "Y", &obj.parallaxLayer2D.repeatY))
                {
                    changed = true;
                }
                if (boolRow("Disable Culling", &obj.parallaxLayer2D.disableCulling)) { changed = true; }

                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                {
                    ImGui::BeginTooltip();
                    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 24.0f);
                    ImGui::TextUnformatted("Prevents this parallax object from being culled when it moves outside the visible world overlay.");
                    ImGui::PopTextWrapPos();
                    ImGui::EndTooltip();
                }
                fieldRow("Repeat Spacing");
                if (ImGui::DragFloat2("##RepeatSpacing", &obj.parallaxLayer2D.repeatSpacing.x, 0.1f, 0.0f, 10000.0f, "%.1f")) {
                    obj.parallaxLayer2D.repeatSpacing.x = std::max(0.0f, obj.parallaxLayer2D.repeatSpacing.x);
                    obj.parallaxLayer2D.repeatSpacing.y = std::max(0.0f, obj.parallaxLayer2D.repeatSpacing.y);
                    changed = true;
                }
                endCompFields();
            }
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

    if (inspectorComponentKey == "audio_source" && obj.hasAudioSource && sharedAudioSource) {
        ImGui::Dummy(ImVec2(0.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.55f, 0.4f, 0.3f, 1.0f));
        bool removeAudioSource = false;
        bool changed = false;
        auto header = drawComponentHeader("Audio Source", "AudioSource", "audio_source", &obj.audioSource.enabled, true, [&]() {
            if (ImGui::MenuItem("Reset Component Values")) {
                obj.audioSource = AudioSourceComponent{};
                changed = true;
            }
            if (ImGui::MenuItem("Add Local Reverb Zone Component", nullptr, false, !obj.hasReverbZone)) {
                obj.hasReverbZone = true;
                obj.reverbZone = ReverbZoneComponent{};
                changed = true;
            }
            drawClipboardMenus(
                "Copy Audio Source Values",
                "Paste Audio Source Values as New",
                "Paste Audio Source Values as Value Overrides",
                inspectorClipboard.kind == InspectorClipboardKind::AudioSource,
                [&]() {
                    inspectorClipboard.kind = InspectorClipboardKind::AudioSource;
                    inspectorClipboard.audioSource = obj.audioSource;
                },
                [&]() {
                    obj.hasAudioSource = true;
                    obj.audioSource = inspectorClipboard.audioSource;
                    changed = true;
                },
                [&]() {
                    obj.audioSource = inspectorClipboard.audioSource;
                    changed = true;
                });
            ImGui::Separator();
            if (ImGui::MenuItem("Remove Component")) {
                removeAudioSource = true;
            }
        });
        if (header.enabledChanged) {
            changed = true;
        }
        if (header.open) {
            ImGui::PushID("AudioSource");
            auto& src = obj.audioSource;
            const std::string previousClipPath = src.clipPath;

            if (drawFileReferenceSlot("Sound Clip", "##AudioClipSlot", src.clipPath, FileCategory::Audio, "None (Sound Clip)")) {
                if (previousClipPath != src.clipPath && audio.isPreviewing(previousClipPath)) {
                    stopClipPreview();
                }
                changed = true;
            }

            if (!src.clipPath.empty()) {
                drawTrimmedPathText(src.clipPath, ImVec4(0.78f, 0.88f, 1.0f, 1.0f));
            }

            const bool usePlanar2DAudio = isProject2DPipeline() || HasUIComponent(obj);
            const char* spatialBlendLabel = usePlanar2DAudio ? "Localization" : "Spatial Blend";
            const char* minDistanceLabel = usePlanar2DAudio ? "Near Distance" : "Min Distance";
            const char* maxDistanceLabel = usePlanar2DAudio ? "Far Distance" : "Max Distance";

            if (beginCompFields("##Fields_AudioSource")) {
                fieldRow("Volume");
                if (ImGui::SliderFloat("##Volume", &src.volume, 0.0f, 1.5f, "%.2f")) {
                    changed = true;
                    syncClipPreviewVolume(AudioPreviewContext::AudioSourceComponent, src.volume);
                }
                if (boolRow("Loop", &src.loop)) { changed = true; }
                if (boolRow("Play On Start", &src.playOnStart)) { changed = true; }
                fieldRow(spatialBlendLabel);
                if (ImGui::SliderFloat("##SpatialBlend", &src.spatialBlend, 0.0f, 1.0f, "%.2f")) {
                    src.spatialBlend = std::clamp(src.spatialBlend, 0.0f, 1.0f);
                    src.spatial = src.spatialBlend > 0.001f;
                    changed = true;
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                {
                    ImGui::BeginTooltip();
                    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 24.0f);
                    ImGui::TextUnformatted(usePlanar2DAudio
                    ? "0 keeps audio global and centered. 1 fully uses world position for pan and falloff."
                    : "0 keeps audio centered. 1 uses full world placement.");
                    ImGui::PopTextWrapPos();
                    ImGui::EndTooltip();
                }
                ImGui::TableNextRow(); ImGui::TableSetColumnIndex(1);
                ImGui::BeginDisabled(src.spatialBlend <= 0.001f);
                fieldRow(minDistanceLabel);
                if (ImGui::DragFloat("##MinDist", &src.minDistance, 0.1f, 0.1f, 200.0f, "%.2f")) {
                    src.minDistance = std::max(0.1f, src.minDistance);
                    changed = true;
                }
                fieldRow(maxDistanceLabel);
                if (ImGui::DragFloat("##MaxDist", &src.maxDistance, 0.1f, src.minDistance + 0.5f, 500.0f, "%.2f")) {
                    src.maxDistance = std::max(src.maxDistance, src.minDistance + 0.5f);
                    changed = true;
                }
                if (!usePlanar2DAudio) {
                    const char* rolloffModes[] = { "Logarithmic", "Linear", "Exponential", "Custom" };
                    int rolloffIndex = static_cast<int>(src.rolloffMode);
                    fieldRow("Rolloff Mode");
                    if (ImGui::Combo("##RolloffMode", &rolloffIndex, rolloffModes, IM_ARRAYSIZE(rolloffModes))) {
                        src.rolloffMode = static_cast<AudioRolloffMode>(rolloffIndex);
                        changed = true;
                    }
                    if (src.rolloffMode != AudioRolloffMode::Custom) {
                        fieldRow("Rolloff Factor");
                        if (ImGui::SliderFloat("##RolloffFactor", &src.rolloff, 0.1f, 4.0f, "%.2f")) {
                            src.rolloff = std::max(0.1f, src.rolloff);
                            changed = true;
                        }
                    } else {
                        fieldRow("Mid Distance");
                        if (ImGui::SliderFloat("##MidDist", &src.customMidDistance, 0.0f, 1.0f, "%.2f")) {
                            src.customMidDistance = std::clamp(src.customMidDistance, 0.0f, 1.0f);
                            changed = true;
                        }
                        fieldRow("Mid Gain");
                        if (ImGui::SliderFloat("##MidGain", &src.customMidGain, 0.0f, 1.0f, "%.2f")) {
                            src.customMidGain = std::clamp(src.customMidGain, 0.0f, 1.0f);
                            changed = true;
                        }
                        fieldRow("End Gain");
                        if (ImGui::SliderFloat("##EndGain", &src.customEndGain, 0.0f, 1.0f, "%.2f")) {
                            src.customEndGain = std::clamp(src.customEndGain, 0.0f, 1.0f);
                            changed = true;
                        }
                    }
                } else {
                    noteRow("2D projects use planar left/right placement with the blend slider.");
                }
                ImGui::EndDisabled();
                endCompFields();
            }

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
                    stopClipPreview();
                } else {
                    beginClipPreview(src.clipPath, src.volume, src.loop, AudioPreviewContext::AudioSourceComponent);
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

            drawAudioPreviewVolumeControl("##AudioSourcePreviewVolume", AudioPreviewContext::AudioSourceComponent, src.volume);

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

            drawAudioTimeReadout(cur, dur);

            ImGui::PopID();
        }
        if (removeAudioSource) {
            if (audio.isPreviewing(obj.audioSource.clipPath)) {
                stopClipPreview();
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

    if (inspectorComponentKey == "video_player" && obj.hasVideoPlayer && sharedVideoPlayer) {
        ImGui::Dummy(ImVec2(0.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.34f, 0.38f, 0.60f, 1.0f));
        bool removeVideoPlayer = false;
        bool changed = false;
        auto header = drawComponentHeader("Video Player", "VideoPlayer", "video_player", &obj.videoPlayer.enabled, true, [&]() {
            if (ImGui::MenuItem("Reset Component Values")) {
                obj.videoPlayer = VideoPlayerComponent{};
                changed = true;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Remove Component")) {
                removeVideoPlayer = true;
            }
        });
        if (header.enabledChanged) {
            changed = true;
        }
        if (header.open) {
            ImGui::PushID("VideoPlayer");
            auto& player = obj.videoPlayer;

            if (beginCompFields("##Fields_VideoPlayer")) {
                fieldRow("Video Path");
                char pathBuf[512] = {};
                std::snprintf(pathBuf, sizeof(pathBuf), "%s", player.videoPath.c_str());
                if (ImGui::InputText("##VideoPath", pathBuf, sizeof(pathBuf))) {
                    player.videoPath = pathBuf;
                    changed = true;
                }
                if (boolRow("Play On Awake", &player.playOnAwake)) { changed = true; }
                if (boolRow("Loop", &player.loop)) { changed = true; }
                fieldRow("Playback Speed");
                if (ImGui::DragFloat("##VideoPlaybackSpeed", &player.playbackSpeed, 0.01f, 0.0f, 8.0f, "%.2f")) {
                    player.playbackSpeed = std::clamp(player.playbackSpeed, 0.0f, 8.0f);
                    changed = true;
                }
                noteRow("Paste a project-relative or absolute video path. Runtime playback updates the renderer albedo texture.");
                endCompFields();
            }

            if (!player.videoPath.empty()) {
                drawTrimmedPathText(player.videoPath, ImVec4(0.78f, 0.88f, 1.0f, 1.0f));
            }

            ImGui::PopID();
        }
        if (removeVideoPlayer) {
            obj.hasVideoPlayer = false;
            obj.videoPlayer = VideoPlayerComponent{};
            changed = true;
        }
        if (changed) {
            videoPlayerSectionChanged = true;
            projectManager.currentProject.hasUnsavedChanges = true;
        }
        ImGui::PopStyleColor();
    }

    if (inspectorComponentKey == "ground_baked" && obj.hasGroundBakedType && sharedGroundBaked) {
        ImGui::Dummy(ImVec2(0.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.28f, 0.5f, 0.34f, 1.0f));
        bool removeGroundBaked = false;
        bool changed = false;
        auto header = drawComponentHeader("GroundBakedType", "GroundBakedType", "ground_baked", &obj.groundBakedType.enabled, true, [&]() {
            drawStandardComponentMenu(
                "ground_baked",
                "Copy Component Values",
                "Paste Component Values as New",
                "Paste Component Values as Value Overrides",
                inspectorClipboard.kind == InspectorClipboardKind::GroundBaked,
                [&]() { obj.groundBakedType = GroundBakedTypeComponent{}; changed = true; },
                [&]() { inspectorClipboard.kind = InspectorClipboardKind::GroundBaked; inspectorClipboard.groundBaked = obj.groundBakedType; },
                [&]() { obj.hasGroundBakedType = true; obj.groundBakedType = inspectorClipboard.groundBaked; changed = true; },
                [&]() { obj.groundBakedType = inspectorClipboard.groundBaked; changed = true; },
                removeGroundBaked);
        });
        if (header.enabledChanged) {
            changed = true;
        }
        if (header.open) {
            ImGui::PushID("GroundBakedType");
            if (ImGui::Button("Open AI Pathfinding")) {
                showAIPathfindingWindow = true;
            }
            if (beginCompFields("##Fields_GroundBaked")) {
                if (boolRow("Include In Bake", &obj.groundBakedType.includeInBake)) { changed = true; }
                fieldRow("Area Cost");
                if (ImGui::DragFloat("##AreaCost", &obj.groundBakedType.areaCost, 0.05f, 0.1f, 100.0f, "%.2f")) {
                    obj.groundBakedType.areaCost = std::clamp(obj.groundBakedType.areaCost, 0.1f, 100.0f);
                    changed = true;
                }
                noteRow("Objects marked here are considered walkable during bake.");
                endCompFields();
            }
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

    if (inspectorComponentKey == "obstacle" && obj.hasObsticleObject && sharedObstacle) {
        ImGui::Dummy(ImVec2(0.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.5f, 0.3f, 0.28f, 1.0f));
        bool removeObstacle = false;
        bool changed = false;
        auto header = drawComponentHeader("ObsticleObject", "ObsticleObject", "obstacle", &obj.obsticleObject.enabled, true, [&]() {
            drawStandardComponentMenu(
                "obstacle",
                "Copy Component Values",
                "Paste Component Values as New",
                "Paste Component Values as Value Overrides",
                inspectorClipboard.kind == InspectorClipboardKind::Obstacle,
                [&]() { obj.obsticleObject = ObsticleObjectComponent{}; changed = true; },
                [&]() { inspectorClipboard.kind = InspectorClipboardKind::Obstacle; inspectorClipboard.obstacle = obj.obsticleObject; },
                [&]() { obj.hasObsticleObject = true; obj.obsticleObject = inspectorClipboard.obstacle; changed = true; },
                [&]() { obj.obsticleObject = inspectorClipboard.obstacle; changed = true; },
                removeObstacle);
        });
        if (header.enabledChanged) {
            changed = true;
        }
        if (header.open) {
            ImGui::PushID("ObsticleObject");
            if (ImGui::Button("Open AI Pathfinding")) {
                showAIPathfindingWindow = true;
            }
            if (beginCompFields("##Fields_Obstacle")) {
                if (boolRow("Carve", &obj.obsticleObject.carve)) { changed = true; }
                fieldRow("Padding");
                if (ImGui::DragFloat("##Padding", &obj.obsticleObject.padding, 0.02f, 0.0f, 10.0f, "%.2f")) {
                    obj.obsticleObject.padding = std::clamp(obj.obsticleObject.padding, 0.0f, 10.0f);
                    changed = true;
                }
                noteRow("Obstacle regions are removed from the baked walkable map.");
                endCompFields();
            }
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

    if (inspectorComponentKey == "ai_agent" && obj.hasAIAgent && sharedAgent) {
        ImGui::Dummy(ImVec2(0.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.32f, 0.4f, 0.58f, 1.0f));
        bool removeAgent = false;
        bool changed = false;
        auto header = drawComponentHeader("AI Agent", "AIAgent", "ai_agent", &obj.aiAgent.enabled, true, [&]() {
            drawStandardComponentMenu(
                "ai_agent",
                "Copy Component Values",
                "Paste Component Values as New",
                "Paste Component Values as Value Overrides",
                inspectorClipboard.kind == InspectorClipboardKind::AIAgent,
                [&]() { obj.aiAgent = AIAgentComponent{}; changed = true; },
                [&]() { inspectorClipboard.kind = InspectorClipboardKind::AIAgent; inspectorClipboard.aiAgent = obj.aiAgent; },
                [&]() { obj.hasAIAgent = true; obj.aiAgent = inspectorClipboard.aiAgent; changed = true; },
                [&]() { obj.aiAgent = inspectorClipboard.aiAgent; changed = true; },
                removeAgent);
        });
        if (header.enabledChanged) {
            changed = true;
        }
        if (header.open) {
            ImGui::PushID("AIAgent");
            if (ImGui::Button("Open AI Pathfinding")) {
                showAIPathfindingWindow = true;
                aiPreviewAgentId = obj.id;
            }
            if (beginCompFields("##Fields_AIAgent")) {
                if (boolRow("Use Target", &obj.aiAgent.useTargetObject)) { changed = true; }
                if (obj.aiAgent.useTargetObject) {
                    endCompFields();
                    if (drawSceneObjectReferenceSlot("Target Object", "##AIAgentTarget", obj.aiAgent.targetId, obj.id, "None (Target Object)")) {
                        aiPreviewTargetId = obj.aiAgent.targetId;
                        changed = true;
                    }
                    beginCompFields("##Fields_AIAgent2");
                }
                fieldRow("Destination");
                if (ImGui::DragFloat3("##Destination", &obj.aiAgent.destination.x, 0.05f, -10000.0f, 10000.0f, "%.2f")) {
                    changed = true;
                }
                ImGui::TableNextRow(); ImGui::TableSetColumnIndex(1);
                if (ImGui::Button("Set To Current")) {
                    obj.aiAgent.destination = obj.position;
                    changed = true;
                }
                fieldRow("Speed");
                if (ImGui::DragFloat("##Speed", &obj.aiAgent.speed, 0.05f, 0.05f, 100.0f, "%.2f")) {
                    obj.aiAgent.speed = std::max(0.05f, obj.aiAgent.speed);
                    changed = true;
                }
                fieldRow("Stop Distance");
                if (ImGui::DragFloat("##StoppingDist", &obj.aiAgent.stoppingDistance, 0.01f, 0.0f, 25.0f, "%.2f")) {
                    obj.aiAgent.stoppingDistance = std::clamp(obj.aiAgent.stoppingDistance, 0.0f, 25.0f);
                    changed = true;
                }
                fieldRow("Repath Interval");
                if (ImGui::DragFloat("##RepathInterval", &obj.aiAgent.repathInterval, 0.05f, 0.05f, 10.0f, "%.2f")) {
                    obj.aiAgent.repathInterval = std::clamp(obj.aiAgent.repathInterval, 0.05f, 10.0f);
                    changed = true;
                }
                if (boolRow("Auto Repath", &obj.aiAgent.autoRepath)) { changed = true; }
                if (boolRow("Align To Path", &obj.aiAgent.alignToPath)) { changed = true; }
                if (boolRow("Debug Draw Path", &obj.aiAgent.debugDrawPath)) { changed = true; }
                endCompFields();
            }
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

    if (inspectorComponentKey == "animation" && obj.hasAnimation && sharedAnimation) {
        ImGui::Dummy(ImVec2(0.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.4f, 0.35f, 0.55f, 1.0f));
        bool removeAnimation = false;
        bool changed = false;
        auto header = drawComponentHeader("Animation", "Animation", "animation", &obj.animation.enabled, true, [&]() {
            if (ImGui::MenuItem("Reset Component Values")) {
                obj.animation = AnimationComponent{};
                changed = true;
            }
            if (ImGui::MenuItem("View Object Animations in Animator")) {
                showAnimationWindow = true;
                animationTargetId = obj.id;
            }
            drawClipboardMenus(
                "Copy Animator Values",
                "Paste Animator Values as New",
                "Paste Animator Values as Value Overrides",
                inspectorClipboard.kind == InspectorClipboardKind::Animation,
                [&]() {
                    inspectorClipboard.kind = InspectorClipboardKind::Animation;
                    inspectorClipboard.animation = obj.animation;
                },
                [&]() {
                    obj.hasAnimation = true;
                    obj.animation = inspectorClipboard.animation;
                    changed = true;
                },
                [&]() {
                    obj.animation = inspectorClipboard.animation;
                    changed = true;
                });
            ImGui::Separator();
            if (ImGui::MenuItem("Remove Component")) {
                removeAnimation = true;
            }
        });
        if (header.enabledChanged) {
            changed = true;
        }
        if (header.open) {
            ImGui::PushID("Animation");
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
            ImGui::TextDisabled("Keyframes: %zu | Tracks: %zu", displayKeyCount, displayTrackCount);

            if (beginCompFields("##Fields_Animation")) {
                fieldRow("Clip Length");
                if (ImGui::DragFloat("##ClipLength", &obj.animation.clipLength, 0.05f, 0.1f, 120.0f, "%.2f")) {
                    obj.animation.clipLength = std::max(0.1f, obj.animation.clipLength);
                    changed = true;
                }
                fieldRow("Play Speed");
                if (ImGui::DragFloat("##PlaySpeed", &obj.animation.playSpeed, 0.05f, 0.05f, 8.0f, "%.2f")) {
                    obj.animation.playSpeed = std::max(0.05f, obj.animation.playSpeed);
                    changed = true;
                }
                if (boolRow("Loop", &obj.animation.loop)) { changed = true; }
                if (boolRow("Play On Awake", &obj.animation.playOnAwake)) { changed = true; }
                if (boolRow("Apply On Scrub", &obj.animation.applyOnScrub)) { changed = true; }
                endCompFields();
            }

            if (ImGui::Button("Clear Keyframes")) {
                obj.animation.keyframes.clear();
                changed = true;
            }

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

    if (inspectorComponentKey == "skeletal_animation" && obj.hasSkeletalAnimation && sharedSkeletal) {
        ImGui::Dummy(ImVec2(0.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.35f, 0.4f, 0.6f, 1.0f));
        bool removeSkeletal = false;
        bool changed = false;
        auto header = drawComponentHeader("Skeletal", "Skeletal", "skeletal_animation", &obj.skeletal.enabled, true, [&]() {
            drawStandardComponentMenu(
                "skeletal_animation",
                "Copy Component Values",
                "Paste Component Values as New",
                "Paste Component Values as Value Overrides",
                inspectorClipboard.kind == InspectorClipboardKind::Skeletal,
                [&]() { obj.skeletal = SkeletalAnimationComponent{}; changed = true; },
                [&]() { inspectorClipboard.kind = InspectorClipboardKind::Skeletal; inspectorClipboard.skeletal = obj.skeletal; },
                [&]() { obj.hasSkeletalAnimation = true; obj.skeletal = inspectorClipboard.skeletal; changed = true; },
                [&]() { obj.skeletal = inspectorClipboard.skeletal; changed = true; },
                removeSkeletal);
        });
        if (header.enabledChanged) {
            changed = true;
        }
        if (header.open) {
            ImGui::PushID("Skeletal");
            if (beginCompFields("##Fields_Skeletal")) {
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
                    fieldRow("Clip");
                    if (ImGui::Combo("##Clip", &clipIndex, clipNames.data(), (int)clipNames.size())) {
                        obj.skeletal.clipIndex = clipIndex;
                        obj.skeletal.time = 0.0f;
                        changed = true;
                    }
                } else {
                    noteRow("No animation clips found");
                }
                if (boolRow("Use Animation", &obj.skeletal.useAnimation)) { changed = true; }
                fieldRow("Play Speed");
                if (ImGui::DragFloat("##PlaySpeed", &obj.skeletal.playSpeed, 0.05f, 0.05f, 8.0f, "%.2f")) {
                    obj.skeletal.playSpeed = std::max(0.05f, obj.skeletal.playSpeed);
                    changed = true;
                }
                if (boolRow("Loop", &obj.skeletal.loop)) { changed = true; }
                if (boolRow("GPU Skinning", &obj.skeletal.useGpuSkinning)) { changed = true; }
                if (boolRow("CPU Fallback", &obj.skeletal.allowCpuFallback)) { changed = true; }
                fieldRow("Max Bones");
                if (ImGui::DragInt("##MaxBones", &obj.skeletal.maxBones, 1, 8, 256)) {
                    obj.skeletal.maxBones = std::clamp(obj.skeletal.maxBones, 8, 256);
                    changed = true;
                }
                endCompFields();
            }
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

    if (inspectorComponentKey == "reverb_zone" && obj.hasReverbZone && sharedReverb) {
        ImGui::Dummy(ImVec2(0.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.4f, 0.45f, 0.6f, 1.0f));
        bool removeReverbZone = false;
        bool changed = false;
        auto header = drawComponentHeader("Reverb Zone", "ReverbZone", "reverb_zone", &obj.reverbZone.enabled, true, [&]() {
            drawStandardComponentMenu(
                "reverb_zone",
                "Copy Component Values",
                "Paste Component Values as New",
                "Paste Component Values as Value Overrides",
                inspectorClipboard.kind == InspectorClipboardKind::ReverbZone,
                [&]() { obj.reverbZone = ReverbZoneComponent{}; changed = true; },
                [&]() { inspectorClipboard.kind = InspectorClipboardKind::ReverbZone; inspectorClipboard.reverbZone = obj.reverbZone; },
                [&]() { obj.hasReverbZone = true; obj.reverbZone = inspectorClipboard.reverbZone; changed = true; },
                [&]() { obj.reverbZone = inspectorClipboard.reverbZone; changed = true; },
                removeReverbZone);
        });
        if (header.enabledChanged) {
            changed = true;
        }
        if (header.open) {
            ImGui::PushID("ReverbZone");
            auto& zone = obj.reverbZone;

            if (beginCompFields("##Fields_ReverbZone")) {
                const char* presets[] = { "Room", "Living Room", "Hall", "Forest", "Custom" };
                int presetIndex = static_cast<int>(zone.preset);
                fieldRow("Preset");
                if (ImGui::Combo("##Preset", &presetIndex, presets, IM_ARRAYSIZE(presets))) {
                    ApplyReverbPreset(zone, static_cast<ReverbPreset>(presetIndex));
                    changed = true;
                }

                const char* shapes[] = { "Box", "Sphere" };
                int shapeIndex = static_cast<int>(zone.shape);
                fieldRow("Shape");
                if (ImGui::Combo("##Shape", &shapeIndex, shapes, IM_ARRAYSIZE(shapes))) {
                    zone.shape = static_cast<ReverbZoneShape>(shapeIndex);
                    changed = true;
                }

                if (zone.shape == ReverbZoneShape::Sphere) {
                    fieldRow("Radius");
                    if (ImGui::DragFloat("##Radius", &zone.radius, 0.1f, 0.1f, 500.0f, "%.2f")) {
                        zone.radius = std::max(0.1f, zone.radius);
                        changed = true;
                    }
                    fieldRow("Min Distance");
                    if (ImGui::DragFloat("##MinDist", &zone.minDistance, 0.05f, 0.0f, 500.0f, "%.2f")) {
                        zone.minDistance = std::max(0.0f, zone.minDistance);
                        changed = true;
                    }
                    fieldRow("Max Distance");
                    if (ImGui::DragFloat("##MaxDist", &zone.maxDistance, 0.05f, zone.minDistance + 0.1f, 1000.0f, "%.2f")) {
                        zone.maxDistance = std::max(zone.maxDistance, zone.minDistance + 0.1f);
                        changed = true;
                    }
                } else {
                    fieldRow("Box Size");
                    if (ImGui::DragFloat3("##BoxSize", &zone.boxSize.x, 0.1f, 0.1f, 500.0f, "%.2f")) {
                        zone.boxSize = glm::max(zone.boxSize, glm::vec3(0.1f));
                        changed = true;
                    }
                    fieldRow("Blend Distance");
                    if (ImGui::DragFloat("##BlendDist", &zone.blendDistance, 0.05f, 0.0f, 50.0f, "%.2f")) {
                        zone.blendDistance = std::max(0.0f, zone.blendDistance);
                        changed = true;
                    }
                }

                fieldRow("Room");
                if (ImGui::SliderFloat("##Room", &zone.room, -10000.0f, 0.0f, "%.0f dB")) { zone.preset = ReverbPreset::Custom; changed = true; }
                fieldRow("Room HF");
                if (ImGui::SliderFloat("##RoomHF", &zone.roomHF, -10000.0f, 0.0f, "%.0f dB")) { zone.preset = ReverbPreset::Custom; changed = true; }
                fieldRow("Room LF");
                if (ImGui::SliderFloat("##RoomLF", &zone.roomLF, -10000.0f, 0.0f, "%.0f dB")) { zone.preset = ReverbPreset::Custom; changed = true; }
                fieldRow("Decay Time");
                if (ImGui::SliderFloat("##DecayTime", &zone.decayTime, 0.1f, 20.0f, "%.2f s")) { zone.preset = ReverbPreset::Custom; changed = true; }
                fieldRow("Decay HF Ratio");
                if (ImGui::SliderFloat("##DecayHFRatio", &zone.decayHFRatio, 0.1f, 2.0f, "%.2f")) { zone.preset = ReverbPreset::Custom; changed = true; }
                fieldRow("Reflections");
                if (ImGui::SliderFloat("##Reflections", &zone.reflections, -10000.0f, 1000.0f, "%.0f dB")) { zone.preset = ReverbPreset::Custom; changed = true; }
                fieldRow("Reflect Delay");
                if (ImGui::SliderFloat("##ReflectDelay", &zone.reflectionsDelay, 0.0f, 0.1f, "%.3f s")) { zone.preset = ReverbPreset::Custom; changed = true; }
                fieldRow("Reverb");
                if (ImGui::SliderFloat("##Reverb", &zone.reverb, -10000.0f, 2000.0f, "%.0f dB")) { zone.preset = ReverbPreset::Custom; changed = true; }
                fieldRow("Reverb Delay");
                if (ImGui::SliderFloat("##ReverbDelay", &zone.reverbDelay, 0.0f, 0.1f, "%.3f s")) { zone.preset = ReverbPreset::Custom; changed = true; }
                fieldRow("HF Reference");
                if (ImGui::SliderFloat("##HFRef", &zone.hfReference, 1000.0f, 20000.0f, "%.0f Hz")) { zone.preset = ReverbPreset::Custom; changed = true; }
                fieldRow("LF Reference");
                if (ImGui::SliderFloat("##LFRef", &zone.lfReference, 20.0f, 1000.0f, "%.0f Hz")) { zone.preset = ReverbPreset::Custom; changed = true; }
                fieldRow("Room Rolloff");
                if (ImGui::SliderFloat("##RoomRolloff", &zone.roomRolloffFactor, 0.0f, 10.0f, "%.2f")) { zone.preset = ReverbPreset::Custom; changed = true; }
                fieldRow("Diffusion");
                if (ImGui::SliderFloat("##Diffusion", &zone.diffusion, 0.0f, 100.0f, "%.0f")) { zone.preset = ReverbPreset::Custom; changed = true; }
                fieldRow("Density");
                if (ImGui::SliderFloat("##Density", &zone.density, 0.0f, 100.0f, "%.0f")) { zone.preset = ReverbPreset::Custom; changed = true; }
                endCompFields();
            }
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

    if (inspectorComponentKey == "camera" && obj.hasCamera && sharedCamera) {
        ImGui::Dummy(ImVec2(0.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.45f, 0.35f, 0.65f, 1.0f));
        bool changed = false;
        bool removeCamera = false;
        auto header = drawComponentHeader("Camera", "Camera", "camera", nullptr, true, [&]() {
            drawStandardComponentMenu(
                "camera",
                "Copy Component Values",
                "Paste Component Values as New",
                "Paste Component Values as Value Overrides",
                inspectorClipboard.kind == InspectorClipboardKind::Camera,
                [&]() { obj.camera = CameraComponent{}; changed = true; },
                [&]() { inspectorClipboard.kind = InspectorClipboardKind::Camera; inspectorClipboard.camera = obj.camera; },
                [&]() { obj.hasCamera = true; obj.camera = inspectorClipboard.camera; changed = true; },
                [&]() { obj.camera = inspectorClipboard.camera; changed = true; },
                removeCamera);
        });
        if (header.open) {
            ImGui::PushID("Camera");
            if (beginCompFields("##Fields_Camera")) {
                const char* cameraTypes[] = { "Scene", "Player" };
                int camType = static_cast<int>(obj.camera.type);
                fieldRow("Type");
                if (ImGui::Combo("##Type", &camType, cameraTypes, IM_ARRAYSIZE(cameraTypes))) {
                    obj.camera.type = static_cast<SceneCameraType>(camType);
                    changed = true;
                }
                fieldRow("FOV");
                if (ImGui::SliderFloat("##FOV", &obj.camera.fov, 20.0f, 120.0f, "%.0f deg")) { changed = true; }
                fieldRow("Near Clip");
                if (ImGui::DragFloat("##NearClip", &obj.camera.nearClip, 0.01f, 0.01f, obj.camera.farClip - 0.01f, "%.3f")) {
                    obj.camera.nearClip = std::max(0.01f, std::min(obj.camera.nearClip, obj.camera.farClip - 0.01f));
                    changed = true;
                }
                fieldRow("Far Clip");
                if (ImGui::DragFloat("##FarClip", &obj.camera.farClip, 0.1f, obj.camera.nearClip + 0.05f, 1000.0f, "%.1f")) {
                    obj.camera.farClip = std::max(obj.camera.nearClip + 0.05f, obj.camera.farClip);
                    changed = true;
                }
                if (boolRow("Apply Post FX", &obj.camera.applyPostFX)) { changed = true; }
                bool project2D = isProject2DPipeline();
                bool project25D = isProject25DPipeline();
                if (project2D) {
                    noteRow("2D camera mode is controlled by Project Pipeline.");
                } else {
                    if (project25D) {
                        noteRow("2.5D projects keep perspective cameras by default; use Legacy 2D Cam only when you want an orthographic shot.");
                    }
                    if (boolRow("Legacy 2D Cam", &obj.camera.use2D)) { changed = true; }
                }
                bool cameraUses2D = project2D || obj.camera.use2D;
                if (cameraUses2D) {
                    fieldRow("Pixels/Unit");
                    if (ImGui::DragFloat("##PixelsPerUnit", &obj.camera.pixelsPerUnit, 1.0f, 1.0f, 2000.0f, "%.1f")) {
                        obj.camera.pixelsPerUnit = std::max(1.0f, obj.camera.pixelsPerUnit);
                        changed = true;
                    }
                    noteRow("Uses X/Y for 2D view; Z stays fixed.");
                }
                endCompFields();
            }
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

    if (inspectorComponentKey == "camera_follow2d" && obj.hasCameraFollow2D && sharedCameraFollow2D) {
        ImGui::Dummy(ImVec2(0.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.35f, 0.55f, 0.4f, 1.0f));
        bool changed = false;
        bool removeFollow = false;
        auto header = drawComponentHeader("Camera Follow 2D", "CameraFollow2D", "camera_follow2d", &obj.cameraFollow2D.enabled, true, [&]() {
            drawStandardComponentMenu(
                "camera_follow2d",
                "Copy Component Values",
                "Paste Component Values as New",
                "Paste Component Values as Value Overrides",
                inspectorClipboard.kind == InspectorClipboardKind::CameraFollow2D,
                [&]() { obj.cameraFollow2D = CameraFollow2DComponent{}; changed = true; },
                [&]() { inspectorClipboard.kind = InspectorClipboardKind::CameraFollow2D; inspectorClipboard.cameraFollow2D = obj.cameraFollow2D; },
                [&]() { obj.hasCameraFollow2D = true; obj.cameraFollow2D = inspectorClipboard.cameraFollow2D; changed = true; },
                [&]() { obj.cameraFollow2D = inspectorClipboard.cameraFollow2D; changed = true; },
                removeFollow);
        });
        if (header.enabledChanged) {
            changed = true;
        }
        if (header.open) {
            ImGui::PushID("CameraFollow2D");
            if (!obj.hasCamera) {
                ImGui::TextDisabled("Requires a Camera component.");
            }
            if (drawSceneObjectReferenceSlot("Target", "##CameraFollowTarget", obj.cameraFollow2D.targetId, obj.id, "None (Target Object)")) {
                changed = true;
            }
            if (beginCompFields("##Fields_CameraFollow2D")) {
                fieldRow("Offset");
                if (ImGui::DragFloat2("##Offset", &obj.cameraFollow2D.offset.x, 0.1f)) { changed = true; }
                fieldRow("Smooth Time");
                if (ImGui::DragFloat("##SmoothTime", &obj.cameraFollow2D.smoothTime, 0.01f, 0.0f, 10.0f, "%.2f s")) {
                    obj.cameraFollow2D.smoothTime = std::max(0.0f, obj.cameraFollow2D.smoothTime);
                    changed = true;
                }
                endCompFields();
            }
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

    if (inspectorComponentKey == "post_fx" && obj.hasPostFX) {
        ImGui::Dummy(ImVec2(0.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.25f, 0.55f, 0.6f, 1.0f));
        bool changed = false;
        bool removePostFx = false;
        auto header = drawComponentHeader("ModuVolume", "PostFX", "post_fx", &obj.postFx.enabled, true, [&]() {
            drawStandardComponentMenu(
                "post_fx",
                "Copy Component Values",
                "Paste Component Values as New",
                "Paste Component Values as Value Overrides",
                inspectorClipboard.kind == InspectorClipboardKind::PostFX,
                [&]() { obj.postFx = PostFXSettings{}; changed = true; },
                [&]() { inspectorClipboard.kind = InspectorClipboardKind::PostFX; inspectorClipboard.postFx = obj.postFx; },
                [&]() { obj.hasPostFX = true; obj.postFx = inspectorClipboard.postFx; changed = true; },
                [&]() { obj.postFx = inspectorClipboard.postFx; changed = true; },
                removePostFx);
        });
        if (header.enabledChanged) {
            changed = true;
        }
        if (header.open) {
            ImGui::PushID("PostFX");
            if (ImGui::CollapsingHeader("Volume", ImGuiTreeNodeFlags_DefaultOpen)) {
                if (beginCompFields("##Fields_PFXVolume")) {
                    if (boolRow("Global Volume", &obj.postFx.isGlobal)) { changed = true; }
                    fieldRow("Priority");
                    if (ImGui::DragFloat("##Priority", &obj.postFx.priority, 0.05f, -100.0f, 100.0f, "%.2f")) { changed = true; }
                    fieldRow("Blend Weight");
                    if (ImGui::SliderFloat("##BlendWeight", &obj.postFx.blendWeight, 0.0f, 1.0f, "%.2f")) { changed = true; }
                    if (!obj.postFx.isGlobal) {
                        fieldRow("Blend Radius");
                        if (ImGui::DragFloat("##BlendRadius", &obj.postFx.blendRadius, 0.1f, 0.1f, 1000.0f, "%.2f")) {
                            obj.postFx.blendRadius = std::max(0.1f, obj.postFx.blendRadius);
                            changed = true;
                        }
                        noteRow("Local volumes use this object's transform and scale as bounds.");
                    }
                    endCompFields();
                }
            }

            if (ImGui::CollapsingHeader("HDR & Tone Mapping", ImGuiTreeNodeFlags_DefaultOpen)) {
                if (beginCompFields("##Fields_PFXHDR")) {
                    if (boolRow("HDR Enabled", &obj.postFx.hdrEnabled)) { changed = true; }
                    const char* toneMapperNames[] = { "None", "Reinhard", "ACES" };
                    int toneMapper = static_cast<int>(obj.postFx.toneMapper);
                    fieldRow("Tone Mapper");
                    if (ImGui::Combo("##ToneMapper", &toneMapper, toneMapperNames, IM_ARRAYSIZE(toneMapperNames))) {
                        obj.postFx.toneMapper = static_cast<PostFXToneMapper>(toneMapper);
                        changed = true;
                    }
                    fieldRow("White Point");
                    if (ImGui::SliderFloat("##WhitePoint", &obj.postFx.whitePoint, 0.25f, 16.0f, "%.2f")) { changed = true; }
                    fieldRow("Output Gamma");
                    if (ImGui::SliderFloat("##OutputGamma", &obj.postFx.gamma, 1.0f, 3.0f, "%.2f")) { changed = true; }
                    endCompFields();
                }
            }

            if (ImGui::CollapsingHeader("Bloom", ImGuiTreeNodeFlags_DefaultOpen)) {
                if (beginCompFields("##Fields_PFXBloom")) {
                    ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0);
                    if (ImGui::Checkbox("##EnabledBloom", &obj.postFx.bloomEnabled)) { changed = true; }
                    ImGui::TableSetColumnIndex(1); ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("Enabled");
                    ImGui::BeginDisabled(!obj.postFx.bloomEnabled);
                    fieldRow("Threshold");
                    if (ImGui::SliderFloat("##BloomThreshold", &obj.postFx.bloomThreshold, 0.0f, 4.0f, "%.2f")) { changed = true; }
                    fieldRow("Soft Knee");
                    if (ImGui::SliderFloat("##BloomSoftKnee", &obj.postFx.bloomSoftKnee, 0.0f, 1.0f, "%.2f")) { changed = true; }
                    fieldRow("Intensity");
                    if (ImGui::SliderFloat("##BloomIntensity", &obj.postFx.bloomIntensity, 0.0f, 4.0f, "%.2f")) { changed = true; }
                    fieldRow("Spread");
                    if (ImGui::SliderFloat("##BloomSpread", &obj.postFx.bloomRadius, 0.5f, 4.5f, "%.2f")) { changed = true; }
                    ImGui::EndDisabled();
                    endCompFields();
                }
            }

            if (ImGui::CollapsingHeader("Color", ImGuiTreeNodeFlags_DefaultOpen)) {
                if (beginCompFields("##Fields_PFXColor")) {
                    ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0);
                    if (ImGui::Checkbox("##EnabledColor", &obj.postFx.colorAdjustEnabled)) { changed = true; }
                    ImGui::TableSetColumnIndex(1); ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("Enabled");
                    ImGui::BeginDisabled(!obj.postFx.colorAdjustEnabled);
                    fieldRow("Exposure (EV)");
                    if (ImGui::SliderFloat("##Exposure", &obj.postFx.exposure, -5.0f, 5.0f, "%.2f")) { changed = true; }
                    fieldRow("Contrast");
                    if (ImGui::SliderFloat("##Contrast", &obj.postFx.contrast, 0.0f, 2.5f, "%.2f")) { changed = true; }
                    fieldRow("Saturation");
                    if (ImGui::SliderFloat("##Saturation", &obj.postFx.saturation, 0.0f, 2.5f, "%.2f")) { changed = true; }
                    fieldRow("Color Filter");
                    if (ImGui::ColorEdit3("##ColorFilter", &obj.postFx.colorFilter.x)) { changed = true; }
                    ImGui::EndDisabled();
                    endCompFields();
                }
            }

            if (ImGui::CollapsingHeader("Motion Blur", ImGuiTreeNodeFlags_DefaultOpen)) {
                if (beginCompFields("##Fields_PFXMotionBlur")) {
                    ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0);
                    if (ImGui::Checkbox("##EnabledMotionBlur", &obj.postFx.motionBlurEnabled)) { changed = true; }
                    ImGui::TableSetColumnIndex(1); ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("Enabled");
                    ImGui::BeginDisabled(!obj.postFx.motionBlurEnabled);
                    fieldRow("Strength");
                    if (ImGui::SliderFloat("##MBStrength", &obj.postFx.motionBlurStrength, 0.0f, 0.95f, "%.2f")) { changed = true; }
                    fieldRow("Threshold");
                    if (ImGui::SliderFloat("##MBThreshold", &obj.postFx.motionBlurThreshold, 0.0f, 0.25f, "%.3f")) { changed = true; }
                    fieldRow("Clamp");
                    if (ImGui::SliderFloat("##MBClamp", &obj.postFx.motionBlurClamp, 0.0f, 1.5f, "%.2f")) { changed = true; }
                    ImGui::EndDisabled();
                    endCompFields();
                }
            }

            if (ImGui::CollapsingHeader("Lens", ImGuiTreeNodeFlags_DefaultOpen)) {
                if (beginCompFields("##Fields_PFXLens")) {
                    ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0);
                    if (ImGui::Checkbox("##EnabledVignette", &obj.postFx.vignetteEnabled)) { changed = true; }
                    ImGui::TableSetColumnIndex(1); ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("Vignette");
                    ImGui::BeginDisabled(!obj.postFx.vignetteEnabled);
                    fieldRow("Intensity");
                    if (ImGui::SliderFloat("##VigIntensity", &obj.postFx.vignetteIntensity, 0.0f, 1.5f, "%.2f")) { changed = true; }
                    fieldRow("Smoothness");
                    if (ImGui::SliderFloat("##VigSmoothness", &obj.postFx.vignetteSmoothness, 0.05f, 1.0f, "%.2f")) { changed = true; }
                    ImGui::EndDisabled();
                    ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0);
                    if (ImGui::Checkbox("##EnabledChromatic", &obj.postFx.chromaticAberrationEnabled)) { changed = true; }
                    ImGui::TableSetColumnIndex(1); ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("Chromatic Aberr.");
                    ImGui::BeginDisabled(!obj.postFx.chromaticAberrationEnabled);
                    fieldRow("Fringe Amount");
                    if (ImGui::SliderFloat("##FringeAmt", &obj.postFx.chromaticAmount, 0.0f, 0.01f, "%.4f")) { changed = true; }
                    ImGui::EndDisabled();
                    ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0);
                    if (ImGui::Checkbox("##EnabledSharpen", &obj.postFx.sharpenEnabled)) { changed = true; }
                    ImGui::TableSetColumnIndex(1); ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("Sharpen");
                    ImGui::BeginDisabled(!obj.postFx.sharpenEnabled);
                    fieldRow("Sharpen Str.");
                    if (ImGui::SliderFloat("##SharpenStr", &obj.postFx.sharpenStrength, 0.0f, 1.0f, "%.2f")) { changed = true; }
                    ImGui::EndDisabled();
                    endCompFields();
                }
            }

            if (ImGui::CollapsingHeader("Ambient Occlusion", ImGuiTreeNodeFlags_DefaultOpen)) {
                if (beginCompFields("##Fields_PFXAO")) {
                    ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0);
                    if (ImGui::Checkbox("##EnabledAO", &obj.postFx.ambientOcclusionEnabled)) { changed = true; }
                    ImGui::TableSetColumnIndex(1); ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("Enabled");
                    ImGui::BeginDisabled(!obj.postFx.ambientOcclusionEnabled);
                    fieldRow("AO Radius");
                    if (ImGui::SliderFloat("##AORadius", &obj.postFx.aoRadius, 0.0005f, 0.01f, "%.4f")) { changed = true; }
                    fieldRow("AO Strength");
                    if (ImGui::SliderFloat("##AOStrength", &obj.postFx.aoStrength, 0.0f, 2.0f, "%.2f")) { changed = true; }
                    ImGui::EndDisabled();
                    endCompFields();
                }
            }

            if (ImGui::CollapsingHeader("Dither", ImGuiTreeNodeFlags_DefaultOpen)) {
                if (beginCompFields("##Fields_PFXDither")) {
                    ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0);
                    if (ImGui::Checkbox("##EnabledDither", &obj.postFx.ditherEnabled)) { changed = true; }
                    ImGui::TableSetColumnIndex(1); ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("Enabled");
                    ImGui::BeginDisabled(!obj.postFx.ditherEnabled);
                    fieldRow("Intensity");
                    if (ImGui::SliderFloat("##DitherIntensity", &obj.postFx.ditherIntensity, 0.0f, 1.5f, "%.2f")) { changed = true; }
                    fieldRow("Color Bit Depth");
                    if (ImGui::SliderInt("##DitherColorBits", &obj.postFx.ditherColorBits, 1, 8, "%d bits")) { changed = true; }
                    fieldRow("Dither Size");
                    if (ImGui::SliderFloat("##DitherSize", &obj.postFx.ditherSize, 1.0f, 8.0f, "%.1f")) { changed = true; }
                    fieldRow("Contrast");
                    if (ImGui::SliderFloat("##DitherContrast", &obj.postFx.ditherContrast, -1.0f, 1.0f, "%.2f")) { changed = true; }
                    fieldRow("Offset");
                    if (ImGui::SliderFloat("##DitherOffset", &obj.postFx.ditherOffset, -1.0f, 1.0f, "%.2f")) { changed = true; }
                    fieldRow("Dark Adjust");
                    if (ImGui::SliderFloat("##DitherDarkAdj", &obj.postFx.ditherDarkAdjustment, 0.0f, 1.0f, "%.2f")) { changed = true; }
                    fieldRow("Pixelation");
                    if (ImGui::SliderFloat("##DitherPixelation", &obj.postFx.ditherPixelation, 0.0f, 64.0f, "%.1f px")) { changed = true; }
                    static const char* ditherPaletteNames[] = { "Full Color", "PS1 Warm", "PS1 Cool", "Mono", "Sepia" };
                    int ditherPalette = static_cast<int>(obj.postFx.ditherPalette);
                    fieldRow("Palette");
                    if (ImGui::Combo("##DitherPalette", &ditherPalette, ditherPaletteNames, IM_ARRAYSIZE(ditherPaletteNames))) {
                        obj.postFx.ditherPalette = static_cast<PostFXDitherPalette>(ditherPalette);
                        changed = true;
                    }
                    static const char* ditherPatternNames[] = { "Classic 4x4", "Bayer 8x8", "Bayer 16x16", "Checker", "Hybrid PS1" };
                    int ditherPattern = static_cast<int>(obj.postFx.ditherPattern);
                    fieldRow("Pattern");
                    if (ImGui::Combo("##DitherPattern", &ditherPattern, ditherPatternNames, IM_ARRAYSIZE(ditherPatternNames))) {
                        obj.postFx.ditherPattern = static_cast<PostFXDitherPattern>(ditherPattern);
                        changed = true;
                    }
                    ImGui::EndDisabled();
                    endCompFields();
                }
            }

            if (ImGui::CollapsingHeader("Static", ImGuiTreeNodeFlags_DefaultOpen)) {
                if (beginCompFields("##Fields_PFXStatic")) {
                    ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0);
                    if (ImGui::Checkbox("##EnabledStatic", &obj.postFx.staticEnabled)) { changed = true; }
                    ImGui::TableSetColumnIndex(1); ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("Enabled");
                    ImGui::BeginDisabled(!obj.postFx.staticEnabled);
                    fieldRow("Intensity");
                    if (ImGui::SliderFloat("##StaticIntensity", &obj.postFx.staticIntensity, 0.0f, 1.5f, "%.2f")) { changed = true; }
                    fieldRow("Grain Scale");
                    if (ImGui::SliderFloat("##StaticGrainScale", &obj.postFx.staticGrainScale, 0.25f, 10.0f, "%.2f")) { changed = true; }
                    fieldRow("Dark Influence");
                    if (ImGui::SliderFloat("##StaticDarkInfluence", &obj.postFx.staticDarkAreaInfluence, 0.0f, 2.0f, "%.2f")) { changed = true; }
                    fieldRow("Speed");
                    if (ImGui::SliderFloat("##StaticSpeed", &obj.postFx.staticSpeed, 0.0f, 20.0f, "%.2f")) { changed = true; }
                    ImGui::EndDisabled();
                    endCompFields();
                }
            }

            if (ImGui::CollapsingHeader("Static Distortion", ImGuiTreeNodeFlags_DefaultOpen)) {
                if (beginCompFields("##Fields_PFXStaticDist")) {
                    ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0);
                    if (ImGui::Checkbox("##EnabledStaticDist", &obj.postFx.staticDistortionEnabled)) { changed = true; }
                    ImGui::TableSetColumnIndex(1); ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("Enabled");
                    ImGui::BeginDisabled(!obj.postFx.staticDistortionEnabled);
                    fieldRow("Horiz Jitter");
                    if (ImGui::SliderFloat("##HorizJitter", &obj.postFx.staticDistortionHorizontalJitterAmount, 0.0f, 0.05f, "%.4f")) { changed = true; }
                    fieldRow("Line Density");
                    if (ImGui::SliderFloat("##LineDensity", &obj.postFx.staticDistortionLineDensity, 1.0f, 256.0f, "%.1f")) { changed = true; }
                    fieldRow("Glitch Freq");
                    if (ImGui::SliderFloat("##GlitchFreq", &obj.postFx.staticDistortionGlitchFrequency, 0.0f, 20.0f, "%.2f")) { changed = true; }
                    fieldRow("Dist Strength");
                    if (ImGui::SliderFloat("##DistStrength", &obj.postFx.staticDistortionStrength, 0.0f, 1.5f, "%.2f")) { changed = true; }
                    ImGui::EndDisabled();
                    endCompFields();
                }
            }

            if (ImGui::CollapsingHeader("Lens Distortion", ImGuiTreeNodeFlags_DefaultOpen)) {
                if (beginCompFields("##Fields_PFXLensDist")) {
                    ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0);
                    if (ImGui::Checkbox("##EnabledLensDist", &obj.postFx.lensDistortionEnabled)) { changed = true; }
                    ImGui::TableSetColumnIndex(1); ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("Enabled");
                    ImGui::BeginDisabled(!obj.postFx.lensDistortionEnabled);
                    fieldRow("Dist Amount");
                    if (ImGui::SliderFloat("##LensDistAmt", &obj.postFx.lensDistortionAmount, -1.0f, 1.0f, "%.3f")) { changed = true; }
                    fieldRow("Edge Falloff");
                    if (ImGui::SliderFloat("##LensEdgeFalloff", &obj.postFx.lensDistortionEdgeFalloff, 0.0f, 1.0f, "%.2f")) { changed = true; }
                    fieldRow("Center Offset");
                    if (ImGui::DragFloat2("##LensCenterOffset", &obj.postFx.lensDistortionCenterOffset.x, 0.001f, -0.25f, 0.25f, "%.3f")) { changed = true; }
                    ImGui::EndDisabled();
                    endCompFields();
                }
            }

            if (ImGui::CollapsingHeader("VHS Overlay", ImGuiTreeNodeFlags_DefaultOpen)) {
                if (beginCompFields("##Fields_PFXVHS")) {
                    ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0);
                    if (ImGui::Checkbox("##EnabledVHS", &obj.postFx.vhsOverlayEnabled)) { changed = true; }
                    ImGui::TableSetColumnIndex(1); ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("Enabled");
                    ImGui::BeginDisabled(!obj.postFx.vhsOverlayEnabled);
                    fieldRow("Opacity");
                    if (ImGui::SliderFloat("##VHSOpacity", &obj.postFx.vhsOverlayOpacity, 0.0f, 1.0f, "%.2f")) { changed = true; }
                    fieldRow("Scanline Str.");
                    if (ImGui::SliderFloat("##VHSScanline", &obj.postFx.vhsOverlayScanlineStrength, 0.0f, 1.0f, "%.2f")) { changed = true; }
                    fieldRow("Tape Noise");
                    if (ImGui::SliderFloat("##VHSTapeNoise", &obj.postFx.vhsOverlayTapeNoise, 0.0f, 1.0f, "%.2f")) { changed = true; }
                    fieldRow("Chroma Bleed");
                    if (ImGui::SliderFloat("##VHSChromaBleed", &obj.postFx.vhsOverlayChromaBleed, 0.0f, 0.02f, "%.4f")) { changed = true; }
                    fieldRow("Band Height");
                    if (ImGui::SliderFloat("##VHSBandHeight", &obj.postFx.vhsOverlayBottomNoiseBandHeight, 0.0f, 0.5f, "%.2f")) { changed = true; }
                    fieldRow("Band Intensity");
                    if (ImGui::SliderFloat("##VHSBandIntensity", &obj.postFx.vhsOverlayBottomNoiseBandIntensity, 0.0f, 2.0f, "%.2f")) { changed = true; }
                    ImGui::EndDisabled();
                    endCompFields();
                }
            }

            if (ImGui::CollapsingHeader("Wavy Effect", ImGuiTreeNodeFlags_DefaultOpen)) {
                if (beginCompFields("##Fields_PFXWavy")) {
                    ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0);
                    if (ImGui::Checkbox("##EnabledWavy", &obj.postFx.wavyEnabled)) { changed = true; }
                    ImGui::TableSetColumnIndex(1); ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("Enabled");
                    ImGui::BeginDisabled(!obj.postFx.wavyEnabled);
                    fieldRow("Amplitude");
                    if (ImGui::SliderFloat("##WavyAmplitude", &obj.postFx.wavyAmplitude, 0.0f, 0.05f, "%.4f")) { changed = true; }
                    fieldRow("Frequency");
                    if (ImGui::SliderFloat("##WavyFrequency", &obj.postFx.wavyFrequency, 0.1f, 80.0f, "%.2f")) { changed = true; }
                    fieldRow("Speed");
                    if (ImGui::SliderFloat("##WavySpeed", &obj.postFx.wavySpeed, 0.0f, 20.0f, "%.2f")) { changed = true; }
                    if (boolRow("Vertical", &obj.postFx.wavyVertical)) { changed = true; }
                    ImGui::EndDisabled();
                    endCompFields();
                }
            }

            /*if (ImGui::CollapsingHeader("Profiling", ImGuiTreeNodeFlags_DefaultOpen)) {
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
                ImGui::Text("Execution Began: %s", postStats.executionBegan ? "Yes" : "No");
                ImGui::Text("Composite Executed: %s", postStats.compositeExecuted ? "Yes" : "No");
                ImGui::Text("Raw Scene Tex/FBO: %u / %u", postStats.sourceTextureId, postStats.sourceFramebufferId);
                ImGui::Text("Bloom Extract Dest: %u / %u",
                            postStats.bloomExtractDestinationTextureId,
                            postStats.bloomExtractDestinationFramebufferId);
                ImGui::Text("Bloom Blur Result: %u / %u",
                            postStats.bloomBlurTextureId,
                            postStats.bloomBlurFramebufferId);
                ImGui::Text("Composite Dest: %u / %u",
                            postStats.compositeDestinationTextureId,
                            postStats.compositeDestinationFramebufferId);
                ImGui::Text("Presented Tex/FBO: %u / %u",
                            postStats.finalPresentedTextureId,
                            postStats.finalPresentedFramebufferId);
                ImGui::Text("Processed Differs: %s", postStats.finalTextureDiffersFromSource ? "Yes" : "No");
                if (!postStats.skipReason.empty()) {
                    ImGui::Text("Skip Reason: %s", postStats.skipReason.c_str());
                }
                ImGui::TextDisabled("Highest-priority active volume wins; local volumes fade by blend radius.");
                ImGui::TextDisabled("Wireframe/line mode auto-disables post effects.");
            }*/

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

    if (inspectorComponentKey == "renderer" && obj.hasRenderer && sharedRenderer) {
        ImGui::Dummy(ImVec2(0.0f, 1.0f));
        bool rendererChanged = false;
        bool removeRenderer = false;
        auto rendererHeader = drawComponentHeader("Renderer", "Renderer", "renderer", nullptr, true, [&]() {
            drawStandardComponentMenu(
                "renderer",
                "Copy Component Values",
                "Paste Component Values as New",
                "Paste Component Values as Value Overrides",
                inspectorClipboard.kind == InspectorClipboardKind::Renderer,
                [&]() {
                    resetRendererComponent(obj);
                    rendererChanged = true;
                },
                [&]() {
                    copyRendererClipboard(obj);
                },
                [&]() {
                    applyRendererClipboard(obj);
                    rendererChanged = true;
                },
                [&]() {
                    applyRendererClipboard(obj);
                    rendererChanged = true;
                },
                removeRenderer);
        });
        if (rendererHeader.open) {
            const char* renderLabel = "None";
            switch (obj.renderType) {
                case RenderType::Cube: renderLabel = "Cube"; break;
                case RenderType::Sphere: renderLabel = "Sphere"; break;
                case RenderType::Capsule: renderLabel = "Capsule"; break;
                case RenderType::OBJMesh: renderLabel = "OBJ Mesh"; break;
                case RenderType::Model: renderLabel = IsRawMeshPath(obj.meshPath) ? "RMesh" : "Model"; break;
                case RenderType::Mirror: renderLabel = "Mirror"; break;
                case RenderType::Plane: renderLabel = "Plane"; break;
                case RenderType::Torus: renderLabel = "Torus"; break;
                case RenderType::Sprite: renderLabel = "Sprite"; break;
                case RenderType::None: break;
            }
            /*ImGui::Text("Render Type: %s", renderLabel);*/

            int& selectedMaterialSlot = selectedRendererMaterialSlots[obj.id];
            const int materialSlotCount = 1 + static_cast<int>(obj.additionalMaterialPaths.size());
            selectedMaterialSlot = std::clamp(selectedMaterialSlot, 0, std::max(0, materialSlotCount - 1));

            auto getBuiltInMeshLabel = [&]() -> const char* {
                switch (obj.renderType) {
                    case RenderType::Cube: return "Built-in Cube";
                    case RenderType::Sphere: return "Built-in Sphere";
                    case RenderType::Capsule: return "Built-in Capsule";
                    case RenderType::Plane: return "Built-in Plane";
                    case RenderType::Torus: return "Built-in Torus";
                    case RenderType::Sprite: return "Built-in Quad";
                    case RenderType::Mirror: return "Built-in Mirror Quad";
                    case RenderType::OBJMesh:
                    case RenderType::Model:
                    case RenderType::None:
                    default: return "No mesh assigned";
                }
            };

            auto assignPrimaryMaterialPath = [&](const std::string& nextPath, bool loadAsset) {
                if (obj.materialPath == nextPath) {
                    return false;
                }
                obj.materialPath = nextPath;
                if (loadAsset && !obj.materialPath.empty()) {
                    loadMaterialFromFile(obj);
                }
                return true;
            };

            auto drawMaterialSlotRow = [&](int slotIndex, std::string& pathRef, bool primarySlot) {
                bool slotChanged = false;
                ImGui::PushID(slotIndex);
                ImGui::RadioButton("##SelectedSlot", &selectedMaterialSlot, slotIndex);
                ImGui::SameLine();
                ImGui::TextDisabled("Slot %d", slotIndex);
                ImGui::SameLine();
                ImGui::SetNextItemWidth(-200.0f);
                char slotBuf[512] = {};
                std::snprintf(slotBuf, sizeof(slotBuf), "%s", pathRef.c_str());
                if (ImGui::InputText("##MaterialSlotPath", slotBuf, sizeof(slotBuf))) {
                    pathRef = slotBuf;
                    slotChanged = true;
                }
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("FILE_PATH")) {
                        const char* dropped = static_cast<const char*>(payload->Data);
                        std::error_code ec;
                        fs::directory_entry droppedEntry(fs::path(dropped), ec);
                        if (!ec && fileBrowser.getFileCategory(droppedEntry) == FileCategory::Material) {
                            if (primarySlot) {
                                slotChanged |= assignPrimaryMaterialPath(dropped, true);
                            } else {
                                pathRef = dropped;
                                slotChanged = true;
                            }
                        }
                    }
                    ImGui::EndDragDropTarget();
                }
                ImGui::SameLine();
                ImGui::BeginDisabled(!browserHasMaterial);
                if (ImGui::SmallButton("Use Selection")) {
                    if (primarySlot) {
                        slotChanged |= assignPrimaryMaterialPath(selectedMaterialPath.string(), true);
                    } else {
                        pathRef = selectedMaterialPath.string();
                        slotChanged = true;
                    }
                }
                ImGui::EndDisabled();
                ImGui::SameLine();
                if (ImGui::SmallButton("Clear")) {
                    pathRef.clear();
                    slotChanged = true;
                }
                if (!primarySlot) {
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Remove")) {
                        obj.additionalMaterialPaths.erase(
                            obj.additionalMaterialPaths.begin() + static_cast<long>(slotIndex - 1));
                        selectedMaterialSlot = std::clamp(
                            selectedMaterialSlot, 0,
                            std::max(0, static_cast<int>(obj.additionalMaterialPaths.size())));
                        slotChanged = true;
                    }
                }
                if (primarySlot && pathRef.empty()) {
                    ImGui::TextDisabled("Slot 0 uses embedded material data until a material asset is assigned.");
                }
                ImGui::PopID();
                return slotChanged;
            };

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::TextDisabled("Mesh");
            const bool usesMeshAsset =
                obj.renderType == RenderType::OBJMesh || obj.renderType == RenderType::Model;
            bool browserHasModel = false;
            if (!fileBrowser.selectedFile.empty() && fs::exists(fileBrowser.selectedFile)) {
                std::error_code modelEc;
                fs::directory_entry modelEntry(fileBrowser.selectedFile, modelEc);
                browserHasModel = !modelEc && fileBrowser.isModelFile(modelEntry);
            }

            if (usesMeshAsset) {
                char meshPathBuf[512] = {};
                std::snprintf(meshPathBuf, sizeof(meshPathBuf), "%s", obj.meshPath.c_str());
                ImGui::SetNextItemWidth(-180.0f);
                if (ImGui::InputText("##RendererMeshPath", meshPathBuf, sizeof(meshPathBuf))) {
                    obj.meshPath = meshPathBuf;
                    rendererChanged = true;
                }
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("FILE_PATH")) {
                        const char* dropped = static_cast<const char*>(payload->Data);
                        if (assignRendererMeshAsset(obj, fs::path(dropped))) {
                            rendererChanged = true;
                        }
                    }
                    ImGui::EndDragDropTarget();
                }
                ImGui::SameLine();
                ImGui::BeginDisabled(!browserHasModel);
                if (ImGui::SmallButton("Use Selection")) {
                    if (assignRendererMeshAsset(obj, fileBrowser.selectedFile)) {
                        rendererChanged = true;
                    }
                }
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::BeginDisabled(obj.meshPath.empty());
                if (ImGui::SmallButton("Reload")) {
                    reloadRendererMeshAsset(obj);
                    rendererChanged = true;
                }
                ImGui::EndDisabled();
                if (obj.meshSourceIndex >= 0) {
                    ImGui::TextDisabled("Source mesh index: %d", obj.meshSourceIndex);
                }
            } else {
                ImGui::TextDisabled("%s", getBuiltInMeshLabel());
                ImGui::SameLine();
                ImGui::BeginDisabled(!browserHasModel);
                if (ImGui::SmallButton("Replace With Selected Mesh")) {
                    if (assignRendererMeshAsset(obj, fileBrowser.selectedFile)) {
                        rendererChanged = true;
                    }
                }
                ImGui::EndDisabled();
            }

            ImGui::Spacing();
            ImGui::Separator();
            /*
            ImGui::TextDisabled("Renderer");
            if (obj.renderType == RenderType::Sprite || obj.renderType == RenderType::Mirror) {
                if (ImGui::Checkbox("Face Camera", &obj.faceCamera)) {
                    rendererChanged = true;
                }
            } else {
                ImGui::TextDisabled("No additional renderer flags for this render type.");
            }
            */
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::TextDisabled("Material Slots");
            rendererChanged |= drawMaterialSlotRow(0, obj.materialPath, true);
            for (size_t slot = 0; slot < obj.additionalMaterialPaths.size(); ++slot) {
                rendererChanged |= drawMaterialSlotRow(
                    static_cast<int>(slot) + 1,
                    obj.additionalMaterialPaths[slot],
                    false
                );
            }
            if (ImGui::SmallButton("Add Material Slot")) {
                obj.additionalMaterialPaths.push_back("");
                selectedMaterialSlot = static_cast<int>(obj.additionalMaterialPaths.size());
                rendererChanged = true;
            }
            std::string selectedSlotName = "Slot " + std::to_string(selectedMaterialSlot);
            if (selectedMaterialSlot == 0 && obj.materialPath.empty()) {
                selectedSlotName += " (Embedded)";
            } else if (selectedMaterialSlot == 0 && !obj.materialPath.empty()) {
                selectedSlotName += ": " + fs::path(obj.materialPath).filename().string();
            } else if (selectedMaterialSlot > 0 &&
                       selectedMaterialSlot - 1 < static_cast<int>(obj.additionalMaterialPaths.size()) &&
                       !obj.additionalMaterialPaths[static_cast<size_t>(selectedMaterialSlot - 1)].empty()) {
                selectedSlotName += ": " +
                    fs::path(obj.additionalMaterialPaths[static_cast<size_t>(selectedMaterialSlot - 1)]).filename().string();
            }
            /*ImGui::TextDisabled("Editing target: %s", selectedSlotName.c_str());*/
            /*
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

            if (obj.renderType == RenderType::Model || IsRawMeshPath(obj.meshPath)) {
                ImGui::Spacing();
                ImGui::Separator();
                if (IsMMeshPath(obj.meshPath)) {
                    ImGui::TextDisabled("MMesh Info");
                    ImGui::Text("Source File:");
                    ImGui::TextDisabled("%s", fs::path(obj.meshPath).filename().string().c_str());

                    std::string loadError;
                    const auto* renderData = tmRenderer.getMeshCache().getOrLoad(obj.meshPath, loadError);
                    if (renderData) {
                        ImGui::Spacing();
                        ImGui::Text("Submeshes: %d", static_cast<int>(renderData->submeshes.size()));
                        ImGui::Text("Triangles: %u", renderData->totalTriangleCount);
                    } else if (!loadError.empty()) {
                        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", loadError.c_str());
                    }

                    ImGui::Spacing();
                    if (ImGui::Button("Reload MMesh", ImVec2(-1, 0))) {
                        tmRenderer.getMeshCache().clear();
                        tmOpenGLRenderer.invalidateCaches();
                        addConsoleMessage("Reloaded MMesh cache", ConsoleMessageType::Success);
                    }
                } else {
                    ImGui::TextDisabled(IsRawMeshPath(obj.meshPath) ? "RMesh Info" : "Model Info");

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

                        if (ImGui::Button(IsRawMeshPath(obj.meshPath) ? "Reload RMesh" : "Reload Model", ImVec2(-1, 0))) {
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
            }
            */

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

    if (inspectorComponentKey == "light" && obj.hasLight && sharedLight) {
        ImGui::Dummy(ImVec2(0.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.5f, 0.45f, 0.2f, 1.0f));
        bool changed = false;
        bool removeLight = false;
        auto header = drawComponentHeader("Light", "Light", "light", &obj.light.enabled, true, [&]() {
            drawStandardComponentMenu(
                "light",
                "Copy Component Values",
                "Paste Component Values as New",
                "Paste Component Values as Value Overrides",
                inspectorClipboard.kind == InspectorClipboardKind::Light,
                [&]() {
                    const LightType preservedType = obj.light.type;
                    obj.light = LightComponent{};
                    obj.light.type = preservedType;
                    changed = true;
                },
                [&]() {
                    inspectorClipboard.kind = InspectorClipboardKind::Light;
                    inspectorClipboard.light = obj.light;
                },
                [&]() {
                    obj.hasLight = true;
                    obj.light = inspectorClipboard.light;
                    UpdateLegacyTypeFromComponents(obj);
                    changed = true;
                },
                [&]() {
                    obj.hasLight = true;
                    obj.light = inspectorClipboard.light;
                    UpdateLegacyTypeFromComponents(obj);
                    changed = true;
                },
                removeLight);
        });
        if (header.enabledChanged) {
            changed = true;
        }
        if (header.open) {
            ImGui::PushID("Light");
            if (beginCompFields("##Fields_Light")) {
                int currentType = static_cast<int>(obj.light.type);
                const char* typeLabels[] = { "Directional", "Point", "Spot", "Area" };
                fieldRow("Type");
                if (ImGui::Combo("##LightType", &currentType, typeLabels, IM_ARRAYSIZE(typeLabels))) {
                    obj.light.type = (currentType == 0 ? LightType::Directional :
                                      currentType == 1 ? LightType::Point :
                                      currentType == 2 ? LightType::Spot : LightType::Area);
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
                fieldRow("Color");
                if (ImGui::ColorEdit3("##LightColor", &obj.light.color.x)) { changed = true; }
                fieldRow("Intensity");
                if (ImGui::SliderFloat("##LightIntensity", &obj.light.intensity, 0.0f, 10.0f)) { changed = true; }
                if (obj.light.type != LightType::Directional) {
                    fieldRow("Range");
                    if (ImGui::SliderFloat("##LightRange", &obj.light.range, 0.0f, 50.0f)) { changed = true; }
                }
                if (obj.light.type == LightType::Spot) {
                    fieldRow("Inner Angle");
                    if (ImGui::SliderFloat("##InnerAngle", &obj.light.innerAngle, 1.0f, 90.0f)) { changed = true; }
                    fieldRow("Outer Angle");
                    if (ImGui::SliderFloat("##OuterAngle", &obj.light.outerAngle, obj.light.innerAngle, 120.0f)) { changed = true; }
                }
                if (obj.light.type == LightType::Area) {
                    fieldRow("Size");
                    if (ImGui::DragFloat2("##AreaSize", &obj.light.size.x, 0.05f, 0.1f, 10.0f)) { changed = true; }
                    fieldRow("Edge Softness");
                    if (ImGui::SliderFloat("##EdgeSoftness", &obj.light.edgeFade, 0.0f, 1.0f, "%.2f")) { changed = true; }
                }
                if (boolRow("Cast Shadows", &obj.light.castShadows)) { changed = true; }
                if (obj.light.castShadows) {
                    bool useGlobalShadowResolution = (obj.light.shadowResolution <= 0);
                    if (boolRow("Use Global Resolution", &useGlobalShadowResolution)) {
                        obj.light.shadowResolution = useGlobalShadowResolution ? 0 : renderer.getShadowMapResolution();
                        changed = true;
                    }
                    if (!useGlobalShadowResolution) {
                        int shadowResolution = std::clamp(obj.light.shadowResolution, 128, 8192);
                        fieldRow("Shadow Resolution");
                        if (ImGui::SliderInt("##ShadowResolution", &shadowResolution, 128, 8192)) {
                            obj.light.shadowResolution = shadowResolution;
                            changed = true;
                        }
                    }
                    if (boolRow("Soft Shadows", &obj.light.softShadows)) { changed = true; }
                    fieldRow("Shadow Bias");
                    if (ImGui::SliderFloat("##ShadowBias", &obj.light.shadowBias, 0.0001f, 0.20f, "%.4f")) { changed = true; }
                    if (obj.light.softShadows) {
                        fieldRow("Shadow Softness");
                        if (ImGui::SliderFloat("##ShadowSoftness", &obj.light.shadowSoftness, 0.001f, 0.20f, "%.3f")) { changed = true; }
                    }
                }
                endCompFields();
            }
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

    if (inspectorComponentKey == "light2d" && has2DWorldPackage() && obj.hasLight2D && sharedLight2D) {
        ImGui::Dummy(ImVec2(0.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.62f, 0.54f, 0.18f, 1.0f));
        bool changed = false;
        bool removeLight2D = false;
        auto header = drawComponentHeader("Light 2D", "Light2D", "light2d", &obj.light2D.enabled, true, [&]() {
            drawStandardComponentMenu(
                "light2d",
                "Copy Component Values",
                "Paste Component Values as New",
                "Paste Component Values as Value Overrides",
                inspectorClipboard.kind == InspectorClipboardKind::Light2D,
                [&]() {
                    const Light2DType preservedType = obj.light2D.type;
                    obj.light2D = Light2DComponent{};
                    obj.light2D.type = preservedType;
                    if ((preservedType == Light2DType::Freeform || preservedType == Light2DType::Sprite) &&
                        obj.light2D.shapePoints.size() < 3) {
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
                },
                [&]() {
                    inspectorClipboard.kind = InspectorClipboardKind::Light2D;
                    inspectorClipboard.light2D = obj.light2D;
                },
                [&]() {
                    obj.hasLight2D = true;
                    obj.light2D = inspectorClipboard.light2D;
                    lighting2DRenderer.clearPolygonCache(obj.id);
                    UpdateLegacyTypeFromComponents(obj);
                    changed = true;
                },
                [&]() {
                    obj.hasLight2D = true;
                    obj.light2D = inspectorClipboard.light2D;
                    lighting2DRenderer.clearPolygonCache(obj.id);
                    UpdateLegacyTypeFromComponents(obj);
                    changed = true;
                },
                removeLight2D);
        });
        if (header.enabledChanged) {
            changed = true;
        }
        if (header.open) {
            ImGui::PushID("Light2D");

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

            if (beginCompFields("##Fields_Light2D")) {
                int currentType = static_cast<int>(obj.light2D.type);
                const char* typeLabels[] = { "Point", "Spot", "Freeform", "Sprite", "Global" };
                fieldRow("Type");
                if (ImGui::Combo("##Light2DType", &currentType, typeLabels, IM_ARRAYSIZE(typeLabels))) {
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
                fieldRow("Color");
                if (ImGui::ColorEdit4("##Light2DColor", &obj.light2D.color.x)) { changed = true; }
                fieldRow("Intensity");
                if (ImGui::SliderFloat("##Light2DIntensity", &obj.light2D.intensity, 0.0f, 16.0f, "%.2f")) { changed = true; }
                if (obj.light2D.type != Light2DType::Global) {
                    fieldRow("Radius");
                    if (ImGui::DragFloat("##Light2DRadius", &obj.light2D.radius, 0.05f, 0.0f, 4096.0f, "%.2f")) {
                        obj.light2D.radius = std::max(0.0f, obj.light2D.radius);
                        changed = true;
                    }
                    fieldRow("Inner Radius");
                    if (ImGui::DragFloat("##Light2DInnerRadius", &obj.light2D.innerRadius, 0.05f, 0.0f, 4096.0f, "%.2f")) {
                        obj.light2D.innerRadius = std::max(0.0f, obj.light2D.innerRadius);
                        obj.light2D.outerRadius = std::max(obj.light2D.outerRadius, obj.light2D.innerRadius);
                        changed = true;
                    }
                    fieldRow("Outer Radius");
                    if (ImGui::DragFloat("##Light2DOuterRadius", &obj.light2D.outerRadius, 0.05f, obj.light2D.innerRadius, 4096.0f, "%.2f")) {
                        obj.light2D.outerRadius = std::max(obj.light2D.innerRadius, obj.light2D.outerRadius);
                        obj.light2D.radius = std::max(obj.light2D.radius, obj.light2D.outerRadius);
                        changed = true;
                    }
                    fieldRow("Falloff");
                    if (ImGui::SliderFloat("##Light2DFalloff", &obj.light2D.falloffStrength, 0.01f, 8.0f, "%.2f")) { changed = true; }
                }
                if (obj.light2D.type == Light2DType::Spot) {
                    fieldRow("Inner Angle");
                    if (ImGui::SliderFloat("##Light2DInnerAngle", &obj.light2D.innerSpotAngle, 0.0f, 360.0f, "%.1f")) {
                        obj.light2D.outerSpotAngle = std::max(obj.light2D.outerSpotAngle, obj.light2D.innerSpotAngle);
                        changed = true;
                    }
                    fieldRow("Outer Angle");
                    if (ImGui::SliderFloat("##Light2DOuterAngle", &obj.light2D.outerSpotAngle, obj.light2D.innerSpotAngle, 360.0f, "%.1f")) { changed = true; }
                }
                fieldRow("Blend Style");
                {
                    int blendStyle = std::clamp(obj.light2D.blendStyle, 0, static_cast<int>(light2DBlendStyles.size()) - 1);
                    const char* currentBlend = light2DBlendStyles[static_cast<size_t>(blendStyle)].name.c_str();
                    if (ImGui::BeginCombo("##BlendStyle", currentBlend)) {
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
                }
                const char* overlapLabels[] = { "Additive", "Max", "Alpha Blend" };
                int overlapMode = static_cast<int>(obj.light2D.overlapOperation);
                fieldRow("Overlap");
                if (ImGui::Combo("##Overlap", &overlapMode, overlapLabels, IM_ARRAYSIZE(overlapLabels))) {
                    obj.light2D.overlapOperation = static_cast<Light2DOverlapOperation>(std::clamp(overlapMode, 0, 2));
                    changed = true;
                }
                fieldRow("Light Order");
                if (ImGui::DragInt("##LightOrder", &obj.light2D.lightOrder, 1.0f, -4096, 4096)) { changed = true; }
                endCompFields();
            }

            drawLayerMaskEditor("Target All Layers", obj.light2D.targetAllLayers, obj.light2D.targetLayerMask);

            if (ImGui::CollapsingHeader("Shadows", ImGuiTreeNodeFlags_DefaultOpen)) {
                if (beginCompFields("##Fields_L2DShadows")) {
                    if (boolRow("Cast Shadows", &obj.light2D.castsShadows)) { changed = true; }
                    fieldRow("Shadow Strength");
                    if (ImGui::SliderFloat("##L2DShadowStr", &obj.light2D.shadowStrength, 0.0f, 1.0f, "%.2f")) { changed = true; }
                    endCompFields();
                }
            }

            if (ImGui::CollapsingHeader("Volumetric")) {
                if (beginCompFields("##Fields_L2DVolumetric")) {
                    if (boolRow("Enabled", &obj.light2D.volumetricEnabled)) { changed = true; }
                    noteRow("Volumetric accumulation is scaffolded for a later pass.");
                    endCompFields();
                }
            }

            if (ImGui::CollapsingHeader("Normal Maps")) {
                if (beginCompFields("##Fields_L2DNormalMaps")) {
                    const char* normalQualityLabels[] = { "Disabled", "Fast", "Accurate" };
                    int quality = static_cast<int>(obj.light2D.normalMapQuality);
                    fieldRow("Quality");
                    if (ImGui::Combo("##NormalQuality", &quality, normalQualityLabels, IM_ARRAYSIZE(normalQualityLabels))) {
                        obj.light2D.normalMapQuality = static_cast<Light2DNormalMapQuality>(std::clamp(quality, 0, 2));
                        changed = true;
                    }
                    fieldRow("Distance");
                    if (ImGui::DragFloat("##NormalDist", &obj.light2D.normalMapDistance, 0.05f, 0.0f, 64.0f, "%.2f")) {
                        obj.light2D.normalMapDistance = std::max(0.0f, obj.light2D.normalMapDistance);
                        changed = true;
                    }
                    endCompFields();
                }
            }

            if (ImGui::CollapsingHeader("Distance Attenuation")) {
                if (beginCompFields("##Fields_L2DDistAtten")) {
                    if (boolRow("Use Dist Exponent", &obj.light2D.useDistanceExponent)) { changed = true; }
                    fieldRow("Dist Exponent");
                    if (ImGui::SliderFloat("##DistExponent", &obj.light2D.distanceExponent, 0.1f, 8.0f, "%.2f")) { changed = true; }
                    endCompFields();
                }
            }

            if (ImGui::CollapsingHeader("Cookie")) {
                if (beginCompFields("##Fields_L2DCookie")) {
                    fieldRow("Texture");
                    {
                        char cookieBuffer[512] = {};
                        std::snprintf(cookieBuffer, sizeof(cookieBuffer), "%s", obj.light2D.cookieTexturePath.c_str());
                        if (ImGui::InputText("##CookieTex", cookieBuffer, sizeof(cookieBuffer))) {
                            obj.light2D.cookieTexturePath = cookieBuffer;
                            changed = true;
                        }
                    }
                    fieldRow("Scale");
                    if (ImGui::DragFloat2("##CookieScale", &obj.light2D.cookieScale.x, 0.01f, 0.01f, 16.0f, "%.2f")) {
                        obj.light2D.cookieScale.x = std::max(0.01f, obj.light2D.cookieScale.x);
                        obj.light2D.cookieScale.y = std::max(0.01f, obj.light2D.cookieScale.y);
                        changed = true;
                    }
                    fieldRow("Rotation");
                    if (ImGui::DragFloat("##CookieRotation", &obj.light2D.cookieRotation, 0.5f, -360.0f, 360.0f, "%.1f")) { changed = true; }
                    endCompFields();
                }
            }

            if (ImGui::CollapsingHeader("Flicker")) {
                if (beginCompFields("##Fields_L2DFlicker")) {
                    if (boolRow("Enabled", &obj.light2D.flicker.enabled)) { changed = true; }
                    fieldRow("Speed");
                    if (ImGui::SliderFloat("##FlickerSpeed", &obj.light2D.flicker.speed, 0.01f, 64.0f, "%.2f")) { changed = true; }
                    fieldRow("Amount");
                    if (ImGui::SliderFloat("##FlickerAmount", &obj.light2D.flicker.amount, 0.0f, 1.0f, "%.2f")) { changed = true; }
                    fieldRow("Seed");
                    if (ImGui::DragFloat("##FlickerSeed", &obj.light2D.flicker.seed, 0.05f, -1000.0f, 1000.0f, "%.2f")) { changed = true; }
                    endCompFields();
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

                    if (beginCompFields("##Fields_L2DShape")) {
                        fieldRow("Feather");
                        if (ImGui::SliderFloat("##L2DFeather", &obj.light2D.freeformFeather, 0.0f, 4.0f, "%.2f")) { changed = true; }
                        fieldRow("Edge Falloff");
                        if (ImGui::SliderFloat("##L2DEdgeFalloff", &obj.light2D.freeformEdgeFalloff, 0.1f, 8.0f, "%.2f")) { changed = true; }
                        endCompFields();
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

    if (inspectorComponentKey == "shadow_caster2d" && obj.hasShadowCaster2D && sharedShadowCaster2D) {
        ImGui::Dummy(ImVec2(0.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.26f, 0.36f, 0.48f, 1.0f));
        bool changed = false;
        bool removeShadowCaster2D = false;
        auto header = drawComponentHeader("Shadow Caster 2D", "ShadowCaster2D", "shadow_caster2d", &obj.shadowCaster2D.enabled, true, [&]() {
            drawStandardComponentMenu(
                "shadow_caster2d",
                "Copy Component Values",
                "Paste Component Values as New",
                "Paste Component Values as Value Overrides",
                inspectorClipboard.kind == InspectorClipboardKind::ShadowCaster2D,
                [&]() {
                    obj.shadowCaster2D = ShadowCaster2DComponent{};
                    obj.shadowCaster2D.points = {
                        glm::vec2(-1.0f, -1.0f),
                        glm::vec2(1.0f, -1.0f),
                        glm::vec2(1.0f, 1.0f),
                        glm::vec2(-1.0f, 1.0f)
                    };
                    UpdateLegacyTypeFromComponents(obj);
                    changed = true;
                },
                [&]() {
                    inspectorClipboard.kind = InspectorClipboardKind::ShadowCaster2D;
                    inspectorClipboard.shadowCaster2D = obj.shadowCaster2D;
                },
                [&]() {
                    obj.hasShadowCaster2D = true;
                    obj.shadowCaster2D = inspectorClipboard.shadowCaster2D;
                    UpdateLegacyTypeFromComponents(obj);
                    changed = true;
                },
                [&]() {
                    obj.hasShadowCaster2D = true;
                    obj.shadowCaster2D = inspectorClipboard.shadowCaster2D;
                    UpdateLegacyTypeFromComponents(obj);
                    changed = true;
                },
                removeShadowCaster2D);
        });
        if (header.enabledChanged) {
            changed = true;
        }
        if (header.open) {
            ImGui::PushID("ShadowCaster2D");

            if (beginCompFields("##Fields_ShadowCaster2D")) {
                if (boolRow("Self Shadow", &obj.shadowCaster2D.castsSelfShadow)) { changed = true; }
                fieldRow("Strength");
                if (ImGui::SliderFloat("##SC2DStrength", &obj.shadowCaster2D.shadowStrength, 0.0f, 1.0f, "%.2f")) { changed = true; }
                if (boolRow("Target All Layers", &obj.shadowCaster2D.targetAllLayers)) { changed = true; }
                endCompFields();
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

    if (IsInspectorScriptComponentKey(inspectorComponentKey)) {
    const int activeInspectorScriptId = ParseInspectorScriptComponentId(inspectorComponentKey);
    for (size_t i = 0; i < obj.scripts.size(); ++i) {
        if (obj.scripts[i].inspectorId != activeInspectorScriptId) {
            continue;
        }
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
        auto header = drawComponentHeader(headerLabel.c_str(), scriptId.c_str(), inspectorComponentKey, &sc.enabled, true, [&]() {
            const bool nativeScript = isNativeScriptLanguage(sc.language);
            if (ImGui::MenuItem("Reset Component Values")) {
                const int inspectorId = sc.inspectorId;
                sc = ScriptComponent{};
                sc.inspectorId = inspectorId;
                scriptsChanged = true;
            }
            if (ImGui::MenuItem("Compile Selected Script", nullptr, false, nativeScript ? !sc.path.empty() : true)) {
                if (nativeScript) {
                    compileScriptFile(sc.path);
                } else {
                    compileManagedScripts();
                }
            }
            if (ImGui::MenuItem("View Script", nullptr, false, !sc.path.empty())) {
                openScriptInEditor(sc.path);
            }
            drawClipboardMenus(
                "Copy Script Values",
                "Paste Script Values as New",
                "Paste Script Values as Value Overrides",
                inspectorClipboard.kind == InspectorClipboardKind::Script,
                [&]() {
                    inspectorClipboard.kind = InspectorClipboardKind::Script;
                    inspectorClipboard.script = sc;
                    inspectorClipboard.script.inspectorId = 0;
                    inspectorClipboard.script.lastBinaryPath.clear();
                    inspectorClipboard.script.lastBinaryVerified = false;
                    inspectorClipboard.script.activeIEnums.clear();
                },
                [&]() {
                    EnsureInspectorComponentMetadata(obj);
                    ScriptComponent pasted = inspectorClipboard.script;
                    pasted.inspectorId = std::max(1, obj.nextInspectorScriptId++);
                    pasted.lastBinaryPath.clear();
                    pasted.lastBinaryVerified = false;
                    pasted.activeIEnums.clear();
                    obj.scripts.insert(obj.scripts.begin() + static_cast<long>(i + 1), pasted);
                    insertInspectorKeyAfter(inspectorComponentKey, MakeInspectorScriptComponentKey(pasted.inspectorId));
                    scriptsChanged = true;
                },
                [&]() {
                    const int inspectorId = sc.inspectorId;
                    ScriptComponent pasted = inspectorClipboard.script;
                    pasted.inspectorId = inspectorId;
                    pasted.lastBinaryPath.clear();
                    pasted.lastBinaryVerified = false;
                    pasted.activeIEnums.clear();
                    sc = std::move(pasted);
                    scriptsChanged = true;
                });
            ImGui::Separator();
            if (ImGui::MenuItem("Remove Component")) {
                scriptToRemove = static_cast<int>(i);
            }
        }, iconScript);
        if (header.enabledChanged) {
            scriptsChanged = true;
        }

        if (scriptToRemove == static_cast<int>(i)) {
            ImGui::PopID();
            continue;
        }

        if (header.open) {
            /*
            ImGui::SeparatorText("Binding");
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 6.0f));
            if (ImGui::BeginTable("ScriptMeta", 2, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoPadOuterX)) {
                ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 112.0f);
                ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

                auto beginMetaRow = [&](const char* label) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextDisabled("%s", label);
                    ImGui::TableSetColumnIndex(1);
                };

                const char* languageLabels[] = {"C++", "C", "C#"};
                int languageIndex = 0;
                if (sc.language == ScriptLanguage::C) {
                    languageIndex = 1;
                } else if (sc.language == ScriptLanguage::CSharp) {
                    languageIndex = 2;
                }

                beginMetaRow("Language");
                ImGui::SetNextItemWidth(160.0f);
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
                beginMetaRow(sc.language == ScriptLanguage::CSharp ? "Assembly Path" : "Path");
                const bool hasFileSelection = !fileBrowser.selectedFile.empty() && fs::exists(fileBrowser.selectedFile);
                bool canUseSelection = false;
                if (hasFileSelection) {
                    fs::directory_entry entry(fileBrowser.selectedFile);
                    if (isNativeScriptLanguage(sc.language)) {
                        canUseSelection = isNativeScriptSourcePath(entry.path());
                    } else {
                        std::string ext = entry.path().extension().string();
                        std::transform(ext.begin(), ext.end(), ext.begin(),
                                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                        canUseSelection = (ext == ".dll" || ext == ".cs");
                    }
                }
                const float useSelectionWidth = 112.0f;
                ImGui::SetNextItemWidth(-useSelectionWidth);
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
                ImGui::BeginDisabled(!canUseSelection);
                if (ImGui::SmallButton("Use Selection")) {
                    fs::directory_entry entry(fileBrowser.selectedFile);
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
                ImGui::EndDisabled();

                if (sc.language == ScriptLanguage::CSharp) {
                    char typeBuf[256] = {};
                    std::snprintf(typeBuf, sizeof(typeBuf), "%s", sc.managedType.c_str());
                    beginMetaRow("Managed Type");
                    ImGui::SetNextItemWidth(-1.0f);
                    if (ImGui::InputText("##ScriptType", typeBuf, sizeof(typeBuf))) {
                        sc.managedType = typeBuf;
                        scriptsChanged = true;
                    }
                }

                ImGui::EndTable();
            }
            ImGui::PopStyleVar();

            ImGui::SeparatorText("Preview");
            if (!sc.path.empty()) {
                ImGui::TextDisabled("%s", fs::path(sc.path).filename().string().c_str());
            } else {
                ImGui::TextDisabled("Assign a script asset to expose runtime inspector fields.");
            }
            */

            if (!sc.path.empty()) {
                ScriptContext ctx;
                ctx.engine = this;
                ctx.object = &obj;
                ctx.script = &sc;
                std::string inspectorId = "ScriptInspector##" + std::to_string(obj.id) + sc.path;
                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 6.0f));
                ImGui::Indent(8.0f);
                if (isPlaying || specMode || testMode) {
                    ImGui::TextDisabled("Custom script preview is paused while the scene is running.");
                } else if (isNativeScriptLanguage(sc.language)) {
                    fs::path binary;
                    const bool attachedBinaryDirectly = IsNativeBinaryPath(fs::path(sc.path));
                    if (!attachedBinaryDirectly && (!sc.lastBinaryVerified || sc.lastBinaryPath.empty())) {
                        binary = resolveScriptBinary(sc.path);
                        if (!binary.empty()) {
                            sc.lastBinaryPath = binary.string();
                        }
                    }
                    if (binary.empty() && !sc.lastBinaryPath.empty()) {
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
                ImGui::Unindent(8.0f);
                ImGui::PopStyleVar();
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
        break;
    }
    }

    if (scriptToRemove >= 0 && scriptToRemove < static_cast<int>(obj.scripts.size())) {
        obj.scripts.erase(obj.scripts.begin() + scriptToRemove);
        scriptsChanged = true;
    }
    }

    if (obj.hasRenderer && sharedRenderer) {
        int selectedMaterialSlot = 0;
        auto selectedSlotIt = selectedRendererMaterialSlots.find(obj.id);
        if (selectedSlotIt != selectedRendererMaterialSlots.end()) {
            selectedMaterialSlot = selectedSlotIt->second;
        }
        selectedMaterialSlot = std::clamp(
            selectedMaterialSlot,
            0,
            std::max(0, static_cast<int>(obj.additionalMaterialPaths.size()))
        );
        std::string matLine = "Material: Slot " + std::to_string(selectedMaterialSlot);
        if (selectedMaterialSlot == 0) {
            matLine += obj.materialPath.empty()
                ? " (Embedded)"
                : " - " + fs::path(obj.materialPath).filename().string();
        } else {
            const size_t slotIndex = static_cast<size_t>(selectedMaterialSlot - 1);
            if (slotIndex < obj.additionalMaterialPaths.size() &&
                !obj.additionalMaterialPaths[slotIndex].empty()) {
                matLine += " - " + fs::path(obj.additionalMaterialPaths[slotIndex]).filename().string();
            }
        }
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
        const bool usesUIOnly2DPhysics = UsesUIOnly2DPhysics(obj);
        const bool supports3DWorldComponents = !usesUIOnly2DPhysics;
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

        auto trimComponentFilter = [](const std::string& value) {
            size_t start = 0;
            while (start < value.size() &&
                   std::isspace(static_cast<unsigned char>(value[start])) != 0) {
                ++start;
            }
            size_t end = value.size();
            while (end > start &&
                   std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
                --end;
            }
            return value.substr(start, end - start);
        };

        const std::string requestedScriptName = trimComponentFilter(componentFilter);
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

        addEntry("Physics/Rigidbody 3D", !obj.hasRigidbody && supports3DWorldComponents, [&]() {
            obj.hasRigidbody = true;
            obj.rigidbody = RigidbodyComponent{};
            componentChanged = true;
        });
        addEntry("Physics/Rigidbody 2D", !obj.hasRigidbody2D && usesUIOnly2DPhysics, [&]() {
            obj.hasRigidbody2D = true;
            obj.rigidbody2D = Rigidbody2DComponent{};
            componentChanged = true;
        });
        addEntry("Physics/Collider 2D", !obj.hasCollider2D && usesUIOnly2DPhysics, [&]() {
            obj.hasCollider2D = true;
            obj.collider2D = Collider2DComponent{};
            obj.collider2D.boxSize = glm::max(obj.ui.size, glm::vec2(1.0f));
            componentChanged = true;
        });
        addEntry("Physics/Parallax Layer 2D", !obj.hasParallaxLayer2D && usesUIOnly2DPhysics, [&]() {
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
            obj.rigidbody.lockRotationX = true;
            obj.rigidbody.lockRotationY = false;
            obj.rigidbody.lockRotationZ = true;
            obj.scale = glm::vec3(obj.playerController.radius * 2.0f, obj.playerController.height, obj.playerController.radius * 2.0f);
            syncLocalTransform(obj);
            componentChanged = true;
        });
        addEntry("Audio/Audio Source", !obj.hasAudioSource, [&]() {
            obj.hasAudioSource = true;
            obj.audioSource = AudioSourceComponent{};
            componentChanged = true;
        });
        addEntry("Rendering/Video Player", !obj.hasVideoPlayer && HasRendererComponent(obj), [&]() {
            obj.hasVideoPlayer = true;
            obj.videoPlayer = VideoPlayerComponent{};
            componentChanged = true;
        });
        addEntry("Audio/Reverb Zone", !obj.hasReverbZone && supports3DWorldComponents, [&]() {
            obj.hasReverbZone = true;
            obj.reverbZone = ReverbZoneComponent{};
            obj.reverbZone.boxSize = glm::max(obj.scale, glm::vec3(1.0f));
            componentChanged = true;
        });
        addEntry("AI Pathfinding/GroundBakedType", !obj.hasGroundBakedType && supports3DWorldComponents, [&]() {
            obj.hasGroundBakedType = true;
            obj.groundBakedType = GroundBakedTypeComponent{};
            showAIPathfindingWindow = true;
            componentChanged = true;
        });
        addEntry("AI Pathfinding/ObsticleObject", !obj.hasObsticleObject && supports3DWorldComponents, [&]() {
            obj.hasObsticleObject = true;
            obj.obsticleObject = ObsticleObjectComponent{};
            showAIPathfindingWindow = true;
            componentChanged = true;
        });
        addEntry("AI Pathfinding/AI Agent", !obj.hasAIAgent && supports3DWorldComponents, [&]() {
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
        /*
        addEntry("Scripting/Empty Script Component", true, [&]() {
            obj.scripts.push_back(ScriptComponent{});
            scriptsChanged = true;
            componentChanged = true;
        });
        */

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

            fs::path outDir;
            ScriptBuildConfig config;
            std::string error;
            fs::path cfgPath = resolveScriptsConfigPath(projectManager.currentProject);
            if (scriptCompiler.loadConfig(cfgPath, config, error)) {
                outDir = config.outDir;
            }

            std::vector<fs::path> scriptRoots = {
                config.scriptsDir,
                projectManager.currentProject.projectPath / "Scripts",
                projectManager.currentProject.projectPath / "Assets" / "Scripts"
            };
            std::unordered_set<std::string> seenRoots;
            std::error_code ec;
            for (const auto& root : scriptRoots) {
                fs::path resolvedRoot = root;
                if (resolvedRoot.empty()) {
                    continue;
                }
                if (!resolvedRoot.is_absolute()) {
                    resolvedRoot = projectManager.currentProject.projectPath / resolvedRoot;
                }
                std::error_code rootEc;
                fs::path normalizedRoot = fs::weakly_canonical(resolvedRoot, rootEc);
                if (rootEc) {
                    normalizedRoot = resolvedRoot.lexically_normal();
                }
                std::string rootKey = normalizedRoot.lexically_normal().string();
                if (!seenRoots.insert(rootKey).second) {
                    continue;
                }
                if (!fs::exists(normalizedRoot, ec) || !fs::is_directory(normalizedRoot, ec)) {
                    continue;
                }
                for (auto it = fs::recursive_directory_iterator(normalizedRoot, ec);
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
                    if (it->is_directory()) {
                        const std::string dirName = it->path().filename().string();
                        if (dirName == ".loaded" || dirName == ".staging") {
                            it.disable_recursion_pending();
                        }
                        continue;
                    }
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
                    sc.managedType = InferManagedTypeFromFile(path).value_or(path.stem().string());
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

        auto createAndAttachScript = [&](ScriptScaffoldKind kind) {
            fs::path createdPath;
            ScriptLanguage language = ScriptLanguage::Cpp;
            std::string managedType;
            std::string createError;
            if (!createScriptAsset(kind, requestedScriptName, {}, createdPath, language, managedType, createError)) {
                addConsoleMessage("Create script failed: " + createError, ConsoleMessageType::Error);
                return;
            }

            ScriptComponent sc;
            sc.language = language;
            sc.path = createdPath.string();
            sc.lastBinaryPath.clear();
            sc.lastBinaryVerified = false;
            if (language == ScriptLanguage::CSharp) {
                sc.managedType = !managedType.empty()
                    ? managedType
                    : InferManagedTypeFromFile(createdPath).value_or(createdPath.stem().string());
            }

            obj.scripts.push_back(std::move(sc));
            scriptsChanged = true;
            componentChanged = true;
            cachedScriptSources.clear();
            cachedScriptBinaries.clear();
            cachedScriptRoot.clear();
            cachedScriptRefresh = 0.0;
            addConsoleMessage("Created and attached script: " + createdPath.string(), ConsoleMessageType::Success);
            ImGui::CloseCurrentPopup();
            openScriptInEditor(createdPath);
        };

        ImGui::Spacing();
        ImGui::TextDisabled("%s", filterLower.empty() ? "Browse categories" : "Search results");
        ImVec2 listSize(ImGui::GetContentRegionAvail().x, 260.0f);
        if (ImGui::BeginChild("ComponentList", listSize, true)) {
            if (filteredEntries.empty()) {
                if (!requestedScriptName.empty()) {
                    ImGui::TextWrapped("No components matched \"%s\".", requestedScriptName.c_str());
                    ImGui::Spacing();
                    ImGui::TextDisabled("Create and attach a script instead:");
                    const float spacing = ImGui::GetStyle().ItemSpacing.x;
                    const float buttonWidth = std::max(110.0f, (ImGui::GetContentRegionAvail().x - spacing) * 0.5f);
                    if (ImGui::Button("ModuCPP", ImVec2(buttonWidth, 0.0f))) {
                        createAndAttachScript(ScriptScaffoldKind::ModuCpp);
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("C++", ImVec2(buttonWidth, 0.0f))) {
                        createAndAttachScript(ScriptScaffoldKind::Cpp);
                    }
                    if (ImGui::Button("C", ImVec2(buttonWidth, 0.0f))) {
                        createAndAttachScript(ScriptScaffoldKind::C);
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("C#", ImVec2(buttonWidth, 0.0f))) {
                        createAndAttachScript(ScriptScaffoldKind::CSharp);
                    }
                } else {
                    ImGui::TextDisabled("No components match the filter.");
                }
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

    if (obj.hasRenderer && sharedRenderer) {
        ImGui::Spacing();
        const int slotCount = 1 + static_cast<int>(obj.additionalMaterialPaths.size());
        int selectedMaterialSlot = selectedRendererMaterialSlots[obj.id];
        selectedMaterialSlot = std::clamp(selectedMaterialSlot, 0, std::max(0, slotCount - 1));
        selectedRendererMaterialSlots[obj.id] = selectedMaterialSlot;
        const bool editingPrimaryMaterial = (selectedMaterialSlot == 0);
        const size_t selectedAdditionalSlotIndex = editingPrimaryMaterial
            ? 0
            : static_cast<size_t>(selectedMaterialSlot - 1);

        if (!editingPrimaryMaterial && selectedAdditionalSlotIndex < obj.additionalMaterialPaths.size()) {
            const std::string& slotPath = obj.additionalMaterialPaths[selectedAdditionalSlotIndex];
            if (slotPath != slotMaterialInspectorPath) {
                slotMaterialInspectorValid = loadMaterialData(
                    slotPath,
                    slotInspectedMaterial,
                    slotInspectedAlbedo,
                    slotInspectedOverlay,
                    slotInspectedNormal,
                    slotInspectedUseOverlay,
                    &slotInspectedShaderPack,
                    &slotInspectedVertShader,
                    &slotInspectedFragShader
                );
                slotMaterialInspectorPath = slotPath;
            }
        }

        std::string materialHeaderName = "Material";
        const MaterialProperties* headerMaterial = &obj.material;
        const std::string* headerAlbedoPath = &obj.albedoTexturePath;
        const std::string* headerOverlayPath = &obj.overlayTexturePath;
        const std::string* headerNormalPath = &obj.normalMapPath;
        bool headerUseOverlay = obj.useOverlay;

        if (editingPrimaryMaterial) {
            if (!obj.materialPath.empty()) {
                materialHeaderName = assetDisplayName(obj.materialPath, "Material");
            }
        } else if (selectedAdditionalSlotIndex < obj.additionalMaterialPaths.size()) {
            const std::string& slotPath = obj.additionalMaterialPaths[selectedAdditionalSlotIndex];
            if (!slotPath.empty()) {
                materialHeaderName = assetDisplayName(slotPath, "Material");
            }
            if (slotMaterialInspectorValid && slotMaterialInspectorPath == slotPath) {
                headerMaterial = &slotInspectedMaterial;
                headerAlbedoPath = &slotInspectedAlbedo;
                headerOverlayPath = &slotInspectedOverlay;
                headerNormalPath = &slotInspectedNormal;
                headerUseOverlay = slotInspectedUseOverlay;
            }
        }

        auto materialHeaderState = drawMaterialSectionHeader("RendererMaterialSection",
                                                             materialHeaderName,
                                                             editingPrimaryMaterial ? obj.shaderPackPath : slotInspectedShaderPack,
                                                             editingPrimaryMaterial ? obj.vertexShaderPath : slotInspectedVertShader,
                                                             editingPrimaryMaterial ? obj.fragmentShaderPath : slotInspectedFragShader,
                                                             *headerMaterial,
                                                             *headerAlbedoPath,
                                                             *headerOverlayPath,
                                                             *headerNormalPath,
                                                             headerUseOverlay,
                                                             editingPrimaryMaterial || slotMaterialInspectorValid,
                                                             1100 + selectedMaterialSlot);
        if (materialHeaderState.second) {
            rendererSectionChanged = true;
            projectManager.currentProject.hasUnsavedChanges = true;
            if (!editingPrimaryMaterial) {
                slotMaterialInspectorValid = true;
            }
        }

        if (materialHeaderState.first) {
            if (selectedMaterialSlot == 0) {
                const bool primaryMaterialChanged = renderMaterialEditorBody(
                    "ObjectMaterial",
                    obj.material,
                    obj.albedoTexturePath,
                    obj.overlayTexturePath,
                    obj.normalMapPath,
                    obj.useOverlay,
                    obj.shaderPackPath,
                    obj.vertexShaderPath,
                    obj.fragmentShaderPath,
                    &obj,
                    objectMaterialPreviewScale,
                    1002
                );

                const bool hasMaterialAsset = !obj.materialPath.empty();
                ImGui::BeginDisabled(!hasMaterialAsset);
                if (ImGui::Button("Reload Material")) {
                    loadMaterialFromFile(obj);
                    rendererSectionChanged = true;
                }
                ImGui::SameLine();
                if (ImGui::Button("Save Material")) {
                    saveMaterialToFile(obj);
                }
                ImGui::EndDisabled();

                if (primaryMaterialChanged) {
                    rendererSectionChanged = true;
                    projectManager.currentProject.hasUnsavedChanges = true;
                }
            } else {
                const size_t slotIndex = static_cast<size_t>(selectedMaterialSlot - 1);
                if (slotIndex >= obj.additionalMaterialPaths.size()) {
                    ImGui::TextDisabled("Selected material slot is no longer available.");
                } else {
                    std::string& slotPath = obj.additionalMaterialPaths[slotIndex];
                    if (slotPath.empty()) {
                        ImGui::TextDisabled("Assign a material asset to this slot in Renderer to edit it here.");
                    } else {
                        if (!slotMaterialInspectorValid) {
                            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                                               "Failed to read material file for this slot.");
                        } else {
                            const bool slotMaterialChanged = renderMaterialEditorBody(
                                "SlotMaterial",
                                slotInspectedMaterial,
                                slotInspectedAlbedo,
                                slotInspectedOverlay,
                                slotInspectedNormal,
                                slotInspectedUseOverlay,
                                slotInspectedShaderPack,
                                slotInspectedVertShader,
                                slotInspectedFragShader,
                                nullptr,
                                objectMaterialPreviewScale,
                                1003
                            );

                            if (ImGui::Button("Reload Material")) {
                                slotMaterialInspectorValid = loadMaterialData(
                                    slotPath,
                                    slotInspectedMaterial,
                                    slotInspectedAlbedo,
                                    slotInspectedOverlay,
                                    slotInspectedNormal,
                                    slotInspectedUseOverlay,
                                    &slotInspectedShaderPack,
                                    &slotInspectedVertShader,
                                    &slotInspectedFragShader
                                );
                                slotMaterialInspectorPath = slotPath;
                            }
                            ImGui::SameLine();
                            if (ImGui::Button("Save Material")) {
                                if (saveMaterialData(
                                        slotPath,
                                        slotInspectedMaterial,
                                        slotInspectedAlbedo,
                                        slotInspectedOverlay,
                                        slotInspectedNormal,
                                        slotInspectedUseOverlay,
                                        slotInspectedShaderPack,
                                        slotInspectedVertShader,
                                        slotInspectedFragShader))
                                {
                                    addConsoleMessage("Saved material: " + slotPath, ConsoleMessageType::Success);
                                } else {
                                    addConsoleMessage("Failed to save material: " + slotPath,
                                                      ConsoleMessageType::Error);
                                }
                            }

                            if (slotMaterialChanged) {
                                slotMaterialInspectorValid = true;
                            }
                        }
                    }
                }
            }
        }
    }

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
                    target.rigidbody.lockRotationX = true;
                    target.rigidbody.lockRotationY = false;
                    target.rigidbody.lockRotationZ = true;
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
        if (videoPlayerSectionChanged && sharedVideoPlayer) {
            forEachSecondarySelected([&](SceneObject& target) {
                target.hasVideoPlayer = obj.hasVideoPlayer;
                target.videoPlayer = obj.videoPlayer;
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
                target.shaderPackPath = obj.shaderPackPath;
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
    if (scriptsChanged || componentChanged || inspectorOrderChanged) {
        EnsureInspectorComponentMetadata(obj);
    }
    if (inspectorOrderChanged) {
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
    ImGui::PopStyleVar(3);
    ImGui::End();
}

#pragma endregion

#pragma region Console Panel
void Engine::renderConsolePanel() {
    const ImGuiViewport* mainViewport = ImGui::GetMainViewport();
    if (!mainViewport) return;
    const EditorChromeMetrics& chrome = getEditorChromeMetrics(uiChromeScale);

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

    const float margin = chrome.consoleMargin;
    const ImVec2 tabSize = chrome.consoleTabSize;
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

    ImVec2 miniSize(ImMin(chrome.consoleMiniSize.x, anchorMax.x - anchorMin.x - margin * 2.0f),
                    ImMin(chrome.consoleMiniSize.y, anchorMax.y - anchorMin.y - tabSize.y - margin * 3.0f));
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
            ImGui::Dummy(ImVec2(0.0f, 1.0f));
            ImGui::PopID();
        }
    }

    if (shouldScroll) {
        ImGui::SetScrollY(ImGui::GetScrollMaxY());
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
    const EditorChromeMetrics& chrome = getEditorChromeMetrics(uiChromeScale);

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
            case ConsoleMessageType::Warning: return ImVec4(0.22f, 0.19f, 0.10f, 0.86f);
            case ConsoleMessageType::Error: return ImVec4(0.22f, 0.12f, 0.12f, 0.86f);
            case ConsoleMessageType::Success: return ImVec4(0.12f, 0.20f, 0.14f, 0.84f);
            case ConsoleMessageType::Info:
            default:
                return ImVec4(0.12f, 0.16f, 0.20f, 0.84f);
        }
    };

    auto typeBorderColor = [](ConsoleMessageType type) -> ImVec4 {
        switch (type) {
            case ConsoleMessageType::Warning: return ImVec4(0.82f, 0.66f, 0.26f, 0.62f);
            case ConsoleMessageType::Error: return ImVec4(0.86f, 0.32f, 0.32f, 0.64f);
            case ConsoleMessageType::Success: return ImVec4(0.30f, 0.72f, 0.42f, 0.60f);
            case ConsoleMessageType::Info:
            default:
                return ImVec4(0.34f, 0.54f, 0.74f, 0.56f);
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

    const float reserveHeight = chrome.bottomReserveHeight;
    const float barHeight = ImMax(16.0f, reserveHeight);
    const float stripTop = hostPos.y + hostSize.y - reserveHeight;

    ImVec2 barMin(hostPos.x, stripTop);
    ImVec2 barMax(hostPos.x + hostSize.x, barMin.y + barHeight);
    if (barMax.x - barMin.x <= 40.0f || barMax.y - barMin.y <= 8.0f) return;

    ImDrawList* draw = ImGui::GetForegroundDrawList(const_cast<ImGuiViewport*>(viewport));
    if (!draw) return;

    const ImU32 bgU32 = ImGui::ColorConvertFloat4ToU32(bgColor);
    const ImU32 borderU32 = ImGui::ColorConvertFloat4ToU32(borderColor);
    const ImU32 accentU32 = ImGui::ColorConvertFloat4ToU32(accentColor);
    const ImU32 textU32 = ImGui::GetColorU32(ImGuiCol_Text);

    draw->AddRectFilled(barMin, barMax, bgU32, 0.0f);
    draw->AddLine(ImVec2(barMin.x, barMin.y), ImVec2(barMax.x, barMin.y), borderU32, 1.0f);

    float cursorX = barMin.x + chrome.consoleMargin * 0.5f;
    const float fontY = barMin.y + (barHeight - ImGui::GetFontSize()) * 0.5f;

    if (icon && icon->GetID()) {
        const float iconSize = ImClamp(barHeight - 6.0f, 10.0f, 16.0f);
        const ImVec2 iconMin(cursorX, barMin.y + (barHeight - iconSize) * 0.5f);
        const ImVec2 iconMax(iconMin.x + iconSize, iconMin.y + iconSize);
        draw->AddImage((ImTextureID)(intptr_t)icon->GetID(), iconMin, iconMax,
                       ImVec2(0, 1), ImVec2(1, 0), IM_COL32_WHITE);
        cursorX += iconSize + 6.0f;
    }

    const char* label = latest ? typeLabel(latestType) : "Status";
    std::string labelText = std::string(label) + ":";
    draw->AddText(ImVec2(cursorX, fontY), accentU32, labelText.c_str());
    cursorX += ImGui::CalcTextSize(labelText.c_str()).x + chrome.consoleMargin * 0.5f;

    draw->PushClipRect(ImVec2(cursorX, barMin.y + 1.0f), ImVec2(barMax.x - chrome.consoleMargin * 0.5f, barMax.y - 1.0f), true);
    draw->AddText(ImVec2(cursorX, fontY), textU32, bodyText.c_str());
    draw->PopClipRect();
}

void Engine::renderEditorToast() {
    if (!editorToast.visible || editorToast.message.empty()) {
        return;
    }

    const double now = glfwGetTime();
    const float appearSeconds = 0.18f;
    const float disappearSeconds = 0.22f;
    const double totalLifetime = appearSeconds + editorToast.holdSeconds + disappearSeconds;
    const double elapsed = now - editorToast.startTime;
    if (elapsed >= totalLifetime) {
        editorToast.visible = false;
        editorToast.message.clear();
        return;
    }

    auto smoothstep = [](float t) {
        t = std::clamp(t, 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    };

    const float appearT = smoothstep(static_cast<float>(elapsed / appearSeconds));
    const float disappearT = smoothstep(static_cast<float>((elapsed - appearSeconds - editorToast.holdSeconds) /
                                                           disappearSeconds));
    const float visibility = appearT * (1.0f - disappearT);
    if (visibility <= 0.001f) {
        return;
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    if (!viewport) {
        return;
    }

    auto accentColor = [](ConsoleMessageType type) -> ImVec4 {
        switch (type) {
            case ConsoleMessageType::Warning: return ImVec4(0.98f, 0.82f, 0.32f, 1.0f);
            case ConsoleMessageType::Error: return ImVec4(0.98f, 0.46f, 0.46f, 1.0f);
            case ConsoleMessageType::Success: return ImVec4(0.47f, 0.91f, 0.57f, 1.0f);
            case ConsoleMessageType::Info:
            default: return ImVec4(0.70f, 0.88f, 1.0f, 1.0f);
        }
    };

    auto backgroundColor = [](ConsoleMessageType type) -> ImVec4 {
        switch (type) {
            case ConsoleMessageType::Warning: return ImVec4(0.20f, 0.15f, 0.05f, 0.94f);
            case ConsoleMessageType::Error: return ImVec4(0.24f, 0.08f, 0.08f, 0.94f);
            case ConsoleMessageType::Success: return ImVec4(0.08f, 0.19f, 0.10f, 0.94f);
            case ConsoleMessageType::Info:
            default: return ImVec4(0.08f, 0.16f, 0.22f, 0.94f);
        }
    };

    auto borderColor = [](ConsoleMessageType type) -> ImVec4 {
        switch (type) {
            case ConsoleMessageType::Warning: return ImVec4(0.80f, 0.62f, 0.18f, 0.90f);
            case ConsoleMessageType::Error: return ImVec4(0.84f, 0.24f, 0.24f, 0.92f);
            case ConsoleMessageType::Success: return ImVec4(0.23f, 0.72f, 0.35f, 0.92f);
            case ConsoleMessageType::Info:
            default: return ImVec4(0.30f, 0.55f, 0.76f, 0.90f);
        }
    };

    ImDrawList* draw = ImGui::GetForegroundDrawList(const_cast<ImGuiViewport*>(viewport));
    if (!draw) {
        return;
    }

    ImFont* font = ImGui::GetFont();
    if (!font) {
        return;
    }

    const float scale = 0.86f + 0.14f * appearT - 0.12f * disappearT;
    const float fontSize = ImGui::GetFontSize() * scale;
    const ImVec2 textSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, editorToast.message.c_str());
    const float padX = 16.0f * scale;
    const float padY = 10.0f * scale;
    const float width = textSize.x + padX * 2.0f;
    const float height = textSize.y + padY * 2.0f;
    const float slideY = (1.0f - appearT) * 26.0f + disappearT * 22.0f;

    const float centerX = viewport->WorkPos.x + viewport->WorkSize.x * 0.5f;
    const float bottomY = viewport->WorkPos.y + viewport->WorkSize.y - getEditorBottomStatusReserveHeight(uiChromeScale) - 16.0f;
    const ImVec2 min(centerX - width * 0.5f, bottomY - height - slideY);
    const ImVec2 max(min.x + width, min.y + height);

    ImVec4 bg = backgroundColor(editorToast.type);
    ImVec4 border = borderColor(editorToast.type);
    ImVec4 accent = accentColor(editorToast.type);
    bg.w *= visibility;
    border.w *= visibility;
    accent.w *= visibility;
    ImVec4 textColor = ImVec4(0.96f, 0.98f, 1.0f, visibility);

    draw->AddRectFilled(ImVec2(min.x + 1.0f, min.y + 3.0f),
                        ImVec2(max.x + 1.0f, max.y + 3.0f),
                        ImGui::GetColorU32(ImVec4(0.0f, 0.0f, 0.0f, 0.20f * visibility)),
                        12.0f);
    draw->AddRectFilled(min, max, ImGui::GetColorU32(bg), 12.0f);
    draw->AddRect(min, max, ImGui::GetColorU32(border), 12.0f, 0, 1.5f);
    draw->AddRectFilled(min,
                        ImVec2(min.x + ImMax(4.0f * scale, 3.0f), max.y),
                        ImGui::GetColorU32(accent),
                        12.0f,
                        ImDrawFlags_RoundCornersLeft);
    draw->AddText(font,
                  fontSize,
                  ImVec2(min.x + padX, min.y + padY),
                  ImGui::GetColorU32(textColor),
                  editorToast.message.c_str());
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
        if (!fileBrowser.selectedFile.empty() && IsRawMeshPath(fileBrowser.selectedFile)) {
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
    ImGui::SameLine();
    if (ImGui::Button("Flip Faces")) {
        meshBuilder.flipFaces();
        addConsoleMessage("Flipped raw mesh face winding", ConsoleMessageType::Success);
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
void Engine::renderLegacySceneLayoutModal() {
    if (!showLegacySceneLayoutDialog) {
        legacySceneLayoutDialogOpened = false;
        return;
    }

    if (!legacySceneLayoutDialogOpened) {
        ImGui::OpenPopup("Old Scene Layout Detected");
        legacySceneLayoutDialogOpened = true;
    }

    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
                            ImGuiCond_Appearing,
                            ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(520.0f, 220.0f), ImGuiCond_Appearing);

    if (ImGui::BeginPopupModal("Old Scene Layout Detected", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoDocking)) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
        ImGui::TextUnformatted("Hey! This is an old scene with an old layout.");
        ImGui::Spacing();
        ImGui::TextUnformatted("Would you like to move this current old scene into a compatibility folder and convert this scene to the new layout?");
        ImGui::PopTextWrapPos();

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        const float buttonWidth = 150.0f;
        if (ImGui::Button("Convert Scene", ImVec2(buttonWidth, 0.0f))) {
            legacySceneSaveChoice = LegacySceneSaveChoice::SaveModular;
            if (executeSceneSave(pendingSceneSaveRequest.destinationSceneName,
                                 SceneSerializer::SavePreference::PreferModular,
                                 true)) {
                showLegacySceneLayoutDialog = false;
                legacySceneLayoutDialogOpened = false;
                ImGui::CloseCurrentPopup();
                continuePendingScenePostAction();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Keep Old Layout For Now", ImVec2(210.0f, 0.0f))) {
            legacySceneSaveChoice = LegacySceneSaveChoice::KeepLegacy;
            if (executeSceneSave(pendingSceneSaveRequest.destinationSceneName,
                                 SceneSerializer::SavePreference::ForceLegacyFlat,
                                 false)) {
                showLegacySceneLayoutDialog = false;
                legacySceneLayoutDialogOpened = false;
                ImGui::CloseCurrentPopup();
                continuePendingScenePostAction();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f))) {
            showLegacySceneLayoutDialog = false;
            legacySceneLayoutDialogOpened = false;
            resetPendingSceneSaveRequest();
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

void Engine::renderDialogs() {
    renderLegacySceneLayoutModal();

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
        struct CompileWindowIcon {
            ImTextureID id = static_cast<ImTextureID>(0);
            bool flipY = false;
        };
        const bool hasVulkanUiImages = usingVulkan() && vulkanRendererInitialized && (vulkanRenderer != nullptr);
        auto resolveCompileWindowIcon = [&](const char* iconPath) -> CompileWindowIcon {
            if (!iconPath || !*iconPath) {
                return {};
            }
            if (rendererInitialized) {
                if (Texture* icon = renderer.getTexture(iconPath, MaterialProperties::TextureFilter::Point);
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
        auto getCompileStateIcon = [&](bool success, bool warning) -> CompileWindowIcon {
            if (!success) {
                return resolveCompileWindowIcon("Resources/Engine-Root/Compiler window/Script Failed.png");
            }
            if (warning) {
                return resolveCompileWindowIcon("Resources/Engine-Root/Compiler window/Script Warning.png");
            }
            return resolveCompileWindowIcon("Resources/Engine-Root/Compiler window/Script Completed.png");
        };

        const double now = glfwGetTime();
        const int historyCount = static_cast<int>(compileHistory.size());
        const int visibleHistoryRows = std::clamp(historyCount, 1, 5);
        const float historyHeight = static_cast<float>(visibleHistoryRows) * 20.0f + 2.0f;
        const bool compileFinished = !compileInProgress && compileCompletionStart > 0.0;
        const float completionBlend = compileFinished
            ? std::clamp(static_cast<float>((now - compileCompletionStart) / 0.24), 0.0f, 1.0f)
            : 0.0f;
        const float jobsBlend = compileInProgress ? 1.0f : std::max(0.0f, 1.0f - completionBlend);
        const float logBlend = compileFinished ? completionBlend : 0.0f;
        const float targetWidth = std::clamp(io.DisplaySize.x * 0.78f, 640.0f, 1600.0f);
        const float targetMinHeight = compileFinished && !lastCompileLog.empty() ? 320.0f : 180.0f;
        const float targetLogHeight =
            logBlend > 0.0f ? std::clamp(io.DisplaySize.y * 0.28f, 160.0f, 420.0f) * logBlend + 12.0f : 0.0f;
        const float targetHeight =
            std::clamp(96.0f + historyHeight * jobsBlend + targetLogHeight, targetMinHeight, io.DisplaySize.y * 0.72f);
        static ImVec2 compilePopupSize = ImVec2(targetWidth, targetHeight);
        const float popupLerp = std::clamp(io.DeltaTime * 12.0f, 0.0f, 1.0f);
        compilePopupSize.x = ImLerp(compilePopupSize.x, targetWidth, popupLerp);
        compilePopupSize.y = ImLerp(compilePopupSize.y, targetHeight, popupLerp);
        ImGui::SetNextWindowPos(center, compileInProgress ? ImGuiCond_Always : ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (compileInProgress) {
            ImGui::SetNextWindowSize(compilePopupSize, ImGuiCond_Always);
        } else {
            ImGui::SetNextWindowSize(compilePopupSize, ImGuiCond_Appearing);
            ImGui::SetNextWindowSizeConstraints(ImVec2(480.0f, 180.0f),
                                                ImVec2(io.DisplaySize.x * 0.96f, io.DisplaySize.y * 0.90f));
        }
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings;
        if (compileInProgress) flags |= ImGuiWindowFlags_NoResize;
        bool allowClose = !compileInProgress;
        if (ImGui::BeginPopupModal("Script Compile", allowClose ? &showCompilePopup : nullptr, flags)) {
            float progress = 1.0f;
            std::string stageText;
            {
                std::lock_guard<std::mutex> lock(compileMutex);
                progress = compileInProgress ? compileProgress : 1.0f;
                stageText = compileInProgress ? compileStage : (lastCompileSuccess ? "Done" : "Failed");
            }
            const char* stageLabel = stageText.empty() ? "Working..." : stageText.c_str();
            if (progress <= 0.0f) progress = 0.02f;

            ImGui::Spacing();
            ImGui::TextUnformatted(lastCompileStatus.empty() ? "Idle" : lastCompileStatus.c_str());
            if (!compileCurrentLabel.empty()) {
                ImGui::SameLine();
                ImGui::TextDisabled("%s", compileCurrentLabel.c_str());
                if (compileBatchTotal > 0) {
                    ImGui::SameLine();
                    ImGui::TextDisabled("%d/%d", std::min(compileBatchCompleted + 1, std::max(1, compileBatchTotal)), std::max(1, compileBatchTotal));
                }
            }
            if (compileInProgress) {
                ImGui::Spacing();
                ImGui::BufferingBar("##CompileLoadingBar",
                                    progress,
                                    ImVec2(ImGui::GetContentRegionAvail().x, 10.0f),
                                    ImGui::GetColorU32(ImVec4(0.20f, 0.24f, 0.32f, 1.0f)),
                                    ImGui::GetColorU32(ImVec4(0.90f, 0.72f, 0.14f, 1.0f)));
                ImGui::TextDisabled("%s", stageLabel);
                if (compileInProgress && !compileRequestQueue.empty()) {
                    ImGui::SameLine();
                    ImGui::TextDisabled("+%zu queued", compileRequestQueue.size());
                }
            } else if (compileFinished) {
                ImGui::Spacing();
                ImDrawList* barDraw = ImGui::GetWindowDrawList();
                ImVec2 barMin = ImGui::GetCursorScreenPos();
                ImVec2 barMax(barMin.x + ImGui::GetContentRegionAvail().x, barMin.y + 10.0f);
                barDraw->AddRectFilled(barMin, barMax, ImGui::GetColorU32(ImVec4(0.18f, 0.21f, 0.28f, 1.0f)), 4.0f);
                barDraw->AddRectFilled(barMin,
                                       ImVec2(barMin.x + (barMax.x - barMin.x) * completionBlend, barMax.y),
                                       ImGui::GetColorU32(ImVec4(0.28f, 0.72f, 0.40f, 1.0f)),
                                       4.0f);
                ImGui::Dummy(ImVec2(0.0f, 12.0f));
                ImGui::TextDisabled("%s", stageLabel);
            }

            if (compileInProgress || jobsBlend > 0.01f) {
                ImGui::Separator();
                ImGui::TextDisabled("Jobs");
                ImGui::BeginChild("CompileHistoryList", ImVec2(0.0f, std::max(20.0f, historyHeight * jobsBlend)), false);
                const float rowAlpha = compileInProgress ? 1.0f : jobsBlend;
                if (historyCount == 0 && !compileInProgress) {
                    ImGui::TextDisabled("No compile results yet.");
                } else {
                    if (compileInProgress) {
                        ImVec2 rowMin = ImGui::GetCursorScreenPos();
                        ImVec2 rowSize(ImGui::GetContentRegionAvail().x, 18.0f);
                        ImGui::InvisibleButton("##CompileActiveRow", rowSize);
                        ImDrawList* rowDraw = ImGui::GetWindowDrawList();
                        const CompileWindowIcon activeIcon = resolveCompileWindowIcon("Resources/Engine-Root/Compiler window/Script Warning.png");
                        if (activeIcon.id != static_cast<ImTextureID>(0)) {
                            const ImVec2 iconMin(rowMin.x + 2.0f, rowMin.y + 4.0f);
                            const ImVec2 iconMax(iconMin.x + 10.0f, iconMin.y + 10.0f);
                            const ImVec2 uvMin = activeIcon.flipY ? ImVec2(0.0f, 1.0f) : ImVec2(0.0f, 0.0f);
                            const ImVec2 uvMax = activeIcon.flipY ? ImVec2(1.0f, 0.0f) : ImVec2(1.0f, 1.0f);
                            rowDraw->AddImage(activeIcon.id, iconMin, iconMax, uvMin, uvMax, IM_COL32(255, 255, 255, static_cast<int>(200.0f * rowAlpha)));
                        }
                        rowDraw->AddText(ImVec2(rowMin.x + 16.0f, rowMin.y + 1.0f),
                                         ImGui::GetColorU32(ImVec4(0.95f, 0.97f, 1.0f, rowAlpha)),
                                         compileCurrentLabel.empty() ? "Current compile" : compileCurrentLabel.c_str());
                        const std::string runningStatus = lastCompileStatus.empty() ? "Working..." : lastCompileStatus;
                        const ImVec2 statusSize = ImGui::CalcTextSize(runningStatus.c_str());
                        rowDraw->AddText(ImVec2(rowMin.x + rowSize.x - statusSize.x - 4.0f, rowMin.y + 1.0f),
                                         ImGui::GetColorU32(ImVec4(0.78f, 0.90f, 1.0f, rowAlpha)),
                                         runningStatus.c_str());
                        rowDraw->AddLine(ImVec2(rowMin.x, rowMin.y + rowSize.y + 1.0f),
                                         ImVec2(rowMin.x + rowSize.x, rowMin.y + rowSize.y + 1.0f),
                                         ImGui::GetColorU32(ImVec4(0.24f, 0.27f, 0.34f, 0.80f * rowAlpha)));
                    }

                    for (size_t index = 0; index < compileHistory.size(); ++index) {
                        const auto& item = compileHistory[index];
                        const float fade = std::clamp(static_cast<float>((now - item.completedAt) / 0.22), 0.0f, 1.0f) * rowAlpha;
                        const float rowHeight = 18.0f;
                        ImVec2 rowMin = ImGui::GetCursorScreenPos();
                        ImVec2 rowSize(ImGui::GetContentRegionAvail().x, rowHeight);
                        std::string rowId = "##CompileHistoryRow" + std::to_string(index);
                        ImGui::InvisibleButton(rowId.c_str(), rowSize);
                        ImDrawList* rowDraw = ImGui::GetWindowDrawList();
                        const CompileWindowIcon icon = getCompileStateIcon(item.success, item.warning);
                        if (icon.id != static_cast<ImTextureID>(0)) {
                            const ImVec2 iconMin(rowMin.x + 2.0f, rowMin.y + 4.0f);
                            const ImVec2 iconMax(iconMin.x + 10.0f, iconMin.y + 10.0f);
                            const ImVec2 uvMin = icon.flipY ? ImVec2(0.0f, 1.0f) : ImVec2(0.0f, 0.0f);
                            const ImVec2 uvMax = icon.flipY ? ImVec2(1.0f, 0.0f) : ImVec2(1.0f, 1.0f);
                            rowDraw->AddImage(icon.id, iconMin, iconMax, uvMin, uvMax, IM_COL32(255, 255, 255, static_cast<int>(fade * 255.0f)));
                        }

                        const ImU32 titleColor = ImGui::GetColorU32(ImVec4(0.96f, 0.98f, 1.0f, fade));
                        rowDraw->AddText(ImVec2(rowMin.x + 16.0f, rowMin.y + 1.0f),
                                         titleColor,
                                         item.displayLabel.c_str());

                        const ImVec2 statusSize = ImGui::CalcTextSize(item.statusLabel.c_str());
                        const ImVec2 statusPos(rowMin.x + rowSize.x - statusSize.x - 4.0f, rowMin.y + 1.0f);
                        rowDraw->AddText(statusPos,
                                         item.success
                                             ? ImGui::GetColorU32(ImVec4(0.60f, 0.95f, 0.72f, fade))
                                             : ImGui::GetColorU32(ImVec4(0.98f, 0.62f, 0.62f, fade)),
                                         item.statusLabel.c_str());

                        rowDraw->AddLine(ImVec2(rowMin.x, rowMin.y + rowSize.y + 1.0f),
                                         ImVec2(rowMin.x + rowSize.x, rowMin.y + rowSize.y + 1.0f),
                                         ImGui::GetColorU32(ImVec4(0.24f, 0.27f, 0.34f, 0.80f * fade)));
                    }
                }
                ImGui::EndChild();
            }

            if (compileFinished && !lastCompileLog.empty()) {
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::TextDisabled("Log");
                const float closeRowReserve =
                    allowClose ? (ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y * 2.0f) : 8.0f;
                const float logHeight = std::max(140.0f, ImGui::GetContentRegionAvail().y - closeRowReserve);
                ImGui::BeginChild("CompileLog", ImVec2(0.0f, logHeight), true);
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextUnformatted(lastCompileLog.c_str());
                ImGui::PopTextWrapPos();
                ImGui::EndChild();
            }

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
                    requestSceneSave(saveSceneAsName,
                                     PendingScenePostAction::None,
                                     "",
                                     true);
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

            auto spriteSheetTargetLabel = [](SpriteSheetImportTarget target) {
                switch (target) {
                    case SpriteSheetImportTarget::Sprite25D: return "2.5D Sprite";
                    case SpriteSheetImportTarget::Sprite2D: return "Sprite2D";
                    case SpriteSheetImportTarget::UIImage:
                    default:
                        return "UI Image";
                }
            };

            if (!has2DWorldPackage() && importSpriteSheetTarget == SpriteSheetImportTarget::Sprite2D) {
                importSpriteSheetTarget = isProject25DPipeline()
                    ? SpriteSheetImportTarget::Sprite25D
                    : SpriteSheetImportTarget::UIImage;
            }

            if (ImGui::BeginCombo("Create As", spriteSheetTargetLabel(importSpriteSheetTarget))) {
                const bool imageSelected = importSpriteSheetTarget == SpriteSheetImportTarget::UIImage;
                if (ImGui::Selectable("UI Image", imageSelected)) {
                    importSpriteSheetTarget = SpriteSheetImportTarget::UIImage;
                }
                if (imageSelected) ImGui::SetItemDefaultFocus();

                const bool sprite25DSelected = importSpriteSheetTarget == SpriteSheetImportTarget::Sprite25D;
                if (ImGui::Selectable("2.5D Sprite", sprite25DSelected)) {
                    importSpriteSheetTarget = SpriteSheetImportTarget::Sprite25D;
                }

                if (has2DWorldPackage()) {
                    const bool sprite2DSelected = importSpriteSheetTarget == SpriteSheetImportTarget::Sprite2D;
                    if (ImGui::Selectable("Sprite2D", sprite2DSelected)) {
                        importSpriteSheetTarget = SpriteSheetImportTarget::Sprite2D;
                    }
                } else {
                    ImGui::BeginDisabled();
                    ImGui::Selectable("Sprite2D (requires moduengine.2d-world)", false);
                    ImGui::EndDisabled();
                }
                ImGui::EndCombo();
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

                ObjectType type = ObjectType::UIImage;
                switch (importSpriteSheetTarget) {
                    case SpriteSheetImportTarget::Sprite25D:
                        type = ObjectType::Sprite25D;
                        break;
                    case SpriteSheetImportTarget::Sprite2D:
                        type = ObjectType::Sprite2D;
                        break;
                    case SpriteSheetImportTarget::UIImage:
                    default:
                        type = ObjectType::UIImage;
                        break;
                }

                const bool needsCanvasParent =
                    (type == ObjectType::UIImage || type == ObjectType::Sprite2D);
                int canvasId = -1;
                if (needsCanvasParent) {
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
                    if (needsCanvasParent && canvasId >= 0) {
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
