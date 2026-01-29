#include "Engine.h"
#include "ModelLoader.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cfloat>
#include <cmath>
#include <cctype>
#include <functional>
#include <sstream>
#include <unordered_set>
#include <unordered_map>
#include <optional>
#include <future>
#include <chrono>
#include <future>

#ifdef _WIN32
#include <shlobj.h>
#endif

#pragma region Gizmo Toolbar
namespace GizmoToolbar {
    enum class Icon {
        Translate,
        Rotate,
        Scale,
        Bounds,
        Universal,
        Mesh,
        GizmoToggle,
        GridToggle,
        SnapToggle,
        LocalMode,
        WorldMode,
        UiWorldToggle
    };

    static ImVec4 ScaleColor(const ImVec4& c, float s) {
        return ImVec4(
            std::clamp(c.x * s, 0.0f, 1.0f),
            std::clamp(c.y * s, 0.0f, 1.0f),
            std::clamp(c.z * s, 0.0f, 1.0f),
            c.w
        );
    }
    
    static bool TextButton(const char* label, bool active, const ImVec2& size, ImU32 base, ImU32 hover, ImU32 activeCol, ImU32 accent, ImU32 textColor) {
        ImGui::PushStyleColor(ImGuiCol_Button, active ? accent : base);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, active ? accent : hover);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, active ? accent : activeCol);
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(textColor));
        bool pressed = ImGui::Button(label, size);
        ImGui::PopStyleColor(4);
        return pressed;
    }

    static void GetIconBounds(const ImVec2& min, const ImVec2& max, ImVec2& outMin, ImVec2& outMax) {
        float size = std::min(max.x - min.x, max.y - min.y);
        ImVec2 center = ImVec2((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
        outMin = ImVec2(center.x - size * 0.5f, center.y - size * 0.5f);
        outMax = ImVec2(center.x + size * 0.5f, center.y + size * 0.5f);
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
        bool shouldClose = closed || !stroke;
        if (shouldClose && current.size() > 2) {
            if (current.front().x != current.back().x || current.front().y != current.back().y) {
                current.push_back(current.front());
            }
        }
        SvgSubpath sub;
        sub.points = std::move(current);
        sub.closed = shouldClose;
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

    static float SvgArea(const std::vector<ImVec2>& pts) {
        if (pts.size() < 3) return 0.0f;
        float a = 0.0f;
        for (size_t i = 0; i + 1 < pts.size(); ++i) {
            a += pts[i].x * pts[i + 1].y - pts[i + 1].x * pts[i].y;
        }
        return a * 0.5f;
    }

    static bool SvgPointInTri(const ImVec2& p, const ImVec2& a, const ImVec2& b, const ImVec2& c) {
        float ab = (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
        float bc = (c.x - b.x) * (p.y - b.y) - (c.y - b.y) * (p.x - b.x);
        float ca = (a.x - c.x) * (p.y - c.y) - (a.y - c.y) * (p.x - c.x);
        bool hasNeg = (ab < 0.0f) || (bc < 0.0f) || (ca < 0.0f);
        bool hasPos = (ab > 0.0f) || (bc > 0.0f) || (ca > 0.0f);
        return !(hasNeg && hasPos);
    }

    static void SvgTriangulate(const std::vector<ImVec2>& pts, std::vector<ImVec2>& outTris) {
        outTris.clear();
        if (pts.size() < 3) return;

        std::vector<ImVec2> poly = pts;
        if (poly.front().x == poly.back().x && poly.front().y == poly.back().y) {
            poly.pop_back();
        }
        int n = static_cast<int>(poly.size());
        if (n < 3) return;

        std::vector<int> idx(n);
        float area = SvgArea(poly);
        if (area > 0.0f) {
            for (int i = 0; i < n; ++i) idx[i] = i;
        } else {
            for (int i = 0; i < n; ++i) idx[i] = n - 1 - i;
        }

        int guard = 0;
        while (idx.size() > 2 && guard < 10000) {
            bool earFound = false;
            int m = static_cast<int>(idx.size());
            for (int i = 0; i < m; ++i) {
                int i0 = idx[(i + m - 1) % m];
                int i1 = idx[i];
                int i2 = idx[(i + 1) % m];
                const ImVec2& a = poly[i0];
                const ImVec2& b = poly[i1];
                const ImVec2& c = poly[i2];

                float cross = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
                if (cross <= 0.0f) continue;

                bool anyInside = false;
                for (int j = 0; j < m; ++j) {
                    int ii = idx[j];
                    if (ii == i0 || ii == i1 || ii == i2) continue;
                    if (SvgPointInTri(poly[ii], a, b, c)) {
                        anyInside = true;
                        break;
                    }
                }
                if (anyInside) continue;

                outTris.push_back(a);
                outTris.push_back(b);
                outTris.push_back(c);
                idx.erase(idx.begin() + i);
                earFound = true;
                break;
            }
            if (!earFound) break;
            ++guard;
        }
    }

    static void BuildSvgIconCache(const SvgIconSpec& spec, SvgIconCache& cache) {
        if (cache.built) return;
        for (int i = 0; i < spec.pathCount; ++i) {
            ParseSvgPathData(spec.paths[i].d, cache.subpaths, spec.paths[i].stroke);
        }
        cache.built = true;
    }

    static ImVec2 SvgTransformPoint(const ImVec2& p, const ImVec2& min, const ImVec2& max, float viewW, float viewH, float scaleFactor) {
        float size = std::min(max.x - min.x, max.y - min.y) * scaleFactor;
        ImVec2 center = ImVec2((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
        float scale = size / std::max(viewW, viewH);
        ImVec2 offset = ImVec2(center.x - (viewW * scale) * 0.5f, center.y - (viewH * scale) * 0.5f);
        return ImVec2(offset.x + p.x * scale, offset.y + p.y * scale);
    }

    static void DrawSvgIcon(ImDrawList* drawList, const SvgIconSpec& spec, SvgIconCache& cache,
                            const ImVec2& min, const ImVec2& max, ImU32 color, float strokeScale, float scaleFactor) {
        BuildSvgIconCache(spec, cache);
        std::vector<ImVec2> tris;
        for (const SvgSubpath& sub : cache.subpaths) {
            if (sub.points.size() < 2) continue;
            if (sub.stroke) {
                drawList->PathClear();
                for (const ImVec2& p : sub.points) {
                    drawList->PathLineTo(SvgTransformPoint(p, min, max, spec.viewW, spec.viewH, scaleFactor));
                }
                ImDrawFlags flags = sub.closed ? ImDrawFlags_Closed : 0;
                drawList->PathStroke(color, flags, strokeScale);
            } else {
                SvgTriangulate(sub.points, tris);
                if (tris.empty()) continue;
                for (size_t i = 0; i + 2 < tris.size(); i += 3) {
                    ImVec2 a = SvgTransformPoint(tris[i], min, max, spec.viewW, spec.viewH, scaleFactor);
                    ImVec2 b = SvgTransformPoint(tris[i + 1], min, max, spec.viewW, spec.viewH, scaleFactor);
                    ImVec2 c = SvgTransformPoint(tris[i + 2], min, max, spec.viewW, spec.viewH, scaleFactor);
                    drawList->AddTriangleFilled(a, b, c, color);
                }
            }
        }
    }

    static const SvgPathSpec kTranslateSvgPaths[] = {
        { "M12 3L12.3123 2.60957L12 2.35969L11.6877 2.60957L12 3ZM11.5 9C11.5 9.27614 11.7239 9.5 12 9.5C12.2761 9.5 12.5 9.27614 12.5 9H11.5ZM16.3123 5.80957L12.3123 2.60957L11.6877 3.39043L15.6877 6.59043L16.3123 5.80957ZM11.6877 2.60957L7.68765 5.80957L8.31235 6.59043L12.3123 3.39043L11.6877 2.60957ZM11.5 3V9H12.5V3H11.5Z", true },
        { "M21 12L21.3904 12.3123L21.6403 12L21.3904 11.6877L21 12ZM15 11.5C14.7239 11.5 14.5 11.7239 14.5 12C14.5 12.2761 14.7239 12.5 15 12.5L15 11.5ZM18.1904 16.3123L21.3904 12.3123L20.6096 11.6877L17.4096 15.6877L18.1904 16.3123ZM21.3904 11.6877L18.1904 7.68765L17.4096 8.31235L20.6096 12.3123L21.3904 11.6877ZM21 11.5L15 11.5L15 12.5L21 12.5L21 11.5Z", true },
        { "M12 21L12.3123 21.3904L12 21.6403L11.6877 21.3904L12 21ZM11.5 15C11.5 14.7239 11.7239 14.5 12 14.5C12.2761 14.5 12.5 14.7239 12.5 15H11.5ZM16.3123 18.1904L12.3123 21.3904L11.6877 20.6096L15.6877 17.4096L16.3123 18.1904ZM11.6877 21.3904L7.68765 18.1904L8.31235 17.4096L12.3123 20.6096L11.6877 21.3904ZM11.5 21V15H12.5V21H11.5Z", true },
        { "M3 12L2.60957 12.3123L2.35969 12L2.60957 11.6877L3 12ZM9 11.5C9.27614 11.5 9.5 11.7239 9.5 12C9.5 12.2761 9.27614 12.5 9 12.5L9 11.5ZM5.80956 16.3123L2.60957 12.3123L3.39043 11.6877L6.59043 15.6877L5.80956 16.3123ZM2.60957 11.6877L5.80957 7.68765L6.59043 8.31235L3.39043 12.3123L2.60957 11.6877ZM3 11.5L9 11.5L9 12.5L3 12.5L3 11.5Z", true }
    };

    static const SvgPathSpec kRotateSvgPaths[] = {
        { "M11.2797426,15.9868494 L10.1464466,14.8535534 C9.95118446,14.6582912 9.95118446,14.3417088 10.1464466,14.1464466 C10.3417088,13.9511845 10.6582912,13.9511845 10.8535534,14.1464466 L12.8535534,16.1464466 C13.0488155,16.3417088 13.0488155,16.6582912 12.8535534,16.8535534 L10.8535534,18.8535534 C10.6582912,19.0488155 10.3417088,19.0488155 10.1464466,18.8535534 C9.95118446,18.6582912 9.95118446,18.3417088 10.1464466,18.1464466 L11.3044061,16.9884871 C10.3667147,16.9573314 9.46306739,16.8635462 8.61196501,16.7145167 C9.33747501,19.2936084 10.6229353,21 12,21 C14.0051086,21 15.8160018,17.3821896 15.9868494,12.7202574 L14.8535534,13.8535534 C14.6582912,14.0488155 14.3417088,14.0488155 14.1464466,13.8535534 C13.9511845,13.6582912 13.9511845,13.3417088 14.1464466,13.1464466 L16.1464466,11.1464466 C16.3417088,10.9511845 16.6582912,10.9511845 16.8535534,11.1464466 L18.8535534,13.1464466 C19.0488155,13.3417088 19.0488155,13.6582912 18.8535534,13.8535534 C18.6582912,14.0488155 18.3417088,14.0488155 18.1464466,13.8535534 L16.9884871,12.6955939 C16.8167229,17.8651676 14.7413901,22 12,22 C9.97580598,22 8.3147521,19.7456544 7.515026,16.484974 C4.2543456,15.6852479 2,14.024194 2,12 C2,9.97580598 4.2543456,8.3147521 7.515026,7.515026 C8.3147521,4.2543456 9.97580598,2 12,2 C13.5021775,2 14.8263891,3.23888365 15.7433738,5.30744582 C15.8552836,5.55989543 15.7413536,5.8552671 15.4889039,5.96717692 C15.2364543,6.07908673 14.9410827,5.96515672 14.8291729,5.71270711 C14.0550111,3.96632921 13.0221261,3 12,3 C10.6229353,3 9.33747501,4.70639159 8.61196501,7.28548333 C9.67174589,7.09991387 10.812997,7 12,7 C17.4892085,7 22,9.13669069 22,12 C22,13.5021775 20.7611164,14.8263891 18.6925542,15.7433738 C18.4401046,15.8552836 18.1447329,15.7413536 18.0328231,15.4889039 C17.9209133,15.2364543 18.0348433,14.9410827 18.2872929,14.8291729 C20.0336708,14.0550111 21,13.0221261 21,12 C21,9.89274656 17.0042017,8 12,8 C10.6991081,8 9.46636321,8.12791023 8.35424759,8.35424759 C8.12791023,9.46636321 8,10.6991081 8,12 C8,13.3008919 8.12791023,14.5336368 8.35424759,15.6457524 C9.25899447,15.8298862 10.2435788,15.9488767 11.2797426,15.9868494 Z M7.28548333,8.61196501 C4.70639159,9.33747501 3,10.6229353 3,12 C3,13.3770647 4.70639159,14.662525 7.28548333,15.388035 C7.09991387,14.3282541 7,13.187003 7,12 C7,10.812997 7.09991387,9.67174589 7.28548333,8.61196501 L7.28548333,8.61196501 Z", true }
    };

    static const SvgPathSpec kScaleSvgPaths[] = {
        { "M20,19.2928932 L20,16.5 C20,16.2238576 20.2238576,16 20.5,16 C20.7761424,16 21,16.2238576 21,16.5 L21,20.5 C21,20.7761424 20.7761424,21 20.5,21 L16.5,21 C16.2238576,21 16,20.7761424 16,20.5 C16,20.2238576 16.2238576,20 16.5,20 L19.2928932,20 L16.1464466,16.8535534 C15.9511845,16.6582912 15.9511845,16.3417088 16.1464466,16.1464466 C16.3417088,15.9511845 16.6582912,15.9511845 16.8535534,16.1464466 L20,19.2928932 Z M4,4.70710678 L4,7.5 C4,7.77614237 3.77614237,8 3.5,8 C3.22385763,8 3,7.77614237 3,7.5 L3,3.5 C3,3.22385763 3.22385763,3 3.5,3 L7.5,3 C7.77614237,3 8,3.22385763 8,3.5 C8,3.77614237 7.77614237,4 7.5,4 L4.70710678,4 L7.85355339,7.14644661 C8.04881554,7.34170876 8.04881554,7.65829124 7.85355339,7.85355339 C7.65829124,8.04881554 7.34170876,8.04881554 7.14644661,7.85355339 L4,4.70710678 Z M4.70710678,20 L7.5,20 C7.77614237,20 8,20.2238576 8,20.5 C8,20.7761424 7.77614237,21 7.5,21 L3.5,21 C3.22385763,21 3,20.7761424 3,20.5 L3,16.5 C3,16.2238576 3.22385763,16 3.5,16 C3.77614237,16 4,16.2238576 4,16.5 L4,19.2928932 L7.14644661,16.1464466 C7.34170876,15.9511845 7.65829124,15.9511845 7.85355339,16.1464466 C8.04881554,16.3417088 8.04881554,16.6582912 7.85355339,16.8535534 L4.70710678,20 Z M19.2928932,4 L16.5,4 C16.2238576,4 16,3.77614237 16,3.5 C16,3.22385763 16.2238576,3 16.5,3 L20.5,3 C20.7761424,3 21,3.22385763 21,3.5 L21,7.53112887 C21,7.80727125 20.7761424,8.03112887 20.5,8.03112887 C20.2238576,8.03112887 20,7.80727125 20,7.53112887 L20,4.70710678 L16.8535534,7.85355339 C16.6582912,8.04881554 16.3417088,8.04881554 16.1464466,7.85355339 C15.9511845,7.65829124 15.9511845,7.34170876 16.1464466,7.14644661 L19.2928932,4 L19.2928932,4 Z M8,10.4949109 C8,9.11668583 9.11540994,7.99843045 10.4936306,7.99491906 L13.4936306,7.98727573 C14.8807119,7.98726762 16,9.10655574 16,10.4872676 L16,13.5 C16,14.8807119 14.8807119,16 13.5,16 L10.5,16 C9.11928813,16 8,14.8807119 8,13.5 L8,10.4949109 Z M9,10.4949109 L9,13.5 C9,14.3284271 9.67157288,15 10.5,15 L13.5,15 C14.3284271,15 15,14.3284271 15,13.5 L15,10.4872676 C15,9.65884049 14.3284271,8.98726762 13.5,8.98726762 L10.4961784,8.99491581 C9.66924596,8.99702265 9,9.66797587 9,10.4949109 Z", true }
    };

    static const SvgPathSpec kBoundsSvgPaths[] = {
        { "M11 13.6V21H3.6C3.26863 21 3 20.7314 3 20.4V13H10.4C10.7314 13 11 13.2686 11 13.6Z", true },
        { "M11 21H14", true },
        { "M3 13V10", true },
        { "M6 3H3.6C3.26863 3 3 3.26863 3 3.6V6", true },
        { "M14 3H10", true },
        { "M21 10V14", true },
        { "M18 3H20.4C20.7314 3 21 3.26863 21 3.6V6", true },
        { "M18 21H20.4C20.7314 21 21 20.7314 21 20.4V18", true },
        { "M11 10H14V13", true }
    };

    static const SvgPathSpec kMeshSvgPaths[] = {
        { "M363.6 36.48c-22.2 0-40 17.8-40 40 0 22.23 17.8 40.02 40 40.02s40-17.79 40-40.02c0-22.2-17.8-40-40-40zm-56.7 51.97c-53.2 18.95-108.7 34.95-169 45.25 1.8 4.6 2.8 9.6 2.8 14.8 0 4.8-.8 9.4-2.4 13.6 96.2 12.9 182.8 36 257.8 71.9 1.6-5.9 4.5-11.3 8.3-15.9-71.2-34.3-152.4-57.2-241.5-70.7 53.2-10.6 102.8-25.4 150.4-42.2-3-5.2-5.2-10.79-6.4-16.75zm97.8 28.85c-4.3 4.3-9.2 8-14.6 10.8 15.3 24.8 26 50.6 31.8 77.8 4.3-1.5 9-2.4 13.8-2.4 1.4 0 2.8.1 4.1.2-6.3-30.3-18.2-59.1-35.1-86.4zm-305 8.2c-12.81 0-23 10.2-23 23s10.19 23 23 23c12.8 0 23-10.2 23-23s-10.2-23-23-23zm34.7 44.6c-3.2 5.2-7.5 9.6-12.6 12.9 32.1 32.6 66.1 65.9 120.6 80.4 0-.9-.1-1.9-.1-2.8 0-5.3 1.3-10.3 3.5-14.8-49.5-13.5-80-43.8-111.4-75.7zm-57 12.7c-21.76 67.8-27.12 137.2-32.29 206 2.13-.5 4.34-.7 6.6-.7 3.99 0 7.81.7 11.35 2.1 5.19-68.4 10.57-136 31.29-201.1-6.18-.8-11.94-3-16.95-6.3zm358.3 38.7c-12.8 0-23 10.2-23 23s10.2 23 23 23 23-10.2 23-23-10.2-23-23-23zm-41 22.2c-28.4 5.8-56.6 10.8-86 10.5.4 2.1.6 4.2.6 6.4 0 4-.7 7.9-2.1 11.5 32 .6 62-4.7 91.2-10.8-2.4-5.1-3.7-10.8-3.7-16.8zm-118.9 1.4c-8.7 0-15.5 6.8-15.5 15.5s6.8 15.5 15.5 15.5 15.5-6.8 15.5-15.5-6.8-15.5-15.5-15.5zM399 262.7c-55.6 45.9-106.6 94.4-143.1 150.7 5.9 1.8 11.2 5 15.6 9.1 34.9-53.5 84.2-100.8 138.8-145.9-4.7-3.7-8.6-8.5-11.3-13.9zm-152 15c-47.9 46.4-109.6 83.2-172.85 119.5 4.36 4.2 7.56 9.6 9.05 15.6C146.8 376.4 210 338.9 260 290.1c-5.4-2.9-9.9-7.2-13-12.4zm179.4 6.7c1.3 28.8 6 57.3 14.3 85.2 4.8-3.4 10.7-5.6 17-6-7.6-26-11.9-52.3-13.2-79.1-2.9.7-5.8 1-8.8 1-3.2 0-6.3-.4-9.3-1.1zm33.3 97.1c-8.4 0-15 6.6-15 15s6.6 15 15 15 15-6.6 15-15-6.6-15-15-15zM51.71 406.1c-8.07 0-14.42 6.4-14.42 14.4 0 8.1 6.35 14.5 14.42 14.5s14.42-6.4 14.42-14.5c0-8-6.35-14.4-14.42-14.4zm376.49.3c-44.7 24.5-93.8 32.6-144.9 35.6.9 3.4 1.4 6.9 1.4 10.5 0 2.6-.3 5.1-.7 7.5 53.1-3.1 105.8-11.6 154.3-38.5-4.7-4-8.2-9.2-10.1-15.1zM83.91 416.8c.14 1.2.22 2.4.22 3.7 0 5-1.15 9.7-3.19 14l121.86 20.3c-.1-.8-.1-1.5-.1-2.3 0-5.4 1.1-10.6 3-15.4zm159.79 12.7c-12.8 0-23 10.2-23 23s10.2 23 23 23 23-10.2 23-23-10.2-23-23-23z", true }
    };

    static const SvgPathSpec kGizmoToggleSvgPaths[] = {
        { "M2 17h1v5h5v1H2zm21 0h-1v5h-5v1h6zM3 3h5V2H2v6h1zm20-1h-6v1h5v5h1zm-9.75 12h-1.5a.75.75 0 0 1-.75-.75v-1.5a.75.75 0 0 1 .75-.75h1.5a.75.75 0 0 1 .75.75v1.5a.75.75 0 0 1-.75.75zM13 12h-1v1h1zm7 0h-5v1h5zm-10 0H5v1h5zm3 8v-5h-1v5zm-1-10h1V5h-1z", false }
    };

    static const SvgPathSpec kGridToggleSvgPaths[] = {
        { "M47.547,63.547V448.453a16,16,0,0,0,16,16H448.453a16,16,0,0,0,16-16V63.547a16,16,0,0,0-16-16H63.547A16,16,0,0,0,47.547,63.547Zm288.6,16h96.3v96.3h-96.3Zm0,128.3h96.3v96.3h-96.3Zm0,128.3h96.3v96.3h-96.3Zm-128.3-256.6h96.3v96.3h-96.3Zm0,128.3h96.3v96.3h-96.3Zm0,128.3h96.3v96.3h-96.3Zm-128.3-256.6h96.3v96.3h-96.3Zm0,128.3h96.3v96.3h-96.3Zm0,128.3h96.3v96.3h-96.3Z", true }
    };

    static const SvgPathSpec kSnapToggleSvgPaths[] = {
        { "M13.3,7.7l8.1,8.1c1.5,1.5,1.5,3.9,0,5.4c-1.5,1.5-3.9,1.5-5.4,0l-8.1-8.1l-4.7,4.7l8.1,8.1 c4.1,4.1,10.7,4.1,14.8,0s4.1-10.7,0-14.8L18,3L13.3,7.7z", true }
    };

    static const SvgPathSpec kLocalModeSvgPaths[] = {
        { "M8 10C9.10457 10 10 9.10457 10 8C10 6.89543 9.10457 6 8 6C6.89543 6 6 6.89543 6 8C6 9.10457 6.89543 10 8 10Z", false },
        { "M2.08296 7C2.50448 4.48749 4.48749 2.50448 7 2.08296V0H9V2.08296C11.5125 2.50448 13.4955 4.48749 13.917 7H16V9H13.917C13.4955 11.5125 11.5125 13.4955 9 13.917V16H7V13.917C4.48749 13.4955 2.50448 11.5125 2.08296 9H0V7H2.08296ZM4 8C4 5.79086 5.79086 4 8 4C10.2091 4 12 5.79086 12 8C12 10.2091 10.2091 12 8 12C5.79086 12 4 10.2091 4 8Z", false }
    };

    static const SvgPathSpec kWorldModeSvgPaths[] = {
        { "M19.5 6L18.0333 7.1C17.6871 7.35964 17.2661 7.5 16.8333 7.5H13.475C12.8775 7.5 12.3312 7.83761 12.064 8.37206V8.37206C11.7342 9.03161 11.9053 9.83161 12.476 10.2986L14.476 11.9349C16.0499 13.2227 16.8644 15.22 16.6399 17.2412L16.6199 17.4206C16.5403 18.1369 16.3643 18.8392 16.0967 19.5083L15.5 21", true },
        { "M2.5 10.5L5.7381 9.96032C7.09174 9.73471 8.26529 10.9083 8.03968 12.2619L7.90517 13.069C7.66434 14.514 8.3941 15.9471 9.70437 16.6022V16.6022C10.7535 17.1268 11.2976 18.3097 11.0131 19.4476L10.5 21.5", true },
        { "M12 2.5C6.75329 2.5 2.5 6.75329 2.5 12C2.5 17.2467 6.75329 21.5 12 21.5C17.2467 21.5 21.5 17.2467 21.5 12C21.5 6.75329 17.2467 2.5 12 2.5Z", true }
    };

    static const SvgPathSpec kUiWorldToggleSvgPaths[] = {
        { "M1 1 L17 1 L17 17 L1 17 L1 1 Z M20 7 L23 7 L23 23 L7 23 L7 20 L7 20", true }
    };

    static const SvgIconSpec kTranslateSvg = { 24.0f, 24.0f, kTranslateSvgPaths, 4 };
    static const SvgIconSpec kRotateSvg = { 24.0f, 24.0f, kRotateSvgPaths, 1 };
    static const SvgIconSpec kScaleSvg = { 24.0f, 24.0f, kScaleSvgPaths, 1 };
    static const SvgIconSpec kBoundsSvg = { 24.0f, 24.0f, kBoundsSvgPaths, 9 };
    static const SvgIconSpec kMeshSvg = { 512.0f, 512.0f, kMeshSvgPaths, 1 };
    static const SvgIconSpec kGizmoToggleSvg = { 20.0f, 20.0f, kGizmoToggleSvgPaths, 1 };
    static const SvgIconSpec kGridToggleSvg = { 512.0f, 512.0f, kGridToggleSvgPaths, 1 };
    static const SvgIconSpec kSnapToggleSvg = { 32.0f, 32.0f, kSnapToggleSvgPaths, 1 };
    static const SvgIconSpec kLocalModeSvg = { 16.0f, 16.0f, kLocalModeSvgPaths, 1 };
    static const SvgIconSpec kWorldModeSvg = { 24.0f, 24.0f, kWorldModeSvgPaths, 3 };
    static const SvgIconSpec kUiWorldToggleSvg = { 24.0f, 24.0f, kUiWorldToggleSvgPaths, 1 };

    static SvgIconCache gTranslateSvgCache;
    static SvgIconCache gRotateSvgCache;
    static SvgIconCache gScaleSvgCache;
    static SvgIconCache gBoundsSvgCache;
    static SvgIconCache gMeshSvgCache;
    static SvgIconCache gGizmoToggleSvgCache;
    static SvgIconCache gGridToggleSvgCache;
    static SvgIconCache gSnapToggleSvgCache;
    static SvgIconCache gLocalModeSvgCache;
    static SvgIconCache gWorldModeSvgCache;
    static SvgIconCache gUiWorldToggleSvgCache;

    static void DrawTranslateIcon(ImDrawList* drawList, const ImVec2& min, const ImVec2& max, ImU32 lineColor, ImU32 accentColor) {
        (void)accentColor;
        DrawSvgIcon(drawList, kTranslateSvg, gTranslateSvgCache, min, max, lineColor, 1.15f, 0.8f);
    }

    static void DrawRotateIcon(ImDrawList* drawList, const ImVec2& min, const ImVec2& max, ImU32 lineColor, ImU32 accentColor) {
        (void)accentColor;
        DrawSvgIcon(drawList, kRotateSvg, gRotateSvgCache, min, max, lineColor, 1.2f, 0.8f);
    }

    static void DrawScaleIcon(ImDrawList* drawList, const ImVec2& min, const ImVec2& max, ImU32 lineColor, ImU32 accentColor) {
        (void)accentColor;
        DrawSvgIcon(drawList, kScaleSvg, gScaleSvgCache, min, max, lineColor, 1.2f, 0.8f);
    }

    static void DrawBoundsIcon(ImDrawList* drawList, const ImVec2& min, const ImVec2& max, ImU32 lineColor, ImU32 accentColor) {
        (void)accentColor;
        float size = std::min(max.x - min.x, max.y - min.y) * 0.8f;
        DrawSvgIcon(drawList, kBoundsSvg, gBoundsSvgCache, min, max, lineColor, size * 0.06f, 0.82f);
    }

    static void DrawUniversalIcon(ImDrawList* drawList, const ImVec2& min, const ImVec2& max, ImU32 lineColor, ImU32 accentColor) {
        (void)accentColor;
        DrawSvgIcon(drawList, kRotateSvg, gRotateSvgCache, min, max, lineColor, 1.1f, 0.85f);
        DrawSvgIcon(drawList, kTranslateSvg, gTranslateSvgCache, min, max, lineColor, 1.1f, 0.62f);
    }

    static void DrawMeshIcon(ImDrawList* drawList, const ImVec2& min, const ImVec2& max, ImU32 lineColor, ImU32 accentColor) {
        (void)accentColor;
        DrawSvgIcon(drawList, kMeshSvg, gMeshSvgCache, min, max, lineColor, 1.0f, 0.78f);
    }

    static void DrawGizmoToggleIcon(ImDrawList* drawList, const ImVec2& min, const ImVec2& max, ImU32 lineColor, ImU32 accentColor) {
        (void)accentColor;
        ImVec2 iconMin, iconMax;
        GetIconBounds(min, max, iconMin, iconMax);
        float size = iconMax.x - iconMin.x;
        float thickness = std::max(1.0f, size * 0.08f);

        auto T = [&](float x, float y) {
            return ImVec2(iconMin.x + (x / 24.0f) * size, iconMin.y + (y / 24.0f) * size);
        };

        // Corner brackets
        drawList->AddLine(T(2, 2), T(8, 2), lineColor, thickness);
        drawList->AddLine(T(2, 2), T(2, 8), lineColor, thickness);
        drawList->AddLine(T(16, 2), T(22, 2), lineColor, thickness);
        drawList->AddLine(T(22, 2), T(22, 8), lineColor, thickness);
        drawList->AddLine(T(2, 22), T(8, 22), lineColor, thickness);
        drawList->AddLine(T(2, 16), T(2, 22), lineColor, thickness);
        drawList->AddLine(T(16, 22), T(22, 22), lineColor, thickness);
        drawList->AddLine(T(22, 16), T(22, 22), lineColor, thickness);

        // Crosshair
        drawList->AddLine(T(5, 12), T(10, 12), lineColor, thickness);
        drawList->AddLine(T(14, 12), T(19, 12), lineColor, thickness);
        drawList->AddLine(T(12, 5), T(12, 10), lineColor, thickness);
        drawList->AddLine(T(12, 14), T(12, 19), lineColor, thickness);

        // Center square
        float half = 1.1f;
        ImVec2 c = T(12, 12);
        drawList->AddRectFilled(ImVec2(c.x - (half / 24.0f) * size, c.y - (half / 24.0f) * size),
                                ImVec2(c.x + (half / 24.0f) * size, c.y + (half / 24.0f) * size),
                                lineColor, 1.5f);
    }

    static void DrawGridToggleIcon(ImDrawList* drawList, const ImVec2& min, const ImVec2& max, ImU32 lineColor, ImU32 accentColor) {
        (void)accentColor;
        DrawSvgIcon(drawList, kGridToggleSvg, gGridToggleSvgCache, min, max, lineColor, 0.8f, 0.72f);
    }

    static void DrawSnapToggleIcon(ImDrawList* drawList, const ImVec2& min, const ImVec2& max, ImU32 lineColor, ImU32 accentColor) {
        (void)accentColor;
        ImVec2 iconMin, iconMax;
        GetIconBounds(min, max, iconMin, iconMax);
        float size = iconMax.x - iconMin.x;
        float thickness = std::max(1.0f, size * 0.075f);
        DrawSvgIcon(drawList, kSnapToggleSvg, gSnapToggleSvgCache, min, max, lineColor, thickness, 0.78f);

        auto T = [&](float x, float y) {
            return ImVec2(iconMin.x + (x / 32.0f) * size, iconMin.y + (y / 32.0f) * size);
        };
        auto DrawRotRect = [&](float cx, float cy, float w, float h) {
            float hx = w * 0.5f;
            float hy = h * 0.5f;
            float c = 0.70710678f;
            float s = 0.70710678f;
            ImVec2 corners[4] = {
                ImVec2(-hx, -hy),
                ImVec2(hx, -hy),
                ImVec2(hx, hy),
                ImVec2(-hx, hy)
            };
            drawList->PathClear();
            for (int i = 0; i < 4; ++i) {
                float rx = corners[i].x * c - corners[i].y * s;
                float ry = corners[i].x * s + corners[i].y * c;
                drawList->PathLineTo(T(cx + rx, cy + ry));
            }
            drawList->PathStroke(lineColor, ImDrawFlags_Closed, thickness);
        };

        DrawRotRect(6.95f, 16.8f, 6.7f, 3.8f);
        DrawRotRect(17.05f, 6.7f, 6.7f, 3.8f);
    }

    static void DrawLocalModeIcon(ImDrawList* drawList, const ImVec2& min, const ImVec2& max, ImU32 lineColor, ImU32 accentColor) {
        (void)accentColor;
        ImVec2 iconMin, iconMax;
        GetIconBounds(min, max, iconMin, iconMax);
        ImVec2 center = ImVec2((iconMin.x + iconMax.x) * 0.5f, (iconMin.y + iconMax.y) * 0.5f);
        float size = iconMax.x - iconMin.x;
        float outerR = size * 0.36f;
        float innerR = size * 0.2f;
        float dotR = size * 0.08f;
        float thickness = std::max(1.0f, size * 0.08f);

        drawList->AddCircle(center, outerR, lineColor, 28, thickness);
        drawList->AddCircleFilled(center, dotR, lineColor, 12);

        const float tickLen = size * 0.12f;
        const float tickR = outerR + tickLen * 0.5f;
        for (int i = 0; i < 4; ++i) {
            float angle = (IM_PI * 0.5f) * static_cast<float>(i);
            ImVec2 dir(std::cos(angle), std::sin(angle));
            ImVec2 a = ImVec2(center.x + dir.x * (outerR - tickLen * 0.2f),
                              center.y + dir.y * (outerR - tickLen * 0.2f));
            ImVec2 b = ImVec2(center.x + dir.x * (outerR + tickLen),
                              center.y + dir.y * (outerR + tickLen));
            drawList->AddLine(a, b, lineColor, thickness);
        }
    }

    static void DrawWorldModeIcon(ImDrawList* drawList, const ImVec2& min, const ImVec2& max, ImU32 lineColor, ImU32 accentColor) {
        (void)accentColor;
        DrawSvgIcon(drawList, kWorldModeSvg, gWorldModeSvgCache, min, max, lineColor, 1.0f, 0.8f);
    }

    static void DrawUiWorldToggleIcon(ImDrawList* drawList, const ImVec2& min, const ImVec2& max, ImU32 lineColor, ImU32 accentColor) {
        (void)accentColor;
        DrawSvgIcon(drawList, kUiWorldToggleSvg, gUiWorldToggleSvgCache, min, max, lineColor, 0.9f, 0.8f);

        ImVec2 iconMin, iconMax;
        GetIconBounds(min, max, iconMin, iconMax);
        float size = iconMax.x - iconMin.x;
        auto T = [&](float x, float y) {
            return ImVec2(iconMin.x + (x / 24.0f) * size, iconMin.y + (y / 24.0f) * size);
        };

        ImVec2 boxMin = T(1, 1);
        ImVec2 boxMax = T(17, 17);
        float fontSize = size * 0.38f;
        ImVec2 textSize = ImGui::CalcTextSize("2D");
        float textScale = fontSize / ImGui::GetFontSize();
        ImVec2 scaledTextSize(textSize.x * textScale, textSize.y * textScale);
        ImVec2 textPos(boxMin.x + (boxMax.x - boxMin.x - scaledTextSize.x) * 0.5f,
                       boxMin.y + (boxMax.y - boxMin.y - scaledTextSize.y) * 0.5f - size * 0.02f);

        ImFont* font = ImGui::GetFont();
        const ImVec2 offsets[] = {
            ImVec2(-0.6f, 0.0f), ImVec2(0.6f, 0.0f),
            ImVec2(0.0f, -0.6f), ImVec2(0.0f, 0.6f)
        };
        for (const ImVec2& off : offsets) {
            drawList->AddText(font, fontSize, ImVec2(textPos.x + off.x, textPos.y + off.y), lineColor, "2D");
        }
    }

    static void DrawIcon(Icon icon, ImDrawList* drawList, const ImVec2& min, const ImVec2& max, ImU32 lineColor, ImU32 accentColor) {
        switch (icon) {
            case Icon::Translate: DrawTranslateIcon(drawList, min, max, lineColor, accentColor); break;
            case Icon::Rotate:    DrawRotateIcon(drawList, min, max, lineColor, accentColor);    break;
            case Icon::Scale:     DrawScaleIcon(drawList, min, max, lineColor, accentColor);     break;
            case Icon::Bounds:    DrawBoundsIcon(drawList, min, max, lineColor, accentColor);    break;
            case Icon::Universal: DrawUniversalIcon(drawList, min, max, lineColor, accentColor); break;
            case Icon::Mesh:      DrawMeshIcon(drawList, min, max, lineColor, accentColor);      break;
            case Icon::GizmoToggle: DrawGizmoToggleIcon(drawList, min, max, lineColor, accentColor); break;
            case Icon::GridToggle:  DrawGridToggleIcon(drawList, min, max, lineColor, accentColor);  break;
            case Icon::SnapToggle:  DrawSnapToggleIcon(drawList, min, max, lineColor, accentColor);  break;
            case Icon::LocalMode:   DrawLocalModeIcon(drawList, min, max, lineColor, accentColor);   break;
            case Icon::WorldMode:   DrawWorldModeIcon(drawList, min, max, lineColor, accentColor);   break;
            case Icon::UiWorldToggle: DrawUiWorldToggleIcon(drawList, min, max, lineColor, accentColor); break;
        }
    }

    static bool IconButton(const char* id, Icon icon, bool active, const ImVec2& size,
                           ImU32 baseColor, ImU32 hoverColor, ImU32 activeColor,
                           ImU32 accentColor, ImU32 iconColor) {
        ImGui::PushID(id);
        ImGui::InvisibleButton("##btn", size);
        bool hovered = ImGui::IsItemHovered();
        bool pressed = ImGui::IsItemClicked();
        ImVec2 min = ImGui::GetItemRectMin();
        ImVec2 max = ImGui::GetItemRectMax();
        float rounding = 9.0f;

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImU32 bg = active ? activeColor : (hovered ? hoverColor : baseColor);

        ImVec4 bgCol = ImGui::ColorConvertU32ToFloat4(bg);
        ImU32 top = ImGui::GetColorU32(ScaleColor(bgCol, 1.07f));
        ImU32 bottom = ImGui::GetColorU32(ScaleColor(bgCol, 0.93f));
        drawList->AddRectFilledMultiColor(min, max, top, top, bottom, bottom);
        drawList->AddRect(min, max, ImGui::GetColorU32(ImVec4(1, 1, 1, active ? 0.35f : 0.18f)), rounding);

        ImDrawListFlags prevFlags = drawList->Flags;
        drawList->Flags |= ImDrawListFlags_AntiAliasedLines | ImDrawListFlags_AntiAliasedFill;
        DrawIcon(icon, drawList, min, max, iconColor, accentColor);
        drawList->Flags = prevFlags;

        ImGui::PopID();
        return pressed;
    }

    static bool TextButton(const char* id, const char* label, bool active, const ImVec2& size,
                           ImU32 baseColor, ImU32 hoverColor, ImU32 activeColor, ImU32 borderColor, ImVec4 textColor) {
        ImGui::PushID(id);
        ImGui::InvisibleButton("##btn", size);
        bool hovered = ImGui::IsItemHovered();
        bool pressed = ImGui::IsItemClicked();
        ImVec2 min = ImGui::GetItemRectMin();
        ImVec2 max = ImGui::GetItemRectMax();
        float rounding = 8.0f;

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImU32 bg = active ? activeColor : (hovered ? hoverColor : baseColor);

        ImVec4 bgCol = ImGui::ColorConvertU32ToFloat4(bg);
        ImU32 top = ImGui::GetColorU32(ScaleColor(bgCol, 1.06f));
        ImU32 bottom = ImGui::GetColorU32(ScaleColor(bgCol, 0.94f));
        drawList->AddRectFilledMultiColor(min, max, top, top, bottom, bottom);
        drawList->AddRect(min, max, borderColor, rounding);

        ImVec2 textSize = ImGui::CalcTextSize(label);
        ImVec2 textPos = ImVec2(
            min.x + (size.x - textSize.x) * 0.5f,
            min.y + (size.y - textSize.y) * 0.5f - 1.0f
        );
        drawList->AddText(textPos, ImGui::GetColorU32(textColor), label);

        ImGui::PopID();
        return pressed;
    }

    static bool ModeButton(const char* label, bool active, const ImVec2& size, ImVec4 baseColor, ImVec4 activeColor, ImVec4 textColor) {
        ImGui::PushStyleColor(ImGuiCol_Button, active ? activeColor : baseColor);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, active ? activeColor : baseColor);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, active ? activeColor : baseColor);
        ImGui::PushStyleColor(ImGuiCol_Text, textColor);
        bool pressed = ImGui::Button(label, size);
        ImGui::PopStyleColor(4);
        return pressed;
    }
}
#pragma endregion


#pragma region Game Viewport Window
void Engine::renderGameViewportWindow() {
    gameViewportFocused = false;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, 6.0f));
    ImGui::Begin("Game Viewport", &showGameViewport, ImGuiWindowFlags_NoScrollbar);

    const bool showGameViewportToolbar = true;
    bool windowFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    struct GameResolutionOption {
        const char* label;
        int width;
        int height;
        bool useWindow;
        bool custom;
    };
    static const std::array<GameResolutionOption, 5> kGameResolutions = {{
        { "Window", 0, 0, true, false },
        { "1920x1080 (1080p)", 1920, 1080, false, false },
        { "1280x720 (720p)", 1280, 720, false, false },
        { "2560x1440 (1440p)", 2560, 1440, false, false },
        { "Custom", 0, 0, false, true }
    }};
    if (gameViewportResolutionIndex < 0 || gameViewportResolutionIndex >= (int)kGameResolutions.size()) {
        gameViewportResolutionIndex = 0;
    }

    SceneObject* playerCam = nullptr;
    for (auto& obj : sceneObjects) {
        if (obj.hasCamera && obj.camera.type == SceneCameraType::Player) {
            playerCam = &obj;
            break;
        }
    }

    bool postFxChanged = false;
    if (!isPlaying && showGameViewportToolbar) {
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.08f, 0.09f, 0.10f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.12f, 0.14f, 0.16f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.14f, 0.18f, 0.20f, 1.0f));
        ImGui::BeginDisabled(playerCam == nullptr);
        bool dummyToggle = false;
        if (playerCam) {
            bool before = playerCam->camera.applyPostFX;
            if (ImGui::Checkbox("Post FX", &playerCam->camera.applyPostFX)) {
                postFxChanged = (before != playerCam->camera.applyPostFX);
            }
        } else {
            ImGui::Checkbox("Post FX", &dummyToggle);
        }
        ImGui::SameLine();
        ImGui::Checkbox("Profiler", &showGameProfiler);
        ImGui::SameLine();
        ImGui::Checkbox("Canvas Guides", &showCanvasOverlay);
        ImGui::SameLine();
        ImGui::Checkbox("UI World", &uiWorldMode);
        ImGui::SameLine();
        ImGui::Checkbox("UI Grid", &showUIWorldGrid);
        ImGui::EndDisabled();
        ImGui::PopStyleColor(3);

        ImGui::Spacing();
    }
    const GameResolutionOption& resOption = kGameResolutions[gameViewportResolutionIndex];

    if (!isPlaying && showGameViewportToolbar) {
        ImGui::SetNextItemWidth(180.0f);
        ImGui::SetNextWindowBgAlpha(0.85f);
        if (ImGui::BeginCombo("Resolution", resOption.label)) {
            for (int i = 0; i < (int)kGameResolutions.size(); ++i) {
                bool selected = (i == gameViewportResolutionIndex);
                if (ImGui::Selectable(kGameResolutions[i].label, selected)) {
                    gameViewportResolutionIndex = i;
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        if (kGameResolutions[gameViewportResolutionIndex].custom) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(90.0f);
            ImGui::DragInt("W", &gameViewportCustomWidth, 1.0f, 64, 8192);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(90.0f);
            ImGui::DragInt("H", &gameViewportCustomHeight, 1.0f, 64, 8192);
        }
        ImGui::SameLine();
        ImGui::Checkbox("Auto Fit", &gameViewportAutoFit);
        ImGui::SameLine();
        ImGui::BeginDisabled(gameViewportAutoFit);
        float zoomPercent = gameViewportZoom * 100.0f;
        ImGui::SetNextItemWidth(140.0f);
        if (ImGui::SliderFloat("Zoom", &zoomPercent, 10.0f, 200.0f, "%.0f%%")) {
            gameViewportZoom = zoomPercent / 100.0f;
        }
        ImGui::EndDisabled();
    }

    ImVec2 avail = ImGui::GetContentRegionAvail();
    int renderWidth = 0;
    int renderHeight = 0;
    if (kGameResolutions[gameViewportResolutionIndex].useWindow) {
        renderWidth = std::max(160, (int)avail.x);
        renderHeight = std::max(120, (int)avail.y);
    } else if (kGameResolutions[gameViewportResolutionIndex].custom) {
        renderWidth = std::clamp(gameViewportCustomWidth, 64, 8192);
        renderHeight = std::clamp(gameViewportCustomHeight, 64, 8192);
    } else {
        renderWidth = kGameResolutions[gameViewportResolutionIndex].width;
        renderHeight = kGameResolutions[gameViewportResolutionIndex].height;
    }
    float zoom = gameViewportZoom;
    if (gameViewportAutoFit) {
        if (kGameResolutions[gameViewportResolutionIndex].useWindow) {
            zoom = 1.0f;
        } else {
            float fitX = (renderWidth > 0) ? (avail.x / (float)renderWidth) : 1.0f;
            float fitY = (renderHeight > 0) ? (avail.y / (float)renderHeight) : 1.0f;
            zoom = std::min(1.0f, std::min(fitX, fitY));
            zoom = std::max(0.01f, zoom);
        }
    }

    if (playerCam && postFxChanged) {
        projectManager.currentProject.hasUnsavedChanges = true;
    }

    if (!isPlaying) {
        gameViewCursorLocked = false;
    }

    if (playerCam && rendererInitialized) {
        unsigned int tex = renderer.renderScenePreview(
            makeCameraFromObject(*playerCam),
            sceneObjects,
            renderWidth,
            renderHeight,
            playerCam->camera.fov,
            playerCam->camera.nearClip,
            playerCam->camera.farClip,
            playerCam->camera.applyPostFX
        );

        ImVec2 imageSize(std::max(1.0f, renderWidth * zoom), std::max(1.0f, renderHeight * zoom));
        ImVec2 cursorPos = ImGui::GetCursorPos();
        float offsetX = std::max(0.0f, (avail.x - imageSize.x) * 0.5f);
        float offsetY = std::max(0.0f, (avail.y - imageSize.y) * 0.5f);
        ImGui::SetCursorPos(ImVec2(cursorPos.x + offsetX, cursorPos.y + offsetY));
        ImGui::Image((void*)(intptr_t)tex, imageSize, ImVec2(0, 1), ImVec2(1, 0));
        bool imageHovered = ImGui::IsItemHovered();
        ImVec2 imageMin = ImGui::GetItemRectMin();
        ImVec2 imageMax = ImGui::GetItemRectMax();
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        float uiScaleX = (renderWidth > 0) ? (imageSize.x / (float)renderWidth) : 1.0f;
        float uiScaleY = (renderHeight > 0) ? (imageSize.y / (float)renderHeight) : 1.0f;
        if (showGameViewportToolbar && showCanvasOverlay) {
            ImVec2 pad(8.0f, 8.0f);
            ImVec2 tl(imageMin.x + pad.x, imageMin.y + pad.y);
            ImVec2 br(imageMax.x - pad.x, imageMax.y - pad.y);
            drawList->AddRect(tl, br, IM_COL32(110, 170, 255, 180), 8.0f, 0, 2.0f);
        }
        if (showGameViewportToolbar && showGameProfiler) {
            float fps = ImGui::GetIO().Framerate;
            float frameMs = (fps > 0.0f) ? (1000.0f / fps) : 0.0f;
            int zoomPercent = (int)std::round(zoom * 100.0f);
            const Renderer::RenderStats& stats = renderer.getLastPreviewStats();

            char line1[128];
            char line2[128];
            char line3[128];
            char line4[128];
            std::snprintf(line1, sizeof(line1), "FPS: %.0f (%.1f ms)", fps, frameMs);
            std::snprintf(line2, sizeof(line2), "Batches: %d", stats.drawCalls);
            std::snprintf(line3, sizeof(line3), "Meshes: %d", stats.meshDraws);
            std::snprintf(line4, sizeof(line4), "Render: %dx%d @ %d%%", renderWidth, renderHeight, zoomPercent);

            const char* lines[] = { line1, line2, line3, line4 };
            float lineHeight = ImGui::GetFontSize() + 2.0f;
            float maxWidth = 0.0f;
            for (const char* line : lines) {
                ImVec2 size = ImGui::CalcTextSize(line);
                maxWidth = std::max(maxWidth, size.x);
            }
            ImVec2 pad(8.0f, 6.0f);
            ImVec2 panelMin(imageMin.x + 14.0f, imageMin.y + 14.0f);
            ImVec2 panelMax(panelMin.x + maxWidth + pad.x * 2.0f,
                            panelMin.y + lineHeight * (float)(sizeof(lines) / sizeof(lines[0])) + pad.y * 2.0f);
            drawList->AddRectFilled(panelMin, panelMax, IM_COL32(18, 18, 24, 210), 6.0f);
            drawList->AddRect(panelMin, panelMax, IM_COL32(255, 255, 255, 40), 6.0f);
            for (int i = 0; i < (int)(sizeof(lines) / sizeof(lines[0])); ++i) {
                ImVec2 textPos(panelMin.x + pad.x, panelMin.y + pad.y + lineHeight * i);
                drawList->AddText(textPos, IM_COL32(235, 235, 245, 255), lines[i]);
            }
        }
        bool uiInteracting = false;
        auto find3DCanvasId = [&](const SceneObject& target) -> int {
            const SceneObject* current = &target;
            while (current) {
                if (current->hasUI && current->ui.type == UIElementType::Canvas && current->ui.renderIn3D) {
                    return current->id;
                }
                if (current->parentId < 0) break;
                auto pit = std::find_if(sceneObjects.begin(), sceneObjects.end(),
                    [&](const SceneObject& o) { return o.id == current->parentId; });
                if (pit == sceneObjects.end()) break;
                current = &(*pit);
            }
            return -1;
        };
        auto isUiOn3DCanvas = [&](const SceneObject& target) {
            return find3DCanvasId(target) >= 0;
        };
        int editCanvas3DId = -1;
        if (SceneObject* selected = getSelectedObject()) {
            editCanvas3DId = find3DCanvasId(*selected);
        }
        auto isUIType = [&](const SceneObject& target) {
            if (!target.hasUI || target.ui.type == UIElementType::None) return false;
            int canvasId = find3DCanvasId(target);
            return (canvasId < 0) || (canvasId == editCanvas3DId);
        };
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::SetCursorScreenPos(imageMin);
        ImGui::BeginChild("GameUIOverlay",
                          ImVec2(imageMax.x - imageMin.x, imageMax.y - imageMin.y),
                          false,
                          ImGuiWindowFlags_NoTitleBar |
                          ImGuiWindowFlags_NoResize |
                          ImGuiWindowFlags_NoMove |
                          ImGuiWindowFlags_NoScrollbar |
                          ImGuiWindowFlags_NoScrollWithMouse |
                          ImGuiWindowFlags_NoSavedSettings |
                          ImGuiWindowFlags_NoBackground);

        auto anchorToPivot = [](UIAnchor anchor, const ImVec2& size) {
            switch (anchor) {
                case UIAnchor::Center: return ImVec2(size.x * 0.5f, size.y * 0.5f);
                case UIAnchor::TopLeft: return ImVec2(0.0f, 0.0f);
                case UIAnchor::TopRight: return ImVec2(size.x, 0.0f);
                case UIAnchor::BottomLeft: return ImVec2(0.0f, size.y);
                case UIAnchor::BottomRight: return ImVec2(size.x, size.y);
                default: return ImVec2(size.x * 0.5f, size.y * 0.5f);
            }
        };
        auto anchorToPoint = [](UIAnchor anchor, const ImVec2& min, const ImVec2& max) {
            switch (anchor) {
                case UIAnchor::Center: return ImVec2((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
                case UIAnchor::TopLeft: return min;
                case UIAnchor::TopRight: return ImVec2(max.x, min.y);
                case UIAnchor::BottomLeft: return ImVec2(min.x, max.y);
                case UIAnchor::BottomRight: return max;
                default: return ImVec2((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
            }
        };

        auto resolveUIRect = [&](const SceneObject& obj, ImVec2& outMin, ImVec2& outMax, ImVec2* parentMin = nullptr, ImVec2* parentMax = nullptr) {
            std::vector<const SceneObject*> chain;
            const SceneObject* current = &obj;
            while (current) {
                if (isUIType(*current)) {
                    chain.push_back(current);
                }
                if (current->parentId < 0) break;
                auto pit = std::find_if(sceneObjects.begin(), sceneObjects.end(),
                    [&](const SceneObject& o) { return o.id == current->parentId; });
                if (pit == sceneObjects.end()) break;
                current = &(*pit);
            }
            std::reverse(chain.begin(), chain.end());

            ImVec2 regionMin = ImGui::GetWindowPos();
            ImVec2 regionMax = ImVec2(regionMin.x + ImGui::GetWindowWidth(), regionMin.y + ImGui::GetWindowHeight());
            for (size_t idx = 0; idx < chain.size(); ++idx) {
                const SceneObject* node = chain[idx];
                if (idx + 1 == chain.size() && parentMin && parentMax) {
                    *parentMin = regionMin;
                    *parentMax = regionMax;
                }
                ImVec2 size = ImVec2(std::max(1.0f, node->ui.size.x * uiScaleX), std::max(1.0f, node->ui.size.y * uiScaleY));
                ImVec2 anchorPoint = anchorToPoint(node->ui.anchor, regionMin, regionMax);
                ImVec2 pivot(anchorPoint.x + node->ui.position.x * uiScaleX, anchorPoint.y + node->ui.position.y * uiScaleY);
                ImVec2 pivotOffset = anchorToPivot(node->ui.anchor, size);
                regionMin = ImVec2(pivot.x - pivotOffset.x, pivot.y - pivotOffset.y);
                regionMax = ImVec2(regionMin.x + size.x, regionMin.y + size.y);
            }
            outMin = regionMin;
            outMax = regionMax;
        };

        ImVec2 overlayPos = ImGui::GetWindowPos();
        ImVec2 overlaySize = ImGui::GetWindowSize();
        bool allowEditorUi = !isPlaying;
        bool useWorldUi = uiWorldMode;
        UIWorldCamera2D uiWorldCameraBackup = uiWorldCamera;
        bool restoreUiWorldCamera = false;
        if (playerCam && playerCam->camera.use2D) {
            useWorldUi = true;
            restoreUiWorldCamera = true;
            uiWorldCamera.position = glm::vec2(playerCam->position.x, playerCam->position.y);
            uiWorldCamera.zoom = std::max(1.0f, playerCam->camera.pixelsPerUnit);
        }
        if (!useWorldUi || !allowEditorUi) {
            uiWorldPanning = false;
        }
        if (useWorldUi) {
            uiWorldCamera.viewportSize = glm::vec2(overlaySize.x, overlaySize.y);
        }
        auto worldToScreen = [&](const glm::vec2& world) {
            glm::vec2 local = uiWorldCamera.WorldToScreen(world);
            return ImVec2(overlayPos.x + local.x, overlayPos.y + local.y);
        };
        auto screenToWorld = [&](const ImVec2& screen) {
            glm::vec2 local(screen.x - overlayPos.x, screen.y - overlayPos.y);
            return uiWorldCamera.ScreenToWorld(local);
        };
        auto getWorldParentOffset = [&](const SceneObject& obj) {
            glm::vec2 offset(0.0f);
            const SceneObject* current = &obj;
            while (current && current->parentId >= 0) {
                auto pit = std::find_if(sceneObjects.begin(), sceneObjects.end(),
                    [&](const SceneObject& o) { return o.id == current->parentId; });
                if (pit == sceneObjects.end()) break;
                current = &(*pit);
                if (current->hasUI && current->ui.type != UIElementType::None) {
                    offset += glm::vec2(current->ui.position.x, current->ui.position.y);
                }
            }
            return offset;
        };
        auto parallaxOffset = [&](const SceneObject& obj) {
            if (!obj.hasParallaxLayer2D || !obj.parallaxLayer2D.enabled) return glm::vec2(0.0f);
            float factor = std::clamp(obj.parallaxLayer2D.factor, 0.0f, 1.0f);
            return uiWorldCamera.position * (1.0f - factor);
        };
        auto resolveUIRectWorld = [&](const SceneObject& obj, ImVec2& outMin, ImVec2& outMax) {
            glm::vec2 parentOffset = getWorldParentOffset(obj);
            glm::vec2 worldPos = parentOffset + glm::vec2(obj.ui.position.x, obj.ui.position.y) + parallaxOffset(obj);
            glm::vec2 sizeWorld(obj.ui.size.x, obj.ui.size.y);
            ImVec2 pivotOffset = anchorToPivot(obj.ui.anchor, ImVec2(sizeWorld.x, sizeWorld.y));
            glm::vec2 worldMin = worldPos - glm::vec2(pivotOffset.x, pivotOffset.y);
            glm::vec2 worldMax = worldMin + sizeWorld;
            ImVec2 s0 = worldToScreen(worldMin);
            ImVec2 s1 = worldToScreen(worldMax);
            outMin = ImVec2(std::min(s0.x, s1.x), std::min(s0.y, s1.y));
            outMax = ImVec2(std::max(s0.x, s1.x), std::max(s0.y, s1.y));
        };
        auto rectOutsideOverlay = [&](const ImVec2& min, const ImVec2& max) {
            return (max.x < overlayPos.x || min.x > overlayPos.x + overlaySize.x ||
                    max.y < overlayPos.y || min.y > overlayPos.y + overlaySize.y);
        };

        bool uiWorldHover = imageHovered || ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
        bool uiWorldCameraActive = false;
        if (useWorldUi && allowEditorUi) {
            ImGuiIO& io = ImGui::GetIO();
            bool panHeld = uiWorldHover && (ImGui::IsMouseDown(ImGuiMouseButton_Middle) ||
                (ImGui::IsKeyDown(ImGuiKey_Space) && ImGui::IsMouseDown(ImGuiMouseButton_Left)));
            if (panHeld) {
                uiWorldPanning = true;
            } else if (!ImGui::IsMouseDown(ImGuiMouseButton_Middle) &&
                       !(ImGui::IsKeyDown(ImGuiKey_Space) && ImGui::IsMouseDown(ImGuiMouseButton_Left))) {
                uiWorldPanning = false;
            }
            if (uiWorldPanning) {
                ImVec2 delta = io.MouseDelta;
                if (delta.x != 0.0f || delta.y != 0.0f) {
                    uiWorldCamera.position.x -= delta.x / uiWorldCamera.zoom;
                    uiWorldCamera.position.y += delta.y / uiWorldCamera.zoom;
                }
                uiWorldCameraActive = true;
            }
            if (uiWorldHover && io.MouseWheel != 0.0f) {
                glm::vec2 mouseLocal(io.MousePos.x - overlayPos.x, io.MousePos.y - overlayPos.y);
                glm::vec2 worldBefore = uiWorldCamera.ScreenToWorld(mouseLocal);
                float zoomFactor = 1.0f + io.MouseWheel * 0.1f;
                float newZoom = std::clamp(uiWorldCamera.zoom * zoomFactor, 5.0f, 2000.0f);
                if (newZoom != uiWorldCamera.zoom) {
                    uiWorldCamera.zoom = newZoom;
                    glm::vec2 worldAfter = uiWorldCamera.ScreenToWorld(mouseLocal);
                    uiWorldCamera.position += (worldBefore - worldAfter);
                    uiWorldCameraActive = true;
                }
            }
            if (uiWorldHover) {
                glm::vec2 panDir(0.0f);
                if (ImGui::IsKeyDown(ImGuiKey_A)) panDir.x -= 1.0f;
                if (ImGui::IsKeyDown(ImGuiKey_D)) panDir.x += 1.0f;
                if (ImGui::IsKeyDown(ImGuiKey_W)) panDir.y += 1.0f;
                if (ImGui::IsKeyDown(ImGuiKey_S)) panDir.y -= 1.0f;
                if (panDir.x != 0.0f || panDir.y != 0.0f) {
                    float panSpeed = 6.0f;
                    if (ImGui::IsKeyDown(ImGuiKey_LeftShift) || ImGui::IsKeyDown(ImGuiKey_RightShift)) {
                        panSpeed *= 2.5f;
                    }
                    uiWorldCamera.position += panDir * (panSpeed * deltaTime);
                    uiWorldCameraActive = true;
                }
            }
        }
        if (playerCam && playerCam->camera.use2D && allowEditorUi && uiWorldCameraActive) {
            playerCam->position.x = uiWorldCamera.position.x;
            playerCam->position.y = uiWorldCamera.position.y;
            playerCam->camera.pixelsPerUnit = uiWorldCamera.zoom;
            syncLocalTransform(*playerCam);
            projectManager.currentProject.hasUnsavedChanges = true;
        }

        auto brighten = [](const ImVec4& c, float k) {
            return ImVec4(std::clamp(c.x * k, 0.0f, 1.0f),
                          std::clamp(c.y * k, 0.0f, 1.0f),
                          std::clamp(c.z * k, 0.0f, 1.0f),
                          c.w);
        };
        float animSpeed = 0.0f;
        if (uiAnimationMode == UIAnimationMode::Fluid) {
            animSpeed = 8.0f;
        } else if (uiAnimationMode == UIAnimationMode::Snappy) {
            animSpeed = 18.0f;
        }
        float animStep = (uiAnimationMode == UIAnimationMode::Off) ? 1.0f
            : (1.0f - std::exp(-animSpeed * ImGui::GetIO().DeltaTime));
        auto animateValue = [&](float& current, float target, bool immediate) {
            if (uiAnimationMode == UIAnimationMode::Off || immediate) {
                current = target;
            } else {
                current += (target - current) * animStep;
            }
            return current;
        };

        if (useWorldUi && showUIWorldGrid) {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 overlayMax(overlayPos.x + overlaySize.x, overlayPos.y + overlaySize.y);
            dl->PushClipRect(overlayPos, overlayMax, true);
            float step = 1.0f;
            float minPx = 30.0f;
            float maxPx = 140.0f;
            while (step * uiWorldCamera.zoom < minPx) step *= 2.0f;
            while (step * uiWorldCamera.zoom > maxPx) step *= 0.5f;

            glm::vec2 worldMin = uiWorldCamera.ScreenToWorld(glm::vec2(0.0f, overlaySize.y));
            glm::vec2 worldMax = uiWorldCamera.ScreenToWorld(glm::vec2(overlaySize.x, 0.0f));
            float startX = std::floor(worldMin.x / step) * step;
            float endX = std::ceil(worldMax.x / step) * step;
            float startY = std::floor(worldMin.y / step) * step;
            float endY = std::ceil(worldMax.y / step) * step;
            ImU32 gridColor = IM_COL32(90, 110, 140, 50);
            ImU32 axisColorX = IM_COL32(240, 120, 120, 170);
            ImU32 axisColorY = IM_COL32(120, 240, 150, 170);

            for (float x = startX; x <= endX; x += step) {
                ImVec2 p0 = worldToScreen(glm::vec2(x, worldMin.y));
                ImVec2 p1 = worldToScreen(glm::vec2(x, worldMax.y));
                dl->AddLine(p0, p1, gridColor, 1.0f);
            }
            for (float y = startY; y <= endY; y += step) {
                ImVec2 p0 = worldToScreen(glm::vec2(worldMin.x, y));
                ImVec2 p1 = worldToScreen(glm::vec2(worldMax.x, y));
                dl->AddLine(p0, p1, gridColor, 1.0f);
            }

            ImVec2 axisX0 = worldToScreen(glm::vec2(worldMin.x, 0.0f));
            ImVec2 axisX1 = worldToScreen(glm::vec2(worldMax.x, 0.0f));
            ImVec2 axisY0 = worldToScreen(glm::vec2(0.0f, worldMin.y));
            ImVec2 axisY1 = worldToScreen(glm::vec2(0.0f, worldMax.y));
            dl->AddLine(axisX0, axisX1, axisColorX, 2.0f);
            dl->AddLine(axisY0, axisY1, axisColorY, 2.0f);

            ImVec2 indicator = ImVec2(overlayPos.x + 36.0f, overlayPos.y + overlaySize.y - 36.0f);
            dl->AddLine(indicator, ImVec2(indicator.x + 22.0f, indicator.y), axisColorX, 2.0f);
            dl->AddLine(indicator, ImVec2(indicator.x, indicator.y - 22.0f), axisColorY, 2.0f);
            dl->AddText(ImVec2(indicator.x + 26.0f, indicator.y - 8.0f), axisColorX, "+X");
            dl->AddText(ImVec2(indicator.x - 16.0f, indicator.y - 30.0f), axisColorY, "+Y");
            dl->PopClipRect();
        }

        std::vector<SceneObject*> uiDrawList;
        uiDrawList.reserve(sceneObjects.size());
        for (auto& obj : sceneObjects) {
            if (!obj.enabled || !isUIType(obj)) continue;
            uiDrawList.push_back(&obj);
        }
        if (uiWorldMode) {
            std::stable_sort(uiDrawList.begin(), uiDrawList.end(),
                             [](const SceneObject* a, const SceneObject* b) {
                                 int orderA = (a->hasParallaxLayer2D && a->parallaxLayer2D.enabled) ? a->parallaxLayer2D.order : 0;
                                 int orderB = (b->hasParallaxLayer2D && b->parallaxLayer2D.enabled) ? b->parallaxLayer2D.order : 0;
                                 return orderA < orderB;
                             });
        }
        glm::vec2 worldViewMin = useWorldUi ? uiWorldCamera.ScreenToWorld(glm::vec2(0.0f, overlaySize.y)) : glm::vec2(0.0f);
        glm::vec2 worldViewMax = useWorldUi ? uiWorldCamera.ScreenToWorld(glm::vec2(overlaySize.x, 0.0f)) : glm::vec2(0.0f);

        for (SceneObject* objPtr : uiDrawList) {
            SceneObject& obj = *objPtr;
            ImVec2 rectMin, rectMax;
            if (useWorldUi) {
                resolveUIRectWorld(obj, rectMin, rectMax);
            } else {
                resolveUIRect(obj, rectMin, rectMax);
            }
            ImVec2 rectSize(rectMax.x - rectMin.x, rectMax.y - rectMin.y);
            if (rectSize.x <= 1.0f || rectSize.y <= 1.0f) continue;
            if (rectOutsideOverlay(rectMin, rectMax)) continue;

            ImGuiStyle savedStyle = ImGui::GetStyle();
            bool styleApplied = false;
            if (!obj.ui.stylePreset.empty()) {
                if (const auto* preset = getUIStylePreset(obj.ui.stylePreset)) {
                    ImGui::GetStyle() = preset->style;
                    styleApplied = true;
                }
            }

            if (obj.ui.type == UIElementType::Canvas) {
                ImDrawList* dl = ImGui::GetWindowDrawList();
                dl->AddRect(rectMin, rectMax, IM_COL32(110, 170, 255, 140), 6.0f, 0, 1.5f);
                if (styleApplied) ImGui::GetStyle() = savedStyle;
                continue;
            }

            ImVec2 drawMin = rectMin;
            ImVec2 drawMax = rectMax;
            ImVec2 drawSize(drawMax.x - drawMin.x, drawMax.y - drawMin.y);
            ImVec2 localMin(drawMin.x - overlayPos.x, drawMin.y - overlayPos.y);

            ImGui::PushID(obj.id);
            UIAnimationState& animState = uiAnimationStates[obj.id];
            if (!animState.initialized) {
                animState.sliderValue = obj.ui.sliderValue;
                animState.initialized = true;
            }
            if (obj.ui.type == UIElementType::Image || obj.ui.type == UIElementType::Sprite2D) {
                unsigned int texId = 0;
                if (!obj.albedoTexturePath.empty()) {
                    if (auto* tex = renderer.getTexture(obj.albedoTexturePath)) {
                        texId = tex->GetID();
                    }
                }
                ImVec4 tint(obj.ui.color.r, obj.ui.color.g, obj.ui.color.b, obj.ui.color.a);
                bool repeatX = useWorldUi && obj.hasParallaxLayer2D && obj.parallaxLayer2D.enabled && obj.parallaxLayer2D.repeatX;
                bool repeatY = useWorldUi && obj.hasParallaxLayer2D && obj.parallaxLayer2D.enabled && obj.parallaxLayer2D.repeatY;
                glm::vec2 spacing = obj.hasParallaxLayer2D ? obj.parallaxLayer2D.repeatSpacing : glm::vec2(0.0f);
                float stepX = obj.ui.size.x + spacing.x;
                float stepY = obj.ui.size.y + spacing.y;
                glm::vec2 baseWorldMin = worldViewMin;
                if (repeatX || repeatY) {
                    glm::vec2 sizeWorld(obj.ui.size.x, obj.ui.size.y);
                    ImVec2 pivotOffset = anchorToPivot(obj.ui.anchor, ImVec2(sizeWorld.x, sizeWorld.y));
                    glm::vec2 parentOffset = getWorldParentOffset(obj);
                    glm::vec2 worldPos = parentOffset + glm::vec2(obj.ui.position.x, obj.ui.position.y) + parallaxOffset(obj);
                    baseWorldMin = worldPos - glm::vec2(pivotOffset.x, pivotOffset.y);
                }
                float angle = glm::radians(obj.ui.rotation);
                auto drawImageRect = [&](const ImVec2& min, const ImVec2& max) {
                    ImVec2 size(max.x - min.x, max.y - min.y);
                    if (size.x <= 1.0f || size.y <= 1.0f) return;
                    if (std::abs(angle) > 1e-4f) {
                        ImDrawList* dl = ImGui::GetWindowDrawList();
                        ImVec2 center = ImVec2((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
                        ImVec2 half = ImVec2(size.x * 0.5f, size.y * 0.5f);
                        float c = std::cos(angle);
                        float s = std::sin(angle);
                        auto rotPt = [&](float x, float y) {
                            return ImVec2(center.x + x * c - y * s, center.y + x * s + y * c);
                        };
                        ImVec2 p0 = rotPt(-half.x, -half.y);
                        ImVec2 p1 = rotPt( half.x, -half.y);
                        ImVec2 p2 = rotPt( half.x,  half.y);
                        ImVec2 p3 = rotPt(-half.x,  half.y);
                        if (texId != 0) {
                            dl->AddImageQuad((ImTextureID)(intptr_t)texId, p0, p1, p2, p3,
                                             ImVec2(0, 1), ImVec2(1, 1), ImVec2(1, 0), ImVec2(0, 0),
                                             ImGui::GetColorU32(tint));
                        } else {
                            ImU32 fill = ImGui::GetColorU32(tint);
                            ImU32 border = ImGui::GetColorU32(brighten(tint, 0.85f));
                            dl->AddQuadFilled(p0, p1, p2, p3, fill);
                            dl->AddQuad(p0, p1, p2, p3, border, 2.0f);
                            ImVec2 textSize = ImGui::CalcTextSize(obj.ui.label.c_str());
                            ImVec2 textPos(center.x - textSize.x * 0.5f, center.y - textSize.y * 0.5f);
                            dl->AddText(textPos, IM_COL32(210, 210, 220, 220), obj.ui.label.c_str());
                        }
                    } else {
                        ImGui::SetCursorPos(ImVec2(min.x - overlayPos.x, min.y - overlayPos.y));
                        if (texId != 0) {
                            ImGui::Image((ImTextureID)(intptr_t)texId, size, ImVec2(0, 1), ImVec2(1, 0), tint, ImVec4(0, 0, 0, 0));
                        } else {
                            ImDrawList* dl = ImGui::GetWindowDrawList();
                            ImU32 fill = ImGui::GetColorU32(tint);
                            ImU32 border = ImGui::GetColorU32(brighten(tint, 0.85f));
                            dl->AddRectFilled(min, max, fill, 6.0f);
                            dl->AddRect(min, max, border, 6.0f);
                            ImVec2 textSize = ImGui::CalcTextSize(obj.ui.label.c_str());
                            ImVec2 textPos(min.x + (size.x - textSize.x) * 0.5f,
                                           min.y + (size.y - textSize.y) * 0.5f);
                            dl->AddText(textPos, IM_COL32(210, 210, 220, 220), obj.ui.label.c_str());
                        }
                    }
                    ImGui::Dummy(size);
                };

                if (repeatX || repeatY) {
                    int startX = repeatX ? static_cast<int>(std::floor((worldViewMin.x - baseWorldMin.x) / stepX)) - 1 : 0;
                    int endX = repeatX ? static_cast<int>(std::ceil((worldViewMax.x - baseWorldMin.x) / stepX)) + 1 : 0;
                    int startY = repeatY ? static_cast<int>(std::floor((worldViewMin.y - baseWorldMin.y) / stepY)) - 1 : 0;
                    int endY = repeatY ? static_cast<int>(std::ceil((worldViewMax.y - baseWorldMin.y) / stepY)) + 1 : 0;
                    for (int ix = startX; ix <= endX; ++ix) {
                        for (int iy = startY; iy <= endY; ++iy) {
                            float dx = repeatX ? (float)ix * stepX : 0.0f;
                            float dy = repeatY ? (float)iy * stepY : 0.0f;
                            glm::vec2 tileMin = baseWorldMin + glm::vec2(dx, dy);
                            ImVec2 s0 = worldToScreen(tileMin);
                            ImVec2 s1 = worldToScreen(tileMin + glm::vec2(obj.ui.size.x, obj.ui.size.y));
                            ImVec2 tMin(std::min(s0.x, s1.x), std::min(s0.y, s1.y));
                            ImVec2 tMax(std::max(s0.x, s1.x), std::max(s0.y, s1.y));
                            drawImageRect(tMin, tMax);
                        }
                    }
                } else if (std::abs(angle) > 1e-4f) {
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    ImVec2 center = ImVec2((drawMin.x + drawMax.x) * 0.5f, (drawMin.y + drawMax.y) * 0.5f);
                    ImVec2 half = ImVec2(drawSize.x * 0.5f, drawSize.y * 0.5f);
                    float c = std::cos(angle);
                    float s = std::sin(angle);
                    auto rotPt = [&](float x, float y) {
                        return ImVec2(center.x + x * c - y * s, center.y + x * s + y * c);
                    };
                    ImVec2 p0 = rotPt(-half.x, -half.y);
                    ImVec2 p1 = rotPt( half.x, -half.y);
                    ImVec2 p2 = rotPt( half.x,  half.y);
                    ImVec2 p3 = rotPt(-half.x,  half.y);
                    if (texId != 0) {
                        dl->AddImageQuad((ImTextureID)(intptr_t)texId, p0, p1, p2, p3,
                                         ImVec2(0, 1), ImVec2(1, 1), ImVec2(1, 0), ImVec2(0, 0),
                                         ImGui::GetColorU32(tint));
                    } else {
                        ImU32 fill = ImGui::GetColorU32(tint);
                        ImU32 border = ImGui::GetColorU32(brighten(tint, 0.85f));
                        dl->AddQuadFilled(p0, p1, p2, p3, fill);
                        dl->AddQuad(p0, p1, p2, p3, border, 2.0f);
                        ImVec2 textSize = ImGui::CalcTextSize(obj.ui.label.c_str());
                        ImVec2 textPos(center.x - textSize.x * 0.5f, center.y - textSize.y * 0.5f);
                        dl->AddText(textPos, IM_COL32(210, 210, 220, 220), obj.ui.label.c_str());
                    }
                    ImGui::Dummy(drawSize);
                } else {
                    ImGui::SetCursorPos(localMin);
                    if (texId != 0) {
                        ImGui::Image((ImTextureID)(intptr_t)texId, drawSize, ImVec2(0, 1), ImVec2(1, 0), tint, ImVec4(0, 0, 0, 0));
                    } else {
                        ImDrawList* dl = ImGui::GetWindowDrawList();
                        ImU32 fill = ImGui::GetColorU32(tint);
                        ImU32 border = ImGui::GetColorU32(brighten(tint, 0.85f));
                        dl->AddRectFilled(drawMin, drawMax, fill, 6.0f);
                        dl->AddRect(drawMin, drawMax, border, 6.0f);
                        ImVec2 textSize = ImGui::CalcTextSize(obj.ui.label.c_str());
                        ImVec2 textPos(drawMin.x + (drawSize.x - textSize.x) * 0.5f, drawMin.y + (drawSize.y - textSize.y) * 0.5f);
                        dl->AddText(textPos, IM_COL32(210, 210, 220, 220), obj.ui.label.c_str());
                        ImGui::Dummy(drawSize);
                    }
                }
            } else if (obj.ui.type == UIElementType::Slider) {
                ImGui::SetCursorPos(localMin);
                ImVec4 tint(obj.ui.color.r, obj.ui.color.g, obj.ui.color.b, obj.ui.color.a);
                if (obj.ui.sliderStyle == UISliderStyle::ImGui) {
                    ImGui::PushItemWidth(drawSize.x);
                    ImGui::BeginDisabled(!obj.ui.interactable || uiWorldCameraActive);
                    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(tint.x * 0.2f, tint.y * 0.2f, tint.z * 0.2f, tint.w * 0.6f));
                    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, brighten(tint, 0.5f));
                    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, brighten(tint, 0.7f));
                    ImGui::PushStyleColor(ImGuiCol_SliderGrab, brighten(tint, 0.9f));
                    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, brighten(tint, 1.1f));
                    if (ImGui::SliderFloat(obj.ui.label.c_str(), &obj.ui.sliderValue, obj.ui.sliderMin, obj.ui.sliderMax)) {
                        projectManager.currentProject.hasUnsavedChanges = true;
                    }
                    ImGui::PopStyleColor(5);
                    ImGui::EndDisabled();
                    ImGui::PopItemWidth();
                } else {
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    ImU32 bg = ImGui::GetColorU32(ImVec4(tint.x * 0.2f, tint.y * 0.2f, tint.z * 0.2f, tint.w * 0.6f));
                    ImU32 fill = ImGui::GetColorU32(tint);
                    ImU32 border = ImGui::GetColorU32(brighten(tint, 0.85f));
                    float minValue = obj.ui.sliderMin;
                    float maxValue = obj.ui.sliderMax;
                    float range = (maxValue - minValue);
                    if (range <= 1e-6f) range = 1.0f;
                    ImGui::BeginDisabled(!obj.ui.interactable || uiWorldCameraActive);
                    ImGui::InvisibleButton("##UISlider", drawSize);
                    bool held = obj.ui.interactable && ImGui::IsItemActive();
                    if (held && ImGui::IsMouseDown(ImGuiMouseButton_Left) && drawSize.x > 1.0f) {
                        float mouseT = (ImGui::GetIO().MousePos.x - drawMin.x) / drawSize.x;
                        mouseT = std::clamp(mouseT, 0.0f, 1.0f);
                        float newValue = minValue + mouseT * range;
                        if (newValue != obj.ui.sliderValue) {
                            obj.ui.sliderValue = newValue;
                            projectManager.currentProject.hasUnsavedChanges = true;
                        }
                    }
                    ImGui::EndDisabled();

                    animateValue(animState.sliderValue, obj.ui.sliderValue, held);
                    float displayValue = (uiAnimationMode == UIAnimationMode::Off) ? obj.ui.sliderValue : animState.sliderValue;
                    float t = (displayValue - minValue) / range;
                    t = std::clamp(t, 0.0f, 1.0f);

                    if (obj.ui.sliderStyle == UISliderStyle::Fill) {
                        float rounding = 6.0f;
                        ImVec2 fillMax(drawMin.x + drawSize.x * t, drawMax.y);
                        dl->AddRectFilled(drawMin, drawMax, bg, rounding);
                        if (fillMax.x > drawMin.x) {
                            dl->AddRectFilled(drawMin, fillMax, fill, rounding);
                        }
                        dl->AddRect(drawMin, drawMax, border, rounding);
                        ImVec2 textSize = ImGui::CalcTextSize(obj.ui.label.c_str());
                        ImVec2 textPos(drawMin.x + (drawSize.x - textSize.x) * 0.5f,
                                       drawMin.y + (drawSize.y - textSize.y) * 0.5f);
                        dl->AddText(textPos, IM_COL32(240, 240, 245, 220), obj.ui.label.c_str());
                    } else if (obj.ui.sliderStyle == UISliderStyle::Circle) {
                        ImVec2 center((drawMin.x + drawMax.x) * 0.5f, (drawMin.y + drawMax.y) * 0.5f);
                        float radius = std::max(2.0f, std::min(drawSize.x, drawSize.y) * 0.5f - 2.0f);
                        dl->AddCircleFilled(center, radius, bg, 32);
                        float start = -IM_PI * 0.5f;
                        float end = start + t * IM_PI * 2.0f;
                        dl->PathClear();
                        dl->PathArcTo(center, radius, start, end, 32);
                        dl->PathLineTo(center);
                        dl->PathFillConvex(fill);
                        dl->AddCircle(center, radius, border, 32, 2.0f);
                        ImVec2 textSize = ImGui::CalcTextSize(obj.ui.label.c_str());
                        ImVec2 textPos(center.x - textSize.x * 0.5f, center.y - textSize.y * 0.5f);
                        dl->AddText(textPos, IM_COL32(240, 240, 245, 220), obj.ui.label.c_str());
                    }
                }
            } else if (obj.ui.type == UIElementType::Button) {
                ImGui::SetCursorPos(localMin);
                ImVec4 tint(obj.ui.color.r, obj.ui.color.g, obj.ui.color.b, obj.ui.color.a);
                obj.ui.buttonPressed = false;
                if (obj.ui.buttonStyle == UIButtonStyle::ImGui) {
                    ImGui::PushStyleColor(ImGuiCol_Button, tint);
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, brighten(tint, 1.1f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, brighten(tint, 1.2f));
                    ImGui::BeginDisabled(!obj.ui.interactable || uiWorldCameraActive);
                    obj.ui.buttonPressed = ImGui::Button(obj.ui.label.c_str(), drawSize);
                    ImGui::EndDisabled();
                    ImGui::PopStyleColor(3);
                } else if (obj.ui.buttonStyle == UIButtonStyle::Outline) {
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    ImU32 border = ImGui::GetColorU32(tint);
                    ImGui::BeginDisabled(!obj.ui.interactable || uiWorldCameraActive);
                    if (ImGui::InvisibleButton("##UIButton", drawSize)) {
                        obj.ui.buttonPressed = obj.ui.interactable;
                    }
                    bool hovered = ImGui::IsItemHovered();
                    bool active = ImGui::IsItemActive();
                    ImGui::EndDisabled();
                    float hoverT = animateValue(animState.hover, hovered ? 1.0f : 0.0f, false);
                    float activeT = animateValue(animState.active, active ? 1.0f : 0.0f, false);
                    if (hoverT > 0.001f) {
                        ImVec4 hoverCol = brighten(tint, 0.45f);
                        hoverCol.w *= std::clamp(hoverT, 0.0f, 1.0f);
                        dl->AddRectFilled(drawMin, drawMax, ImGui::GetColorU32(hoverCol), 6.0f);
                    }
                    if (activeT > 0.001f) {
                        ImVec4 activeCol = brighten(tint, 0.65f);
                        activeCol.w *= std::clamp(activeT, 0.0f, 1.0f);
                        dl->AddRectFilled(drawMin, drawMax, ImGui::GetColorU32(activeCol), 6.0f);
                    }
                    dl->AddRect(drawMin, drawMax, border, 6.0f, 0, 2.0f);
                    ImVec2 textSize = ImGui::CalcTextSize(obj.ui.label.c_str());
                    ImVec2 textPos(drawMin.x + (drawSize.x - textSize.x) * 0.5f,
                                   drawMin.y + (drawSize.y - textSize.y) * 0.5f);
                    dl->AddText(textPos, IM_COL32(240, 240, 245, 220), obj.ui.label.c_str());
                }
            } else if (obj.ui.type == UIElementType::Text) {
                ImDrawList* dl = ImGui::GetWindowDrawList();
                ImVec4 tint(obj.ui.color.r, obj.ui.color.g, obj.ui.color.b, obj.ui.color.a);
                float scale = std::max(0.1f, obj.ui.textScale);
                float scaleFactor = useWorldUi ? std::max(0.01f, uiWorldCamera.zoom / 100.0f)
                                               : std::min(uiScaleX, uiScaleY);
                float fontSize = std::max(1.0f, ImGui::GetFontSize() * scale * scaleFactor);
                ImVec2 textPos = ImVec2(drawMin.x + 4.0f, drawMin.y + 2.0f);
                ImGui::PushClipRect(drawMin, drawMax, true);
                dl->AddText(ImGui::GetFont(), fontSize, textPos, ImGui::GetColorU32(tint), obj.ui.label.c_str());
                ImGui::PopClipRect();
            }
            ImGui::PopID();
            if (styleApplied) ImGui::GetStyle() = savedStyle;
        }

        bool gizmoUsed = false;
        if (!isPlaying) {
            SceneObject* selected = getSelectedObject();
            if (selected && isUIType(*selected) && selected->ui.type != UIElementType::Canvas) {
                ImVec2 rectMin, rectMax;
                ImVec2 parentMin, parentMax;
                if (useWorldUi) {
                    resolveUIRectWorld(*selected, rectMin, rectMax);
                } else {
                    resolveUIRect(*selected, rectMin, rectMax, &parentMin, &parentMax);
                }
                ImVec2 rectSize(rectMax.x - rectMin.x, rectMax.y - rectMin.y);

                ImGuizmo::OPERATION op = ImGuizmo::TRANSLATE;
                if (mCurrentGizmoOperation == ImGuizmo::SCALE) {
                    op = ImGuizmo::SCALE;
                } else if (mCurrentGizmoOperation == ImGuizmo::ROTATE) {
                    op = ImGuizmo::ROTATE;
                }
                glm::mat4 view(1.0f);
                glm::mat4 proj = glm::ortho(0.0f, (float)(imageMax.x - imageMin.x),
                                            (float)(imageMax.y - imageMin.y), 0.0f, -1.0f, 1.0f);
                glm::vec2 parentOffset = getWorldParentOffset(*selected);
                glm::vec2 pivotWorld = parentOffset + glm::vec2(selected->ui.position.x, selected->ui.position.y);
                ImVec2 pivotScreen;
                if (useWorldUi) {
                    pivotScreen = worldToScreen(pivotWorld);
                } else {
                    ImVec2 anchorPoint = anchorToPoint(selected->ui.anchor, parentMin, parentMax);
                    pivotScreen = ImVec2(anchorPoint.x + selected->ui.position.x * uiScaleX,
                                         anchorPoint.y + selected->ui.position.y * uiScaleY);
                }
                ImVec2 rectCenter(pivotScreen.x - imageMin.x, pivotScreen.y - imageMin.y);
                glm::vec3 gizmoScale(1.0f, 1.0f, 1.0f);
                if (op == ImGuizmo::SCALE) {
                    gizmoScale = glm::vec3(rectSize.x, rectSize.y, 1.0f);
                }
                glm::mat4 model(1.0f);
                model = glm::translate(model, glm::vec3(rectCenter.x, rectCenter.y, 0.0f));
                model = glm::rotate(model, glm::radians(selected->ui.rotation), glm::vec3(0.0f, 0.0f, 1.0f));
                model = glm::scale(model, gizmoScale);

                ImGuizmo::BeginFrame();
                ImGuizmo::Enable(true);
                ImGuizmo::SetOrthographic(true);
                ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
                ImGuizmo::SetRect(imageMin.x, imageMin.y, imageMax.x - imageMin.x, imageMax.y - imageMin.y);
                glm::mat4 delta(1.0f);
                ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(proj), op, ImGuizmo::LOCAL, glm::value_ptr(model), glm::value_ptr(delta));
                if (ImGuizmo::IsUsing()) {
                    glm::vec3 pos, rot, scl;
                    DecomposeMatrix(model, pos, rot, scl);
                    glm::vec3 euler = NormalizeEulerDegrees(glm::degrees(rot));
                    ImVec2 newPivot(imageMin.x + pos.x, imageMin.y + pos.y);
                    if (op == ImGuizmo::ROTATE) {
                        selected->ui.rotation = euler.z;
                    } else if (op == ImGuizmo::TRANSLATE) {
                        if (useWorldUi) {
                            glm::vec2 worldPivot = screenToWorld(newPivot);
                            selected->ui.position = worldPivot - parentOffset - parallaxOffset(*selected);
                        } else {
                            ImVec2 anchorPoint = anchorToPoint(selected->ui.anchor, parentMin, parentMax);
                            float invScaleX = (uiScaleX > 0.0f) ? 1.0f / uiScaleX : 1.0f;
                            float invScaleY = (uiScaleY > 0.0f) ? 1.0f / uiScaleY : 1.0f;
                            selected->ui.position = glm::vec2((newPivot.x - anchorPoint.x) * invScaleX,
                                                             (newPivot.y - anchorPoint.y) * invScaleY);
                        }
                    } else if (op == ImGuizmo::SCALE) {
                        ImVec2 newSize(std::max(1.0f, scl.x), std::max(1.0f, scl.y));
                        if (useWorldUi) {
                            glm::vec2 worldSize = glm::vec2(newSize.x, newSize.y) / uiWorldCamera.zoom;
                            selected->ui.position = pivotWorld - parentOffset - parallaxOffset(*selected);
                            selected->ui.size = worldSize;
                        } else {
                            float invScaleX = (uiScaleX > 0.0f) ? 1.0f / uiScaleX : 1.0f;
                            float invScaleY = (uiScaleY > 0.0f) ? 1.0f / uiScaleY : 1.0f;
                            selected->ui.size = glm::vec2(newSize.x * invScaleX, newSize.y * invScaleY);
                        }
                    }
                    projectManager.currentProject.hasUnsavedChanges = true;
                    gizmoUsed = true;
                }
            }
        }

        uiInteracting = ImGui::IsAnyItemActive() || gizmoUsed || uiWorldCameraActive;

        ImGui::EndChild();
        ImGui::PopStyleVar();
        bool clicked = imageHovered && isPlaying && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !uiInteracting;

        if (clicked && !gameViewCursorLocked) {
            gameViewCursorLocked = true;
        }
        if (gameViewCursorLocked && (!isPlaying || ImGui::IsKeyPressed(ImGuiKey_Escape))) {
            gameViewCursorLocked = false;
        }

        gameViewportFocused = windowFocused || gameViewCursorLocked;
        if (restoreUiWorldCamera) {
            uiWorldCamera = uiWorldCameraBackup;
        }
    } else {
        ImGui::TextDisabled("No player camera found (Camera Type: Player).");
        gameViewportFocused = ImGui::IsWindowFocused();
    }

    ImGui::End();
    ImGui::PopStyleVar();
}
#pragma endregion

#pragma region Play Controls Bar
void Engine::renderPlayControlsBar() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec2 buttonPadding(10.0f, 4.0f);
    const char* playLabel = isPlaying ? "Stop" : "Play";
    const char* pauseLabel = isPaused ? "Resume" : "Pause";
    const char* specLabel = specMode ? "Spec On" : "Spec Mode";
    auto brighten = [](ImVec4 color, float scale) {
        return ImVec4(
            std::min(1.0f, color.x * scale),
            std::min(1.0f, color.y * scale),
            std::min(1.0f, color.z * scale),
            color.w
        );
    };
    ImVec4 accent = ImGui::GetStyleColorVec4(ImGuiCol_Button);
    ImVec4 accentHover = ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered);
    ImVec4 accentActive = ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive);
    float animSpeed = 0.0f;
    if (uiAnimationMode == UIAnimationMode::Fluid) {
        animSpeed = 8.0f;
    } else if (uiAnimationMode == UIAnimationMode::Snappy) {
        animSpeed = 18.0f;
    }
    float animStep = (uiAnimationMode == UIAnimationMode::Off) ? 1.0f
        : (1.0f - std::exp(-animSpeed * ImGui::GetIO().DeltaTime));

    auto buttonWidth = [&](const char* label) {
        ImVec2 textSize = ImGui::CalcTextSize(label);
        return textSize.x + buttonPadding.x * 2.0f + style.FrameBorderSize * 2.0f;
    };

    float playWidth = buttonWidth(playLabel);
    float pauseWidth = buttonWidth(pauseLabel);
    float specWidth = buttonWidth(specLabel);
    float spacing = style.ItemSpacing.x;
    float totalWidth = playWidth + pauseWidth + specWidth + spacing * 2.0f;

    // Center the controls inside the dockspace menu bar.
    float regionMinX = ImGui::GetWindowContentRegionMin().x;
    float regionMaxX = ImGui::GetWindowContentRegionMax().x;
    float regionWidth = regionMaxX - regionMinX;
    float startX = (regionWidth - totalWidth) * 0.5f + regionMinX;
    if (startX < regionMinX) startX = regionMinX;

    ImVec2 cursor = ImGui::GetCursorPos();
    ImGui::SetCursorPos(ImVec2(startX, cursor.y));

    auto animatedButton = [&](const char* label, const ImVec2& baseSize) -> bool {
        ImGuiID id = ImGui::GetID(label);
        UIAnimationState& st = editorUiAnimationStates[id];
        float scale = 1.0f + st.hover * 0.08f + st.active * 0.14f;
        ImVec2 size(baseSize.x * scale, baseSize.y * scale);
        ImVec2 pos = ImGui::GetCursorScreenPos();
        bool pressed = ImGui::InvisibleButton(label, size);
        bool hovered = ImGui::IsItemHovered();
        bool active = ImGui::IsItemActive();
        if (uiAnimationMode == UIAnimationMode::Off) {
            st.hover = hovered ? 1.0f : 0.0f;
            st.active = active ? 1.0f : 0.0f;
        } else {
            float hoverTarget = hovered ? 1.0f : 0.0f;
            float activeTarget = active ? 1.0f : 0.0f;
            st.hover += (hoverTarget - st.hover) * animStep;
            st.active += (activeTarget - st.active) * animStep;
        }
        ImVec4 col = accent;
        if (active) col = accentActive;
        else if (hovered) col = accentHover;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 max(pos.x + size.x, pos.y + size.y);
        dl->AddRectFilled(pos, max, ImGui::GetColorU32(col), 6.0f);
        ImVec2 textSize = ImGui::CalcTextSize(label);
        ImVec2 textPos(pos.x + (size.x - textSize.x) * 0.5f,
                       pos.y + (size.y - textSize.y) * 0.5f);
        dl->AddText(textPos, ImGui::GetColorU32(ImGui::GetStyleColorVec4(ImGuiCol_Text)), label);
        return pressed;
    };

    ImVec2 basePlay(playWidth, ImGui::GetFrameHeight());
    ImVec2 basePause(pauseWidth, ImGui::GetFrameHeight());
    ImVec2 baseSpec(specWidth, ImGui::GetFrameHeight());

    bool playPressed = animatedButton(playLabel, basePlay);
    ImGui::SameLine(0.0f, spacing);
    bool pausePressed = animatedButton(pauseLabel, basePause);
    ImGui::SameLine(0.0f, spacing);
    bool specPressed = animatedButton(specLabel, baseSpec);

    if (playPressed) {
        bool newState = !isPlaying;
        if (newState) {
            if (physics.isReady() || physics.init()) {
                physics.onPlayStart(sceneObjects);
            } else {
                addConsoleMessage("PhysX failed to initialize; physics disabled for play mode", ConsoleMessageType::Warning);
            }
            audio.onPlayStart(sceneObjects);
            bool hasPlayerController = false;
            for (const auto& obj : sceneObjects) {
                if (obj.enabled && obj.hasPlayerController && obj.playerController.enabled) {
                    hasPlayerController = true;
                    break;
                }
            }
            if (hasPlayerController && showGameViewport) {
                gameViewCursorLocked = true;
                gameViewportFocused = true;
            }
        } else {
            physics.onPlayStop();
            audio.onPlayStop();
            isPaused = false;
            if (specMode && (physics.isReady() || physics.init())) {
                physics.onPlayStart(sceneObjects);
            }
        }
        isPlaying = newState;
    }
    if (pausePressed) {
        isPaused = !isPaused;
        if (isPaused) isPlaying = true; // placeholder: pausing implies we’re in play mode
    }
    if (specPressed) {
        bool enable = !specMode;
        if (enable && !physics.isReady() && !physics.init()) {
            addConsoleMessage("PhysX failed to initialize; spec mode disabled", ConsoleMessageType::Warning);
            enable = false;
        }
        specMode = enable;
        if (!isPlaying) {
            if (specMode) {
                physics.onPlayStart(sceneObjects);
                audio.onPlayStart(sceneObjects);
            } else {
                physics.onPlayStop();
                audio.onPlayStop();
            }
        }
    }
}

#pragma endregion

#pragma region Main Menu Bar
void Engine::renderMainMenuBar() {
    refreshScriptEditorWindows();

    if (ImGui::BeginMainMenuBar()) {
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(14.0f, 8.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12.0f, 6.0f));
        ImVec4 accent = ImGui::GetStyleColorVec4(ImGuiCol_CheckMark);
        ImVec4 subtle = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);

        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New Scene", "Ctrl+N")) {
                showNewSceneDialog = true;
                memset(newSceneName, 0, sizeof(newSceneName));
            }
            if (ImGui::MenuItem("Save Scene", "Ctrl+S")) {
                saveCurrentScene();
            }
            if (ImGui::MenuItem("Save Scene As...")) {
                showSaveSceneAsDialog = true;
                strncpy(saveSceneAsName, projectManager.currentProject.currentSceneName.c_str(),
                       sizeof(saveSceneAsName) - 1);
            }
            if (ImGui::MenuItem("Build Settings...")) {
                showBuildSettings = true;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Close Project")) {
                if (projectManager.currentProject.hasUnsavedChanges) {
                    saveCurrentScene();
                }
                projectManager.currentProject = Project();
                sceneObjects.clear();
                clearSelection();
                scriptEditorWindows.clear();
                scriptEditorWindowsDirty = true;
                resetBuildSettings();
                showBuildSettings = false;
                playerMode = false;
                autoStartRequested = false;
                showLauncher = true;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit")) {
                glfwSetWindowShouldClose(editorWindow, true);
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Edit")) {
            if (ImGui::MenuItem("Undo", "Ctrl+Z", false, false)) {}
            if (ImGui::MenuItem("Redo", "Ctrl+Y", false, false)) {}
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Hierarchy", nullptr, &showHierarchy);
            ImGui::MenuItem("Inspector", nullptr, &showInspector);
            ImGui::MenuItem("File Browser", nullptr, &showFileBrowser);
            ImGui::MenuItem("Console", nullptr, &showConsole);
            ImGui::MenuItem("Scripting", nullptr, &showScriptingWindow);
            ImGui::MenuItem("Project Manager", nullptr, &showProjectBrowser);
            ImGui::MenuItem("Mesh Builder", nullptr, &showMeshBuilder);
            ImGui::MenuItem("Environment", nullptr, &showEnvironmentWindow);
            ImGui::MenuItem("Camera", nullptr, &showCameraWindow);
            bool prevAnimationWindow = showAnimationWindow;
            ImGui::MenuItem("Animation", nullptr, &showAnimationWindow);
            if (prevAnimationWindow != showAnimationWindow) {
                saveEditorUserSettings();
            }
            ImGui::MenuItem("View Output", nullptr, &showViewOutput);
            ImGui::Separator();
            ImGui::MenuItem("UI World Overlay", nullptr, &uiWorldMode);
            ImGui::MenuItem("UI World Grid", nullptr, &showUIWorldGrid);
            ImGui::MenuItem("3D Grid", nullptr, &showSceneGrid3D);
            if (!scriptEditorWindows.empty()) {
                ImGui::Separator();
                ImGui::TextDisabled("Scripted Windows");
                for (auto& window : scriptEditorWindows) {
                    ImGui::MenuItem(window.label.c_str(), nullptr, &window.open);
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Fullscreen Viewport", "F11", viewportFullscreen)) {
                viewportFullscreen = !viewportFullscreen;
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Style")) {
            ImGui::TextDisabled("Editor Styles");
            for (size_t i = 0; i < uiStylePresets.size(); ++i) {
                bool selected = static_cast<int>(i) == uiStylePresetIndex;
                if (ImGui::MenuItem(uiStylePresets[i].name.c_str(), nullptr, selected)) {
                    applyUIStylePresetByName(uiStylePresets[i].name);
                    saveEditorUserSettings();
                }
            }
            ImGui::Separator();
            ImGui::TextDisabled("UI Animations");
            if (ImGui::MenuItem("Fluid", nullptr, uiAnimationMode == UIAnimationMode::Fluid)) {
                uiAnimationMode = UIAnimationMode::Fluid;
                saveEditorUserSettings();
            }
            if (ImGui::MenuItem("Snappy", nullptr, uiAnimationMode == UIAnimationMode::Snappy)) {
                uiAnimationMode = UIAnimationMode::Snappy;
                saveEditorUserSettings();
            }
            if (ImGui::MenuItem("Off", nullptr, uiAnimationMode == UIAnimationMode::Off)) {
                uiAnimationMode = UIAnimationMode::Off;
                saveEditorUserSettings();
            }
            ImGui::Separator();
            ImGui::MenuItem("Style Editor", nullptr, &showStyleEditor);
            if (ImGui::MenuItem("Export Theme + Layout")) {
                exportEditorThemeLayout();
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Scripts")) {
            auto toggleSpec = [&](bool enabled) {
                if (specMode == enabled) return;
                if (enabled && !physics.isReady() && !physics.init()) {
                    addConsoleMessage("PhysX failed to initialize; spec mode disabled", ConsoleMessageType::Warning);
                    specMode = false;
                    return;
                }
                specMode = enabled;
                if (!isPlaying) {
                    if (specMode) physics.onPlayStart(sceneObjects);
                    else physics.onPlayStop();
                }
            };
            bool specValue = specMode;
            if (ImGui::MenuItem("Spec Mode (run Script_Spec)", nullptr, &specValue)) {
                toggleSpec(specValue);
            }
            ImGui::MenuItem("Test Mode (run Script_TestEditor)", nullptr, &testMode);
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Create")) {
            if (ImGui::MenuItem("Empty")) addObject(ObjectType::Empty, "Empty");
            if (ImGui::MenuItem("Cube")) addObject(ObjectType::Cube, "Cube");
            if (ImGui::MenuItem("Sphere")) addObject(ObjectType::Sphere, "Sphere");
            if (ImGui::MenuItem("Capsule")) addObject(ObjectType::Capsule, "Capsule");
            if (ImGui::MenuItem("Plane")) addObject(ObjectType::Plane, "Plane");
            if (ImGui::MenuItem("Torus")) addObject(ObjectType::Torus, "Torus");
            if (ImGui::MenuItem("Mirror")) addObject(ObjectType::Mirror, "Mirror");
            if (ImGui::MenuItem("Camera")) addObject(ObjectType::Camera, "Camera");
            if (ImGui::MenuItem("Directional Light")) addObject(ObjectType::DirectionalLight, "Directional Light");
            if (ImGui::MenuItem("Point Light")) addObject(ObjectType::PointLight, "Point Light");
            if (ImGui::MenuItem("Spot Light")) addObject(ObjectType::SpotLight, "Spot Light");
            if (ImGui::MenuItem("Area Light")) addObject(ObjectType::AreaLight, "Area Light");
            if (ImGui::MenuItem("Post FX Node")) addObject(ObjectType::PostFXNode, "Post FX");
            if (ImGui::MenuItem("Audio Reverb Zone")) {
                addObject(ObjectType::Empty, "Reverb Zone");
                if (!sceneObjects.empty()) {
                    sceneObjects.back().hasReverbZone = true;
                    sceneObjects.back().reverbZone = ReverbZoneComponent{};
                    sceneObjects.back().reverbZone.boxSize = glm::max(sceneObjects.back().scale, glm::vec3(1.0f));
                }
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Help")) {
            if (ImGui::MenuItem("About")) {
                logToConsole("Modularity Engine - Beta V6.3\nThis build is in beta and might have issues,\n\nif you'd like to report any bugs or missing features, feel free to contact us!");
            }
            ImGui::EndMenu();
        }

        ImGui::Separator();
        ImGui::TextColored(subtle, "Workspace");
        ImGui::SameLine();
        auto drawWorkspaceButton = [&](const char* label, WorkspaceMode mode) {
            bool selected = (currentWorkspace == mode);
            ImVec4 base = ImGui::GetStyleColorVec4(ImGuiCol_Button);
            ImVec4 hover = ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered);
            ImVec4 active = ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive);
            if (selected) {
                base = ImVec4(accent.x * 0.9f, accent.y * 0.9f, accent.z * 0.9f, 1.0f);
                hover = ImVec4(accent.x, accent.y, accent.z, 1.0f);
                active = ImVec4(accent.x, accent.y, accent.z, 1.0f);
            }
            ImGui::PushStyleColor(ImGuiCol_Button, base);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hover);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, active);
            if (ImGui::Button(label)) {
                applyWorkspacePreset(mode, true);
                saveEditorUserSettings();
            }
            ImGui::PopStyleColor(3);
            ImGui::SameLine();
        };
        drawWorkspaceButton("Default", WorkspaceMode::Default);
        drawWorkspaceButton("Animation", WorkspaceMode::Animation);
        drawWorkspaceButton("Scripting", WorkspaceMode::Scripting);
        ImGui::TextColored(subtle, "Project");
        ImGui::SameLine();
        std::string projectLabel = projectManager.currentProject.name.empty() ?
            "New Project" : projectManager.currentProject.name;
        ImGui::TextColored(accent, "%s", projectLabel.c_str());
        ImGui::SameLine();
        ImGui::TextColored(subtle, "|");
        ImGui::SameLine();
        std::string sceneLabel = projectManager.currentProject.currentSceneName.empty() ?
            "No Scene Loaded" : projectManager.currentProject.currentSceneName;
        ImGui::TextUnformatted(sceneLabel.c_str());

        float rightX = ImGui::GetWindowWidth() - 220.0f;
        if (rightX > ImGui::GetCursorPosX()) {
            ImGui::SameLine(rightX);
        } else {
            ImGui::SameLine();
        }
        ImGui::TextColored(subtle, "Viewport");
        ImGui::SameLine();
        ImGui::TextColored(accent, viewportFullscreen ? "Fullscreen" : "Docked");

        ImGui::PopStyleVar(2);
        ImGui::EndMainMenuBar();
    }

    if (workspaceLayoutDirty) {
        buildWorkspaceLayout(currentWorkspace);
    }

    if (showStyleEditor) {
        if (ImGui::Begin("Style Editor", &showStyleEditor)) {
            if (ImGui::Button("Save Colors")) {
                saveEditorUserSettings();
            }
            ImGui::SameLine();
            if (ImGui::Button("Export Theme + Layout")) {
                exportEditorThemeLayout();
            }
            ImGui::SameLine();
            ImGui::TextDisabled("Applies to all presets");
            ImGui::Separator();
            ImGuiStyle& style = ImGui::GetStyle();
            ImGui::ShowStyleEditor(&style);
        }
        ImGui::End();
    }
}

void Engine::applyWorkspacePreset(WorkspaceMode mode, bool rebuildLayout) {
    currentWorkspace = mode;
    switch (mode) {
        case WorkspaceMode::Default:
            showHierarchy = true;
            showInspector = true;
            showFileBrowser = true;
            showConsole = true;
            showScriptingWindow = false;
            showAnimationWindow = false;
            showEnvironmentWindow = true;
            showCameraWindow = true;
            showGameViewport = true;
            break;
        case WorkspaceMode::Animation:
            showHierarchy = true;
            showInspector = true;
            showFileBrowser = false;
            showConsole = true;
            showScriptingWindow = false;
            showAnimationWindow = true;
            showEnvironmentWindow = false;
            showCameraWindow = false;
            showGameViewport = true;
            break;
        case WorkspaceMode::Scripting:
            showHierarchy = true;
            showInspector = true;
            showFileBrowser = true;
            showConsole = true;
            showScriptingWindow = true;
            showAnimationWindow = false;
            showEnvironmentWindow = false;
            showCameraWindow = false;
            showGameViewport = true;
            break;
    }

    fs::path layoutPath = getWorkspaceLayoutPath(mode);
    if (!layoutPath.empty() && fs::exists(layoutPath)) {
        pendingWorkspaceIniPath = layoutPath;
        pendingWorkspaceReload = true;
        workspaceLayoutDirty = false;
        return;
    }

    if (rebuildLayout) {
        buildWorkspaceLayout(mode);
    }
    workspaceLayoutDirty = false;
}

void Engine::buildWorkspaceLayout(WorkspaceMode mode) {
    ImGuiID dockspaceId = ImGui::GetID("MainDockspace");
    ImGuiViewport* viewport = ImGui::GetMainViewport();

    ImGui::DockBuilderRemoveNode(dockspaceId);
    ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->WorkSize);

    ImGuiID dockMain = dockspaceId;
    if (mode == WorkspaceMode::Default) {
        ImGuiID dockLeft = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Left, 0.20f, nullptr, &dockMain);
        ImGuiID dockRight = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Right, 0.26f, nullptr, &dockMain);
        ImGuiID dockBottom = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Down, 0.28f, nullptr, &dockMain);

        ImGui::DockBuilderDockWindow("Hierarchy", dockLeft);
        ImGui::DockBuilderDockWindow("Inspector", dockRight);
        ImGui::DockBuilderDockWindow("Project", dockBottom);
        ImGui::DockBuilderDockWindow("Console", dockBottom);
        ImGui::DockBuilderDockWindow("Viewport", dockMain);
        ImGui::DockBuilderDockWindow("Game Viewport", dockMain);
    } else if (mode == WorkspaceMode::Animation) {
        ImGuiID dockLeft = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Left, 0.20f, nullptr, &dockMain);
        ImGuiID dockRight = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Right, 0.25f, nullptr, &dockMain);
        ImGuiID dockBottom = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Down, 0.35f, nullptr, &dockMain);

        ImGui::DockBuilderDockWindow("Hierarchy", dockLeft);
        ImGui::DockBuilderDockWindow("Inspector", dockRight);
        ImGui::DockBuilderDockWindow("Animation", dockBottom);
        ImGui::DockBuilderDockWindow("Console", dockBottom);
        ImGui::DockBuilderDockWindow("Project", dockBottom);
        ImGui::DockBuilderDockWindow("Viewport", dockMain);
    } else {
        ImGuiID dockLeft = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Left, 0.25f, nullptr, &dockMain);
        ImGuiID dockRight = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Right, 0.35f, nullptr, &dockMain);
        ImGuiID dockBottom = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Down, 0.25f, nullptr, &dockMain);

        ImGui::DockBuilderDockWindow("Project", dockLeft);
        ImGui::DockBuilderDockWindow("Hierarchy", dockLeft);
        ImGui::DockBuilderDockWindow("Scripting", dockRight);
        ImGui::DockBuilderDockWindow("Inspector", dockRight);
        ImGui::DockBuilderDockWindow("Console", dockBottom);
        ImGui::DockBuilderDockWindow("Viewport", dockMain);
        ImGui::DockBuilderDockWindow("Game Viewport", dockMain);
    }

    ImGui::DockBuilderFinish(dockspaceId);
    workspaceLayoutDirty = false;
}

#pragma endregion

#pragma region Scene Viewport
// Final scene output for the editor viewport.
void Engine::renderViewport() {
    ImGuiWindowFlags viewportFlags = ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoScrollbar;

    if (viewportFullscreen) {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        viewportFlags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDocking;
    }

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("Viewport", nullptr, viewportFlags);
    ImGui::PopStyleVar();

    ImVec2 fullAvail = ImGui::GetContentRegionAvail();

    const float toolbarHeight = 0.0f;
    ImVec2 imageSize = fullAvail;
    imageSize.y = ImMax(1.0f, imageSize.y - toolbarHeight);

    if (imageSize.x > 0 && imageSize.y > 0) {
        viewportWidth  = static_cast<int>(imageSize.x);
        viewportHeight = static_cast<int>(imageSize.y);
        if (rendererInitialized) {
            renderer.resize(viewportWidth, viewportHeight);
        }
    }

    bool mouseOverViewportImage = false;
    bool blockSelection = false;

    if (rendererInitialized) {
        glm::mat4 proj = glm::perspective(
            glm::radians(FOV),
            (float)viewportWidth / (float)viewportHeight,
            NEAR_PLANE, FAR_PLANE
        );

        glm::mat4 view = camera.getViewMatrix();

        renderer.beginRender(view, proj, camera.position);
        renderer.renderScene(camera, sceneObjects, selectedObjectId, FOV, NEAR_PLANE, FAR_PLANE, collisionWireframe);
        unsigned int tex = renderer.getViewportTexture();

        ImGui::Image((void*)(intptr_t)tex, imageSize, ImVec2(0, 1), ImVec2(1, 0));

        ImVec2 imageMin = ImGui::GetItemRectMin();
        ImVec2 imageMax = ImGui::GetItemRectMax();
        mouseOverViewportImage = ImGui::IsItemHovered();
        ImDrawList* viewportDrawList = ImGui::GetWindowDrawList();

        if (uiWorldMode) {
            viewportDrawList->AddRectFilled(imageMin, imageMax, IM_COL32(14, 16, 20, 255));
        } else if (showSceneGrid3D) {
            auto projectToScreen = [&](const glm::vec3& p) -> std::optional<ImVec2> {
                glm::vec4 clip = proj * view * glm::vec4(p, 1.0f);
                if (clip.w <= 0.0f) return std::nullopt;
                glm::vec3 ndc = glm::vec3(clip) / clip.w;
                ImVec2 screen;
                screen.x = imageMin.x + (ndc.x * 0.5f + 0.5f) * (imageMax.x - imageMin.x);
                screen.y = imageMin.y + (1.0f - (ndc.y * 0.5f + 0.5f)) * (imageMax.y - imageMin.y);
                return screen;
            };
            auto clipLineToScreen = [&](glm::vec3 a, glm::vec3 b, ImVec2& outA, ImVec2& outB) -> bool {
                glm::vec4 va = view * glm::vec4(a, 1.0f);
                glm::vec4 vb = view * glm::vec4(b, 1.0f);
                const float nearZ = -NEAR_PLANE;
                if (va.z > nearZ && vb.z > nearZ) {
                    return false;
                }
                if (va.z > nearZ || vb.z > nearZ) {
                    float t = (nearZ - va.z) / (vb.z - va.z);
                    t = std::clamp(t, 0.0f, 1.0f);
                    glm::vec4 vclip = va + (vb - va) * t;
                    if (va.z > nearZ) {
                        va = vclip;
                    } else {
                        vb = vclip;
                    }
                }
                glm::vec4 ca = proj * va;
                glm::vec4 cb = proj * vb;
                if (ca.w <= 0.0f || cb.w <= 0.0f) return false;
                glm::vec3 ndcA = glm::vec3(ca) / ca.w;
                glm::vec3 ndcB = glm::vec3(cb) / cb.w;
                outA = ImVec2(
                    imageMin.x + (ndcA.x * 0.5f + 0.5f) * (imageMax.x - imageMin.x),
                    imageMin.y + (1.0f - (ndcA.y * 0.5f + 0.5f)) * (imageMax.y - imageMin.y)
                );
                outB = ImVec2(
                    imageMin.x + (ndcB.x * 0.5f + 0.5f) * (imageMax.x - imageMin.x),
                    imageMin.y + (1.0f - (ndcB.y * 0.5f + 0.5f)) * (imageMax.y - imageMin.y)
                );
                return true;
            };
            glm::vec2 camXZ(camera.position.x, camera.position.z);
            float camDist = glm::length(camXZ);
            float extent = 60.0f + camDist * 0.5f + std::abs(camera.position.y) * 4.0f;
            extent = std::clamp(extent, 60.0f, 1200.0f);
            float step = 1.0f;
            if (extent > 400.0f) {
                step = 20.0f;
            } else if (extent > 200.0f) {
                step = 10.0f;
            } else if (extent > 120.0f) {
                step = 5.0f;
            } else if (extent > 70.0f) {
                step = 2.0f;
            }
            float gridStrength = std::clamp(camDist / 120.0f, 0.15f, 1.0f);
            ImVec4 baseCol(0.35f, 0.43f, 0.55f, 0.55f * gridStrength);
            ImVec4 axisXCol(0.94f, 0.45f, 0.45f, 0.9f);
            ImVec4 axisZCol(0.5f, 0.7f, 0.95f, 0.9f);

            float startX = std::floor((camera.position.x - extent) / step) * step;
            float endX = std::floor((camera.position.x + extent) / step) * step;
            for (float x = startX; x <= endX; x += step) {
                float t = 1.0f - std::min(1.0f, std::abs(x - camera.position.x) / extent);
                ImVec4 col = baseCol;
                col.w *= t;
                if (col.w < 0.02f) continue;
                ImVec2 s0, s1;
                if (clipLineToScreen(glm::vec3(x, 0.0f, camera.position.z - extent),
                                     glm::vec3(x, 0.0f, camera.position.z + extent), s0, s1)) {
                    viewportDrawList->AddLine(s0, s1, ImGui::GetColorU32(col), 1.0f);
                }
            }
            float startZ = std::floor((camera.position.z - extent) / step) * step;
            float endZ = std::floor((camera.position.z + extent) / step) * step;
            for (float z = startZ; z <= endZ; z += step) {
                float t = 1.0f - std::min(1.0f, std::abs(z - camera.position.z) / extent);
                ImVec4 col = baseCol;
                col.w *= t;
                if (col.w < 0.02f) continue;
                ImVec2 s0, s1;
                if (clipLineToScreen(glm::vec3(camera.position.x - extent, 0.0f, z),
                                     glm::vec3(camera.position.x + extent, 0.0f, z), s0, s1)) {
                    viewportDrawList->AddLine(s0, s1, ImGui::GetColorU32(col), 1.0f);
                }
            }
            ImVec2 ax0, ax1;
            if (clipLineToScreen(glm::vec3(-extent, 0.0f, 0.0f), glm::vec3(extent, 0.0f, 0.0f), ax0, ax1)) {
                viewportDrawList->AddLine(ax0, ax1, ImGui::GetColorU32(axisXCol), 2.0f);
            }
            ImVec2 az0, az1;
            if (clipLineToScreen(glm::vec3(0.0f, 0.0f, -extent), glm::vec3(0.0f, 0.0f, extent), az0, az1)) {
                viewportDrawList->AddLine(az0, az1, ImGui::GetColorU32(axisZCol), 2.0f);
            }
        }

        auto importDroppedModel = [&](const fs::path& path) {
            std::error_code ec;
            fs::directory_entry entry(path, ec);
            if (ec || !fileBrowser.isModelFile(entry)) {
                return;
            }
            if (fileBrowser.isOBJFile(entry)) {
                importOBJToScene(path.string(), "");
            } else {
                importModelToScene(path.string(), "");
            }
        };

        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("FILE_PATH")) {
                const char* path = static_cast<const char*>(payload->Data);
                importDroppedModel(fs::path(path));
            }
            ImGui::EndDragDropTarget();
        }

        auto setCameraFacing = [&](const glm::vec3& dir) {
            glm::vec3 worldUp = glm::vec3(0, 1, 0);
            glm::vec3 n = glm::normalize(dir);
            glm::vec3 up = worldUp;
            if (std::abs(glm::dot(n, worldUp)) > 0.98f) {
                up = glm::vec3(0, 0, 1);
            }
            glm::vec3 right = glm::normalize(glm::cross(up, n));
            if (glm::length(right) < 1e-4f) {
                right = glm::vec3(1, 0, 0);
            }
            up = glm::normalize(glm::cross(n, right));

            camera.front = n;
            camera.up = up;
            camera.pitch = glm::degrees(std::asin(glm::clamp(n.y, -1.0f, 1.0f)));
            camera.pitch = glm::clamp(camera.pitch, -89.0f, 89.0f);
            camera.yaw = glm::degrees(std::atan2(n.z, n.x));
            camera.firstMouse = true;
        };

        // Draw small axis widget in top-right of viewport
        if (!uiWorldMode) {
            const float widgetSize = 94.0f;
            const float padding = 12.0f;
            ImVec2 center = ImVec2(
                imageMax.x - padding - widgetSize * 0.5f,
                imageMin.y + padding + widgetSize * 0.5f
            );
            float radius = widgetSize * 0.46f;
            ImU32 ringCol = ImGui::GetColorU32(ImVec4(0.07f, 0.07f, 0.1f, 0.9f));
            ImU32 ringBorder = ImGui::GetColorU32(ImVec4(1, 1, 1, 0.18f));
            viewportDrawList->AddCircleFilled(center, radius + 10.0f, ringCol, 48);
            viewportDrawList->AddCircle(center, radius + 10.0f, ringBorder, 48);
            viewportDrawList->AddCircle(center, radius + 3.0f, ImGui::GetColorU32(ImVec4(1,1,1,0.08f)), 32);
            viewportDrawList->AddCircleFilled(center, 5.5f, ImGui::GetColorU32(ImVec4(1,1,1,0.6f)), 24);

            glm::mat3 viewRot = glm::mat3(view);
            ImVec2 widgetMin = ImVec2(center.x - widgetSize * 0.5f, center.y - widgetSize * 0.5f);
            ImVec2 widgetMax = ImVec2(center.x + widgetSize * 0.5f, center.y + widgetSize * 0.5f);
            bool widgetHover = ImGui::IsMouseHoveringRect(widgetMin, widgetMax);
            struct AxisArrow {
                glm::vec3 dir;
                ImU32 color;
                const char* label;
            };
            AxisArrow arrows[] = {
                { glm::vec3(1, 0, 0), ImGui::GetColorU32(ImVec4(0.9f, 0.2f, 0.2f, 1.0f)), "X" },
                { glm::vec3(-1, 0, 0), ImGui::GetColorU32(ImVec4(0.6f, 0.2f, 0.2f, 1.0f)), "-X" },
                { glm::vec3(0, 1, 0), ImGui::GetColorU32(ImVec4(0.2f, 0.9f, 0.2f, 1.0f)), "Y" },
                { glm::vec3(0,-1, 0), ImGui::GetColorU32(ImVec4(0.2f, 0.6f, 0.2f, 1.0f)), "-Y" },
                { glm::vec3(0, 0, 1), ImGui::GetColorU32(ImVec4(0.2f, 0.4f, 0.9f, 1.0f)), "Z" },
                { glm::vec3(0, 0,-1), ImGui::GetColorU32(ImVec4(0.2f, 0.3f, 0.6f, 1.0f)), "-Z" },
            };

            ImVec2 mouse = ImGui::GetIO().MousePos;
            int clickedIdx = -1;
            float clickRadius = 12.0f;

            for (int i = 0; i < 6; ++i) {
                glm::vec3 camSpace = viewRot * arrows[i].dir;
                glm::vec2 dir2 = glm::normalize(glm::vec2(camSpace.x, -camSpace.y));
                float depthScale = glm::clamp(0.35f + 0.65f * ((camSpace.z + 1.0f) * 0.5f), 0.25f, 1.0f);
                float len = radius * depthScale;
                ImVec2 tip = ImVec2(center.x + dir2.x * len, center.y + dir2.y * len);

                ImVec2 base1 = ImVec2(center.x + dir2.x * (len * 0.55f) + dir2.y * (len * 0.12f),
                                      center.y + dir2.y * (len * 0.55f) - dir2.x * (len * 0.12f));
                ImVec2 base2 = ImVec2(center.x + dir2.x * (len * 0.55f) - dir2.y * (len * 0.12f),
                                      center.y + dir2.y * (len * 0.55f) + dir2.x * (len * 0.12f));

                viewportDrawList->AddTriangleFilled(base1, tip, base2, arrows[i].color);
                viewportDrawList->AddTriangle(base1, tip, base2, ImGui::GetColorU32(ImVec4(0,0,0,0.35f)));

                ImVec2 labelPos = ImVec2(center.x + dir2.x * (len * 0.78f), center.y + dir2.y * (len * 0.78f));
                viewportDrawList->AddCircleFilled(labelPos, 6.0f, ImGui::GetColorU32(ImVec4(0,0,0,0.5f)), 12);
                viewportDrawList->AddText(ImVec2(labelPos.x - 4.0f, labelPos.y - 7.0f), ImGui::GetColorU32(ImVec4(1,1,1,0.95f)), arrows[i].label);

                if (widgetHover) {
                    float dx = mouse.x - tip.x;
                    float dy = mouse.y - tip.y;
                    if (std::sqrt(dx*dx + dy*dy) <= clickRadius && ImGui::IsMouseReleased(0)) {
                        clickedIdx = i;
                    }
                }
            }

            if (clickedIdx >= 0) {
                setCameraFacing(arrows[clickedIdx].dir);
            }

            // Prevent viewport picking when interacting with the axis widget.
            if (widgetHover) {
                blockSelection = true;
            }
        }

    bool windowActive = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) ||
                        ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);
    const float toolbarWidthEstimate = 520.0f;
    const float toolbarHeightEstimate = 42.0f;
    static ImVec2 toolbarSizeCache(toolbarWidthEstimate, toolbarHeightEstimate);
    ImVec2 toolbarRectMin(imageMin.x, imageMin.y);
    ImVec2 toolbarRectMax(imageMin.x, imageMin.y);
    auto computeToolbarRect = [&]() {
        ImVec2 desiredBottomLeft = ImVec2(imageMin.x + 12.0f, imageMax.y - 12.0f);
        float minX = imageMin.x + 12.0f;
        float maxX = imageMax.x - 12.0f;
        float toolbarLeft = desiredBottomLeft.x;
        if (toolbarLeft + toolbarSizeCache.x > maxX) toolbarLeft = maxX - toolbarSizeCache.x;
        if (toolbarLeft < minX) toolbarLeft = minX;
        float minY = imageMin.y + 12.0f;
        float toolbarTop = desiredBottomLeft.y - toolbarSizeCache.y;
        if (toolbarTop < minY) toolbarTop = minY;
        toolbarRectMin = ImVec2(toolbarLeft, toolbarTop);
        toolbarRectMax = ImVec2(toolbarLeft + toolbarSizeCache.x, toolbarTop + toolbarSizeCache.y);
    };
    computeToolbarRect();
    static float toolbarHideAnim = 0.0f;
    float toolbarHideOffset = toolbarSizeCache.y + 14.0f;
    ImVec2 toolbarRectMinAnim = ImVec2(toolbarRectMin.x, toolbarRectMin.y + toolbarHideOffset * toolbarHideAnim);
    ImVec2 toolbarRectMaxAnim = ImVec2(toolbarRectMax.x, toolbarRectMax.y + toolbarHideOffset * toolbarHideAnim);
    ImVec2 toolbarMousePos = ImGui::GetIO().MousePos;
    bool mouseInToolbar = (toolbarMousePos.x >= toolbarRectMinAnim.x && toolbarMousePos.x <= toolbarRectMaxAnim.x &&
                           toolbarMousePos.y >= toolbarRectMinAnim.y && toolbarMousePos.y <= toolbarRectMaxAnim.y);
    bool toolbarAllowed = !gameViewportFocused && !(isPlaying && showGameViewport);
    bool showViewportToolbar = toolbarAllowed &&
                               (windowActive || mouseOverViewportImage || mouseInToolbar || toolbarHideAnim < 0.999f);
    bool toolbarHover = toolbarAllowed && (mouseOverViewportImage || mouseInToolbar);
    float toolbarAnimSpeed = 10.0f;
    float toolbarTarget = toolbarHover ? 0.0f : 1.0f;
    float toolbarAnimStep = 1.0f - std::exp(-toolbarAnimSpeed * ImGui::GetIO().DeltaTime);
    toolbarHideAnim += (toolbarTarget - toolbarHideAnim) * toolbarAnimStep;
    toolbarRectMin.y += toolbarHideOffset * toolbarHideAnim;
    toolbarRectMax.y += toolbarHideOffset * toolbarHideAnim;

    bool uiWorldCameraActive = false;
    if (uiWorldMode) {
        auto find3DCanvasId = [&](const SceneObject& target) -> int {
            const SceneObject* current = &target;
            while (current) {
                if (current->hasUI && current->ui.type == UIElementType::Canvas && current->ui.renderIn3D) {
                    return current->id;
                }
                if (current->parentId < 0) break;
                auto pit = std::find_if(sceneObjects.begin(), sceneObjects.end(),
                    [&](const SceneObject& o) { return o.id == current->parentId; });
                if (pit == sceneObjects.end()) break;
                current = &(*pit);
            }
            return -1;
        };
        auto isUiOn3DCanvas = [&](const SceneObject& target) {
            return find3DCanvasId(target) >= 0;
        };
        int editCanvas3DId = -1;
        if (SceneObject* selected = getSelectedObject()) {
            editCanvas3DId = find3DCanvasId(*selected);
        }
        auto isUIType = [&](const SceneObject& target) {
            if (!target.hasUI || target.ui.type == UIElementType::None) return false;
            int canvasId = find3DCanvasId(target);
            return (canvasId < 0) || (canvasId == editCanvas3DId);
        };
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::SetCursorScreenPos(imageMin);
        ImGui::BeginChild("SceneUIWorldOverlay",
                          ImVec2(imageMax.x - imageMin.x, imageMax.y - imageMin.y),
                          false,
                          ImGuiWindowFlags_NoTitleBar |
                          ImGuiWindowFlags_NoResize |
                          ImGuiWindowFlags_NoMove |
                          ImGuiWindowFlags_NoScrollbar |
                          ImGuiWindowFlags_NoScrollWithMouse |
                          ImGuiWindowFlags_NoSavedSettings |
                          ImGuiWindowFlags_NoBackground);

        ImVec2 overlayPos = ImGui::GetWindowPos();
        ImVec2 overlaySize = ImGui::GetWindowSize();
        uiWorldCamera.viewportSize = glm::vec2(overlaySize.x, overlaySize.y);
        ImVec2 mousePos = ImGui::GetIO().MousePos;
        bool mouseInToolbar = (mousePos.x >= toolbarRectMin.x && mousePos.x <= toolbarRectMax.x &&
                               mousePos.y >= toolbarRectMin.y && mousePos.y <= toolbarRectMax.y);
        bool uiWorldHover = (mouseOverViewportImage || ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)) && !mouseInToolbar;
        auto worldToScreen = [&](const glm::vec2& world) {
            glm::vec2 local = uiWorldCamera.WorldToScreen(world);
            return ImVec2(overlayPos.x + local.x, overlayPos.y + local.y);
        };
        auto screenToWorld = [&](const ImVec2& screen) {
            glm::vec2 local(screen.x - overlayPos.x, screen.y - overlayPos.y);
            return uiWorldCamera.ScreenToWorld(local);
        };
        auto getWorldParentOffset = [&](const SceneObject& obj) {
            glm::vec2 offset(0.0f);
            const SceneObject* current = &obj;
            while (current && current->parentId >= 0) {
                auto pit = std::find_if(sceneObjects.begin(), sceneObjects.end(),
                    [&](const SceneObject& o) { return o.id == current->parentId; });
                if (pit == sceneObjects.end()) break;
                current = &(*pit);
                if (current->hasUI && current->ui.type != UIElementType::None) {
                    offset += glm::vec2(current->ui.position.x, current->ui.position.y);
                }
            }
            return offset;
        };
        auto parallaxOffset = [&](const SceneObject& obj) {
            if (!obj.hasParallaxLayer2D || !obj.parallaxLayer2D.enabled) return glm::vec2(0.0f);
            float factor = std::clamp(obj.parallaxLayer2D.factor, 0.0f, 1.0f);
            return uiWorldCamera.position * (1.0f - factor);
        };
        auto resolveUIRectWorld = [&](const SceneObject& obj, ImVec2& outMin, ImVec2& outMax) {
            glm::vec2 parentOffset = getWorldParentOffset(obj);
            glm::vec2 worldPos = parentOffset + glm::vec2(obj.ui.position.x, obj.ui.position.y) + parallaxOffset(obj);
            glm::vec2 sizeWorld(obj.ui.size.x, obj.ui.size.y);
            ImVec2 pivotOffset = ImVec2(sizeWorld.x * 0.5f, sizeWorld.y * 0.5f);
            switch (obj.ui.anchor) {
                case UIAnchor::TopLeft: pivotOffset = ImVec2(0.0f, 0.0f); break;
                case UIAnchor::TopRight: pivotOffset = ImVec2(sizeWorld.x, 0.0f); break;
                case UIAnchor::BottomLeft: pivotOffset = ImVec2(0.0f, sizeWorld.y); break;
                case UIAnchor::BottomRight: pivotOffset = ImVec2(sizeWorld.x, sizeWorld.y); break;
                default: break;
            }
            glm::vec2 worldMin = worldPos - glm::vec2(pivotOffset.x, pivotOffset.y);
            glm::vec2 worldMax = worldMin + sizeWorld;
            ImVec2 s0 = worldToScreen(worldMin);
            ImVec2 s1 = worldToScreen(worldMax);
            outMin = ImVec2(std::min(s0.x, s1.x), std::min(s0.y, s1.y));
            outMax = ImVec2(std::max(s0.x, s1.x), std::max(s0.y, s1.y));
        };
        auto rectOutsideOverlay = [&](const ImVec2& min, const ImVec2& max) {
            return (max.x < overlayPos.x || min.x > overlayPos.x + overlaySize.x ||
                    max.y < overlayPos.y || min.y > overlayPos.y + overlaySize.y);
        };

        if (uiWorldHover) {
            ImGuiIO& io = ImGui::GetIO();
            bool panHeld = ImGui::IsMouseDown(ImGuiMouseButton_Middle) ||
                (ImGui::IsKeyDown(ImGuiKey_Space) && ImGui::IsMouseDown(ImGuiMouseButton_Left));
            if (panHeld) {
                uiWorldPanning = true;
            } else if (!ImGui::IsMouseDown(ImGuiMouseButton_Middle) &&
                       !(ImGui::IsKeyDown(ImGuiKey_Space) && ImGui::IsMouseDown(ImGuiMouseButton_Left))) {
                uiWorldPanning = false;
            }
            if (uiWorldPanning) {
                ImVec2 delta = io.MouseDelta;
                if (delta.x != 0.0f || delta.y != 0.0f) {
                    uiWorldCamera.position.x -= delta.x / uiWorldCamera.zoom;
                    uiWorldCamera.position.y += delta.y / uiWorldCamera.zoom;
                }
                uiWorldCameraActive = true;
            }
            if (io.MouseWheel != 0.0f) {
                glm::vec2 mouseLocal(io.MousePos.x - overlayPos.x, io.MousePos.y - overlayPos.y);
                glm::vec2 worldBefore = uiWorldCamera.ScreenToWorld(mouseLocal);
                float zoomFactor = 1.0f + io.MouseWheel * 0.1f;
                float newZoom = std::clamp(uiWorldCamera.zoom * zoomFactor, 5.0f, 2000.0f);
                if (newZoom != uiWorldCamera.zoom) {
                    uiWorldCamera.zoom = newZoom;
                    glm::vec2 worldAfter = uiWorldCamera.ScreenToWorld(mouseLocal);
                    uiWorldCamera.position += (worldBefore - worldAfter);
                    uiWorldCameraActive = true;
                }
            }
            glm::vec2 panDir(0.0f);
            if (ImGui::IsKeyDown(ImGuiKey_A)) panDir.x -= 1.0f;
            if (ImGui::IsKeyDown(ImGuiKey_D)) panDir.x += 1.0f;
            if (ImGui::IsKeyDown(ImGuiKey_W)) panDir.y += 1.0f;
            if (ImGui::IsKeyDown(ImGuiKey_S)) panDir.y -= 1.0f;
            if (panDir.x != 0.0f || panDir.y != 0.0f) {
                float panSpeed = 6.0f;
                if (ImGui::IsKeyDown(ImGuiKey_LeftShift) || ImGui::IsKeyDown(ImGuiKey_RightShift)) {
                    panSpeed *= 2.5f;
                }
                uiWorldCamera.position += panDir * (panSpeed * deltaTime);
                uiWorldCameraActive = true;
            }
        }

        auto brighten = [](const ImVec4& c, float k) {
            return ImVec4(std::clamp(c.x * k, 0.0f, 1.0f),
                          std::clamp(c.y * k, 0.0f, 1.0f),
                          std::clamp(c.z * k, 0.0f, 1.0f),
                          c.w);
        };

        if (showUIWorldGrid) {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 overlayMax(overlayPos.x + overlaySize.x, overlayPos.y + overlaySize.y);
            if (showViewportToolbar && toolbarRectMin.y > overlayPos.y) {
                overlayMax.y = std::min(overlayMax.y, toolbarRectMin.y - 2.0f);
            }
            dl->PushClipRect(overlayPos, overlayMax, true);
            float step = 1.0f;
            float minPx = 30.0f;
            float maxPx = 140.0f;
            while (step * uiWorldCamera.zoom < minPx) step *= 2.0f;
            while (step * uiWorldCamera.zoom > maxPx) step *= 0.5f;

            glm::vec2 worldMin = uiWorldCamera.ScreenToWorld(glm::vec2(0.0f, overlaySize.y));
            glm::vec2 worldMax = uiWorldCamera.ScreenToWorld(glm::vec2(overlaySize.x, 0.0f));
            float startX = std::floor(worldMin.x / step) * step;
            float endX = std::ceil(worldMax.x / step) * step;
            float startY = std::floor(worldMin.y / step) * step;
            float endY = std::ceil(worldMax.y / step) * step;
            ImU32 gridColor = IM_COL32(90, 110, 140, 50);
            ImU32 axisColorX = IM_COL32(240, 120, 120, 170);
            ImU32 axisColorY = IM_COL32(120, 240, 150, 170);

            for (float x = startX; x <= endX; x += step) {
                ImVec2 p0 = worldToScreen(glm::vec2(x, worldMin.y));
                ImVec2 p1 = worldToScreen(glm::vec2(x, worldMax.y));
                dl->AddLine(p0, p1, gridColor, 1.0f);
            }
            for (float y = startY; y <= endY; y += step) {
                ImVec2 p0 = worldToScreen(glm::vec2(worldMin.x, y));
                ImVec2 p1 = worldToScreen(glm::vec2(worldMax.x, y));
                dl->AddLine(p0, p1, gridColor, 1.0f);
            }

            ImVec2 axisX0 = worldToScreen(glm::vec2(worldMin.x, 0.0f));
            ImVec2 axisX1 = worldToScreen(glm::vec2(worldMax.x, 0.0f));
            ImVec2 axisY0 = worldToScreen(glm::vec2(0.0f, worldMin.y));
            ImVec2 axisY1 = worldToScreen(glm::vec2(0.0f, worldMax.y));
            dl->AddLine(axisX0, axisX1, axisColorX, 2.0f);
            dl->AddLine(axisY0, axisY1, axisColorY, 2.0f);

            ImVec2 indicator = ImVec2(overlayPos.x + 36.0f, overlayPos.y + overlaySize.y - 36.0f);
            dl->AddLine(indicator, ImVec2(indicator.x + 22.0f, indicator.y), axisColorX, 2.0f);
            dl->AddLine(indicator, ImVec2(indicator.x, indicator.y - 22.0f), axisColorY, 2.0f);
            dl->AddText(ImVec2(indicator.x + 26.0f, indicator.y - 8.0f), axisColorX, "+X");
            dl->AddText(ImVec2(indicator.x - 16.0f, indicator.y - 30.0f), axisColorY, "+Y");
            dl->PopClipRect();
        }

        float animSpeed = 0.0f;
        if (uiAnimationMode == UIAnimationMode::Fluid) {
            animSpeed = 8.0f;
        } else if (uiAnimationMode == UIAnimationMode::Snappy) {
            animSpeed = 18.0f;
        }
        float animStep = (uiAnimationMode == UIAnimationMode::Off) ? 1.0f
            : (1.0f - std::exp(-animSpeed * ImGui::GetIO().DeltaTime));
        auto animateValue = [&](float& current, float target, bool immediate) {
            if (uiAnimationMode == UIAnimationMode::Off || immediate) {
                current = target;
            } else {
                current += (target - current) * animStep;
            }
            return current;
        };

        std::vector<SceneObject*> uiDrawList;
        uiDrawList.reserve(sceneObjects.size());
        for (auto& obj : sceneObjects) {
            if (!obj.enabled || !isUIType(obj)) continue;
            uiDrawList.push_back(&obj);
        }
        if (uiWorldMode) {
            std::stable_sort(uiDrawList.begin(), uiDrawList.end(),
                             [](const SceneObject* a, const SceneObject* b) {
                                 int orderA = (a->hasParallaxLayer2D && a->parallaxLayer2D.enabled) ? a->parallaxLayer2D.order : 0;
                                 int orderB = (b->hasParallaxLayer2D && b->parallaxLayer2D.enabled) ? b->parallaxLayer2D.order : 0;
                                 return orderA < orderB;
                             });
        }

        glm::vec2 worldViewMin = uiWorldCamera.ScreenToWorld(glm::vec2(0.0f, overlaySize.y));
        glm::vec2 worldViewMax = uiWorldCamera.ScreenToWorld(glm::vec2(overlaySize.x, 0.0f));

        for (SceneObject* objPtr : uiDrawList) {
            SceneObject& obj = *objPtr;
            ImVec2 rectMin, rectMax;
            resolveUIRectWorld(obj, rectMin, rectMax);
            ImVec2 rectSize(rectMax.x - rectMin.x, rectMax.y - rectMin.y);
            if (rectSize.x <= 1.0f || rectSize.y <= 1.0f) continue;
            if (rectOutsideOverlay(rectMin, rectMax)) continue;
            if (rectMin.y < toolbarRectMax.y && rectMax.y > toolbarRectMin.y &&
                rectMin.x < toolbarRectMax.x && rectMax.x > toolbarRectMin.x) {
                continue;
            }

            ImGuiStyle savedStyle = ImGui::GetStyle();
            bool styleApplied = false;
            if (!obj.ui.stylePreset.empty()) {
                if (const auto* preset = getUIStylePreset(obj.ui.stylePreset)) {
                    ImGui::GetStyle() = preset->style;
                    styleApplied = true;
                }
            }

            if (obj.ui.type == UIElementType::Canvas) {
                ImDrawList* dl = ImGui::GetWindowDrawList();
                dl->AddRect(rectMin, rectMax, IM_COL32(110, 170, 255, 140), 6.0f, 0, 1.5f);
                if (styleApplied) ImGui::GetStyle() = savedStyle;
                continue;
            }

            ImVec2 drawMin = rectMin;
            ImVec2 drawMax = rectMax;
            ImVec2 drawSize(drawMax.x - drawMin.x, drawMax.y - drawMin.y);
            ImVec2 localMin(drawMin.x - overlayPos.x, drawMin.y - overlayPos.y);

            ImGui::PushID(obj.id);
            UIAnimationState& animState = uiAnimationStates[obj.id];
            if (!animState.initialized) {
                animState.sliderValue = obj.ui.sliderValue;
                animState.initialized = true;
            }
            if (obj.ui.type == UIElementType::Image || obj.ui.type == UIElementType::Sprite2D) {
                unsigned int texId = 0;
                if (!obj.albedoTexturePath.empty()) {
                    if (auto* tex = renderer.getTexture(obj.albedoTexturePath)) {
                        texId = tex->GetID();
                    }
                }
                ImVec4 tint(obj.ui.color.r, obj.ui.color.g, obj.ui.color.b, obj.ui.color.a);
                bool repeatX = obj.hasParallaxLayer2D && obj.parallaxLayer2D.enabled && obj.parallaxLayer2D.repeatX;
                bool repeatY = obj.hasParallaxLayer2D && obj.parallaxLayer2D.enabled && obj.parallaxLayer2D.repeatY;
                glm::vec2 spacing = obj.hasParallaxLayer2D ? obj.parallaxLayer2D.repeatSpacing : glm::vec2(0.0f);
                float stepX = drawSize.x + spacing.x;
                float stepY = drawSize.y + spacing.y;
                glm::vec2 baseWorldMin = worldViewMin;
                if (repeatX || repeatY) {
                    glm::vec2 sizeWorld(obj.ui.size.x, obj.ui.size.y);
                    ImVec2 pivotOffset = ImVec2(sizeWorld.x * 0.5f, sizeWorld.y * 0.5f);
                    switch (obj.ui.anchor) {
                        case UIAnchor::TopLeft: pivotOffset = ImVec2(0.0f, 0.0f); break;
                        case UIAnchor::TopRight: pivotOffset = ImVec2(sizeWorld.x, 0.0f); break;
                        case UIAnchor::BottomLeft: pivotOffset = ImVec2(0.0f, sizeWorld.y); break;
                        case UIAnchor::BottomRight: pivotOffset = ImVec2(sizeWorld.x, sizeWorld.y); break;
                        default: break;
                    }
                    glm::vec2 parentOffset = getWorldParentOffset(obj);
                    glm::vec2 worldPos = parentOffset + glm::vec2(obj.ui.position.x, obj.ui.position.y) + parallaxOffset(obj);
                    baseWorldMin = worldPos - glm::vec2(pivotOffset.x, pivotOffset.y);
                }
                float angle = glm::radians(obj.ui.rotation);
                auto drawImageRect = [&](const ImVec2& min, const ImVec2& max) {
                    ImVec2 size(max.x - min.x, max.y - min.y);
                    if (size.x <= 1.0f || size.y <= 1.0f) return;
                    ImVec2 drawMinLocal(min.x, min.y);
                    ImVec2 drawMaxLocal(max.x, max.y);
                    if (std::abs(angle) > 1e-4f) {
                        ImDrawList* dl = ImGui::GetWindowDrawList();
                        ImVec2 center((drawMinLocal.x + drawMaxLocal.x) * 0.5f, (drawMinLocal.y + drawMaxLocal.y) * 0.5f);
                        ImVec2 half(size.x * 0.5f, size.y * 0.5f);
                        float c = std::cos(angle);
                        float s = std::sin(angle);
                        auto rotPt = [&](float x, float y) {
                            return ImVec2(center.x + x * c - y * s, center.y + x * s + y * c);
                        };
                        ImVec2 p0 = rotPt(-half.x, -half.y);
                        ImVec2 p1 = rotPt( half.x, -half.y);
                        ImVec2 p2 = rotPt( half.x,  half.y);
                        ImVec2 p3 = rotPt(-half.x,  half.y);
                        if (texId != 0) {
                            dl->AddImageQuad((ImTextureID)(intptr_t)texId, p0, p1, p2, p3,
                                             ImVec2(0, 1), ImVec2(1, 1), ImVec2(1, 0), ImVec2(0, 0),
                                             ImGui::GetColorU32(tint));
                        } else {
                            ImU32 fill = ImGui::GetColorU32(tint);
                            ImU32 border = ImGui::GetColorU32(brighten(tint, 0.85f));
                            dl->AddQuadFilled(p0, p1, p2, p3, fill);
                            dl->AddQuad(p0, p1, p2, p3, border, 2.0f);
                            ImVec2 textSize = ImGui::CalcTextSize(obj.ui.label.c_str());
                            ImVec2 textPos(center.x - textSize.x * 0.5f, center.y - textSize.y * 0.5f);
                            dl->AddText(textPos, IM_COL32(210, 210, 220, 220), obj.ui.label.c_str());
                        }
                    } else {
                        ImGui::SetCursorPos(ImVec2(drawMinLocal.x - overlayPos.x, drawMinLocal.y - overlayPos.y));
                        if (texId != 0) {
                            ImGui::Image((ImTextureID)(intptr_t)texId, size, ImVec2(0, 1), ImVec2(1, 0), tint, ImVec4(0, 0, 0, 0));
                        } else {
                            ImDrawList* dl = ImGui::GetWindowDrawList();
                            ImU32 fill = ImGui::GetColorU32(tint);
                            ImU32 border = ImGui::GetColorU32(brighten(tint, 0.85f));
                            dl->AddRectFilled(drawMinLocal, drawMaxLocal, fill, 6.0f);
                            dl->AddRect(drawMinLocal, drawMaxLocal, border, 6.0f);
                            ImVec2 textSize = ImGui::CalcTextSize(obj.ui.label.c_str());
                            ImVec2 textPos(drawMinLocal.x + (size.x - textSize.x) * 0.5f,
                                           drawMinLocal.y + (size.y - textSize.y) * 0.5f);
                            dl->AddText(textPos, IM_COL32(210, 210, 220, 220), obj.ui.label.c_str());
                        }
                    }
                    ImGui::Dummy(size);
                };

                if (repeatX || repeatY) {
                    int startX = repeatX ? static_cast<int>(std::floor((worldViewMin.x - baseWorldMin.x) / stepX)) - 1 : 0;
                    int endX = repeatX ? static_cast<int>(std::ceil((worldViewMax.x - baseWorldMin.x) / stepX)) + 1 : 0;
                    int startY = repeatY ? static_cast<int>(std::floor((worldViewMin.y - baseWorldMin.y) / stepY)) - 1 : 0;
                    int endY = repeatY ? static_cast<int>(std::ceil((worldViewMax.y - baseWorldMin.y) / stepY)) + 1 : 0;
                    for (int ix = startX; ix <= endX; ++ix) {
                        for (int iy = startY; iy <= endY; ++iy) {
                            float dx = repeatX ? (float)ix * stepX : 0.0f;
                            float dy = repeatY ? (float)iy * stepY : 0.0f;
                            glm::vec2 tileMin = baseWorldMin + glm::vec2(dx, dy);
                            ImVec2 s0 = worldToScreen(tileMin);
                            ImVec2 s1 = worldToScreen(tileMin + glm::vec2(obj.ui.size.x, obj.ui.size.y));
                            ImVec2 tMin(std::min(s0.x, s1.x), std::min(s0.y, s1.y));
                            ImVec2 tMax(std::max(s0.x, s1.x), std::max(s0.y, s1.y));
                            drawImageRect(tMin, tMax);
                        }
                    }
                } else if (std::abs(angle) > 1e-4f) {
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    ImVec2 center = ImVec2((drawMin.x + drawMax.x) * 0.5f, (drawMin.y + drawMax.y) * 0.5f);
                    ImVec2 half = ImVec2(drawSize.x * 0.5f, drawSize.y * 0.5f);
                    float c = std::cos(angle);
                    float s = std::sin(angle);
                    auto rotPt = [&](float x, float y) {
                        return ImVec2(center.x + x * c - y * s, center.y + x * s + y * c);
                    };
                    ImVec2 p0 = rotPt(-half.x, -half.y);
                    ImVec2 p1 = rotPt( half.x, -half.y);
                    ImVec2 p2 = rotPt( half.x,  half.y);
                    ImVec2 p3 = rotPt(-half.x,  half.y);
                    if (texId != 0) {
                        dl->AddImageQuad((ImTextureID)(intptr_t)texId, p0, p1, p2, p3,
                                         ImVec2(0, 1), ImVec2(1, 1), ImVec2(1, 0), ImVec2(0, 0),
                                         ImGui::GetColorU32(tint));
                    } else {
                        ImU32 fill = ImGui::GetColorU32(tint);
                        ImU32 border = ImGui::GetColorU32(brighten(tint, 0.85f));
                        dl->AddQuadFilled(p0, p1, p2, p3, fill);
                        dl->AddQuad(p0, p1, p2, p3, border, 2.0f);
                        ImVec2 textSize = ImGui::CalcTextSize(obj.ui.label.c_str());
                        ImVec2 textPos(center.x - textSize.x * 0.5f, center.y - textSize.y * 0.5f);
                        dl->AddText(textPos, IM_COL32(210, 210, 220, 220), obj.ui.label.c_str());
                    }
                    ImGui::Dummy(drawSize);
                } else {
                    ImGui::SetCursorPos(localMin);
                    if (texId != 0) {
                        ImGui::Image((ImTextureID)(intptr_t)texId, drawSize, ImVec2(0, 1), ImVec2(1, 0), tint, ImVec4(0, 0, 0, 0));
                    } else {
                        ImDrawList* dl = ImGui::GetWindowDrawList();
                        ImU32 fill = ImGui::GetColorU32(tint);
                        ImU32 border = ImGui::GetColorU32(brighten(tint, 0.85f));
                        dl->AddRectFilled(drawMin, drawMax, fill, 6.0f);
                        dl->AddRect(drawMin, drawMax, border, 6.0f);
                        ImVec2 textSize = ImGui::CalcTextSize(obj.ui.label.c_str());
                        ImVec2 textPos(drawMin.x + (drawSize.x - textSize.x) * 0.5f,
                                       drawMin.y + (drawSize.y - textSize.y) * 0.5f);
                        dl->AddText(textPos, IM_COL32(210, 210, 220, 220), obj.ui.label.c_str());
                        ImGui::Dummy(drawSize);
                    }
                }
            } else if (obj.ui.type == UIElementType::Slider) {
                ImGui::SetCursorPos(localMin);
                ImVec4 tint(obj.ui.color.r, obj.ui.color.g, obj.ui.color.b, obj.ui.color.a);
                if (obj.ui.sliderStyle == UISliderStyle::ImGui) {
                    ImGui::PushItemWidth(drawSize.x);
                    ImGui::BeginDisabled(!obj.ui.interactable || uiWorldCameraActive);
                    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(tint.x * 0.2f, tint.y * 0.2f, tint.z * 0.2f, tint.w * 0.6f));
                    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, brighten(tint, 0.5f));
                    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, brighten(tint, 0.7f));
                    ImGui::PushStyleColor(ImGuiCol_SliderGrab, brighten(tint, 0.9f));
                    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, brighten(tint, 1.1f));
                    if (ImGui::SliderFloat(obj.ui.label.c_str(), &obj.ui.sliderValue, obj.ui.sliderMin, obj.ui.sliderMax)) {
                        projectManager.currentProject.hasUnsavedChanges = true;
                    }
                    ImGui::PopStyleColor(5);
                    ImGui::EndDisabled();
                    ImGui::PopItemWidth();
                } else {
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    ImU32 bg = ImGui::GetColorU32(ImVec4(tint.x * 0.2f, tint.y * 0.2f, tint.z * 0.2f, tint.w * 0.6f));
                    ImU32 fill = ImGui::GetColorU32(tint);
                    ImU32 border = ImGui::GetColorU32(brighten(tint, 0.85f));
                    float minValue = obj.ui.sliderMin;
                    float maxValue = obj.ui.sliderMax;
                    float range = (maxValue - minValue);
                    if (range <= 1e-6f) range = 1.0f;
                    ImGui::BeginDisabled(!obj.ui.interactable || uiWorldCameraActive);
                    ImGui::InvisibleButton("##UISlider", drawSize);
                    bool held = obj.ui.interactable && !uiWorldCameraActive && ImGui::IsItemActive();
                    if (held && ImGui::IsMouseDown(ImGuiMouseButton_Left) && drawSize.x > 1.0f) {
                        float mouseT = (ImGui::GetIO().MousePos.x - drawMin.x) / drawSize.x;
                        mouseT = std::clamp(mouseT, 0.0f, 1.0f);
                        float newValue = minValue + mouseT * range;
                        if (newValue != obj.ui.sliderValue) {
                            obj.ui.sliderValue = newValue;
                            projectManager.currentProject.hasUnsavedChanges = true;
                        }
                    }
                    ImGui::EndDisabled();

                    animateValue(animState.sliderValue, obj.ui.sliderValue, held);
                    float displayValue = (uiAnimationMode == UIAnimationMode::Off) ? obj.ui.sliderValue : animState.sliderValue;
                    float t = (displayValue - minValue) / range;
                    t = std::clamp(t, 0.0f, 1.0f);

                    if (obj.ui.sliderStyle == UISliderStyle::Fill) {
                        float rounding = 6.0f;
                        ImVec2 fillMax(drawMin.x + drawSize.x * t, drawMax.y);
                        dl->AddRectFilled(drawMin, drawMax, bg, rounding);
                        if (fillMax.x > drawMin.x) {
                            dl->AddRectFilled(drawMin, fillMax, fill, rounding);
                        }
                        dl->AddRect(drawMin, drawMax, border, rounding);
                        ImVec2 textSize = ImGui::CalcTextSize(obj.ui.label.c_str());
                        ImVec2 textPos(drawMin.x + (drawSize.x - textSize.x) * 0.5f,
                                       drawMin.y + (drawSize.y - textSize.y) * 0.5f);
                        dl->AddText(textPos, IM_COL32(240, 240, 245, 220), obj.ui.label.c_str());
                    } else if (obj.ui.sliderStyle == UISliderStyle::Circle) {
                        ImVec2 center((drawMin.x + drawMax.x) * 0.5f, (drawMin.y + drawMax.y) * 0.5f);
                        float radius = std::max(2.0f, std::min(drawSize.x, drawSize.y) * 0.5f - 2.0f);
                        dl->AddCircleFilled(center, radius, bg, 32);
                        float start = -IM_PI * 0.5f;
                        float end = start + t * IM_PI * 2.0f;
                        dl->PathClear();
                        dl->PathArcTo(center, radius, start, end, 32);
                        dl->PathLineTo(center);
                        dl->PathFillConvex(fill);
                        dl->AddCircle(center, radius, border, 32, 2.0f);
                        ImVec2 textSize = ImGui::CalcTextSize(obj.ui.label.c_str());
                        ImVec2 textPos(center.x - textSize.x * 0.5f, center.y - textSize.y * 0.5f);
                        dl->AddText(textPos, IM_COL32(240, 240, 245, 220), obj.ui.label.c_str());
                    }
                }
            } else if (obj.ui.type == UIElementType::Button) {
                ImGui::SetCursorPos(localMin);
                ImVec4 tint(obj.ui.color.r, obj.ui.color.g, obj.ui.color.b, obj.ui.color.a);
                obj.ui.buttonPressed = false;
                if (obj.ui.buttonStyle == UIButtonStyle::ImGui) {
                    ImGui::PushStyleColor(ImGuiCol_Button, tint);
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, brighten(tint, 1.1f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, brighten(tint, 1.2f));
                    ImGui::BeginDisabled(!obj.ui.interactable || uiWorldCameraActive);
                    obj.ui.buttonPressed = ImGui::Button(obj.ui.label.c_str(), drawSize);
                    ImGui::EndDisabled();
                    ImGui::PopStyleColor(3);
                } else if (obj.ui.buttonStyle == UIButtonStyle::Outline) {
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    ImU32 border = ImGui::GetColorU32(tint);
                    ImGui::BeginDisabled(!obj.ui.interactable || uiWorldCameraActive);
                    if (ImGui::InvisibleButton("##UIButton", drawSize)) {
                        obj.ui.buttonPressed = obj.ui.interactable && !uiWorldCameraActive;
                    }
                    bool hovered = ImGui::IsItemHovered();
                    bool active = ImGui::IsItemActive();
                    ImGui::EndDisabled();
                    float hoverT = animateValue(animState.hover, hovered ? 1.0f : 0.0f, false);
                    float activeT = animateValue(animState.active, active ? 1.0f : 0.0f, false);
                    if (hoverT > 0.001f) {
                        ImVec4 hoverCol = brighten(tint, 0.45f);
                        hoverCol.w *= std::clamp(hoverT, 0.0f, 1.0f);
                        dl->AddRectFilled(drawMin, drawMax, ImGui::GetColorU32(hoverCol), 6.0f);
                    }
                    if (activeT > 0.001f) {
                        ImVec4 activeCol = brighten(tint, 0.65f);
                        activeCol.w *= std::clamp(activeT, 0.0f, 1.0f);
                        dl->AddRectFilled(drawMin, drawMax, ImGui::GetColorU32(activeCol), 6.0f);
                    }
                    dl->AddRect(drawMin, drawMax, border, 6.0f, 0, 2.0f);
                    ImVec2 textSize = ImGui::CalcTextSize(obj.ui.label.c_str());
                    ImVec2 textPos(drawMin.x + (drawSize.x - textSize.x) * 0.5f,
                                   drawMin.y + (drawSize.y - textSize.y) * 0.5f);
                    dl->AddText(textPos, IM_COL32(240, 240, 245, 220), obj.ui.label.c_str());
                }
            } else if (obj.ui.type == UIElementType::Text) {
                ImDrawList* dl = ImGui::GetWindowDrawList();
                ImVec4 tint(obj.ui.color.r, obj.ui.color.g, obj.ui.color.b, obj.ui.color.a);
                float scale = std::max(0.1f, obj.ui.textScale);
                float scaleFactor = std::max(0.01f, uiWorldCamera.zoom / 100.0f);
                float fontSize = std::max(1.0f, ImGui::GetFontSize() * scale * scaleFactor);
                ImVec2 textPos = ImVec2(drawMin.x + 4.0f, drawMin.y + 2.0f);
                ImGui::PushClipRect(drawMin, drawMax, true);
                dl->AddText(ImGui::GetFont(), fontSize, textPos, ImGui::GetColorU32(tint), obj.ui.label.c_str());
                ImGui::PopClipRect();
            }
            ImGui::PopID();
            if (styleApplied) ImGui::GetStyle() = savedStyle;
        }

        bool gizmoUsed = false;
        if (uiWorldHover && !uiWorldCameraActive && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
            !ImGuizmo::IsUsing() && !ImGuizmo::IsOver()) {
            ImVec2 mouse = ImGui::GetIO().MousePos;
            bool additive = ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeyShift;
            int hitId = -1;
            for (auto it = sceneObjects.rbegin(); it != sceneObjects.rend(); ++it) {
                const SceneObject& obj = *it;
                if (!obj.enabled || !isUIType(obj) || obj.ui.type == UIElementType::Canvas) continue;
                ImVec2 rectMin, rectMax;
                resolveUIRectWorld(obj, rectMin, rectMax);
                if (mouse.x >= rectMin.x && mouse.x <= rectMax.x &&
                    mouse.y >= rectMin.y && mouse.y <= rectMax.y) {
                    hitId = obj.id;
                    break;
                }
            }
            if (hitId >= 0) {
                setPrimarySelection(hitId, additive);
                gizmoUsed = true;
            } else if (!additive) {
                clearSelection();
            }
        }

        SceneObject* selected = getSelectedObject();
        if (selected && isUIType(*selected) && selected->ui.type != UIElementType::Canvas) {
            ImVec2 rectMin, rectMax;
            resolveUIRectWorld(*selected, rectMin, rectMax);
            ImVec2 rectSize(rectMax.x - rectMin.x, rectMax.y - rectMin.y);
            if (rectSize.x > 1.0f && rectSize.y > 1.0f) {
                ImGuizmo::OPERATION op = ImGuizmo::TRANSLATE;
                if (mCurrentGizmoOperation == ImGuizmo::SCALE) {
                    op = ImGuizmo::SCALE;
                } else if (mCurrentGizmoOperation == ImGuizmo::ROTATE) {
                    op = ImGuizmo::ROTATE;
                }
                glm::mat4 view(1.0f);
                glm::mat4 proj = glm::ortho(0.0f, (float)(imageMax.x - imageMin.x),
                                            (float)(imageMax.y - imageMin.y), 0.0f, -1.0f, 1.0f);
                glm::vec2 parentOffset = getWorldParentOffset(*selected);
                glm::vec2 worldSize(selected->ui.size.x, selected->ui.size.y);
                auto anchorToPivotUI = [](UIAnchor anchor, const ImVec2& size) {
                    switch (anchor) {
                        case UIAnchor::TopLeft: return ImVec2(0.0f, 0.0f);
                        case UIAnchor::TopRight: return ImVec2(size.x, 0.0f);
                        case UIAnchor::BottomLeft: return ImVec2(0.0f, size.y);
                        case UIAnchor::BottomRight: return ImVec2(size.x, size.y);
                        default: return ImVec2(size.x * 0.5f, size.y * 0.5f);
                    }
                };
                ImVec2 rectCenter((rectMin.x + rectMax.x) * 0.5f - imageMin.x,
                                  (rectMin.y + rectMax.y) * 0.5f - imageMin.y);
                glm::vec3 gizmoScale(1.0f, 1.0f, 1.0f);
                if (op == ImGuizmo::SCALE) {
                    gizmoScale = glm::vec3(rectSize.x, rectSize.y, 1.0f);
                }
                glm::mat4 model(1.0f);
                model = glm::translate(model, glm::vec3(rectCenter.x, rectCenter.y, 0.0f));
                model = glm::rotate(model, glm::radians(selected->ui.rotation), glm::vec3(0.0f, 0.0f, 1.0f));
                model = glm::scale(model, gizmoScale);

                ImGuizmo::BeginFrame();
                ImGuizmo::Enable(true);
                ImGuizmo::SetOrthographic(true);
                ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
                ImGuizmo::SetRect(imageMin.x, imageMin.y, imageMax.x - imageMin.x, imageMax.y - imageMin.y);
                glm::mat4 delta(1.0f);
                ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(proj), op, ImGuizmo::LOCAL, glm::value_ptr(model), glm::value_ptr(delta));
                if (ImGuizmo::IsUsing()) {
                    glm::vec3 pos, rot, scl;
                    DecomposeMatrix(model, pos, rot, scl);
                    glm::vec3 euler = NormalizeEulerDegrees(glm::degrees(rot));
                    ImVec2 newCenter(imageMin.x + pos.x, imageMin.y + pos.y);
                    glm::vec2 worldCenter = screenToWorld(newCenter);
                    if (op == ImGuizmo::ROTATE) {
                        selected->ui.rotation = euler.z;
                    } else if (op == ImGuizmo::TRANSLATE) {
                        ImVec2 pivotOffset = anchorToPivotUI(selected->ui.anchor, ImVec2(worldSize.x, worldSize.y));
                        glm::vec2 worldMin = worldCenter - worldSize * 0.5f;
                        glm::vec2 worldPivot = worldMin + glm::vec2(pivotOffset.x, pivotOffset.y);
                        selected->ui.position = worldPivot - parentOffset - parallaxOffset(*selected);
                    } else if (op == ImGuizmo::SCALE) {
                        ImVec2 newSize(std::max(1.0f, scl.x), std::max(1.0f, scl.y));
                        worldSize = glm::vec2(newSize.x, newSize.y) / uiWorldCamera.zoom;
                        ImVec2 pivotOffset = anchorToPivotUI(selected->ui.anchor, ImVec2(worldSize.x, worldSize.y));
                        glm::vec2 worldMin = worldCenter - worldSize * 0.5f;
                        glm::vec2 worldPivot = worldMin + glm::vec2(pivotOffset.x, pivotOffset.y);
                        selected->ui.position = worldPivot - parentOffset - parallaxOffset(*selected);
                        selected->ui.size = worldSize;
                    }
                    projectManager.currentProject.hasUnsavedChanges = true;
                    gizmoUsed = true;
                }
            }
        }

        ImGui::EndChild();
        ImGui::PopStyleVar();

        if (ImGui::IsAnyItemActive() || uiWorldCameraActive || gizmoUsed) {
            blockSelection = true;
        }
    }

        auto projectToScreen = [&](const glm::vec3& p) -> std::optional<ImVec2> {
            glm::vec4 clip = proj * view * glm::vec4(p, 1.0f);
            if (clip.w <= 0.0f) return std::nullopt;
            glm::vec3 ndc = glm::vec3(clip) / clip.w;
            ImVec2 screen;
            screen.x = imageMin.x + (ndc.x * 0.5f + 0.5f) * (imageMax.x - imageMin.x);
            screen.y = imageMin.y + (1.0f - (ndc.y * 0.5f + 0.5f)) * (imageMax.y - imageMin.y);
            return screen;
        };

        SceneObject* selectedObj = getSelectedObject();
        bool selectedIsUiCanvas3D = selectedObj && selectedObj->hasUI &&
                                    selectedObj->ui.type == UIElementType::Canvas &&
                                    selectedObj->ui.renderIn3D;
        if (!uiWorldMode && selectedObj && !selectedObj->hasPostFX &&
            (!HasUIComponent(*selectedObj) || selectedIsUiCanvas3D)) {
            ImGuizmo::BeginFrame();
            ImGuizmo::Enable(true);
            ImGuizmo::SetOrthographic(false);
            ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
            ImGuizmo::SetRect(
                imageMin.x,
                imageMin.y,
                imageMax.x - imageMin.x,
                imageMax.y - imageMin.y
            );

            auto compose = [](const SceneObject& o) {
                glm::mat4 m(1.0f);
                m = glm::translate(m, o.position);
                m = glm::rotate(m, glm::radians(o.rotation.x), glm::vec3(1, 0, 0));
                m = glm::rotate(m, glm::radians(o.rotation.y), glm::vec3(0, 1, 0));
                m = glm::rotate(m, glm::radians(o.rotation.z), glm::vec3(0, 0, 1));
                m = glm::scale(m, o.scale);
                return m;
            };

            bool meshModeActive = meshEditMode && ensureMeshEditTarget(selectedObj);

            glm::vec3 pivotPos = selectedObj->position;
            if (!meshModeActive && selectedObjectIds.size() > 1 && mCurrentGizmoMode == ImGuizmo::WORLD) {
                pivotPos = getSelectionCenterWorld(true);
            }

            glm::mat4 modelMatrix(1.0f);
            modelMatrix = glm::translate(modelMatrix, pivotPos);
            modelMatrix = glm::rotate(modelMatrix, glm::radians(selectedObj->rotation.x), glm::vec3(1, 0, 0));
            modelMatrix = glm::rotate(modelMatrix, glm::radians(selectedObj->rotation.y), glm::vec3(0, 1, 0));
            modelMatrix = glm::rotate(modelMatrix, glm::radians(selectedObj->rotation.z), glm::vec3(0, 0, 1));
            modelMatrix = glm::scale(modelMatrix, selectedObj->scale);
            glm::mat4 originalModel = modelMatrix;

            if (meshModeActive && !meshEditAsset.positions.empty()) {
                // Build helper edge list (dedup) for edge/face modes
                std::vector<glm::u32vec2> edges;
                edges.reserve(meshEditAsset.faces.size() * 3);
                std::unordered_set<uint64_t> edgeSet;
                std::unordered_map<uint64_t, int> edgeIndex;
                auto edgeKey = [](uint32_t a, uint32_t b) {
                    return (static_cast<uint64_t>(std::min(a,b)) << 32) | static_cast<uint64_t>(std::max(a,b));
                };
                for (size_t fi = 0; fi < meshEditAsset.faces.size(); ++fi) {
                    const auto& f = meshEditAsset.faces[fi];
                    uint32_t tri[3] = { f.x, f.y, f.z };
                    for (int e = 0; e < 3; ++e) {
                        uint32_t a = tri[e];
                        uint32_t b = tri[(e+1)%3];
                        uint64_t key = edgeKey(a,b);
                        if (edgeSet.insert(key).second) {
                            int idx = (int)edges.size();
                            edges.push_back(glm::u32vec2(std::min(a,b), std::max(a,b)));
                            edgeIndex[key] = idx;
                        }
                    }
                }

                ImDrawList* dl = ImGui::GetWindowDrawList();
                ImU32 vertCol = ImGui::GetColorU32(ImVec4(0.35f, 0.75f, 1.0f, 0.9f));
                ImU32 selCol  = ImGui::GetColorU32(ImVec4(1.0f, 0.6f, 0.2f, 1.0f));
                float edgeAlpha = (meshEditSelectionMode == MeshEditSelectionMode::Face) ? 0.35f : 0.6f;
                ImU32 edgeCol = ImGui::GetColorU32(ImVec4(0.6f, 0.9f, 1.0f, edgeAlpha));
                ImU32 faceSelFillCol = ImGui::GetColorU32(ImVec4(1.0f, 0.6f, 0.2f, 0.38f));

                float selectRadius = 10.0f;
                ImVec2 mouse = ImGui::GetIO().MousePos;
                bool clicked = mouseOverViewportImage && ImGui::IsMouseClicked(0) && !ImGuizmo::IsUsing() && !ImGuizmo::IsOver();
                bool doubleClicked = mouseOverViewportImage && ImGui::IsMouseDoubleClicked(0) && !ImGuizmo::IsUsing() && !ImGuizmo::IsOver();
                bool additiveClick = ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeyShift;

                glm::mat4 invModel = glm::inverse(modelMatrix);
                glm::mat4 invViewProj = glm::inverse(proj * view);

                auto distPointToSegment = [](const ImVec2& p, const ImVec2& a, const ImVec2& b) {
                    ImVec2 ab = ImVec2(b.x - a.x, b.y - a.y);
                    float len2 = ab.x * ab.x + ab.y * ab.y;
                    if (len2 < 1e-4f) {
                        float dx = p.x - a.x;
                        float dy = p.y - a.y;
                        return std::sqrt(dx * dx + dy * dy);
                    }
                    float t = ((p.x - a.x) * ab.x + (p.y - a.y) * ab.y) / len2;
                    t = std::clamp(t, 0.0f, 1.0f);
                    ImVec2 proj = ImVec2(a.x + ab.x * t, a.y + ab.y * t);
                    float dx = p.x - proj.x;
                    float dy = p.y - proj.y;
                    return std::sqrt(dx * dx + dy * dy);
                };

                auto makeRay = [&](const ImVec2& pos) {
                    float x = (pos.x - imageMin.x) / (imageMax.x - imageMin.x);
                    float y = (pos.y - imageMin.y) / (imageMax.y - imageMin.y);
                    x = x * 2.0f - 1.0f;
                    y = 1.0f - y * 2.0f;

                    glm::vec4 nearPt = invViewProj * glm::vec4(x, y, -1.0f, 1.0f);
                    glm::vec4 farPt  = invViewProj * glm::vec4(x, y,  1.0f, 1.0f);
                    nearPt /= nearPt.w;
                    farPt  /= farPt.w;

                    glm::vec3 origin = glm::vec3(nearPt);
                    glm::vec3 dir = glm::normalize(glm::vec3(farPt - nearPt));
                    return std::make_pair(origin, dir);
                };

                auto rayTriangle = [](const glm::vec3& orig, const glm::vec3& dir, const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2, float& tHit) {
                    const float EPSILON = 1e-6f;
                    glm::vec3 e1 = v1 - v0;
                    glm::vec3 e2 = v2 - v0;
                    glm::vec3 pvec = glm::cross(dir, e2);
                    float det = glm::dot(e1, pvec);
                    if (fabs(det) < EPSILON) return false;
                    float invDet = 1.0f / det;
                    glm::vec3 tvec = orig - v0;
                    float u = glm::dot(tvec, pvec) * invDet;
                    if (u < 0.0f || u > 1.0f) return false;
                    glm::vec3 qvec = glm::cross(tvec, e1);
                    float v = glm::dot(dir, qvec) * invDet;
                    if (v < 0.0f || u + v > 1.0f) return false;
                    float t = glm::dot(e2, qvec) * invDet;
                    if (t < 0.0f) return false;
                    tHit = t;
                    return true;
                };

                float baseEdgeThickness = (meshEditSelectionMode == MeshEditSelectionMode::Edge) ? 2.2f : 1.4f;
                for (size_t ei = 0; ei < edges.size(); ++ei) {
                    const auto& e = edges[ei];
                    glm::vec3 a = glm::vec3(modelMatrix * glm::vec4(meshEditAsset.positions[e.x], 1.0f));
                    glm::vec3 b = glm::vec3(modelMatrix * glm::vec4(meshEditAsset.positions[e.y], 1.0f));
                    auto sa = projectToScreen(a);
                    auto sb = projectToScreen(b);
                    if (!sa || !sb) continue;
                    bool sel = meshEditSelectionMode == MeshEditSelectionMode::Edge &&
                               std::find(meshEditSelectedEdges.begin(), meshEditSelectedEdges.end(), (int)ei) != meshEditSelectedEdges.end();
                    float thickness = sel ? baseEdgeThickness + 1.1f : baseEdgeThickness;
                    ImU32 color = sel ? selCol : edgeCol;
                    dl->AddLine(*sa, *sb, color, thickness);
                }

                if (meshEditSelectionMode == MeshEditSelectionMode::Vertex) {
                    const size_t maxDraw = std::min<size_t>(meshEditAsset.positions.size(), 2000);
                    float bestDist = selectRadius;
                    int clickedIndex = -1;
                    for (size_t i = 0; i < maxDraw; ++i) {
                        glm::vec3 world = glm::vec3(modelMatrix * glm::vec4(meshEditAsset.positions[i], 1.0f));
                        auto screen = projectToScreen(world);
                        if (!screen) continue;
                        bool sel = std::find(meshEditSelectedVertices.begin(), meshEditSelectedVertices.end(), (int)i) != meshEditSelectedVertices.end();
                        float radius = sel ? 6.5f : 5.0f;
                        dl->AddCircleFilled(*screen, radius, sel ? selCol : vertCol);

                        if (clicked) {
                            float dx = screen->x - mouse.x;
                            float dy = screen->y - mouse.y;
                            float dist = std::sqrt(dx*dx + dy*dy);
                            if (dist < bestDist) {
                                bestDist = dist;
                                clickedIndex = static_cast<int>(i);
                            }
                        }
                    }

                    if (clicked) {
                        if (clickedIndex >= 0) {
                            if (additiveClick) {
                                auto itSel = std::find(meshEditSelectedVertices.begin(), meshEditSelectedVertices.end(), clickedIndex);
                                if (itSel == meshEditSelectedVertices.end()) {
                                    meshEditSelectedVertices.push_back(clickedIndex);
                                } else {
                                    meshEditSelectedVertices.erase(itSel);
                                }
                            } else {
                                meshEditSelectedVertices.clear();
                                meshEditSelectedVertices.push_back(clickedIndex);
                            }
                        } else if (!additiveClick) {
                            meshEditSelectedVertices.clear();
                        }
                        meshEditSelectedEdges.clear();
                        meshEditSelectedFaces.clear();
                    }
                } else if (meshEditSelectionMode == MeshEditSelectionMode::Edge) {
                    int clickedIndex = -1;
                    if (clicked) {
                        auto ray = makeRay(mouse);
                        glm::vec3 localOrigin = glm::vec3(invModel * glm::vec4(ray.first, 1.0f));
                        glm::vec3 localDir = glm::normalize(glm::vec3(invModel * glm::vec4(ray.second, 0.0f)));
                        float bestT = FLT_MAX;
                        int hitFace = -1;
                        for (size_t fi = 0; fi < meshEditAsset.faces.size(); ++fi) {
                            const auto& f = meshEditAsset.faces[fi];
                            if (f.x >= meshEditAsset.positions.size() || f.y >= meshEditAsset.positions.size() || f.z >= meshEditAsset.positions.size()) continue;
                            float tHit = 0.0f;
                            if (rayTriangle(localOrigin, localDir,
                                            meshEditAsset.positions[f.x],
                                            meshEditAsset.positions[f.y],
                                            meshEditAsset.positions[f.z],
                                            tHit))
                            {
                                if (tHit < bestT) {
                                    bestT = tHit;
                                    hitFace = static_cast<int>(fi);
                                }
                            }
                        }

                        if (hitFace >= 0) {
                            const auto& f = meshEditAsset.faces[hitFace];
                            uint32_t tri[3] = { f.x, f.y, f.z };
                            float bestDist = selectRadius;
                            for (int e = 0; e < 3; ++e) {
                                uint32_t aIdx = tri[e];
                                uint32_t bIdx = tri[(e + 1) % 3];
                                glm::vec3 a = glm::vec3(modelMatrix * glm::vec4(meshEditAsset.positions[aIdx], 1.0f));
                                glm::vec3 b = glm::vec3(modelMatrix * glm::vec4(meshEditAsset.positions[bIdx], 1.0f));
                                auto sa = projectToScreen(a);
                                auto sb = projectToScreen(b);
                                if (!sa || !sb) continue;
                                float dist = distPointToSegment(mouse, *sa, *sb);
                                if (dist < bestDist) {
                                    bestDist = dist;
                                    auto it = edgeIndex.find(edgeKey(aIdx, bIdx));
                                    if (it != edgeIndex.end()) {
                                        clickedIndex = it->second;
                                    }
                                }
                            }
                        }
                    }

                    if (clicked) {
                        if (clickedIndex >= 0) {
                            if (additiveClick) {
                                auto itSel = std::find(meshEditSelectedEdges.begin(), meshEditSelectedEdges.end(), clickedIndex);
                                if (itSel == meshEditSelectedEdges.end()) {
                                    meshEditSelectedEdges.push_back(clickedIndex);
                                } else {
                                    meshEditSelectedEdges.erase(itSel);
                                }
                            } else {
                                meshEditSelectedEdges.clear();
                                meshEditSelectedEdges.push_back(clickedIndex);
                            }
                        } else if (!additiveClick) {
                            meshEditSelectedEdges.clear();
                        }
                        meshEditSelectedVertices.clear();
                        meshEditSelectedFaces.clear();
                    }
                } else if (meshEditSelectionMode == MeshEditSelectionMode::Face) {
                    auto computeFaceNormal = [&](const glm::u32vec3& f, glm::vec3& out) -> bool {
                        if (f.x >= meshEditAsset.positions.size() ||
                            f.y >= meshEditAsset.positions.size() ||
                            f.z >= meshEditAsset.positions.size()) {
                            return false;
                        }
                        const glm::vec3& a = meshEditAsset.positions[f.x];
                        const glm::vec3& b = meshEditAsset.positions[f.y];
                        const glm::vec3& c = meshEditAsset.positions[f.z];
                        glm::vec3 n = glm::cross(b - a, c - a);
                        float len = glm::length(n);
                        if (len < 1e-6f) {
                            return false;
                        }
                        out = n / len;
                        return true;
                    };
                    auto gatherCoplanarFaces = [&](int seed) {
                        std::vector<int> group;
                        const size_t faceCount = meshEditAsset.faces.size();
                        if (seed < 0 || seed >= (int)faceCount) return group;
                        glm::vec3 seedNormal(0.0f);
                        if (!computeFaceNormal(meshEditAsset.faces[seed], seedNormal)) {
                            group.push_back(seed);
                            return group;
                        }

                        std::unordered_map<uint64_t, std::vector<int>> edgeToFaces;
                        edgeToFaces.reserve(faceCount * 3);
                        auto edgeKey = [](uint32_t a, uint32_t b) {
                            return (static_cast<uint64_t>(std::min(a, b)) << 32) |
                                   static_cast<uint64_t>(std::max(a, b));
                        };
                        for (size_t fi = 0; fi < faceCount; ++fi) {
                            const auto& f = meshEditAsset.faces[fi];
                            uint32_t tri[3] = { f.x, f.y, f.z };
                            for (int e = 0; e < 3; ++e) {
                                edgeToFaces[edgeKey(tri[e], tri[(e + 1) % 3])].push_back((int)fi);
                            }
                        }

                        std::vector<char> visited(faceCount, 0);
                        std::vector<int> stack;
                        visited[seed] = 1;
                        stack.push_back(seed);
                        group.push_back(seed);

                        const auto& seedFace = meshEditAsset.faces[seed];
                        glm::vec3 seedPoint = meshEditAsset.positions[seedFace.x];
                        float seedD = glm::dot(seedNormal, seedPoint);
                        const float normalThreshold = 0.995f;
                        const float planeEpsilon = 1e-3f;

                        while (!stack.empty()) {
                            int current = stack.back();
                            stack.pop_back();
                            const auto& f = meshEditAsset.faces[current];
                            uint32_t tri[3] = { f.x, f.y, f.z };
                            for (int e = 0; e < 3; ++e) {
                                auto it = edgeToFaces.find(edgeKey(tri[e], tri[(e + 1) % 3]));
                                if (it == edgeToFaces.end()) continue;
                                for (int neighbor : it->second) {
                                    if (neighbor < 0 || neighbor >= (int)faceCount) continue;
                                    if (visited[neighbor]) continue;
                                    glm::vec3 n(0.0f);
                                    if (!computeFaceNormal(meshEditAsset.faces[neighbor], n)) continue;
                                    if (glm::dot(seedNormal, n) < normalThreshold) continue;
                                    const auto& nf = meshEditAsset.faces[neighbor];
                                    const glm::vec3& na = meshEditAsset.positions[nf.x];
                                    const glm::vec3& nb = meshEditAsset.positions[nf.y];
                                    const glm::vec3& nc = meshEditAsset.positions[nf.z];
                                    if (std::abs(glm::dot(seedNormal, na) - seedD) > planeEpsilon ||
                                        std::abs(glm::dot(seedNormal, nb) - seedD) > planeEpsilon ||
                                        std::abs(glm::dot(seedNormal, nc) - seedD) > planeEpsilon) {
                                        continue;
                                    }
                                    visited[neighbor] = 1;
                                    stack.push_back(neighbor);
                                    group.push_back(neighbor);
                                }
                            }
                        }
                        std::sort(group.begin(), group.end());
                        group.erase(std::unique(group.begin(), group.end()), group.end());
                        return group;
                    };
                    auto gatherQuadFaces = [&](int seed) {
                        std::vector<int> group;
                        const size_t faceCount = meshEditAsset.faces.size();
                        if (seed < 0 || seed >= (int)faceCount) return group;
                        glm::vec3 seedNormal(0.0f);
                        if (!computeFaceNormal(meshEditAsset.faces[seed], seedNormal)) {
                            group.push_back(seed);
                            return group;
                        }
                        const auto& seedFace = meshEditAsset.faces[seed];
                        uint32_t seedIdx[3] = { seedFace.x, seedFace.y, seedFace.z };
                        group.push_back(seed);

                        for (size_t fi = 0; fi < faceCount; ++fi) {
                            if ((int)fi == seed) continue;
                            const auto& f = meshEditAsset.faces[fi];
                            uint32_t idx[3] = { f.x, f.y, f.z };
                            int shared = 0;
                            for (int i = 0; i < 3; ++i) {
                                for (int j = 0; j < 3; ++j) {
                                    if (seedIdx[i] == idx[j]) {
                                        shared++;
                                        break;
                                    }
                                }
                            }
                            if (shared >= 2) {
                                glm::vec3 n(0.0f);
                                if (!computeFaceNormal(f, n)) continue;
                                if (glm::dot(seedNormal, n) < 0.995f) continue;
                                group.push_back((int)fi);
                                break;
                            }
                        }

                        if (group.size() > 1) {
                            std::sort(group.begin(), group.end());
                            group.erase(std::unique(group.begin(), group.end()), group.end());
                        }
                        return group;
                    };

                    for (int fi : meshEditSelectedFaces) {
                        if (fi < 0 || fi >= (int)meshEditAsset.faces.size()) continue;
                        const auto& f = meshEditAsset.faces[fi];
                        glm::vec3 a = glm::vec3(modelMatrix * glm::vec4(meshEditAsset.positions[f.x], 1.0f));
                        glm::vec3 b = glm::vec3(modelMatrix * glm::vec4(meshEditAsset.positions[f.y], 1.0f));
                        glm::vec3 c = glm::vec3(modelMatrix * glm::vec4(meshEditAsset.positions[f.z], 1.0f));
                        auto sa = projectToScreen(a);
                        auto sb = projectToScreen(b);
                        auto sc = projectToScreen(c);
                        if (!sa || !sb || !sc) continue;
                        dl->AddTriangleFilled(*sa, *sb, *sc, faceSelFillCol);
                        dl->AddTriangle(*sa, *sb, *sc, selCol, 2.0f);
                    }

                    if (clicked || doubleClicked) {
                        auto ray = makeRay(mouse);
                        glm::vec3 localOrigin = glm::vec3(invModel * glm::vec4(ray.first, 1.0f));
                        glm::vec3 localDir = glm::normalize(glm::vec3(invModel * glm::vec4(ray.second, 0.0f)));
                        float bestT = FLT_MAX;
                        int clickedIndex = -1;
                        for (size_t fi = 0; fi < meshEditAsset.faces.size(); ++fi) {
                            const auto& f = meshEditAsset.faces[fi];
                            if (f.x >= meshEditAsset.positions.size() || f.y >= meshEditAsset.positions.size() || f.z >= meshEditAsset.positions.size()) continue;
                            float tHit = 0.0f;
                            if (rayTriangle(localOrigin, localDir,
                                            meshEditAsset.positions[f.x],
                                            meshEditAsset.positions[f.y],
                                            meshEditAsset.positions[f.z],
                                            tHit)) {
                                if (tHit < bestT) {
                                    bestT = tHit;
                                    clickedIndex = static_cast<int>(fi);
                                }
                            }
                        }
                        if (clickedIndex >= 0) {
                            std::vector<int> group;
                            if (doubleClicked) {
                                group = gatherQuadFaces(clickedIndex);
                            }
                            if (group.empty()) group.push_back(clickedIndex);
                            if (additiveClick) {
                                bool allSelected = true;
                                for (int fi : group) {
                                    if (std::find(meshEditSelectedFaces.begin(), meshEditSelectedFaces.end(), fi) == meshEditSelectedFaces.end()) {
                                        allSelected = false;
                                        break;
                                    }
                                }
                                if (allSelected) {
                                    for (int fi : group) {
                                        auto itSel = std::find(meshEditSelectedFaces.begin(), meshEditSelectedFaces.end(), fi);
                                        if (itSel != meshEditSelectedFaces.end()) {
                                            meshEditSelectedFaces.erase(itSel);
                                        }
                                    }
                                } else {
                                    for (int fi : group) {
                                        if (std::find(meshEditSelectedFaces.begin(), meshEditSelectedFaces.end(), fi) == meshEditSelectedFaces.end()) {
                                            meshEditSelectedFaces.push_back(fi);
                                        }
                                    }
                                }
                            } else {
                                meshEditSelectedFaces.clear();
                                meshEditSelectedFaces = std::move(group);
                            }
                        } else if (!additiveClick) {
                            meshEditSelectedFaces.clear();
                        }
                        meshEditSelectedVertices.clear();
                        meshEditSelectedEdges.clear();
                    }
                }

                // Compute affected vertices from selection
                std::vector<int> baseAffectedVerts = meshEditSelectedVertices;
                auto pushUnique = [&](int idx) {
                    if (idx < 0) return;
                    if (std::find(baseAffectedVerts.begin(), baseAffectedVerts.end(), idx) == baseAffectedVerts.end()) {
                        baseAffectedVerts.push_back(idx);
                    }
                };
                if (meshEditSelectionMode == MeshEditSelectionMode::Edge) {
                    for (int ei : meshEditSelectedEdges) {
                        if (ei < 0 || ei >= (int)edges.size()) continue;
                        pushUnique(edges[ei].x);
                        pushUnique(edges[ei].y);
                    }
                } else if (meshEditSelectionMode == MeshEditSelectionMode::Face) {
                    auto computeFaceNormal = [&](const glm::u32vec3& f, glm::vec3& out) -> bool {
                        if (f.x >= meshEditAsset.positions.size() ||
                            f.y >= meshEditAsset.positions.size() ||
                            f.z >= meshEditAsset.positions.size()) {
                            return false;
                        }
                        const glm::vec3& a = meshEditAsset.positions[f.x];
                        const glm::vec3& b = meshEditAsset.positions[f.y];
                        const glm::vec3& c = meshEditAsset.positions[f.z];
                        glm::vec3 n = glm::cross(b - a, c - a);
                        float len = glm::length(n);
                        if (len < 1e-6f) return false;
                        out = n / len;
                        return true;
                    };
                    for (int fi : meshEditSelectedFaces) {
                        if (fi < 0 || fi >= (int)meshEditAsset.faces.size()) continue;
                        const auto& f = meshEditAsset.faces[fi];
                        pushUnique(f.x);
                        pushUnique(f.y);
                        pushUnique(f.z);

                        glm::vec3 seedNormal(0.0f);
                        if (!computeFaceNormal(f, seedNormal)) continue;
                        uint32_t seedIdx[3] = { f.x, f.y, f.z };
                        for (size_t nfi = 0; nfi < meshEditAsset.faces.size(); ++nfi) {
                            if ((int)nfi == fi) continue;
                            const auto& nf = meshEditAsset.faces[nfi];
                            uint32_t idx[3] = { nf.x, nf.y, nf.z };
                            int shared = 0;
                            for (int i = 0; i < 3; ++i) {
                                for (int j = 0; j < 3; ++j) {
                                    if (seedIdx[i] == idx[j]) {
                                        shared++;
                                        break;
                                    }
                                }
                            }
                            if (shared < 2) continue;
                            glm::vec3 n(0.0f);
                            if (!computeFaceNormal(nf, n)) continue;
                            if (glm::dot(seedNormal, n) < 0.995f) continue;
                            pushUnique(nf.x);
                            pushUnique(nf.y);
                            pushUnique(nf.z);
                            break;
                        }
                    }
                }
                if (meshEditSelectionMode == MeshEditSelectionMode::Face && !baseAffectedVerts.empty()) {
                    struct PosKey {
                        int64_t x;
                        int64_t y;
                        int64_t z;
                    };
                    struct PosKeyHash {
                        size_t operator()(const PosKey& k) const {
                            size_t h1 = std::hash<int64_t>{}(k.x);
                            size_t h2 = std::hash<int64_t>{}(k.y);
                            size_t h3 = std::hash<int64_t>{}(k.z);
                            return h1 ^ (h2 << 1) ^ (h3 << 2);
                        }
                    };
                    struct PosKeyEq {
                        bool operator()(const PosKey& a, const PosKey& b) const {
                            return a.x == b.x && a.y == b.y && a.z == b.z;
                        }
                    };

                    const float epsilon = 1e-5f;
                    const float invEps = 1.0f / epsilon;
                    std::unordered_map<PosKey, std::vector<int>, PosKeyHash, PosKeyEq> keyToVerts;
                    keyToVerts.reserve(meshEditAsset.positions.size());

                    auto makeKey = [&](const glm::vec3& p) {
                        return PosKey{
                            (int64_t)llround(p.x * invEps),
                            (int64_t)llround(p.y * invEps),
                            (int64_t)llround(p.z * invEps)
                        };
                    };

                    for (size_t i = 0; i < meshEditAsset.positions.size(); ++i) {
                        keyToVerts[makeKey(meshEditAsset.positions[i])].push_back((int)i);
                    }

                    std::unordered_set<int> expanded(baseAffectedVerts.begin(), baseAffectedVerts.end());
                    for (int idx : baseAffectedVerts) {
                        if (idx < 0 || idx >= (int)meshEditAsset.positions.size()) continue;
                        auto it = keyToVerts.find(makeKey(meshEditAsset.positions[idx]));
                        if (it == keyToVerts.end()) continue;
                        for (int v : it->second) {
                            expanded.insert(v);
                        }
                    }

                    baseAffectedVerts.assign(expanded.begin(), expanded.end());
                    std::sort(baseAffectedVerts.begin(), baseAffectedVerts.end());
                }

                auto recalcMesh = [&]() {
                    meshEditAsset.boundsMin = glm::vec3(FLT_MAX);
                    meshEditAsset.boundsMax = glm::vec3(-FLT_MAX);
                    for (const auto& p : meshEditAsset.positions) {
                        meshEditAsset.boundsMin.x = std::min(meshEditAsset.boundsMin.x, p.x);
                        meshEditAsset.boundsMin.y = std::min(meshEditAsset.boundsMin.y, p.y);
                        meshEditAsset.boundsMin.z = std::min(meshEditAsset.boundsMin.z, p.z);
                        meshEditAsset.boundsMax.x = std::max(meshEditAsset.boundsMax.x, p.x);
                        meshEditAsset.boundsMax.y = std::max(meshEditAsset.boundsMax.y, p.y);
                        meshEditAsset.boundsMax.z = std::max(meshEditAsset.boundsMax.z, p.z);
                    }

                    meshEditAsset.normals.assign(meshEditAsset.positions.size(), glm::vec3(0.0f));
                    for (const auto& f : meshEditAsset.faces) {
                        if (f.x >= meshEditAsset.positions.size() || f.y >= meshEditAsset.positions.size() || f.z >= meshEditAsset.positions.size()) continue;
                        const glm::vec3& a = meshEditAsset.positions[f.x];
                        const glm::vec3& b = meshEditAsset.positions[f.y];
                        const glm::vec3& c = meshEditAsset.positions[f.z];
                        glm::vec3 n = glm::normalize(glm::cross(b - a, c - a));
                        meshEditAsset.normals[f.x] += n;
                        meshEditAsset.normals[f.y] += n;
                        meshEditAsset.normals[f.z] += n;
                    }
                    for (auto& n : meshEditAsset.normals) {
                        if (glm::length(n) > 1e-6f) n = glm::normalize(n);
                    }
                    meshEditAsset.hasNormals = true;
                };

                static bool meshEditHistoryCaptured = false;
                static bool meshEditWasUsing = false;
                static bool meshEditExtruding = false;
                static std::vector<int> meshEditExtrudeVerts;

                if (!baseAffectedVerts.empty()) {
                    glm::vec3 pivotWorld(0.0f);
                    for (int idx : baseAffectedVerts) {
                        glm::vec3 wp = glm::vec3(modelMatrix * glm::vec4(meshEditAsset.positions[idx], 1.0f));
                        pivotWorld += wp;
                    }
                    pivotWorld /= (float)baseAffectedVerts.size();

                    glm::mat4 gizmoMat = glm::translate(glm::mat4(1.0f), pivotWorld);

                    ImGuizmo::Manipulate(
                        glm::value_ptr(view),
                        glm::value_ptr(proj),
                        ImGuizmo::TRANSLATE,
                        ImGuizmo::WORLD,
                        glm::value_ptr(gizmoMat)
                    );

                    bool usingNow = ImGuizmo::IsUsing();
                    if (usingNow && !meshEditWasUsing) {
                        bool wantsExtrude = meshEditExtrudeMode || ImGui::GetIO().KeyShift;
                        bool seams = ImGui::GetIO().KeyShift && ImGui::GetIO().KeyCtrl;
                        meshEditExtruding = false;
                        meshEditExtrudeVerts.clear();
                        int originalVertexCount = static_cast<int>(meshEditAsset.positions.size());
                        int originalFaceCount = static_cast<int>(meshEditAsset.faces.size());
                        int newFaceStart = -1;

                        auto duplicateVertex = [&](uint32_t idx) -> uint32_t {
                            uint32_t newIdx = static_cast<uint32_t>(meshEditAsset.positions.size());
                            meshEditAsset.positions.push_back(meshEditAsset.positions[idx]);
                            if (idx < meshEditAsset.normals.size()) {
                                meshEditAsset.normals.push_back(meshEditAsset.normals[idx]);
                            } else {
                                meshEditAsset.normals.push_back(glm::vec3(0.0f));
                            }
                            if (idx < meshEditAsset.uvs.size()) {
                                meshEditAsset.uvs.push_back(meshEditAsset.uvs[idx]);
                            } else {
                                meshEditAsset.uvs.push_back(glm::vec2(0.0f));
                            }
                            return newIdx;
                        };
                        auto rebuildAffectedVerts = [&]() {
                            baseAffectedVerts = meshEditSelectedVertices;
                            auto pushUnique = [&](int idx) {
                                if (idx < 0) return;
                                if (std::find(baseAffectedVerts.begin(), baseAffectedVerts.end(), idx) == baseAffectedVerts.end()) {
                                    baseAffectedVerts.push_back(idx);
                                }
                            };
                            if (meshEditSelectionMode == MeshEditSelectionMode::Edge) {
                                for (int ei : meshEditSelectedEdges) {
                                    if (ei < 0 || ei >= (int)edges.size()) continue;
                                    pushUnique(edges[ei].x);
                                    pushUnique(edges[ei].y);
                                }
                            } else if (meshEditSelectionMode == MeshEditSelectionMode::Face) {
                                for (int fi : meshEditSelectedFaces) {
                                    if (fi < 0 || fi >= (int)meshEditAsset.faces.size()) continue;
                                    const auto& f = meshEditAsset.faces[fi];
                                    pushUnique(f.x);
                                    pushUnique(f.y);
                                    pushUnique(f.z);
                                }
                            }
                        };
                        auto pushExtrudeVert = [&](int idx) {
                            if (std::find(meshEditExtrudeVerts.begin(), meshEditExtrudeVerts.end(), idx) == meshEditExtrudeVerts.end()) {
                                meshEditExtrudeVerts.push_back(idx);
                            }
                        };
                        auto ensureUvs = [&]() {
                            if (meshEditAsset.uvs.size() < meshEditAsset.positions.size()) {
                                meshEditAsset.uvs.resize(meshEditAsset.positions.size(), glm::vec2(0.0f));
                            }
                        };
                        auto applyPlanarUV = [&](const glm::u32vec3& face) -> bool {
                            if (face.x >= meshEditAsset.positions.size() ||
                                face.y >= meshEditAsset.positions.size() ||
                                face.z >= meshEditAsset.positions.size()) {
                                return false;
                            }
                            const glm::vec3& a = meshEditAsset.positions[face.x];
                            const glm::vec3& b = meshEditAsset.positions[face.y];
                            const glm::vec3& c = meshEditAsset.positions[face.z];
                            glm::vec3 n = glm::normalize(glm::cross(b - a, c - a));
                            glm::vec2 ua(a.x, a.y), ub(b.x, b.y), uc(c.x, c.y);
                            if (std::abs(n.x) >= std::abs(n.y) && std::abs(n.x) >= std::abs(n.z)) {
                                ua = glm::vec2(a.y, a.z);
                                ub = glm::vec2(b.y, b.z);
                                uc = glm::vec2(c.y, c.z);
                            } else if (std::abs(n.y) >= std::abs(n.z)) {
                                ua = glm::vec2(a.x, a.z);
                                ub = glm::vec2(b.x, b.z);
                                uc = glm::vec2(c.x, c.z);
                            }
                            glm::vec2 minUV = glm::min(glm::min(ua, ub), uc);
                            glm::vec2 maxUV = glm::max(glm::max(ua, ub), uc);
                            glm::vec2 span = maxUV - minUV;
                            auto toUv = [&](const glm::vec2& v) {
                                return glm::vec2(
                                    span.x > 1e-5f ? (v.x - minUV.x) / span.x : 0.0f,
                                    span.y > 1e-5f ? (v.y - minUV.y) / span.y : 0.0f
                                );
                            };
                            meshEditAsset.uvs[face.x] = toUv(ua);
                            meshEditAsset.uvs[face.y] = toUv(ub);
                            meshEditAsset.uvs[face.z] = toUv(uc);
                            return true;
                        };

                        if (wantsExtrude && meshEditSelectionMode == MeshEditSelectionMode::Face && !meshEditSelectedFaces.empty()) {
                            newFaceStart = (int)meshEditAsset.faces.size();
                            const size_t faceCount = meshEditAsset.faces.size();
                            std::vector<glm::u32vec3> originalFaces = meshEditAsset.faces;
                            std::vector<bool> faceSelected(faceCount, false);
                            for (int fi : meshEditSelectedFaces) {
                                if (fi >= 0 && fi < (int)faceCount) faceSelected[fi] = true;
                            }

                            std::unordered_map<uint32_t, uint32_t> vertexMap;
                            std::unordered_map<int, glm::u32vec3> newFaceVerts;
                            std::vector<int> newFaceSelection;
                            vertexMap.reserve(meshEditSelectedFaces.size() * 3);
                            newFaceVerts.reserve(meshEditSelectedFaces.size());
                            newFaceSelection.reserve(meshEditSelectedFaces.size());
                            for (int fi : meshEditSelectedFaces) {
                                if (fi < 0 || fi >= (int)faceCount) continue;
                                const auto f = originalFaces[fi];
                                uint32_t idx[3] = { f.x, f.y, f.z };
                                uint32_t newIdx[3];
                                for (int k = 0; k < 3; ++k) {
                                    if (seams) {
                                        newIdx[k] = duplicateVertex(idx[k]);
                                    } else {
                                        auto it = vertexMap.find(idx[k]);
                                        if (it == vertexMap.end()) {
                                            uint32_t created = duplicateVertex(idx[k]);
                                            vertexMap[idx[k]] = created;
                                            newIdx[k] = created;
                                        } else {
                                            newIdx[k] = it->second;
                                        }
                                    }
                                    pushExtrudeVert((int)newIdx[k]);
                                }
                                meshEditAsset.faces.push_back(glm::u32vec3(newIdx[0], newIdx[1], newIdx[2]));
                                int newFaceIndex = (int)meshEditAsset.faces.size() - 1;
                                newFaceVerts[fi] = glm::u32vec3(newIdx[0], newIdx[1], newIdx[2]);
                                newFaceSelection.push_back(newFaceIndex);
                            }

                            auto addSide = [&](uint32_t a, uint32_t b, uint32_t aNew, uint32_t bNew) {
                                meshEditAsset.faces.push_back(glm::u32vec3(a, b, bNew));
                                meshEditAsset.faces.push_back(glm::u32vec3(a, bNew, aNew));
                            };

                            if (seams) {
                                for (int fi : meshEditSelectedFaces) {
                                    if (fi < 0 || fi >= (int)faceCount) continue;
                                    auto itFace = newFaceVerts.find(fi);
                                    if (itFace == newFaceVerts.end()) continue;
                                    const auto f = itFace->second;
                                    const auto oldF = originalFaces[fi];
                                    uint32_t oldIdx[3] = { oldF.x, oldF.y, oldF.z };
                                    uint32_t newIdx[3] = { f.x, f.y, f.z };
                                    addSide(oldIdx[0], oldIdx[1], newIdx[0], newIdx[1]);
                                    addSide(oldIdx[1], oldIdx[2], newIdx[1], newIdx[2]);
                                    addSide(oldIdx[2], oldIdx[0], newIdx[2], newIdx[0]);
                                }
                            } else {
                                struct EdgeInfo { int total = 0; int selected = 0; };
                                std::unordered_map<uint64_t, EdgeInfo> edgeInfo;
                                edgeInfo.reserve(faceCount * 3);
                                auto edgeKey = [](uint32_t a, uint32_t b) {
                                    return (static_cast<uint64_t>(std::min(a,b)) << 32) | static_cast<uint64_t>(std::max(a,b));
                                };
                                for (size_t fi = 0; fi < faceCount; ++fi) {
                                    const auto& f = originalFaces[fi];
                                    uint32_t tri[3] = { f.x, f.y, f.z };
                                    for (int e = 0; e < 3; ++e) {
                                        uint32_t a = tri[e];
                                        uint32_t b = tri[(e + 1) % 3];
                                        auto& info = edgeInfo[edgeKey(a, b)];
                                        info.total += 1;
                                        if (faceSelected[fi]) info.selected += 1;
                                    }
                                }
                                for (int fi : meshEditSelectedFaces) {
                                    if (fi < 0 || fi >= (int)faceCount) continue;
                                    const auto& f = originalFaces[fi];
                                    uint32_t tri[3] = { f.x, f.y, f.z };
                                    for (int e = 0; e < 3; ++e) {
                                        uint32_t a = tri[e];
                                        uint32_t b = tri[(e + 1) % 3];
                                        auto it = edgeInfo.find(edgeKey(a, b));
                                        if (it == edgeInfo.end()) continue;
                                        if (it->second.selected == 1 && it->second.selected < it->second.total) {
                                            uint32_t aNew = vertexMap[a];
                                            uint32_t bNew = vertexMap[b];
                                            addSide(a, b, aNew, bNew);
                                        } else if (it->second.total == 1) {
                                            uint32_t aNew = vertexMap[a];
                                            uint32_t bNew = vertexMap[b];
                                            addSide(a, b, aNew, bNew);
                                        }
                                    }
                                }
                            }

                            if (!newFaceSelection.empty()) {
                                meshEditSelectedFaces = newFaceSelection;
                                meshEditSelectedVertices.clear();
                                meshEditSelectedEdges.clear();
                            }

                            meshEditExtruding = !meshEditExtrudeVerts.empty();
                        } else if (wantsExtrude && meshEditSelectionMode == MeshEditSelectionMode::Edge && !meshEditSelectedEdges.empty()) {
                            newFaceStart = (int)meshEditAsset.faces.size();
                            std::unordered_map<uint32_t, uint32_t> vertexMap;
                            if (!seams) {
                                vertexMap.reserve(meshEditSelectedEdges.size() * 2);
                            }

                            auto addSide = [&](uint32_t a, uint32_t b, uint32_t aNew, uint32_t bNew) {
                                meshEditAsset.faces.push_back(glm::u32vec3(a, b, bNew));
                                meshEditAsset.faces.push_back(glm::u32vec3(a, bNew, aNew));
                            };

                            for (int ei : meshEditSelectedEdges) {
                                if (ei < 0 || ei >= (int)edges.size()) continue;
                                uint32_t a = edges[ei].x;
                                uint32_t b = edges[ei].y;
                                uint32_t aNew = 0;
                                uint32_t bNew = 0;
                                if (seams) {
                                    aNew = duplicateVertex(a);
                                    bNew = duplicateVertex(b);
                                } else {
                                    auto ita = vertexMap.find(a);
                                    if (ita == vertexMap.end()) {
                                        aNew = duplicateVertex(a);
                                        vertexMap[a] = aNew;
                                    } else {
                                        aNew = ita->second;
                                    }
                                    auto itb = vertexMap.find(b);
                                    if (itb == vertexMap.end()) {
                                        bNew = duplicateVertex(b);
                                        vertexMap[b] = bNew;
                                    } else {
                                        bNew = itb->second;
                                    }
                                }
                                pushExtrudeVert((int)aNew);
                                pushExtrudeVert((int)bNew);
                                addSide(a, b, aNew, bNew);
                            }

                            meshEditExtruding = !meshEditExtrudeVerts.empty();
                        }

                        if (newFaceStart >= 0 && newFaceStart < (int)meshEditAsset.faces.size()) {
                            ensureUvs();
                            bool wroteUvs = false;
                            for (int fi = newFaceStart; fi < (int)meshEditAsset.faces.size(); ++fi) {
                                const auto& f = meshEditAsset.faces[fi];
                                bool shouldWrite = !meshEditAsset.hasUVs ||
                                                   f.x >= (uint32_t)originalVertexCount ||
                                                   f.y >= (uint32_t)originalVertexCount ||
                                                   f.z >= (uint32_t)originalVertexCount;
                                if (shouldWrite) {
                                    wroteUvs |= applyPlanarUV(f);
                                }
                            }
                            if (wroteUvs) {
                                meshEditAsset.hasUVs = true;
                            }
                        }
                    }

                    std::vector<int> affectedVerts = baseAffectedVerts;
                    if (meshEditExtruding && !meshEditExtrudeVerts.empty()) {
                        affectedVerts = meshEditExtrudeVerts;
                    }

                    if (usingNow) {
                        if (!meshEditHistoryCaptured) {
                            recordState("meshEdit");
                            meshEditHistoryCaptured = true;
                        }
                        glm::vec3 deltaWorld = glm::vec3(gizmoMat[3]) - pivotWorld;
                        for (int idx : affectedVerts) {
                            glm::vec3 wp = glm::vec3(modelMatrix * glm::vec4(meshEditAsset.positions[idx], 1.0f));
                            wp += deltaWorld;
                            glm::vec3 newLocal = glm::vec3(invModel * glm::vec4(wp, 1.0f));
                            meshEditAsset.positions[idx] = newLocal;
                        }

                        recalcMesh();
                        meshEditDirty = true;

                        syncMeshEditToGPU(selectedObj);
                    } else {
                        meshEditHistoryCaptured = false;
                        meshEditExtruding = false;
                        meshEditExtrudeVerts.clear();
                    }

                    meshEditWasUsing = usingNow;
                } else {
                    meshEditHistoryCaptured = false;
                    meshEditExtruding = false;
                    meshEditExtrudeVerts.clear();
                    meshEditWasUsing = false;
                }
            } else {
                // Object transform mode
                float* snapPtr = nullptr;
                float snapRot[3] = { rotationSnapValue, rotationSnapValue, rotationSnapValue };

                if (useSnap) {
                    if (mCurrentGizmoOperation == ImGuizmo::ROTATE) {
                        snapPtr = snapRot;
                    } else {
                        snapPtr = snapValue;
                    }
                }

                glm::vec3 gizmoBoundsMin(-0.5f);
                glm::vec3 gizmoBoundsMax(0.5f);

                switch (selectedObj->type) {
                    case ObjectType::Cube:
                        gizmoBoundsMin = glm::vec3(-0.5f);
                        gizmoBoundsMax = glm::vec3(0.5f);
                        break;
                    case ObjectType::Sphere:
                        gizmoBoundsMin = glm::vec3(-0.5f);
                        gizmoBoundsMax = glm::vec3(0.5f);
                        break;
                    case ObjectType::Capsule:
                        gizmoBoundsMin = glm::vec3(-0.35f, -0.9f, -0.35f);
                        gizmoBoundsMax = glm::vec3(0.35f, 0.9f, 0.35f);
                        break;
                    case ObjectType::Plane:
                        gizmoBoundsMin = glm::vec3(-0.5f, -0.5f, -0.02f);
                        gizmoBoundsMax = glm::vec3(0.5f, 0.5f, 0.02f);
                        break;
                    case ObjectType::Mirror:
                    case ObjectType::Sprite:
                        gizmoBoundsMin = glm::vec3(-0.5f, -0.5f, -0.02f);
                        gizmoBoundsMax = glm::vec3(0.5f, 0.5f, 0.02f);
                        break;
                    case ObjectType::Torus:
                        gizmoBoundsMin = glm::vec3(-0.5f);
                        gizmoBoundsMax = glm::vec3(0.5f);
                        break;
                    case ObjectType::OBJMesh: {
                        const auto* info = g_objLoader.getMeshInfo(selectedObj->meshId);
                        if (info && info->boundsMin.x < info->boundsMax.x) {
                            gizmoBoundsMin = info->boundsMin;
                            gizmoBoundsMax = info->boundsMax;
                        }
                        break;
                    }
                    case ObjectType::Model: {
                        const auto* info = getModelLoader().getMeshInfo(selectedObj->meshId);
                        if (info && info->boundsMin.x < info->boundsMax.x) {
                            gizmoBoundsMin = info->boundsMin;
                            gizmoBoundsMax = info->boundsMax;
                        }
                        break;
                    }
                    case ObjectType::Camera:
                        gizmoBoundsMin = glm::vec3(-0.3f);
                        gizmoBoundsMax = glm::vec3(0.3f);
                        break;
                    case ObjectType::DirectionalLight:
                    case ObjectType::PointLight:
                    case ObjectType::SpotLight:
                    case ObjectType::AreaLight:
                        gizmoBoundsMin = glm::vec3(-0.3f);
                        gizmoBoundsMax = glm::vec3(0.3f);
                        break;
                    case ObjectType::PostFXNode:
                        gizmoBoundsMin = glm::vec3(-0.25f);
                        gizmoBoundsMax = glm::vec3(0.25f);
                        break;
                    case ObjectType::Empty:
                        gizmoBoundsMin = glm::vec3(-0.2f);
                        gizmoBoundsMax = glm::vec3(0.2f);
                        break;
                    case ObjectType::Sprite2D:
                    case ObjectType::Canvas:
                    case ObjectType::UIImage:
                    case ObjectType::UISlider:
                    case ObjectType::UIButton:
                    case ObjectType::UIText:
                        gizmoBoundsMin = glm::vec3(-0.5f, -0.5f, -0.01f);
                        gizmoBoundsMax = glm::vec3(0.5f, 0.5f, 0.01f);
                        break;
                }

                float bounds[6] = {
                    gizmoBoundsMin.x, gizmoBoundsMin.y, gizmoBoundsMin.z,
                    gizmoBoundsMax.x, gizmoBoundsMax.y, gizmoBoundsMax.z
                };
                float boundsSnap[3] = { snapValue[0], snapValue[1], snapValue[2] };
                const float* boundsPtr = (mCurrentGizmoOperation == ImGuizmo::BOUNDS) ? bounds : nullptr;
                const float* boundsSnapPtr = (useSnap && mCurrentGizmoOperation == ImGuizmo::BOUNDS) ? boundsSnap : nullptr;

                ImGuizmo::Manipulate(
                    glm::value_ptr(view),
                    glm::value_ptr(proj),
                    mCurrentGizmoOperation,
                    mCurrentGizmoMode,
                    glm::value_ptr(modelMatrix),
                    nullptr,
                    snapPtr,
                    boundsPtr,
                    boundsSnapPtr
                );

                std::array<glm::vec3, 8> corners = {
                    glm::vec3(gizmoBoundsMin.x, gizmoBoundsMin.y, gizmoBoundsMin.z),
                    glm::vec3(gizmoBoundsMax.x, gizmoBoundsMin.y, gizmoBoundsMin.z),
                    glm::vec3(gizmoBoundsMax.x, gizmoBoundsMax.y, gizmoBoundsMin.z),
                    glm::vec3(gizmoBoundsMin.x, gizmoBoundsMax.y, gizmoBoundsMin.z),
                    glm::vec3(gizmoBoundsMin.x, gizmoBoundsMin.y, gizmoBoundsMax.z),
                    glm::vec3(gizmoBoundsMax.x, gizmoBoundsMin.y, gizmoBoundsMax.z),
                    glm::vec3(gizmoBoundsMax.x, gizmoBoundsMax.y, gizmoBoundsMax.z),
                    glm::vec3(gizmoBoundsMin.x, gizmoBoundsMax.y, gizmoBoundsMax.z),
                };

                std::array<ImVec2, 8> projected{};
                bool allProjected = true;
                for (size_t i = 0; i < corners.size(); ++i) {
                    glm::vec3 world = glm::vec3(modelMatrix * glm::vec4(corners[i], 1.0f));
                    auto p = projectToScreen(world);
                    if (!p.has_value()) { allProjected = false; break; }
                    projected[i] = *p;
                }

                if (allProjected) {
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    ImU32 col = ImGui::GetColorU32(ImVec4(1.0f, 0.93f, 0.35f, 0.45f));
                    const int edges[12][2] = {
                        {0,1},{1,2},{2,3},{3,0},
                        {4,5},{5,6},{6,7},{7,4},
                        {0,4},{1,5},{2,6},{3,7}
                    };
                    for (auto& e : edges) {
                        dl->AddLine(projected[e[0]], projected[e[1]], col, 2.0f);
                    }
                }

                if (ImGuizmo::IsUsing()) {
                    if (!gizmoHistoryCaptured) {
                        recordState("gizmo");
                        gizmoHistoryCaptured = true;
                    }
                    glm::mat4 delta = modelMatrix * glm::inverse(originalModel);

                    auto applyDelta = [&](SceneObject& o) {
                        glm::mat4 m = compose(o);
                        glm::mat4 newM = delta * m;
                        glm::vec3 t, r, s;
                        DecomposeMatrix(newM, t, r, s);
                        o.position = t;
                        o.rotation = NormalizeEulerDegrees(glm::degrees(r));
                        o.scale = s;
                        syncLocalTransform(o);
                    };

                    if (selectedObjectIds.size() <= 1) {
                        applyDelta(*selectedObj);
                    } else {
                        std::unordered_set<int> selectedSet(selectedObjectIds.begin(), selectedObjectIds.end());
                        auto getParentId = [&](int id) -> int {
                            auto it = std::find_if(sceneObjects.begin(), sceneObjects.end(),
                                [id](const SceneObject& o){ return o.id == id; });
                            if (it == sceneObjects.end()) return -1;
                            return it->parentId;
                        };
                        auto hasSelectedAncestor = [&](int id) -> bool {
                            int parentId = getParentId(id);
                            while (parentId != -1) {
                                if (selectedSet.count(parentId)) {
                                    return true;
                                }
                                parentId = getParentId(parentId);
                            }
                            return false;
                        };
                        for (int id : selectedObjectIds) {
                            if (hasSelectedAncestor(id)) {
                                continue;
                            }
                            auto itObj = std::find_if(sceneObjects.begin(), sceneObjects.end(),
                                [id](const SceneObject& o){ return o.id == id; });
                            if (itObj != sceneObjects.end()) {
                                applyDelta(*itObj);
                            }
                        }
                    }

                    projectManager.currentProject.hasUnsavedChanges = true;
                } else {
                    gizmoHistoryCaptured = false;
                }
            }
        }

        auto drawCameraDirection = [&](const SceneObject& camObj) {
            glm::quat q = glm::quat(glm::radians(camObj.rotation));
            glm::mat3 rot = glm::mat3_cast(q);
            glm::vec3 forward = glm::normalize(rot * glm::vec3(0.0f, 0.0f, -1.0f));
            glm::vec3 upDir = glm::normalize(rot * glm::vec3(0.0f, 1.0f, 0.0f));
            if (!std::isfinite(forward.x) || glm::length(forward) < 1e-3f) return;

            auto start = projectToScreen(camObj.position);
            auto end = projectToScreen(camObj.position + forward * 1.4f);
            auto upTip = projectToScreen(camObj.position + upDir * 0.6f);
            if (start && end) {
                ImDrawList* dl = ImGui::GetWindowDrawList();
                ImU32 lineCol = ImGui::GetColorU32(ImVec4(0.3f, 0.8f, 1.0f, 0.9f));
                ImU32 headCol = ImGui::GetColorU32(ImVec4(0.9f, 1.0f, 1.0f, 0.95f));
                dl->AddLine(*start, *end, lineCol, 2.5f);
                ImVec2 dir = ImVec2(end->x - start->x, end->y - start->y);
                float len = sqrtf(dir.x * dir.x + dir.y * dir.y);
                if (len > 1.0f) {
                    ImVec2 normDir = ImVec2(dir.x / len, dir.y / len);
                    ImVec2 left = ImVec2(-normDir.y, normDir.x);
                    float head = 10.0f;
                    ImVec2 tip = *end;
                    ImVec2 p1 = ImVec2(tip.x - normDir.x * head + left.x * head * 0.6f, tip.y - normDir.y * head + left.y * head * 0.6f);
                    ImVec2 p2 = ImVec2(tip.x - normDir.x * head - left.x * head * 0.6f, tip.y - normDir.y * head - left.y * head * 0.6f);
                    dl->AddTriangleFilled(tip, p1, p2, headCol);
                }
                if (upTip) {
                    dl->AddCircleFilled(*upTip, 3.0f, ImGui::GetColorU32(ImVec4(0.8f, 1.0f, 0.6f, 0.8f)));
                }
            }
        };

        if (showSceneGizmos && !uiWorldMode) {
            for (const auto& obj : sceneObjects) {
                if (obj.hasCamera) {
                    drawCameraDirection(obj);
                }
            }
        }

        // Light visualization overlays
        auto drawLightOverlays = [&](const SceneObject& lightObj) {
            if (!lightObj.light.enabled) return;
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImU32 col = ImGui::GetColorU32(ImVec4(1.0f, 0.9f, 0.4f, 0.7f));
            ImU32 faint = ImGui::GetColorU32(ImVec4(1.0f, 0.9f, 0.4f, 0.25f));
            auto forwardFromRotation = [](const SceneObject& obj) {
                glm::vec3 f = glm::normalize(glm::vec3(
                    glm::sin(glm::radians(obj.rotation.y)) * glm::cos(glm::radians(obj.rotation.x)),
                    glm::sin(glm::radians(obj.rotation.x)),
                    glm::cos(glm::radians(obj.rotation.y)) * glm::cos(glm::radians(obj.rotation.x))
                ));
                if (glm::length(f) < 1e-3f || !std::isfinite(f.x)) f = glm::vec3(0.0f, -1.0f, 0.0f);
                return f;
            };

            if (lightObj.light.type == LightType::Point) {
                auto center = projectToScreen(lightObj.position);
                glm::vec3 offset = lightObj.position + glm::vec3(lightObj.light.range, 0.0f, 0.0f);
                auto edge = projectToScreen(offset);
                if (center && edge) {
                    float r = std::sqrt((center->x - edge->x)*(center->x - edge->x) + (center->y - edge->y)*(center->y - edge->y));
                    dl->AddCircle(*center, r, faint, 48, 2.0f);
                }
            } else if (lightObj.light.type == LightType::Spot) {
                glm::vec3 dir = forwardFromRotation(lightObj);
                glm::vec3 tip = lightObj.position;
                glm::vec3 end = tip + dir * lightObj.light.range;
                float innerRad = glm::tan(glm::radians(lightObj.light.innerAngle)) * lightObj.light.range;
                float outerRad = glm::tan(glm::radians(lightObj.light.outerAngle)) * lightObj.light.range;

                // Build basis
                glm::vec3 up = glm::abs(dir.y) > 0.9f ? glm::vec3(1,0,0) : glm::vec3(0,1,0);
                glm::vec3 right = glm::normalize(glm::cross(dir, up));
                up = glm::normalize(glm::cross(right, dir));

                auto drawConeRing = [&](float radius, ImU32 color) {
                    const int segments = 24;
                    ImVec2 prev;
                    bool first = true;
                    for (int i = 0; i <= segments; ++i) {
                        float a = (float)i / segments * 2.0f * PI;
                        glm::vec3 p = end + right * std::cos(a) * radius + up * std::sin(a) * radius;
                        auto sp = projectToScreen(p);
                        if (!sp) continue;
                        if (first) { prev = *sp; first = false; continue; }
                        dl->AddLine(prev, *sp, color, 1.5f);
                        prev = *sp;
                    }
                };

                auto sTip = projectToScreen(tip);
                auto sEnd = projectToScreen(end);
                if (sTip && sEnd) {
                    dl->AddLine(*sTip, *sEnd, col, 2.0f);
                    drawConeRing(innerRad, col);
                    drawConeRing(outerRad, faint);
                }
            } else if (lightObj.light.type == LightType::Area) {
                glm::vec3 n = forwardFromRotation(lightObj);
                glm::vec3 up = glm::abs(n.y) > 0.9f ? glm::vec3(1,0,0) : glm::vec3(0,1,0);
                glm::vec3 tangent = glm::normalize(glm::cross(up, n));
                glm::vec3 bitangent = glm::cross(n, tangent);
                glm::vec2 half = lightObj.light.size * 0.5f;
                glm::vec3 c = lightObj.position;
                glm::vec3 corners[4] = {
                    c + tangent * half.x + bitangent * half.y,
                    c - tangent * half.x + bitangent * half.y,
                    c - tangent * half.x - bitangent * half.y,
                    c + tangent * half.x - bitangent * half.y
                };
                ImVec2 projected[4];
                bool ok = true;
                for (int i = 0; i < 4; ++i) {
                    auto p = projectToScreen(corners[i]);
                    if (!p) { ok = false; break; }
                    projected[i] = *p;
                }
                if (ok) {
                    for (int i = 0; i < 4; ++i) {
                        dl->AddLine(projected[i], projected[(i+1)%4], col, 2.0f);
                    }
                    // normal indicator
                    auto cproj = projectToScreen(c);
                    auto nproj = projectToScreen(c + n * glm::max(lightObj.light.range, 0.5f));
                    if (cproj && nproj) {
                        dl->AddLine(*cproj, *nproj, col, 2.0f);
                        dl->AddCircleFilled(*nproj, 4.0f, col);
                    }
                }
            }
        };

        if (showSceneGizmos && !uiWorldMode) {
            for (const auto& obj : sceneObjects) {
                if (!obj.hasLight) continue;
                if (obj.light.type == LightType::Point || obj.light.type == LightType::Spot || obj.light.type == LightType::Area) {
                    drawLightOverlays(obj);
                }
            }
        }

        auto drawArmatureOverlays = [&](const SceneObject& skinnedObj,
                                        const std::unordered_map<int, const SceneObject*>& idLookup) {
            if (!skinnedObj.hasSkeletalAnimation || !skinnedObj.skeletal.enabled) return;
            if (skinnedObj.skeletal.boneNodeIds.empty()) return;

            std::unordered_set<int> boneIds;
            for (int id : skinnedObj.skeletal.boneNodeIds) {
                if (id >= 0) boneIds.insert(id);
            }
            if (boneIds.empty()) return;

            if (boneIds.size() <= 2 && skinnedObj.skeletal.skeletonRootId >= 0) {
                std::vector<int> stack;
                stack.push_back(skinnedObj.skeletal.skeletonRootId);
                while (!stack.empty()) {
                    int currentId = stack.back();
                    stack.pop_back();
                    auto it = idLookup.find(currentId);
                    if (it == idLookup.end() || !it->second) continue;
                    const SceneObject* node = it->second;
                    if (node->type == ObjectType::Empty) {
                        boneIds.insert(node->id);
                    }
                    for (int childId : node->childIds) {
                        if (childId >= 0) {
                            stack.push_back(childId);
                        }
                    }
                }
            }

            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImU32 lineCol = ImGui::GetColorU32(ImVec4(0.55f, 0.9f, 0.8f, 0.75f));
            ImU32 nodeCol = ImGui::GetColorU32(ImVec4(0.85f, 0.95f, 0.9f, 0.9f));
            ImU32 rootCol = ImGui::GetColorU32(ImVec4(1.0f, 0.85f, 0.45f, 0.95f));

            for (int id : boneIds) {
                auto it = idLookup.find(id);
                if (it == idLookup.end() || !it->second) continue;
                const SceneObject* boneObj = it->second;
                auto boneScreen = projectToScreen(boneObj->position);
                if (!boneScreen) continue;

                bool isRoot = boneObj->parentId < 0 || boneIds.find(boneObj->parentId) == boneIds.end();
                float radius = isRoot ? 4.5f : 3.0f;
                dl->AddCircleFilled(*boneScreen, radius, isRoot ? rootCol : nodeCol);

                if (boneObj->parentId >= 0) {
                    auto parentIt = idLookup.find(boneObj->parentId);
                    if (parentIt != idLookup.end() && parentIt->second &&
                        boneIds.find(boneObj->parentId) != boneIds.end()) {
                        auto parentScreen = projectToScreen(parentIt->second->position);
                        if (parentScreen) {
                            dl->AddLine(*parentScreen, *boneScreen, lineCol, 2.0f);
                        }
                    }
                }
            }
        };

        const float toolbarPadding = 6.0f;
        const float toolbarSpacing = 5.0f;
        const ImVec2 gizmoButtonSize(60.0f, 24.0f);
        const ImVec2 gizmoIconButtonSize(32.0f, 24.0f);
        if (showSceneGizmos && !uiWorldMode) {
            std::unordered_map<int, const SceneObject*> idLookup;
            idLookup.reserve(sceneObjects.size());
            for (const auto& obj : sceneObjects) {
                idLookup.emplace(obj.id, &obj);
            }
            for (const auto& obj : sceneObjects) {
                drawArmatureOverlays(obj, idLookup);
            }
        }

        const ImGuiStyle& style = ImGui::GetStyle();
        ImVec4 bgCol = style.Colors[ImGuiCol_PopupBg];
        bgCol.w = 0.78f;
        ImVec4 baseCol = style.Colors[ImGuiCol_FrameBg];
        baseCol.w = 0.85f;
        ImVec4 hoverCol = style.Colors[ImGuiCol_ButtonHovered];
        hoverCol.w = 0.95f;
        ImVec4 activeCol = style.Colors[ImGuiCol_ButtonActive];
        activeCol.w = 1.0f;
        ImVec4 accentCol = style.Colors[ImGuiCol_HeaderActive];
        accentCol.w = 1.0f;
        ImVec4 textCol = style.Colors[ImGuiCol_Text];

        ImU32 baseBtn = ImGui::GetColorU32(baseCol);
        ImU32 hoverBtn = ImGui::GetColorU32(GizmoToolbar::ScaleColor(hoverCol, 1.05f));
        ImU32 activeBtn = ImGui::GetColorU32(GizmoToolbar::ScaleColor(activeCol, 1.08f));
        ImU32 accent = ImGui::GetColorU32(accentCol);
        ImU32 iconColor = ImGui::GetColorU32(ImVec4(0.95f, 0.98f, 1.0f, 0.95f));
        ImU32 toolbarBg = ImGui::GetColorU32(bgCol);
        ImU32 toolbarOutline = ImGui::GetColorU32(ImVec4(1, 1, 1, 0.0f));

        if (showViewportToolbar) {
            ImGui::SetCursorScreenPos(ImVec2(toolbarRectMin.x + toolbarPadding, toolbarRectMin.y + toolbarPadding));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(toolbarSpacing, toolbarSpacing));

            ImDrawList* toolbarDrawList = ImGui::GetWindowDrawList();
            ImDrawListSplitter splitter;
            splitter.Split(toolbarDrawList, 2);
            splitter.SetCurrentChannel(toolbarDrawList, 1);

            ImGui::BeginGroup();

        auto gizmoButton = [&](const char* id, GizmoToolbar::Icon icon, ImGuizmo::OPERATION op, const char* tooltip) {
            if (GizmoToolbar::IconButton(id, icon, mCurrentGizmoOperation == op, gizmoIconButtonSize, baseBtn, hoverBtn, activeBtn, accent, iconColor)) {
                mCurrentGizmoOperation = op;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", tooltip);
            }
        };

        gizmoButton("##gizmo_move", GizmoToolbar::Icon::Translate, ImGuizmo::TRANSLATE, "Translate");
        ImGui::SameLine(0.0f, toolbarSpacing);
        gizmoButton("##gizmo_rotate", GizmoToolbar::Icon::Rotate, ImGuizmo::ROTATE, "Rotate");
        ImGui::SameLine(0.0f, toolbarSpacing);
        gizmoButton("##gizmo_scale", GizmoToolbar::Icon::Scale, ImGuizmo::SCALE, "Scale");
        ImGui::SameLine(0.0f, toolbarSpacing);
        bool canMeshEdit = false;
        if (selectedObj) {
            std::string ext = fs::path(selectedObj->meshPath).extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            canMeshEdit = ext == ".rmesh";
        }
        ImGui::BeginDisabled(!canMeshEdit);
        if (GizmoToolbar::IconButton("##gizmo_mesh_edit", GizmoToolbar::Icon::Mesh, meshEditMode, gizmoIconButtonSize, baseBtn, hoverBtn, activeBtn, accent, iconColor)) {
            meshEditMode = !meshEditMode;
            if (!meshEditMode) {
                meshEditLoaded = false;
                meshEditPath.clear();
                meshEditDirty = false;
                meshEditExtrudeMode = false;
                meshEditSelectedVertices.clear();
                meshEditSelectedEdges.clear();
                meshEditSelectedFaces.clear();
            }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Toggle mesh vertex edit mode");
        ImGui::EndDisabled();
        if (meshEditMode) {
            ImGui::SameLine(0.0f, toolbarSpacing);
            if (GizmoToolbar::ModeButton("Verts", meshEditSelectionMode == MeshEditSelectionMode::Vertex, ImVec2(50,24), baseCol, accentCol, textCol)) {
                meshEditSelectionMode = MeshEditSelectionMode::Vertex;
            }
            ImGui::SameLine(0.0f, toolbarSpacing * 0.6f);
            if (GizmoToolbar::ModeButton("Edges", meshEditSelectionMode == MeshEditSelectionMode::Edge, ImVec2(50,24), baseCol, accentCol, textCol)) {
                meshEditSelectionMode = MeshEditSelectionMode::Edge;
            }
            ImGui::SameLine(0.0f, toolbarSpacing * 0.6f);
            if (GizmoToolbar::ModeButton("Faces", meshEditSelectionMode == MeshEditSelectionMode::Face, ImVec2(50,24), baseCol, accentCol, textCol)) {
                meshEditSelectionMode = MeshEditSelectionMode::Face;
            }
            ImGui::SameLine(0.0f, toolbarSpacing * 0.6f);
            if (GizmoToolbar::ModeButton("Extrude", meshEditExtrudeMode, ImVec2(68,24), baseCol, accentCol, textCol)) {
                meshEditExtrudeMode = !meshEditExtrudeMode;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Toggle extrude mode (Shift to extrude, Shift+Ctrl for seams)");
            }
            ImGui::SameLine(0.0f, toolbarSpacing * 0.8f);
            ImGui::BeginDisabled(!meshEditLoaded || meshEditPath.empty());
            if (GizmoToolbar::TextButton("Save", meshEditDirty, ImVec2(52,24), baseBtn, hoverBtn, activeBtn, accent, iconColor)) {
                std::string err;
                if (!saveMeshEditAsset(err)) {
                    addConsoleMessage("Mesh save failed: " + err, ConsoleMessageType::Error);
                } else {
                    addConsoleMessage("Saved mesh: " + meshEditPath, ConsoleMessageType::Success);
                }
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(meshEditDirty ? "Save edited mesh to disk" : "Mesh is up to date");
            }
            ImGui::EndDisabled();
        }
        ImGui::SameLine(0.0f, toolbarSpacing);
        gizmoButton("##gizmo_bounds", GizmoToolbar::Icon::Bounds, ImGuizmo::BOUNDS, "Rect scale");
        ImGui::SameLine(0.0f, toolbarSpacing);
        gizmoButton("##gizmo_universal", GizmoToolbar::Icon::Universal, ImGuizmo::UNIVERSAL, "Universal");

        ImGui::SameLine(0.0f, toolbarSpacing * 1.25f);
        if (GizmoToolbar::IconButton("##mode_local", GizmoToolbar::Icon::LocalMode, mCurrentGizmoMode == ImGuizmo::LOCAL, gizmoIconButtonSize, baseBtn, hoverBtn, activeBtn, accent, iconColor)) {
            mCurrentGizmoMode = ImGuizmo::LOCAL;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Local");
        }
        ImGui::SameLine(0.0f, toolbarSpacing * 0.8f);
        if (GizmoToolbar::IconButton("##mode_world", GizmoToolbar::Icon::WorldMode, mCurrentGizmoMode == ImGuizmo::WORLD, gizmoIconButtonSize, baseBtn, hoverBtn, activeBtn, accent, iconColor)) {
            mCurrentGizmoMode = ImGuizmo::WORLD;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("World");
        }

        ImGui::SameLine(0.0f, toolbarSpacing);
        if (GizmoToolbar::IconButton("##snap_toggle", GizmoToolbar::Icon::SnapToggle, useSnap, gizmoIconButtonSize, baseBtn, hoverBtn, activeBtn, accent, iconColor)) {
            useSnap = !useSnap;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Snap");
        }

        if (useSnap) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(100);
            if (mCurrentGizmoOperation == ImGuizmo::ROTATE) {
                ImGui::DragFloat("##snapAngle", &rotationSnapValue, 1.0f, 1.0f, 90.0f, "%.0f deg");
            } else {
                ImGui::DragFloat("##snapVal", &snapValue[0], 0.1f, 0.1f, 10.0f, "%.1f");
                snapValue[1] = snapValue[2] = snapValue[0];
            }
        }

        ImGui::SameLine(0.0f, toolbarSpacing * 1.25f);
        if (GizmoToolbar::IconButton("##gizmo_toggle", GizmoToolbar::Icon::GizmoToggle, showSceneGizmos, gizmoIconButtonSize, baseBtn, hoverBtn, activeBtn, accent, iconColor)) {
            showSceneGizmos = !showSceneGizmos;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Toggle light/camera scene symbols");
        }
        ImGui::SameLine(0.0f, toolbarSpacing * 0.8f);
        if (GizmoToolbar::IconButton("##grid_toggle", GizmoToolbar::Icon::GridToggle, showSceneGrid3D, gizmoIconButtonSize, baseBtn, hoverBtn, activeBtn, accent, iconColor)) {
            showSceneGrid3D = !showSceneGrid3D;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Toggle 3D grid");
        }
        ImGui::SameLine(0.0f, toolbarSpacing * 0.8f);
        if (GizmoToolbar::IconButton("##ui_world_toggle", GizmoToolbar::Icon::UiWorldToggle, uiWorldMode, gizmoIconButtonSize, baseBtn, hoverBtn, activeBtn, accent, iconColor)) {
            uiWorldMode = !uiWorldMode;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Toggle 2D UI world overlay");
        }

            ImGui::EndGroup();

            ImVec2 groupMin = ImGui::GetItemRectMin();
            ImVec2 groupMax = ImGui::GetItemRectMax();
            ImVec2 bgMin = ImVec2(groupMin.x - toolbarPadding, groupMin.y - toolbarPadding);
            ImVec2 bgMax = ImVec2(groupMax.x + toolbarPadding, groupMax.y + toolbarPadding);

            splitter.SetCurrentChannel(toolbarDrawList, 0);
            float rounding = 10.0f;
            toolbarDrawList->AddRectFilled(bgMin, bgMax, toolbarBg, rounding, ImDrawFlags_RoundCornersAll);
            toolbarDrawList->AddRect(bgMin, bgMax, toolbarOutline, rounding, ImDrawFlags_RoundCornersAll, 1.5f);

            splitter.Merge(toolbarDrawList);

            toolbarSizeCache = ImVec2(bgMax.x - bgMin.x, bgMax.y - bgMin.y);
            toolbarRectMin = bgMin;
            toolbarRectMax = bgMax;

        if (ImGui::IsAnyItemHovered() ||
            ImGui::IsMouseHoveringRect(toolbarRectMin, toolbarRectMax)) {
            blockSelection = true;
        }
            ImGui::PopStyleVar();
        }

        if (uiWorldMode) {
            blockSelection = true;
        }
        // Left-click picking inside viewport
        if (mouseOverViewportImage &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
            !ImGuizmo::IsUsing() && !ImGuizmo::IsOver() &&
            !blockSelection)
        {
            glm::mat4 invViewProj = glm::inverse(proj * view);
            ImVec2 mousePos = ImGui::GetMousePos();

            auto makeRay = [&](const ImVec2& pos) {
                float x = (pos.x - imageMin.x) / (imageMax.x - imageMin.x);
                float y = (pos.y - imageMin.y) / (imageMax.y - imageMin.y);
                x = x * 2.0f - 1.0f;
                y = 1.0f - y * 2.0f;

                glm::vec4 nearPt = invViewProj * glm::vec4(x, y, -1.0f, 1.0f);
                glm::vec4 farPt  = invViewProj * glm::vec4(x, y,  1.0f, 1.0f);
                nearPt /= nearPt.w;
                farPt  /= farPt.w;

                glm::vec3 origin = glm::vec3(nearPt);
                glm::vec3 dir = glm::normalize(glm::vec3(farPt - nearPt));
                return std::make_pair(origin, dir);
            };

            auto rayAabb = [](const glm::vec3& orig, const glm::vec3& dir, const glm::vec3& bmin, const glm::vec3& bmax, float& tHit) {
                float tmin = -FLT_MAX;
                float tmax = FLT_MAX;
                for (int i = 0; i < 3; ++i) {
                    if (std::abs(dir[i]) < 1e-6f) {
                        if (orig[i] < bmin[i] || orig[i] > bmax[i]) return false;
                        continue;
                    }
                    float invD = 1.0f / dir[i];
                    float t1 = (bmin[i] - orig[i]) * invD;
                    float t2 = (bmax[i] - orig[i]) * invD;
                    if (t1 > t2) std::swap(t1, t2);
                    tmin = std::max(tmin, t1);
                    tmax = std::min(tmax, t2);
                    if (tmin > tmax) return false;
                }
                tHit = (tmin >= 0.0f) ? tmin : tmax;
                return tmax >= 0.0f;
            };

            auto raySphere = [](const glm::vec3& orig, const glm::vec3& dir, float radius, float& tHit) {
                float b = glm::dot(dir, orig);
                float c = glm::dot(orig, orig) - radius * radius;
                float disc = b * b - c;
                if (disc < 0.0f) return false;
                float sqrtDisc = sqrtf(disc);
                float t0 = -b - sqrtDisc;
                float t1 = -b + sqrtDisc;
                float t = (t0 >= 0.0f) ? t0 : t1;
                if (t < 0.0f) return false;
                tHit = t;
                return true;
            };

            auto rayTriangle = [](const glm::vec3& orig, const glm::vec3& dir, const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2, float& tHit) {
                const float EPSILON = 1e-6f;
                glm::vec3 e1 = v1 - v0;
                glm::vec3 e2 = v2 - v0;
                glm::vec3 pvec = glm::cross(dir, e2);
                float det = glm::dot(e1, pvec);
                if (fabs(det) < EPSILON) return false;
                float invDet = 1.0f / det;
                glm::vec3 tvec = orig - v0;
                float u = glm::dot(tvec, pvec) * invDet;
                if (u < 0.0f || u > 1.0f) return false;
                glm::vec3 qvec = glm::cross(tvec, e1);
                float v = glm::dot(dir, qvec) * invDet;
                if (v < 0.0f || u + v > 1.0f) return false;
                float t = glm::dot(e2, qvec) * invDet;
                if (t < 0.0f) return false;
                tHit = t;
                return true;
            };

            auto ray = makeRay(mousePos);
            float closest = FLT_MAX;
            int hitId = -1;

            for (const auto& obj : sceneObjects) {
                glm::vec3 aabbMin(-0.5f);
                glm::vec3 aabbMax(0.5f);

                glm::mat4 model(1.0f);
                model = glm::translate(model, obj.position);
                model = glm::rotate(model, glm::radians(obj.rotation.x), glm::vec3(1, 0, 0));
                model = glm::rotate(model, glm::radians(obj.rotation.y), glm::vec3(0, 1, 0));
                model = glm::rotate(model, glm::radians(obj.rotation.z), glm::vec3(0, 0, 1));
                model = glm::scale(model, obj.scale);

                glm::mat4 invModel = glm::inverse(model);
                glm::vec3 localOrigin = glm::vec3(invModel * glm::vec4(ray.first, 1.0f));
                glm::vec3 localDir = glm::normalize(glm::vec3(invModel * glm::vec4(ray.second, 0.0f)));

                float hitT = 0.0f;
                bool hit = false;
                switch (obj.type) {
                    case ObjectType::Cube:
                        hit = rayAabb(localOrigin, localDir, glm::vec3(-0.5f), glm::vec3(0.5f), hitT);
                        break;
                    case ObjectType::Sphere:
                        hit = raySphere(localOrigin, localDir, 0.5f, hitT);
                        break;
                    case ObjectType::Capsule:
                        hit = rayAabb(localOrigin, localDir, glm::vec3(-0.35f, -0.9f, -0.35f), glm::vec3(0.35f, 0.9f, 0.35f), hitT);
                        break;
                    case ObjectType::Plane:
                        hit = rayAabb(localOrigin, localDir, glm::vec3(-0.5f, -0.5f, -0.02f), glm::vec3(0.5f, 0.5f, 0.02f), hitT);
                        break;
                    case ObjectType::Mirror:
                        hit = rayAabb(localOrigin, localDir, glm::vec3(-0.5f, -0.5f, -0.02f), glm::vec3(0.5f, 0.5f, 0.02f), hitT);
                        break;
                    case ObjectType::Sprite:
                        hit = rayAabb(localOrigin, localDir, glm::vec3(-0.5f, -0.5f, -0.02f), glm::vec3(0.5f, 0.5f, 0.02f), hitT);
                        break;
                    case ObjectType::Torus:
                        hit = raySphere(localOrigin, localDir, 0.5f, hitT);
                        break;
                    case ObjectType::Sprite2D:
                    case ObjectType::Canvas:
                    case ObjectType::UIImage:
                    case ObjectType::UISlider:
                    case ObjectType::UIButton:
                    case ObjectType::UIText:
                        hit = false;
                        break;
                    case ObjectType::OBJMesh: {
                        const auto* info = g_objLoader.getMeshInfo(obj.meshId);
                        if (info && info->boundsMin.x < info->boundsMax.x) {
                            aabbMin = info->boundsMin;
                            aabbMax = info->boundsMax;
                        }
                        bool aabbHit = rayAabb(localOrigin, localDir, aabbMin, aabbMax, hitT);
                        if (aabbHit && info && !info->triangleVertices.empty()) {
                            float triBest = FLT_MAX;
                            for (size_t i = 0; i + 2 < info->triangleVertices.size(); i += 3) {
                                float triT = 0.0f;
                                if (rayTriangle(localOrigin, localDir, info->triangleVertices[i], info->triangleVertices[i + 1], info->triangleVertices[i + 2], triT)) {
                                    if (triT < triBest && triT >= 0.0f) triBest = triT;
                                }
                            }
                            if (triBest < FLT_MAX) {
                                hit = true;
                                hitT = triBest;
                            } else {
                                hit = false;
                            }
                        } else {
                            hit = aabbHit;
                        }
                        break;
                    }
                    case ObjectType::Model: {
                        const auto* info = getModelLoader().getMeshInfo(obj.meshId);
                        if (info && info->boundsMin.x < info->boundsMax.x) {
                            aabbMin = info->boundsMin;
                            aabbMax = info->boundsMax;
                        }
                        bool aabbHit = rayAabb(localOrigin, localDir, aabbMin, aabbMax, hitT);
                        if (aabbHit && info && !info->triangleVertices.empty()) {
                            float triBest = FLT_MAX;
                            for (size_t i = 0; i + 2 < info->triangleVertices.size(); i += 3) {
                                float triT = 0.0f;
                                if (rayTriangle(localOrigin, localDir, info->triangleVertices[i], info->triangleVertices[i + 1], info->triangleVertices[i + 2], triT)) {
                                    if (triT < triBest && triT >= 0.0f) triBest = triT;
                                }
                            }
                            if (triBest < FLT_MAX) {
                                hit = true;
                                hitT = triBest;
                            } else {
                                hit = false;
                            }
                        } else {
                            hit = aabbHit;
                        }
                        break;
                    }
                    case ObjectType::Camera:
                        hit = raySphere(localOrigin, localDir, 0.3f, hitT);
                        break;
                    case ObjectType::DirectionalLight:
                    case ObjectType::PointLight:
                    case ObjectType::SpotLight:
                    case ObjectType::AreaLight:
                        hit = raySphere(localOrigin, localDir, 0.3f, hitT);
                        break;
                    case ObjectType::PostFXNode:
                        hit = false;
                        break;
                    case ObjectType::Empty:
                        hit = false;
                        break;
                }

                if (hit && hitT < closest && hitT >= 0.0f) {
                    closest = hitT;
                    hitId = obj.id;
                }
            }

            viewportController.setFocused(true);
            if (hitId != -1) {
                bool additive = ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeyShift;
                setPrimarySelection(hitId, additive);
            } else {
                clearSelection();
            }
        }

        if (mouseOverViewportImage && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            viewportController.setFocused(true);
            cursorLocked = true;
            camera.firstMouse = true;
        }

        if (cursorLocked && !ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
            cursorLocked = false;
            camera.firstMouse = true;
        }
        if (cursorLocked) {
            viewportController.setFocused(true);
        }

        if (isPlaying && showViewOutput) {
            std::vector<const SceneObject*> playerCams;
            for (const auto& obj : sceneObjects) {
                if (obj.hasCamera && obj.camera.type == SceneCameraType::Player) {
                    playerCams.push_back(&obj);
                }
            }

            if (playerCams.empty()) {
                previewCameraId = -1;
            } else {
                auto findCamById = [&](int id) -> const SceneObject* {
                    auto it = std::find_if(playerCams.begin(), playerCams.end(), [id](const SceneObject* o) { return o->id == id; });
                    return (it != playerCams.end()) ? *it : nullptr;
                };
                const SceneObject* previewCam = findCamById(previewCameraId);
                if (!previewCam) {
                    previewCam = playerCams.front();
                    previewCameraId = previewCam->id;
                }

                int previewWidth = static_cast<int>(imageSize.x * 0.28f);
                previewWidth = std::clamp(previewWidth, 180, 420);
                int previewHeight = static_cast<int>(previewWidth / 16.0f * 9.0f);
                unsigned int previewTex = renderer.renderScenePreview(
                    makeCameraFromObject(*previewCam),
                    sceneObjects,
                    previewWidth,
                    previewHeight,
                    previewCam->camera.fov,
                    previewCam->camera.nearClip,
                    previewCam->camera.farClip,
                    previewCam->camera.applyPostFX
                );

                if (previewTex != 0) {
                    ImVec2 overlaySize(previewWidth + 20.0f, previewHeight + 64.0f);
                    ImVec2 overlayPos = ImVec2(imageMax.x - overlaySize.x - 12.0f, imageMax.y - overlaySize.y - 12.0f);
                    ImVec2 winPos = ImGui::GetWindowPos();
                    ImVec2 localPos = ImVec2(overlayPos.x - winPos.x, overlayPos.y - winPos.y);
                    ImGui::SetCursorPos(localPos);
                    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 8.0f));
                    ImGui::BeginChild("ViewOutputOverlay", overlaySize, true, ImGuiWindowFlags_NoScrollbar);
                    ImGui::TextDisabled("View Output");
                    ImGuiID comboId = ImGui::GetID("##ViewOutputCamera");
                    UIAnimationState& comboAnim = editorUiAnimationStates[comboId];
                    float comboAnimSpeed = 0.0f;
                    if (uiAnimationMode == UIAnimationMode::Fluid) {
                        comboAnimSpeed = 8.0f;
                    } else if (uiAnimationMode == UIAnimationMode::Snappy) {
                        comboAnimSpeed = 18.0f;
                    }
                    float comboAnimStep = (uiAnimationMode == UIAnimationMode::Off) ? 1.0f
                        : (1.0f - std::exp(-comboAnimSpeed * ImGui::GetIO().DeltaTime));
                    bool comboOpen = ImGui::IsPopupOpen(comboId, ImGuiPopupFlags_None);
                    if (uiAnimationMode == UIAnimationMode::Off) {
                        comboAnim.active = comboOpen ? 1.0f : 0.0f;
                    } else {
                        float target = comboOpen ? 1.0f : 0.0f;
                        comboAnim.active += (target - comboAnim.active) * comboAnimStep;
                    }
                    ImGui::SetNextWindowBgAlpha(0.85f * std::clamp(comboAnim.active, 0.0f, 1.0f));
                    if (ImGui::BeginCombo("##ViewOutputCamera", previewCam->name.c_str())) {
                        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, std::clamp(comboAnim.active, 0.0f, 1.0f));
                        for (const auto* cam : playerCams) {
                            bool selected = cam->id == previewCameraId;
                            if (ImGui::Selectable(cam->name.c_str(), selected)) {
                                previewCameraId = cam->id;
                            }
                            if (selected) ImGui::SetItemDefaultFocus();
                        }
                        ImGui::PopStyleVar();
                        ImGui::EndCombo();
                    }
                    ImGui::Image((void*)(intptr_t)previewTex, ImVec2((float)previewWidth, (float)previewHeight), ImVec2(0, 1), ImVec2(1, 0));
                    ImGui::EndChild();
                    ImGui::PopStyleVar();
                }
            }
        } else {
            previewCameraId = -1;
        }
    }

    // Overlay hint
    ImGui::SetCursorPos(ImVec2(10, 30));
    ImGui::TextColored(
        ImVec4(1, 1, 1, 0.3f),
        "Hold RMB: Look & Move | LMB: Select | WASD+QE: Move | ESC: Release | F11: Fullscreen"
    );

    if (cursorLocked) {
        ImGui::SetCursorPos(ImVec2(10, 50));
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Freelook Active");
    } else if (viewportController.isViewportFocused()) {
        ImGui::SetCursorPos(ImVec2(10, 50));
        ImGui::TextColored(ImVec4(0.7f, 0.9f, 1.0f, 1.0f), "Viewport Focused");
    }

    bool windowFocused = ImGui::IsWindowFocused();
    viewportController.updateFocusFromImGui(windowFocused, cursorLocked);

    ImGui::End();
}
#pragma endregion

#pragma region 3D UI Canvas Targets
void Engine::renderUiCanvas3DTargets() {
    if (!rendererInitialized || !projectManager.currentProject.isLoaded) return;

    ImGuiContext* mainContext = ImGui::GetCurrentContext();
    if (!mainContext) return;

    ImGuiStyle mainStyle = ImGui::GetStyle();
    ImGuiIO& mainIo = ImGui::GetIO();

    std::unordered_map<int, SceneObject*> byId;
    byId.reserve(sceneObjects.size());
    for (auto& obj : sceneObjects) {
        byId[obj.id] = &obj;
    }

    auto isUiType = [](const SceneObject& target) {
        return target.hasUI && target.ui.type != UIElementType::None;
    };

    for (auto& obj : sceneObjects) {
        if (obj.hasUI && obj.ui.type == UIElementType::Canvas && !obj.ui.renderIn3D) {
            if (obj.hasRenderer && obj.renderType == RenderType::Sprite) {
                obj.hasRenderer = false;
                obj.renderType = RenderType::None;
            }
        }
    }

    auto findCanvasRoot = [&](const SceneObject& obj) -> const SceneObject* {
        const SceneObject* current = &obj;
        const SceneObject* found = nullptr;
        while (current) {
            if (current->hasUI && current->ui.type == UIElementType::Canvas) {
                found = current;
            }
            if (current->parentId < 0) break;
            auto it = byId.find(current->parentId);
            if (it == byId.end()) break;
            current = it->second;
        }
        return found;
    };

    std::unordered_set<int> activeCanvasIds;
    for (auto& canvas : sceneObjects) {
        if (!canvas.enabled || !canvas.hasUI || canvas.ui.type != UIElementType::Canvas || !canvas.ui.renderIn3D) continue;
        activeCanvasIds.insert(canvas.id);

        canvas.hasRenderer = true;
        canvas.renderType = RenderType::Sprite;
        canvas.material.textureMix = 1.0f;

        int targetWidth = (canvas.ui.renderTargetSize.x > 0) ? canvas.ui.renderTargetSize.x : static_cast<int>(canvas.ui.size.x);
        int targetHeight = (canvas.ui.renderTargetSize.y > 0) ? canvas.ui.renderTargetSize.y : static_cast<int>(canvas.ui.size.y);
        targetWidth = std::clamp(targetWidth, 16, 4096);
        targetHeight = std::clamp(targetHeight, 16, 4096);
        float layoutWidth = std::max(1.0f, canvas.ui.size.x);
        float layoutHeight = std::max(1.0f, canvas.ui.size.y);

        Renderer::UiTargetInfo target = renderer.ensureUiTarget(canvas.id, targetWidth, targetHeight);
        if (target.fbo == 0 || target.texture == 0) continue;

        UiCanvas3DContext& ctxEntry = uiCanvas3DContexts[canvas.id];
        if (!ctxEntry.context) {
            ctxEntry.context = ImGui::CreateContext();
            ImGui::SetCurrentContext(ctxEntry.context);
            ImGuiIO& io = ImGui::GetIO();
            io.Fonts->AddFontDefault();
            ImGui_ImplOpenGL3_Init("#version 330");
            ImGui_ImplOpenGL3_CreateDeviceObjects();
            ctxEntry.backendReady = true;
        }

        ImGui::SetCurrentContext(ctxEntry.context);
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(layoutWidth, layoutHeight);
        io.DisplayFramebufferScale = ImVec2(
            (layoutWidth > 0.0f) ? (static_cast<float>(targetWidth) / layoutWidth) : 1.0f,
            (layoutHeight > 0.0f) ? (static_cast<float>(targetHeight) / layoutHeight) : 1.0f
        );
        io.DeltaTime = (mainIo.DeltaTime > 0.0f) ? mainIo.DeltaTime : (1.0f / 60.0f);
        auto inputIt = uiCanvas3DInputs.find(canvas.id);
        if (inputIt != uiCanvas3DInputs.end() && inputIt->second.hasInput) {
            io.MousePos = inputIt->second.mousePos;
            io.MouseDown[0] = inputIt->second.mouseDown[0];
            io.MouseDown[1] = inputIt->second.mouseDown[1];
            io.MouseDown[2] = inputIt->second.mouseDown[2];
            io.MouseWheel = inputIt->second.mouseWheel;
        } else {
            io.MousePos = ImVec2(-FLT_MAX, -FLT_MAX);
            io.MouseDown[0] = false;
            io.MouseDown[1] = false;
            io.MouseDown[2] = false;
            io.MouseWheel = 0.0f;
        }

        ImGui::GetStyle() = mainStyle;
        ImGui_ImplOpenGL3_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(ImVec2(layoutWidth, layoutHeight));
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
                                 ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBringToFrontOnFocus |
                                 ImGuiWindowFlags_NoBackground;
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
        ImGui::Begin("##Canvas3D", nullptr, flags);

        auto anchorToPivot = [](UIAnchor anchor, const ImVec2& size) {
            switch (anchor) {
                case UIAnchor::Center: return ImVec2(size.x * 0.5f, size.y * 0.5f);
                case UIAnchor::TopLeft: return ImVec2(0.0f, 0.0f);
                case UIAnchor::TopRight: return ImVec2(size.x, 0.0f);
                case UIAnchor::BottomLeft: return ImVec2(0.0f, size.y);
                case UIAnchor::BottomRight: return ImVec2(size.x, size.y);
                default: return ImVec2(size.x * 0.5f, size.y * 0.5f);
            }
        };
        auto anchorToPoint = [](UIAnchor anchor, const ImVec2& min, const ImVec2& max) {
            switch (anchor) {
                case UIAnchor::Center: return ImVec2((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
                case UIAnchor::TopLeft: return min;
                case UIAnchor::TopRight: return ImVec2(max.x, min.y);
                case UIAnchor::BottomLeft: return ImVec2(min.x, max.y);
                case UIAnchor::BottomRight: return max;
                default: return ImVec2((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
            }
        };
        auto resolveUIRect = [&](const SceneObject& obj, ImVec2& outMin, ImVec2& outMax) {
            std::vector<const SceneObject*> chain;
            const SceneObject* current = &obj;
            while (current) {
                if (isUiType(*current) && current->id != canvas.id) {
                    chain.push_back(current);
                }
                if (current->parentId < 0) break;
                auto it = byId.find(current->parentId);
                if (it == byId.end()) break;
                current = it->second;
                if (current->id == canvas.id) break;
            }
            std::reverse(chain.begin(), chain.end());

            ImVec2 regionMin = ImGui::GetWindowPos();
            ImVec2 regionMax = ImVec2(regionMin.x + layoutWidth, regionMin.y + layoutHeight);
            for (const SceneObject* node : chain) {
                ImVec2 size = ImVec2(std::max(1.0f, node->ui.size.x),
                                     std::max(1.0f, node->ui.size.y));
                ImVec2 anchorPoint = anchorToPoint(node->ui.anchor, regionMin, regionMax);
                ImVec2 pivot(anchorPoint.x + node->ui.position.x,
                             anchorPoint.y + node->ui.position.y);
                ImVec2 pivotOffset = anchorToPivot(node->ui.anchor, size);
                regionMin = ImVec2(pivot.x - pivotOffset.x, pivot.y - pivotOffset.y);
                regionMax = ImVec2(regionMin.x + size.x, regionMin.y + size.y);
            }
            outMin = regionMin;
            outMax = regionMax;
        };

        auto brighten = [](const ImVec4& c, float k) {
            return ImVec4(std::clamp(c.x * k, 0.0f, 1.0f),
                          std::clamp(c.y * k, 0.0f, 1.0f),
                          std::clamp(c.z * k, 0.0f, 1.0f),
                          c.w);
        };
        float animSpeed = 0.0f;
        if (uiAnimationMode == UIAnimationMode::Fluid) {
            animSpeed = 8.0f;
        } else if (uiAnimationMode == UIAnimationMode::Snappy) {
            animSpeed = 18.0f;
        }
        float animStep = (uiAnimationMode == UIAnimationMode::Off) ? 1.0f
            : (1.0f - std::exp(-animSpeed * ImGui::GetIO().DeltaTime));
        auto animateValue = [&](float& current, float target, bool immediate) {
            if (uiAnimationMode == UIAnimationMode::Off || immediate) {
                current = target;
            } else {
                current += (target - current) * animStep;
            }
            return current;
        };

        for (auto& obj : sceneObjects) {
            if (!obj.enabled || !isUiType(obj)) continue;
            const SceneObject* root = findCanvasRoot(obj);
            if (!root || root->id != canvas.id) continue;
            if (obj.ui.type == UIElementType::Canvas) continue;

            ImVec2 rectMin, rectMax;
            resolveUIRect(obj, rectMin, rectMax);
            ImVec2 rectSize(rectMax.x - rectMin.x, rectMax.y - rectMin.y);
            if (rectSize.x <= 1.0f || rectSize.y <= 1.0f) continue;

            ImGuiStyle savedStyle = ImGui::GetStyle();
            bool styleApplied = false;
            if (!obj.ui.stylePreset.empty()) {
                if (const auto* preset = getUIStylePreset(obj.ui.stylePreset)) {
                    ImGui::GetStyle() = preset->style;
                    styleApplied = true;
                }
            }

            ImVec2 drawMin = rectMin;
            ImVec2 drawMax = rectMax;
            ImVec2 drawSize(drawMax.x - drawMin.x, drawMax.y - drawMin.y);
            ImVec2 localMin(drawMin.x - ImGui::GetWindowPos().x, drawMin.y - ImGui::GetWindowPos().y);

            ImGui::PushID(obj.id);
            UIAnimationState& animState = uiAnimationStates[obj.id];
            if (!animState.initialized) {
                animState.sliderValue = obj.ui.sliderValue;
                animState.initialized = true;
            }
            if (obj.ui.type == UIElementType::Image || obj.ui.type == UIElementType::Sprite2D) {
                unsigned int texId = 0;
                if (!obj.albedoTexturePath.empty()) {
                    if (auto* tex = renderer.getTexture(obj.albedoTexturePath)) {
                        texId = tex->GetID();
                    }
                }
                ImVec4 tint(obj.ui.color.r, obj.ui.color.g, obj.ui.color.b, obj.ui.color.a);
                float angle = glm::radians(obj.ui.rotation);
                if (std::abs(angle) > 1e-4f) {
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    ImVec2 center = ImVec2((drawMin.x + drawMax.x) * 0.5f, (drawMin.y + drawMax.y) * 0.5f);
                    ImVec2 half = ImVec2(drawSize.x * 0.5f, drawSize.y * 0.5f);
                    float c = std::cos(angle);
                    float s = std::sin(angle);
                    auto rotPt = [&](float x, float y) {
                        return ImVec2(center.x + x * c - y * s, center.y + x * s + y * c);
                    };
                    ImVec2 p0 = rotPt(-half.x, -half.y);
                    ImVec2 p1 = rotPt( half.x, -half.y);
                    ImVec2 p2 = rotPt( half.x,  half.y);
                    ImVec2 p3 = rotPt(-half.x,  half.y);
                    if (texId != 0) {
                        dl->AddImageQuad((ImTextureID)(intptr_t)texId, p0, p1, p2, p3,
                                         ImVec2(0, 1), ImVec2(1, 1), ImVec2(1, 0), ImVec2(0, 0),
                                         ImGui::GetColorU32(tint));
                    } else {
                        ImU32 fill = ImGui::GetColorU32(tint);
                        ImU32 border = ImGui::GetColorU32(brighten(tint, 0.85f));
                        dl->AddQuadFilled(p0, p1, p2, p3, fill);
                        dl->AddQuad(p0, p1, p2, p3, border, 2.0f);
                        ImVec2 textSize = ImGui::CalcTextSize(obj.ui.label.c_str());
                        ImVec2 textPos(center.x - textSize.x * 0.5f, center.y - textSize.y * 0.5f);
                        dl->AddText(textPos, IM_COL32(210, 210, 220, 220), obj.ui.label.c_str());
                    }
                    ImGui::Dummy(drawSize);
                } else {
                    ImGui::SetCursorPos(localMin);
                    if (texId != 0) {
                        ImGui::Image((ImTextureID)(intptr_t)texId, drawSize, ImVec2(0, 1), ImVec2(1, 0), tint, ImVec4(0, 0, 0, 0));
                    } else {
                        ImDrawList* dl = ImGui::GetWindowDrawList();
                        ImU32 fill = ImGui::GetColorU32(tint);
                        ImU32 border = ImGui::GetColorU32(brighten(tint, 0.85f));
                        dl->AddRectFilled(drawMin, drawMax, fill, 6.0f);
                        dl->AddRect(drawMin, drawMax, border, 6.0f);
                        ImVec2 textSize = ImGui::CalcTextSize(obj.ui.label.c_str());
                        ImVec2 textPos(drawMin.x + (drawSize.x - textSize.x) * 0.5f,
                                       drawMin.y + (drawSize.y - textSize.y) * 0.5f);
                        dl->AddText(textPos, IM_COL32(210, 210, 220, 220), obj.ui.label.c_str());
                        ImGui::Dummy(drawSize);
                    }
                }
            } else if (obj.ui.type == UIElementType::Slider) {
                ImGui::SetCursorPos(localMin);
                ImVec4 tint(obj.ui.color.r, obj.ui.color.g, obj.ui.color.b, obj.ui.color.a);
                if (obj.ui.sliderStyle == UISliderStyle::ImGui) {
                    ImGui::PushItemWidth(drawSize.x);
                    ImGui::BeginDisabled(!obj.ui.interactable);
                    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(tint.x * 0.2f, tint.y * 0.2f, tint.z * 0.2f, tint.w * 0.6f));
                    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, brighten(tint, 0.5f));
                    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, brighten(tint, 0.7f));
                    ImGui::PushStyleColor(ImGuiCol_SliderGrab, brighten(tint, 0.9f));
                    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, brighten(tint, 1.1f));
                    ImGui::SliderFloat(obj.ui.label.c_str(), &obj.ui.sliderValue, obj.ui.sliderMin, obj.ui.sliderMax);
                    ImGui::PopStyleColor(5);
                    ImGui::EndDisabled();
                    ImGui::PopItemWidth();
                } else {
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    ImU32 bg = ImGui::GetColorU32(ImVec4(tint.x * 0.2f, tint.y * 0.2f, tint.z * 0.2f, tint.w * 0.6f));
                    ImU32 fill = ImGui::GetColorU32(tint);
                    ImU32 border = ImGui::GetColorU32(brighten(tint, 0.85f));
                    float minValue = obj.ui.sliderMin;
                    float maxValue = obj.ui.sliderMax;
                    float range = (maxValue - minValue);
                    if (range <= 1e-6f) range = 1.0f;
                    ImGui::BeginDisabled(!obj.ui.interactable);
                    ImGui::InvisibleButton("##UISlider", drawSize);
                    bool held = obj.ui.interactable && ImGui::IsItemActive();
                    if (held && ImGui::IsMouseDown(ImGuiMouseButton_Left) && drawSize.x > 1.0f) {
                        float mouseT = (ImGui::GetIO().MousePos.x - drawMin.x) / drawSize.x;
                        mouseT = std::clamp(mouseT, 0.0f, 1.0f);
                        float newValue = minValue + mouseT * range;
                        obj.ui.sliderValue = newValue;
                    }
                    ImGui::EndDisabled();

                    animateValue(animState.sliderValue, obj.ui.sliderValue, held);
                    float displayValue = (uiAnimationMode == UIAnimationMode::Off) ? obj.ui.sliderValue : animState.sliderValue;
                    float t = (displayValue - minValue) / range;
                    t = std::clamp(t, 0.0f, 1.0f);

                    if (obj.ui.sliderStyle == UISliderStyle::Fill) {
                        float rounding = 6.0f;
                        ImVec2 fillMax(drawMin.x + drawSize.x * t, drawMax.y);
                        dl->AddRectFilled(drawMin, drawMax, bg, rounding);
                        if (fillMax.x > drawMin.x) {
                            dl->AddRectFilled(drawMin, fillMax, fill, rounding);
                        }
                        dl->AddRect(drawMin, drawMax, border, rounding);
                        ImVec2 textSize = ImGui::CalcTextSize(obj.ui.label.c_str());
                        ImVec2 textPos(drawMin.x + (drawSize.x - textSize.x) * 0.5f,
                                       drawMin.y + (drawSize.y - textSize.y) * 0.5f);
                        dl->AddText(textPos, IM_COL32(240, 240, 245, 220), obj.ui.label.c_str());
                    } else if (obj.ui.sliderStyle == UISliderStyle::Circle) {
                        ImVec2 center((drawMin.x + drawMax.x) * 0.5f, (drawMin.y + drawMax.y) * 0.5f);
                        float radius = std::max(2.0f, std::min(drawSize.x, drawSize.y) * 0.5f - 2.0f);
                        dl->AddCircleFilled(center, radius, bg, 32);
                        float start = -IM_PI * 0.5f;
                        float end = start + t * IM_PI * 2.0f;
                        dl->PathClear();
                        dl->PathArcTo(center, radius, start, end, 32);
                        dl->PathLineTo(center);
                        dl->PathFillConvex(fill);
                        dl->AddCircle(center, radius, border, 32, 2.0f);
                        ImVec2 textSize = ImGui::CalcTextSize(obj.ui.label.c_str());
                        ImVec2 textPos(center.x - textSize.x * 0.5f, center.y - textSize.y * 0.5f);
                        dl->AddText(textPos, IM_COL32(240, 240, 245, 220), obj.ui.label.c_str());
                    }
                }
            } else if (obj.ui.type == UIElementType::Button) {
                ImGui::SetCursorPos(localMin);
                ImVec4 tint(obj.ui.color.r, obj.ui.color.g, obj.ui.color.b, obj.ui.color.a);
                obj.ui.buttonPressed = false;
                if (obj.ui.buttonStyle == UIButtonStyle::ImGui) {
                    ImGui::PushStyleColor(ImGuiCol_Button, tint);
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, brighten(tint, 1.1f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, brighten(tint, 1.2f));
                    ImGui::BeginDisabled(!obj.ui.interactable);
                    obj.ui.buttonPressed = ImGui::Button(obj.ui.label.c_str(), drawSize);
                    ImGui::EndDisabled();
                    ImGui::PopStyleColor(3);
                } else if (obj.ui.buttonStyle == UIButtonStyle::Outline) {
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    ImU32 border = ImGui::GetColorU32(tint);
                    ImGui::BeginDisabled(!obj.ui.interactable);
                    if (ImGui::InvisibleButton("##UIButton", drawSize)) {
                        obj.ui.buttonPressed = obj.ui.interactable;
                    }
                    bool hovered = ImGui::IsItemHovered();
                    bool active = ImGui::IsItemActive();
                    ImGui::EndDisabled();
                    float hoverT = animateValue(animState.hover, hovered ? 1.0f : 0.0f, false);
                    float activeT = animateValue(animState.active, active ? 1.0f : 0.0f, false);
                    if (hoverT > 0.001f) {
                        ImVec4 hoverCol = brighten(tint, 0.45f);
                        hoverCol.w *= std::clamp(hoverT, 0.0f, 1.0f);
                        dl->AddRectFilled(drawMin, drawMax, ImGui::GetColorU32(hoverCol), 6.0f);
                    }
                    if (activeT > 0.001f) {
                        ImVec4 activeCol = brighten(tint, 0.65f);
                        activeCol.w *= std::clamp(activeT, 0.0f, 1.0f);
                        dl->AddRectFilled(drawMin, drawMax, ImGui::GetColorU32(activeCol), 6.0f);
                    }
                    dl->AddRect(drawMin, drawMax, border, 6.0f, 0, 2.0f);
                    ImVec2 textSize = ImGui::CalcTextSize(obj.ui.label.c_str());
                    ImVec2 textPos(drawMin.x + (drawSize.x - textSize.x) * 0.5f,
                                   drawMin.y + (drawSize.y - textSize.y) * 0.5f);
                    dl->AddText(textPos, IM_COL32(240, 240, 245, 220), obj.ui.label.c_str());
                }
            } else if (obj.ui.type == UIElementType::Text) {
                ImDrawList* dl = ImGui::GetWindowDrawList();
                ImVec4 tint(obj.ui.color.r, obj.ui.color.g, obj.ui.color.b, obj.ui.color.a);
                float scale = std::max(0.1f, obj.ui.textScale);
                float fontSize = std::max(1.0f, ImGui::GetFontSize() * scale);
                ImVec2 textPos = ImVec2(drawMin.x + 4.0f, drawMin.y + 2.0f);
                ImGui::PushClipRect(drawMin, drawMax, true);
                dl->AddText(ImGui::GetFont(), fontSize, textPos, ImGui::GetColorU32(tint), obj.ui.label.c_str());
                ImGui::PopClipRect();
            }
            ImGui::PopID();
            if (styleApplied) ImGui::GetStyle() = savedStyle;
        }

        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::Render();

        GLint prevFbo = 0;
        GLint prevViewport[4] = {};
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
        glGetIntegerv(GL_VIEWPORT, prevViewport);

        glBindFramebuffer(GL_FRAMEBUFFER, target.fbo);
        glViewport(0, 0, target.width, target.height);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
        glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    }

    for (auto it = uiCanvas3DContexts.begin(); it != uiCanvas3DContexts.end(); ) {
        if (activeCanvasIds.find(it->first) == activeCanvasIds.end()) {
            if (it->second.context) {
                ImGui::SetCurrentContext(it->second.context);
                if (it->second.backendReady) {
                    ImGui_ImplOpenGL3_Shutdown();
                }
                ImGui::DestroyContext(it->second.context);
            }
            it = uiCanvas3DContexts.erase(it);
        } else {
            ++it;
        }
    }
    renderer.cleanupUiTargets(activeCanvasIds);

    ImGui::SetCurrentContext(mainContext);
}
#pragma endregion

#pragma region Player Viewport
void Engine::renderPlayerViewport() {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    if (playerMode && isPlaying && gameViewCursorLocked) {
        ImGui::SetNextWindowFocus();
    }

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoBringToFrontOnFocus |
                             ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoScrollWithMouse |
                             ImGuiWindowFlags_NoScrollbar;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("PlayerViewport", nullptr, flags);
    ImGui::PopStyleVar();

    bool windowFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    ImVec2 imageSize = ImGui::GetContentRegionAvail();
    if (imageSize.x > 0 && imageSize.y > 0) {
        viewportWidth = static_cast<int>(imageSize.x);
        viewportHeight = static_cast<int>(imageSize.y);
        if (rendererInitialized) {
            renderer.resize(viewportWidth, viewportHeight);
        }
    }

    if (rendererInitialized) {
        unsigned int tex = renderer.getViewportTexture();
        ImGui::Image((void*)(intptr_t)tex, imageSize, ImVec2(0, 1), ImVec2(1, 0));
        ImVec2 imageMin = ImGui::GetItemRectMin();
        ImVec2 imageMax = ImGui::GetItemRectMax();
        bool imageHovered = ImGui::IsItemHovered();

        auto updateUiCanvas3DInput = [&](const Camera& cam, float fovDeg, float nearPlane, float farPlane) {
            if (!imageHovered) return;
            ImVec2 mouse = ImGui::GetIO().MousePos;
            if (mouse.x < imageMin.x || mouse.x > imageMax.x || mouse.y < imageMin.y || mouse.y > imageMax.y) {
                return;
            }
            float width = std::max(1.0f, imageMax.x - imageMin.x);
            float height = std::max(1.0f, imageMax.y - imageMin.y);
            float ndcX = ((mouse.x - imageMin.x) / width) * 2.0f - 1.0f;
            float ndcY = 1.0f - ((mouse.y - imageMin.y) / height) * 2.0f;

            glm::mat4 view = cam.getViewMatrix();
            glm::mat4 proj = glm::perspective(glm::radians(fovDeg), width / height, nearPlane, farPlane);
            glm::mat4 inv = glm::inverse(proj * view);
            glm::vec4 nearP = inv * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
            glm::vec4 farP = inv * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
            glm::vec3 origin = glm::vec3(nearP) / nearP.w;
            glm::vec3 dir = glm::normalize(glm::vec3(farP) / farP.w - origin);

            const float eps = 1e-5f;
            for (auto& canvas : sceneObjects) {
                if (!canvas.enabled || !canvas.hasUI || canvas.ui.type != UIElementType::Canvas || !canvas.ui.renderIn3D) continue;
                glm::mat4 model(1.0f);
                model = glm::translate(model, canvas.position);
                model = glm::rotate(model, glm::radians(canvas.rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
                model = glm::rotate(model, glm::radians(canvas.rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
                model = glm::rotate(model, glm::radians(canvas.rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
                model = glm::scale(model, canvas.scale);

                glm::vec3 planePoint = glm::vec3(model * glm::vec4(0, 0, 0, 1));
                glm::vec3 planeNormal = glm::normalize(glm::mat3(model) * glm::vec3(0, 0, 1));
                float denom = glm::dot(planeNormal, dir);
                if (std::abs(denom) < eps) continue;
                float t = glm::dot(planeNormal, planePoint - origin) / denom;
                if (t < 0.0f) continue;
                glm::vec3 hit = origin + dir * t;
                glm::vec4 local4 = glm::inverse(model) * glm::vec4(hit, 1.0f);
                glm::vec3 local = glm::vec3(local4);
                if (std::abs(local.x) > 0.5f || std::abs(local.y) > 0.5f) continue;

                float u = local.x + 0.5f;
                float v = 0.5f - local.y;
                float layoutW = std::max(1.0f, canvas.ui.size.x);
                float layoutH = std::max(1.0f, canvas.ui.size.y);
                ImVec2 canvasPos(u * layoutW, v * layoutH);

                UiCanvas3DInput& input = uiCanvas3DInputs[canvas.id];
                if (!input.hasInput || t < input.hitT) {
                    input.mousePos = canvasPos;
                    input.mouseDown[0] = ImGui::GetIO().MouseDown[0];
                    input.mouseDown[1] = ImGui::GetIO().MouseDown[1];
                    input.mouseDown[2] = ImGui::GetIO().MouseDown[2];
                    input.mouseWheel = ImGui::GetIO().MouseWheel;
                    input.hasInput = true;
                    input.hitT = t;
                }
            }
        };

        updateUiCanvas3DInput(camera, FOV, NEAR_PLANE, FAR_PLANE);

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        float uiScaleX = (viewportWidth > 0) ? (imageSize.x / (float)viewportWidth) : 1.0f;
        float uiScaleY = (viewportHeight > 0) ? (imageSize.y / (float)viewportHeight) : 1.0f;

        if (showCanvasOverlay) {
            ImVec2 pad(8.0f, 8.0f);
            ImVec2 tl(imageMin.x + pad.x, imageMin.y + pad.y);
            ImVec2 br(imageMax.x - pad.x, imageMax.y - pad.y);
            drawList->AddRect(tl, br, IM_COL32(110, 170, 255, 180), 8.0f, 0, 2.0f);
        }

        bool uiInteracting = false;
        auto find3DCanvasId = [&](const SceneObject& target) -> int {
            const SceneObject* current = &target;
            while (current) {
                if (current->hasUI && current->ui.type == UIElementType::Canvas && current->ui.renderIn3D) {
                    return current->id;
                }
                if (current->parentId < 0) break;
                auto pit = std::find_if(sceneObjects.begin(), sceneObjects.end(),
                    [&](const SceneObject& o) { return o.id == current->parentId; });
                if (pit == sceneObjects.end()) break;
                current = &(*pit);
            }
            return -1;
        };
        auto isUiOn3DCanvas = [&](const SceneObject& target) {
            return find3DCanvasId(target) >= 0;
        };
        int editCanvas3DId = -1;
        if (SceneObject* selected = getSelectedObject()) {
            editCanvas3DId = find3DCanvasId(*selected);
        }
        auto isUIType = [&](const SceneObject& target) {
            if (!target.hasUI || target.ui.type == UIElementType::None) return false;
            int canvasId = find3DCanvasId(target);
            return (canvasId < 0) || (canvasId == editCanvas3DId);
        };
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::SetCursorScreenPos(imageMin);
        ImGui::BeginChild("PlayerUIOverlay",
                          ImVec2(imageMax.x - imageMin.x, imageMax.y - imageMin.y),
                          false,
                          ImGuiWindowFlags_NoTitleBar |
                          ImGuiWindowFlags_NoResize |
                          ImGuiWindowFlags_NoMove |
                          ImGuiWindowFlags_NoScrollbar |
                          ImGuiWindowFlags_NoScrollWithMouse |
                          ImGuiWindowFlags_NoSavedSettings |
                          ImGuiWindowFlags_NoBackground);

        auto anchorToPivot = [](UIAnchor anchor, const ImVec2& size) {
            switch (anchor) {
                case UIAnchor::Center: return ImVec2(size.x * 0.5f, size.y * 0.5f);
                case UIAnchor::TopLeft: return ImVec2(0.0f, 0.0f);
                case UIAnchor::TopRight: return ImVec2(size.x, 0.0f);
                case UIAnchor::BottomLeft: return ImVec2(0.0f, size.y);
                case UIAnchor::BottomRight: return ImVec2(size.x, size.y);
                default: return ImVec2(size.x * 0.5f, size.y * 0.5f);
            }
        };
        auto anchorToPoint = [](UIAnchor anchor, const ImVec2& min, const ImVec2& max) {
            switch (anchor) {
                case UIAnchor::Center: return ImVec2((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
                case UIAnchor::TopLeft: return min;
                case UIAnchor::TopRight: return ImVec2(max.x, min.y);
                case UIAnchor::BottomLeft: return ImVec2(min.x, max.y);
                case UIAnchor::BottomRight: return max;
                default: return ImVec2((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
            }
        };

        auto resolveUIRect = [&](const SceneObject& obj, ImVec2& outMin, ImVec2& outMax) {
            std::vector<const SceneObject*> chain;
            const SceneObject* current = &obj;
            while (current) {
                if (isUIType(*current)) {
                    chain.push_back(current);
                }
                if (current->parentId < 0) break;
                auto pit = std::find_if(sceneObjects.begin(), sceneObjects.end(),
                    [&](const SceneObject& o) { return o.id == current->parentId; });
                if (pit == sceneObjects.end()) break;
                current = &(*pit);
            }
            std::reverse(chain.begin(), chain.end());

            ImVec2 regionMin = ImGui::GetWindowPos();
            ImVec2 regionMax = ImVec2(regionMin.x + ImGui::GetWindowWidth(), regionMin.y + ImGui::GetWindowHeight());
            for (const SceneObject* node : chain) {
                ImVec2 size = ImVec2(std::max(1.0f, node->ui.size.x * uiScaleX),
                                     std::max(1.0f, node->ui.size.y * uiScaleY));
                ImVec2 anchorPoint = anchorToPoint(node->ui.anchor, regionMin, regionMax);
                ImVec2 pivot(anchorPoint.x + node->ui.position.x * uiScaleX,
                             anchorPoint.y + node->ui.position.y * uiScaleY);
                ImVec2 pivotOffset = anchorToPivot(node->ui.anchor, size);
                regionMin = ImVec2(pivot.x - pivotOffset.x, pivot.y - pivotOffset.y);
                regionMax = ImVec2(regionMin.x + size.x, regionMin.y + size.y);
            }
            outMin = regionMin;
            outMax = regionMax;
        };

        ImVec2 overlayPos = ImGui::GetWindowPos();
        ImVec2 overlaySize = ImGui::GetWindowSize();
        bool useWorldUi = uiWorldMode;
        if (!useWorldUi) {
            uiWorldPanning = false;
        }
        if (useWorldUi) {
            uiWorldCamera.viewportSize = glm::vec2(overlaySize.x, overlaySize.y);
        }
        auto worldToScreen = [&](const glm::vec2& world) {
            glm::vec2 local = uiWorldCamera.WorldToScreen(world);
            return ImVec2(overlayPos.x + local.x, overlayPos.y + local.y);
        };
        auto screenToWorld = [&](const ImVec2& screen) {
            glm::vec2 local(screen.x - overlayPos.x, screen.y - overlayPos.y);
            return uiWorldCamera.ScreenToWorld(local);
        };
        auto getWorldParentOffset = [&](const SceneObject& obj) {
            glm::vec2 offset(0.0f);
            const SceneObject* current = &obj;
            while (current && current->parentId >= 0) {
                auto pit = std::find_if(sceneObjects.begin(), sceneObjects.end(),
                    [&](const SceneObject& o) { return o.id == current->parentId; });
                if (pit == sceneObjects.end()) break;
                current = &(*pit);
                if (current->hasUI && current->ui.type != UIElementType::None) {
                    offset += glm::vec2(current->ui.position.x, current->ui.position.y);
                }
            }
            return offset;
        };
        auto resolveUIRectWorld = [&](const SceneObject& obj, ImVec2& outMin, ImVec2& outMax) {
            glm::vec2 parentOffset = getWorldParentOffset(obj);
            glm::vec2 worldPos = parentOffset + glm::vec2(obj.ui.position.x, obj.ui.position.y);
            glm::vec2 sizeWorld(obj.ui.size.x, obj.ui.size.y);
            ImVec2 pivotOffset = anchorToPivot(obj.ui.anchor, ImVec2(sizeWorld.x, sizeWorld.y));
            glm::vec2 worldMin = worldPos - glm::vec2(pivotOffset.x, pivotOffset.y);
            glm::vec2 worldMax = worldMin + sizeWorld;
            ImVec2 s0 = worldToScreen(worldMin);
            ImVec2 s1 = worldToScreen(worldMax);
            outMin = ImVec2(std::min(s0.x, s1.x), std::min(s0.y, s1.y));
            outMax = ImVec2(std::max(s0.x, s1.x), std::max(s0.y, s1.y));
        };
        auto rectOutsideOverlay = [&](const ImVec2& min, const ImVec2& max) {
            return (max.x < overlayPos.x || min.x > overlayPos.x + overlaySize.x ||
                    max.y < overlayPos.y || min.y > overlayPos.y + overlaySize.y);
        };

        bool uiWorldHover = imageHovered || ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
        bool uiWorldCameraActive = false;
        if (useWorldUi) {
            ImGuiIO& io = ImGui::GetIO();
            bool panHeld = uiWorldHover && (ImGui::IsMouseDown(ImGuiMouseButton_Middle) ||
                (ImGui::IsKeyDown(ImGuiKey_Space) && ImGui::IsMouseDown(ImGuiMouseButton_Left)));
            if (panHeld) {
                uiWorldPanning = true;
            } else if (!ImGui::IsMouseDown(ImGuiMouseButton_Middle) &&
                       !(ImGui::IsKeyDown(ImGuiKey_Space) && ImGui::IsMouseDown(ImGuiMouseButton_Left))) {
                uiWorldPanning = false;
            }
            if (uiWorldPanning) {
                ImVec2 delta = io.MouseDelta;
                if (delta.x != 0.0f || delta.y != 0.0f) {
                    uiWorldCamera.position.x -= delta.x / uiWorldCamera.zoom;
                    uiWorldCamera.position.y += delta.y / uiWorldCamera.zoom;
                }
                uiWorldCameraActive = true;
            }
            if (uiWorldHover && io.MouseWheel != 0.0f) {
                glm::vec2 mouseLocal(io.MousePos.x - overlayPos.x, io.MousePos.y - overlayPos.y);
                glm::vec2 worldBefore = uiWorldCamera.ScreenToWorld(mouseLocal);
                float zoomFactor = 1.0f + io.MouseWheel * 0.1f;
                float newZoom = std::clamp(uiWorldCamera.zoom * zoomFactor, 5.0f, 2000.0f);
                if (newZoom != uiWorldCamera.zoom) {
                    uiWorldCamera.zoom = newZoom;
                    glm::vec2 worldAfter = uiWorldCamera.ScreenToWorld(mouseLocal);
                    uiWorldCamera.position += (worldBefore - worldAfter);
                    uiWorldCameraActive = true;
                }
            }
            if (uiWorldHover) {
                glm::vec2 panDir(0.0f);
                if (ImGui::IsKeyDown(ImGuiKey_A)) panDir.x -= 1.0f;
                if (ImGui::IsKeyDown(ImGuiKey_D)) panDir.x += 1.0f;
                if (ImGui::IsKeyDown(ImGuiKey_W)) panDir.y += 1.0f;
                if (ImGui::IsKeyDown(ImGuiKey_S)) panDir.y -= 1.0f;
                if (panDir.x != 0.0f || panDir.y != 0.0f) {
                    float panSpeed = 6.0f;
                    if (ImGui::IsKeyDown(ImGuiKey_LeftShift) || ImGui::IsKeyDown(ImGuiKey_RightShift)) {
                        panSpeed *= 2.5f;
                    }
                    uiWorldCamera.position += panDir * (panSpeed * deltaTime);
                    uiWorldCameraActive = true;
                }
            }
        }

        auto brighten = [](const ImVec4& c, float k) {
            return ImVec4(std::clamp(c.x * k, 0.0f, 1.0f),
                          std::clamp(c.y * k, 0.0f, 1.0f),
                          std::clamp(c.z * k, 0.0f, 1.0f),
                          c.w);
        };
        float animSpeed = 0.0f;
        if (uiAnimationMode == UIAnimationMode::Fluid) {
            animSpeed = 8.0f;
        } else if (uiAnimationMode == UIAnimationMode::Snappy) {
            animSpeed = 18.0f;
        }
        float animStep = (uiAnimationMode == UIAnimationMode::Off) ? 1.0f
            : (1.0f - std::exp(-animSpeed * ImGui::GetIO().DeltaTime));
        auto animateValue = [&](float& current, float target, bool immediate) {
            if (uiAnimationMode == UIAnimationMode::Off || immediate) {
                current = target;
            } else {
                current += (target - current) * animStep;
            }
            return current;
        };

        if (useWorldUi && showUIWorldGrid) {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 overlayMax(overlayPos.x + overlaySize.x, overlayPos.y + overlaySize.y);
            dl->PushClipRect(overlayPos, overlayMax, true);
            float step = 1.0f;
            float minPx = 30.0f;
            float maxPx = 140.0f;
            while (step * uiWorldCamera.zoom < minPx) step *= 2.0f;
            while (step * uiWorldCamera.zoom > maxPx) step *= 0.5f;

            glm::vec2 worldMin = uiWorldCamera.ScreenToWorld(glm::vec2(0.0f, overlaySize.y));
            glm::vec2 worldMax = uiWorldCamera.ScreenToWorld(glm::vec2(overlaySize.x, 0.0f));
            float startX = std::floor(worldMin.x / step) * step;
            float endX = std::ceil(worldMax.x / step) * step;
            float startY = std::floor(worldMin.y / step) * step;
            float endY = std::ceil(worldMax.y / step) * step;
            ImU32 gridColor = IM_COL32(90, 110, 140, 50);
            ImU32 axisColorX = IM_COL32(240, 120, 120, 170);
            ImU32 axisColorY = IM_COL32(120, 240, 150, 170);

            for (float x = startX; x <= endX; x += step) {
                ImVec2 p0 = worldToScreen(glm::vec2(x, worldMin.y));
                ImVec2 p1 = worldToScreen(glm::vec2(x, worldMax.y));
                dl->AddLine(p0, p1, gridColor, 1.0f);
            }
            for (float y = startY; y <= endY; y += step) {
                ImVec2 p0 = worldToScreen(glm::vec2(worldMin.x, y));
                ImVec2 p1 = worldToScreen(glm::vec2(worldMax.x, y));
                dl->AddLine(p0, p1, gridColor, 1.0f);
            }

            ImVec2 axisX0 = worldToScreen(glm::vec2(worldMin.x, 0.0f));
            ImVec2 axisX1 = worldToScreen(glm::vec2(worldMax.x, 0.0f));
            ImVec2 axisY0 = worldToScreen(glm::vec2(0.0f, worldMin.y));
            ImVec2 axisY1 = worldToScreen(glm::vec2(0.0f, worldMax.y));
            dl->AddLine(axisX0, axisX1, axisColorX, 2.0f);
            dl->AddLine(axisY0, axisY1, axisColorY, 2.0f);

            ImVec2 indicator = ImVec2(overlayPos.x + 36.0f, overlayPos.y + overlaySize.y - 36.0f);
            dl->AddLine(indicator, ImVec2(indicator.x + 22.0f, indicator.y), axisColorX, 2.0f);
            dl->AddLine(indicator, ImVec2(indicator.x, indicator.y - 22.0f), axisColorY, 2.0f);
            dl->AddText(ImVec2(indicator.x + 26.0f, indicator.y - 8.0f), axisColorX, "+X");
            dl->AddText(ImVec2(indicator.x - 16.0f, indicator.y - 30.0f), axisColorY, "+Y");
            dl->PopClipRect();
        }

        for (auto& obj : sceneObjects) {
            if (!obj.enabled || !isUIType(obj)) continue;
            ImVec2 rectMin, rectMax;
            if (useWorldUi) {
                resolveUIRectWorld(obj, rectMin, rectMax);
            } else {
                resolveUIRect(obj, rectMin, rectMax);
            }
            ImVec2 rectSize(rectMax.x - rectMin.x, rectMax.y - rectMin.y);
            if (rectSize.x <= 1.0f || rectSize.y <= 1.0f) continue;

            ImGuiStyle savedStyle = ImGui::GetStyle();
            bool styleApplied = false;
            if (!obj.ui.stylePreset.empty()) {
                if (const auto* preset = getUIStylePreset(obj.ui.stylePreset)) {
                    ImGui::GetStyle() = preset->style;
                    styleApplied = true;
                }
            }

            if (obj.ui.type == UIElementType::Canvas) {
                ImDrawList* dl = ImGui::GetWindowDrawList();
                dl->AddRect(rectMin, rectMax, IM_COL32(110, 170, 255, 140), 6.0f, 0, 1.5f);
                if (styleApplied) ImGui::GetStyle() = savedStyle;
                continue;
            }

            ImVec2 drawMin = rectMin;
            ImVec2 drawMax = rectMax;
            ImVec2 drawSize(drawMax.x - drawMin.x, drawMax.y - drawMin.y);
            ImVec2 localMin(drawMin.x - overlayPos.x, drawMin.y - overlayPos.y);

            ImGui::PushID(obj.id);
            UIAnimationState& animState = uiAnimationStates[obj.id];
            if (!animState.initialized) {
                animState.sliderValue = obj.ui.sliderValue;
                animState.initialized = true;
            }
            if (obj.ui.type == UIElementType::Image || obj.ui.type == UIElementType::Sprite2D) {
                unsigned int texId = 0;
                if (!obj.albedoTexturePath.empty()) {
                    if (auto* tex = renderer.getTexture(obj.albedoTexturePath)) {
                        texId = tex->GetID();
                    }
                }
                ImVec4 tint(obj.ui.color.r, obj.ui.color.g, obj.ui.color.b, obj.ui.color.a);
                float angle = glm::radians(obj.ui.rotation);
                if (std::abs(angle) > 1e-4f) {
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    ImVec2 center = ImVec2((drawMin.x + drawMax.x) * 0.5f, (drawMin.y + drawMax.y) * 0.5f);
                    ImVec2 half = ImVec2(drawSize.x * 0.5f, drawSize.y * 0.5f);
                    float c = std::cos(angle);
                    float s = std::sin(angle);
                    auto rotPt = [&](float x, float y) {
                        return ImVec2(center.x + x * c - y * s, center.y + x * s + y * c);
                    };
                    ImVec2 p0 = rotPt(-half.x, -half.y);
                    ImVec2 p1 = rotPt( half.x, -half.y);
                    ImVec2 p2 = rotPt( half.x,  half.y);
                    ImVec2 p3 = rotPt(-half.x,  half.y);
                    if (texId != 0) {
                        dl->AddImageQuad((ImTextureID)(intptr_t)texId, p0, p1, p2, p3,
                                         ImVec2(0, 1), ImVec2(1, 1), ImVec2(1, 0), ImVec2(0, 0),
                                         ImGui::GetColorU32(tint));
                    } else {
                        ImU32 fill = ImGui::GetColorU32(tint);
                        ImU32 border = ImGui::GetColorU32(brighten(tint, 0.85f));
                        dl->AddQuadFilled(p0, p1, p2, p3, fill);
                        dl->AddQuad(p0, p1, p2, p3, border, 2.0f);
                        ImVec2 textSize = ImGui::CalcTextSize(obj.ui.label.c_str());
                        ImVec2 textPos(center.x - textSize.x * 0.5f, center.y - textSize.y * 0.5f);
                        dl->AddText(textPos, IM_COL32(210, 210, 220, 220), obj.ui.label.c_str());
                    }
                    ImGui::Dummy(drawSize);
                } else {
                    ImGui::SetCursorPos(localMin);
                    if (texId != 0) {
                        ImGui::Image((ImTextureID)(intptr_t)texId, drawSize, ImVec2(0, 1), ImVec2(1, 0), tint, ImVec4(0, 0, 0, 0));
                    } else {
                        ImDrawList* dl = ImGui::GetWindowDrawList();
                        ImU32 fill = ImGui::GetColorU32(tint);
                        ImU32 border = ImGui::GetColorU32(brighten(tint, 0.85f));
                        dl->AddRectFilled(drawMin, drawMax, fill, 6.0f);
                        dl->AddRect(drawMin, drawMax, border, 6.0f);
                        ImVec2 textSize = ImGui::CalcTextSize(obj.ui.label.c_str());
                        ImVec2 textPos(drawMin.x + (drawSize.x - textSize.x) * 0.5f,
                                       drawMin.y + (drawSize.y - textSize.y) * 0.5f);
                        dl->AddText(textPos, IM_COL32(210, 210, 220, 220), obj.ui.label.c_str());
                        ImGui::Dummy(drawSize);
                    }
                }
            } else if (obj.ui.type == UIElementType::Slider) {
                ImGui::SetCursorPos(localMin);
                ImVec4 tint(obj.ui.color.r, obj.ui.color.g, obj.ui.color.b, obj.ui.color.a);
                if (obj.ui.sliderStyle == UISliderStyle::ImGui) {
                    ImGui::PushItemWidth(drawSize.x);
                    ImGui::BeginDisabled(!obj.ui.interactable || uiWorldCameraActive);
                    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(tint.x * 0.2f, tint.y * 0.2f, tint.z * 0.2f, tint.w * 0.6f));
                    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, brighten(tint, 0.5f));
                    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, brighten(tint, 0.7f));
                    ImGui::PushStyleColor(ImGuiCol_SliderGrab, brighten(tint, 0.9f));
                    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, brighten(tint, 1.1f));
                    if (ImGui::SliderFloat(obj.ui.label.c_str(), &obj.ui.sliderValue, obj.ui.sliderMin, obj.ui.sliderMax)) {
                        projectManager.currentProject.hasUnsavedChanges = true;
                    }
                    ImGui::PopStyleColor(5);
                    ImGui::EndDisabled();
                    ImGui::PopItemWidth();
                } else {
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    ImU32 bg = ImGui::GetColorU32(ImVec4(tint.x * 0.2f, tint.y * 0.2f, tint.z * 0.2f, tint.w * 0.6f));
                    ImU32 fill = ImGui::GetColorU32(tint);
                    ImU32 border = ImGui::GetColorU32(brighten(tint, 0.85f));
                    float minValue = obj.ui.sliderMin;
                    float maxValue = obj.ui.sliderMax;
                    float range = (maxValue - minValue);
                    if (range <= 1e-6f) range = 1.0f;
                    ImGui::BeginDisabled(!obj.ui.interactable || uiWorldCameraActive);
                    ImGui::InvisibleButton("##UISlider", drawSize);
                    bool held = obj.ui.interactable && ImGui::IsItemActive();
                    if (held && ImGui::IsMouseDown(ImGuiMouseButton_Left) && drawSize.x > 1.0f) {
                        float mouseT = (ImGui::GetIO().MousePos.x - drawMin.x) / drawSize.x;
                        mouseT = std::clamp(mouseT, 0.0f, 1.0f);
                        float newValue = minValue + mouseT * range;
                        if (newValue != obj.ui.sliderValue) {
                            obj.ui.sliderValue = newValue;
                            projectManager.currentProject.hasUnsavedChanges = true;
                        }
                    }
                    ImGui::EndDisabled();

                    animateValue(animState.sliderValue, obj.ui.sliderValue, held);
                    float displayValue = (uiAnimationMode == UIAnimationMode::Off) ? obj.ui.sliderValue : animState.sliderValue;
                    float t = (displayValue - minValue) / range;
                    t = std::clamp(t, 0.0f, 1.0f);

                    if (obj.ui.sliderStyle == UISliderStyle::Fill) {
                        float rounding = 6.0f;
                        ImVec2 fillMax(drawMin.x + drawSize.x * t, drawMax.y);
                        dl->AddRectFilled(drawMin, drawMax, bg, rounding);
                        if (fillMax.x > drawMin.x) {
                            dl->AddRectFilled(drawMin, fillMax, fill, rounding);
                        }
                        dl->AddRect(drawMin, drawMax, border, rounding);
                        ImVec2 textSize = ImGui::CalcTextSize(obj.ui.label.c_str());
                        ImVec2 textPos(drawMin.x + (drawSize.x - textSize.x) * 0.5f,
                                       drawMin.y + (drawSize.y - textSize.y) * 0.5f);
                        dl->AddText(textPos, IM_COL32(240, 240, 245, 220), obj.ui.label.c_str());
                    } else if (obj.ui.sliderStyle == UISliderStyle::Circle) {
                        ImVec2 center((drawMin.x + drawMax.x) * 0.5f, (drawMin.y + drawMax.y) * 0.5f);
                        float radius = std::max(2.0f, std::min(drawSize.x, drawSize.y) * 0.5f - 2.0f);
                        dl->AddCircleFilled(center, radius, bg, 32);
                        float start = -IM_PI * 0.5f;
                        float end = start + t * IM_PI * 2.0f;
                        dl->PathClear();
                        dl->PathArcTo(center, radius, start, end, 32);
                        dl->PathLineTo(center);
                        dl->PathFillConvex(fill);
                        dl->AddCircle(center, radius, border, 32, 2.0f);
                        ImVec2 textSize = ImGui::CalcTextSize(obj.ui.label.c_str());
                        ImVec2 textPos(center.x - textSize.x * 0.5f, center.y - textSize.y * 0.5f);
                        dl->AddText(textPos, IM_COL32(240, 240, 245, 220), obj.ui.label.c_str());
                    }
                }
            } else if (obj.ui.type == UIElementType::Button) {
                ImGui::SetCursorPos(localMin);
                ImVec4 tint(obj.ui.color.r, obj.ui.color.g, obj.ui.color.b, obj.ui.color.a);
                obj.ui.buttonPressed = false;
                if (obj.ui.buttonStyle == UIButtonStyle::ImGui) {
                    ImGui::PushStyleColor(ImGuiCol_Button, tint);
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, brighten(tint, 1.1f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, brighten(tint, 1.2f));
                    ImGui::BeginDisabled(!obj.ui.interactable || uiWorldCameraActive);
                    obj.ui.buttonPressed = ImGui::Button(obj.ui.label.c_str(), drawSize);
                    ImGui::EndDisabled();
                    ImGui::PopStyleColor(3);
                } else if (obj.ui.buttonStyle == UIButtonStyle::Outline) {
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    ImU32 border = ImGui::GetColorU32(tint);
                    ImGui::BeginDisabled(!obj.ui.interactable || uiWorldCameraActive);
                    if (ImGui::InvisibleButton("##UIButton", drawSize)) {
                        obj.ui.buttonPressed = obj.ui.interactable;
                    }
                    bool hovered = ImGui::IsItemHovered();
                    bool active = ImGui::IsItemActive();
                    ImGui::EndDisabled();
                    float hoverT = animateValue(animState.hover, hovered ? 1.0f : 0.0f, false);
                    float activeT = animateValue(animState.active, active ? 1.0f : 0.0f, false);
                    if (hoverT > 0.001f) {
                        ImVec4 hoverCol = brighten(tint, 0.45f);
                        hoverCol.w *= std::clamp(hoverT, 0.0f, 1.0f);
                        dl->AddRectFilled(drawMin, drawMax, ImGui::GetColorU32(hoverCol), 6.0f);
                    }
                    if (activeT > 0.001f) {
                        ImVec4 activeCol = brighten(tint, 0.65f);
                        activeCol.w *= std::clamp(activeT, 0.0f, 1.0f);
                        dl->AddRectFilled(drawMin, drawMax, ImGui::GetColorU32(activeCol), 6.0f);
                    }
                    dl->AddRect(drawMin, drawMax, border, 6.0f, 0, 2.0f);
                    ImVec2 textSize = ImGui::CalcTextSize(obj.ui.label.c_str());
                    ImVec2 textPos(drawMin.x + (drawSize.x - textSize.x) * 0.5f,
                                   drawMin.y + (drawSize.y - textSize.y) * 0.5f);
                    dl->AddText(textPos, IM_COL32(240, 240, 245, 220), obj.ui.label.c_str());
                }
            } else if (obj.ui.type == UIElementType::Text) {
                ImDrawList* dl = ImGui::GetWindowDrawList();
                ImVec4 tint(obj.ui.color.r, obj.ui.color.g, obj.ui.color.b, obj.ui.color.a);
                float scale = std::max(0.1f, obj.ui.textScale);
                float scaleFactor = useWorldUi ? std::max(0.01f, uiWorldCamera.zoom / 100.0f)
                                               : std::min(uiScaleX, uiScaleY);
                float fontSize = std::max(1.0f, ImGui::GetFontSize() * scale * scaleFactor);
                ImVec2 textPos = ImVec2(drawMin.x + 4.0f, drawMin.y + 2.0f);
                ImGui::PushClipRect(drawMin, drawMax, true);
                dl->AddText(ImGui::GetFont(), fontSize, textPos, ImGui::GetColorU32(tint), obj.ui.label.c_str());
                ImGui::PopClipRect();
            }
            ImGui::PopID();
            if (styleApplied) ImGui::GetStyle() = savedStyle;
        }

        uiInteracting = ImGui::IsAnyItemActive() || uiWorldCameraActive;
        ImGui::EndChild();
        ImGui::PopStyleVar();

        bool clicked = imageHovered && isPlaying && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !uiInteracting;
        if (clicked && !gameViewCursorLocked) {
            gameViewCursorLocked = true;
        }
        if (gameViewCursorLocked && (!isPlaying || ImGui::IsKeyPressed(ImGuiKey_Escape))) {
            gameViewCursorLocked = false;
        }
        gameViewportFocused = windowFocused || gameViewCursorLocked;
    }

    ImGui::End();
}
#pragma endregion
