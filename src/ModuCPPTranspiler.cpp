#include "ModuCPPTranspiler.h"

#include <algorithm>
#include <cctype>
#include <functional>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <unordered_set>

namespace {

enum class FieldVisibility {
    Public,
    Private
};

enum class FieldKind {
    Float,
    Int,
    Bool,
    Vec3,
    String,
    ObjectList,
    Custom
};

struct FieldSpec {
    FieldVisibility visibility = FieldVisibility::Private;
    FieldKind kind = FieldKind::Custom;
    std::string rawType;
    std::string baseType;
    std::vector<std::string> arrayDimensions;
    std::string name;
    std::string initializer;
    std::optional<std::string> rangeMinExpr;
    std::optional<std::string> rangeMaxExpr;
    std::optional<std::string> stepExpr;
    bool persist = false;
};

struct MethodSpec {
    std::string returnType = "void";
    std::string name;
    std::string originalParams;
    std::string transpiledParams;
    std::string body;
    bool hasContext = false;
    bool contextIsPointer = false;
    std::string contextParamName;
    bool hasManualPrelude = false;
    bool autoInjectedContext = false;
    bool autoInjectedDeltaTime = false;
    bool hasDeltaTimeParam = false;
    std::string deltaTimeParamName = "dt";
};

struct ClassSpec {
    std::string name;
    std::vector<std::string> includeDirectives;
    std::vector<std::string> usingDirectives;
    std::vector<FieldSpec> fields;
    std::vector<MethodSpec> methods;
    std::string inspectorBlock;
    std::string passthroughCode;
};

size_t findTopLevelChar(const std::string& text, char needle);
bool isVoidReturnType(const std::string& returnType);

std::string trimCopy(const std::string& value) {
    size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start])) != 0) {
        ++start;
    }
    size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }
    return value.substr(start, end - start);
}

size_t lineStartFromOffset(const std::string& text, size_t offset) {
    if (text.empty()) {
        return 0;
    }
    const size_t clamped = std::min(offset, text.size());
    const size_t lineBreak = text.rfind('\n', clamped == 0 ? 0 : clamped - 1);
    return lineBreak == std::string::npos ? 0 : lineBreak + 1;
}

size_t lineEndFromOffset(const std::string& text, size_t offset) {
    const size_t clamped = std::min(offset, text.size());
    const size_t lineBreak = text.find('\n', clamped);
    return lineBreak == std::string::npos ? text.size() : lineBreak;
}

size_t firstNonWhitespaceFrom(const std::string& text, size_t offset) {
    size_t pos = std::min(offset, text.size());
    while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos])) != 0) {
        ++pos;
    }
    return pos;
}

size_t previousStatementEnd(const std::string& text, size_t offset) {
    size_t pos = std::min(offset, text.size());
    while (pos > 0) {
        const unsigned char c = static_cast<unsigned char>(text[pos - 1]);
        if (c == '\n') {
            break;
        }
        if (std::isspace(c) == 0) {
            return pos;
        }
        --pos;
    }

    const size_t lineStart = lineStartFromOffset(text, offset);
    const size_t lineEnd = lineEndFromOffset(text, lineStart);
    size_t linePos = lineEnd;
    while (linePos > lineStart) {
        const unsigned char c = static_cast<unsigned char>(text[linePos - 1]);
        if (std::isspace(c) == 0) {
            return linePos;
        }
        --linePos;
    }
    return firstNonWhitespaceFrom(text, lineStart);
}

bool startsWithAccessModifier(const std::string& text, size_t offset) {
    if (offset >= text.size()) {
        return false;
    }
    return text.compare(offset, 7, "public ") == 0 ||
           text.compare(offset, 8, "private ") == 0 ||
           text.compare(offset, 7, "public:") == 0 ||
           text.compare(offset, 8, "private:") == 0;
}

std::optional<size_t> findSecondAccessModifierBoundary(const std::string& text) {
    bool foundAccessModifier = false;
    size_t lineStart = 0;
    while (lineStart <= text.size()) {
        const size_t lineFirstToken = firstNonWhitespaceFrom(text, lineStart);
        if (startsWithAccessModifier(text, lineFirstToken)) {
            if (foundAccessModifier) {
                return previousStatementEnd(text, lineStart);
            }
            foundAccessModifier = true;
        }

        const size_t lineBreak = text.find('\n', lineStart);
        if (lineBreak == std::string::npos) {
            break;
        }
        lineStart = lineBreak + 1;
    }
    return std::nullopt;
}

int lineNumberForOffset(const std::string& text, size_t offset) {
    const size_t clamped = std::min(offset, text.size());
    int line = 1;
    for (size_t i = 0; i < clamped; ++i) {
        if (text[i] == '\n') {
            ++line;
        }
    }
    return line;
}

int columnNumberForOffset(const std::string& text, size_t offset) {
    const size_t clamped = std::min(offset, text.size());
    const size_t lineStart = lineStartFromOffset(text, clamped);
    return static_cast<int>(clamped - lineStart) + 1;
}

std::string formatLocatedParseError(const std::string& message,
                                    const std::string& sourceText,
                                    size_t offset) {
    const size_t clamped = std::min(offset, sourceText.size());
    const int line = lineNumberForOffset(sourceText, clamped);
    const int column = columnNumberForOffset(sourceText, clamped);
    std::ostringstream out;
    out << message << " at line " << line << ", column " << column << ".";
    return out.str();
}

std::optional<size_t> findLikelyMissingSemicolonInFieldDecl(const std::string& fieldDecl) {
    if (std::optional<size_t> boundary = findSecondAccessModifierBoundary(fieldDecl)) {
        return boundary;
    }
    return std::nullopt;
}

std::optional<size_t> findLikelyMissingSemicolonBeforeMethod(const std::string& declaration) {
    if (std::optional<size_t> boundary = findSecondAccessModifierBoundary(declaration)) {
        return boundary;
    }

    const size_t eqPos = findTopLevelChar(declaration, '=');
    const size_t openParenPos = findTopLevelChar(declaration, '(');
    if (eqPos == std::string::npos || openParenPos == std::string::npos || eqPos > openParenPos) {
        return std::nullopt;
    }

    return previousStatementEnd(declaration, eqPos + 1);
}

std::string toLowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string removeWhitespaceCopy(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (char c : value) {
        if (std::isspace(static_cast<unsigned char>(c)) == 0) {
            out.push_back(c);
        }
    }
    return out;
}

std::string stripCommentsPreserveLayout(const std::string& source) {
    std::string out = source;
    enum class Mode {
        Normal,
        LineComment,
        BlockComment,
        StringLiteral,
        CharLiteral
    };

    Mode mode = Mode::Normal;
    bool escaped = false;
    for (size_t i = 0; i < out.size(); ++i) {
        char c = out[i];
        char next = (i + 1 < out.size()) ? out[i + 1] : '\0';

        switch (mode) {
            case Mode::Normal:
                if (c == '/' && next == '/') {
                    mode = Mode::LineComment;
                    out[i] = ' ';
                    out[i + 1] = ' ';
                    ++i;
                } else if (c == '/' && next == '*') {
                    mode = Mode::BlockComment;
                    out[i] = ' ';
                    out[i + 1] = ' ';
                    ++i;
                } else if (c == '"') {
                    mode = Mode::StringLiteral;
                    escaped = false;
                } else if (c == '\'') {
                    mode = Mode::CharLiteral;
                    escaped = false;
                }
                break;
            case Mode::LineComment:
                if (c == '\n') {
                    mode = Mode::Normal;
                } else {
                    out[i] = ' ';
                }
                break;
            case Mode::BlockComment:
                if (c == '*' && next == '/') {
                    out[i] = ' ';
                    out[i + 1] = ' ';
                    ++i;
                    mode = Mode::Normal;
                } else if (c != '\n') {
                    out[i] = ' ';
                }
                break;
            case Mode::StringLiteral:
                if (escaped) {
                    escaped = false;
                } else if (c == '\\') {
                    escaped = true;
                } else if (c == '"') {
                    mode = Mode::Normal;
                }
                break;
            case Mode::CharLiteral:
                if (escaped) {
                    escaped = false;
                } else if (c == '\\') {
                    escaped = true;
                } else if (c == '\'') {
                    mode = Mode::Normal;
                }
                break;
        }
    }

    return out;
}

size_t findMatchingBrace(const std::string& text, size_t openBracePos) {
    if (openBracePos >= text.size() || text[openBracePos] != '{') return std::string::npos;
    int depth = 1;
    bool inString = false;
    bool inChar = false;
    bool escaped = false;

    for (size_t i = openBracePos + 1; i < text.size(); ++i) {
        const char c = text[i];
        if (inString) {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                inString = false;
            }
            continue;
        }
        if (inChar) {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '\'') {
                inChar = false;
            }
            continue;
        }

        if (c == '"') {
            inString = true;
            escaped = false;
            continue;
        }
        if (c == '\'') {
            inChar = true;
            escaped = false;
            continue;
        }
        if (c == '{') {
            ++depth;
        } else if (c == '}') {
            --depth;
            if (depth == 0) {
                return i;
            }
        }
    }
    return std::string::npos;
}

size_t findMatchingParen(const std::string& text, size_t openParenPos) {
    if (openParenPos >= text.size() || text[openParenPos] != '(') return std::string::npos;
    int depth = 1;
    bool inString = false;
    bool inChar = false;
    bool escaped = false;

    for (size_t i = openParenPos + 1; i < text.size(); ++i) {
        const char c = text[i];
        if (inString) {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                inString = false;
            }
            continue;
        }
        if (inChar) {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '\'') {
                inChar = false;
            }
            continue;
        }

        if (c == '"') {
            inString = true;
            escaped = false;
            continue;
        }
        if (c == '\'') {
            inChar = true;
            escaped = false;
            continue;
        }
        if (c == '(') {
            ++depth;
        } else if (c == ')') {
            --depth;
            if (depth == 0) {
                return i;
            }
        }
    }
    return std::string::npos;
}

size_t findTopLevelChar(const std::string& text, char needle) {
    int parenDepth = 0;
    int angleDepth = 0;
    int bracketDepth = 0;
    int braceDepth = 0;
    bool inString = false;
    bool inChar = false;
    bool escaped = false;
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (inString) {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                inString = false;
            }
            continue;
        }
        if (inChar) {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '\'') {
                inChar = false;
            }
            continue;
        }

        if (c == '"') {
            inString = true;
            escaped = false;
            continue;
        }
        if (c == '\'') {
            inChar = true;
            escaped = false;
            continue;
        }

        if (c == needle && parenDepth == 0 && angleDepth == 0 &&
            bracketDepth == 0 && braceDepth == 0) {
            return i;
        }

        if (c == '(') ++parenDepth;
        else if (c == ')') parenDepth = std::max(0, parenDepth - 1);
        else if (c == '<') ++angleDepth;
        else if (c == '>') angleDepth = std::max(0, angleDepth - 1);
        else if (c == '[') ++bracketDepth;
        else if (c == ']') bracketDepth = std::max(0, bracketDepth - 1);
        else if (c == '{') ++braceDepth;
        else if (c == '}') braceDepth = std::max(0, braceDepth - 1);
    }
    return std::string::npos;
}

std::vector<std::string> splitTopLevel(const std::string& text, char delimiter) {
    std::vector<std::string> parts;
    std::string current;
    int parenDepth = 0;
    int angleDepth = 0;
    int bracketDepth = 0;
    int braceDepth = 0;
    bool inString = false;
    bool inChar = false;
    bool escaped = false;

    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (inString) {
            current.push_back(c);
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                inString = false;
            }
            continue;
        }
        if (inChar) {
            current.push_back(c);
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '\'') {
                inChar = false;
            }
            continue;
        }

        if (c == '"') {
            inString = true;
            escaped = false;
            current.push_back(c);
            continue;
        }
        if (c == '\'') {
            inChar = true;
            escaped = false;
            current.push_back(c);
            continue;
        }

        if (c == '(') ++parenDepth;
        else if (c == ')') parenDepth = std::max(0, parenDepth - 1);
        else if (c == '<') ++angleDepth;
        else if (c == '>') angleDepth = std::max(0, angleDepth - 1);
        else if (c == '[') ++bracketDepth;
        else if (c == ']') bracketDepth = std::max(0, bracketDepth - 1);
        else if (c == '{') ++braceDepth;
        else if (c == '}') braceDepth = std::max(0, braceDepth - 1);

        if (c == delimiter && parenDepth == 0 && angleDepth == 0 &&
            bracketDepth == 0 && braceDepth == 0) {
            parts.push_back(trimCopy(current));
            current.clear();
            continue;
        }

        current.push_back(c);
    }

    parts.push_back(trimCopy(current));
    return parts;
}

std::string escapeCStringLiteral(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 8);
    for (char c : value) {
        if (c == '\\') out += "\\\\";
        else if (c == '"') out += "\\\"";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else out.push_back(c);
    }
    return out;
}

std::string inspectorLabelFromFieldName(const std::string& name) {
    std::string out;
    out.reserve(name.size() + 8);
    char prev = '\0';
    for (size_t i = 0; i < name.size(); ++i) {
        const char c = name[i];
        const bool isUpper = std::isupper(static_cast<unsigned char>(c)) != 0;
        const bool isDigit = std::isdigit(static_cast<unsigned char>(c)) != 0;
        const bool prevLower = std::islower(static_cast<unsigned char>(prev)) != 0;
        const bool prevDigit = std::isdigit(static_cast<unsigned char>(prev)) != 0;
        if (i > 0) {
            if (c == '_' || c == '-') {
                if (!out.empty() && out.back() != ' ') out.push_back(' ');
                prev = c;
                continue;
            }
            if ((isUpper && prevLower) ||
                (isDigit && !prevDigit) ||
                (!isDigit && prevDigit)) {
                if (!out.empty() && out.back() != ' ') out.push_back(' ');
            }
        }
        out.push_back(c);
        prev = c;
    }
    if (!out.empty()) {
        out[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(out[0])));
    }
    return out;
}

std::string identifierTailFromExpression(const std::string& expression) {
    std::string expr = trimCopy(expression);
    if (expr.empty()) return {};

    size_t end = expr.size();
    while (end > 0 && std::isspace(static_cast<unsigned char>(expr[end - 1])) != 0) {
        --end;
    }
    while (end > 0 &&
           (expr[end - 1] == ')' || expr[end - 1] == ']' || expr[end - 1] == ';')) {
        --end;
    }
    size_t start = end;
    while (start > 0) {
        const unsigned char c = static_cast<unsigned char>(expr[start - 1]);
        if (std::isalnum(c) == 0 && c != '_') {
            break;
        }
        --start;
    }
    if (start >= end) return {};
    return expr.substr(start, end - start);
}

std::string inspectorLabelFromExpression(const std::string& expression) {
    std::string tail = identifierTailFromExpression(expression);
    if (tail.empty()) {
        return "Value";
    }
    return inspectorLabelFromFieldName(tail);
}

std::string extractParamName(const std::string& parameter) {
    if (parameter.empty()) return {};
    std::string preAssign = parameter;
    const size_t eqPos = findTopLevelChar(preAssign, '=');
    if (eqPos != std::string::npos) {
        preAssign = preAssign.substr(0, eqPos);
    }
    preAssign = trimCopy(preAssign);
    if (preAssign.empty()) return {};

    size_t end = preAssign.size();
    while (end > 0 && std::isspace(static_cast<unsigned char>(preAssign[end - 1])) != 0) {
        --end;
    }
    size_t start = end;
    while (start > 0) {
        const unsigned char c = static_cast<unsigned char>(preAssign[start - 1]);
        if (std::isalnum(c) == 0 && c != '_') {
            break;
        }
        --start;
    }
    if (start >= end) return {};
    return preAssign.substr(start, end - start);
}

bool isListType(const std::string& rawType) {
    const std::string normalized = toLowerCopy(removeWhitespaceCopy(rawType));
    return normalized == "list<sceneobj*>" || normalized == "list<sceneobject*>";
}

bool isLifecycleMethodName(const std::string& name) {
    static const std::unordered_set<std::string> kNames = {
        "Begin",
        "TickUpdate",
        "Update",
        "Spec",
        "TestEditor",
        "RenderEditorWindow",
        "ExitRenderEditorWindow",
        "Script_OnInspector"
    };
    return kNames.find(name) != kNames.end();
}

bool looksLikeDeltaTimeParameter(const std::string& parameter) {
    const std::string lower = toLowerCopy(removeWhitespaceCopy(parameter));
    return lower.find("floatdt") != std::string::npos ||
           lower.find("doubledt") != std::string::npos ||
           lower.find("floatdeltatime") != std::string::npos ||
           lower.find("doubledeltatime") != std::string::npos;
}

std::string mapScriptBaseTypeToCpp(const std::string& rawType) {
    const std::string normalized = toLowerCopy(removeWhitespaceCopy(rawType));
    if (normalized == "string") return "std::string";
    if (normalized == "vector2" || normalized == "vec2") return "glm::vec2";
    if (normalized == "vector3" || normalized == "vec3") return "glm::vec3";
    return rawType;
}

bool parseArrayType(const std::string& rawType,
                    std::string& outBaseType,
                    std::vector<std::string>& outDimensions) {
    std::string remaining = trimCopy(rawType);
    outDimensions.clear();

    while (!remaining.empty() && remaining.back() == ']') {
        const size_t open = remaining.rfind('[');
        if (open == std::string::npos) {
            return false;
        }
        const std::string dim = trimCopy(remaining.substr(open + 1, remaining.size() - open - 2));
        if (dim.empty()) {
            return false;
        }
        outDimensions.insert(outDimensions.begin(), dim);
        remaining = trimCopy(remaining.substr(0, open));
    }

    outBaseType = remaining;
    return true;
}

FieldKind publicFieldKindForType(const std::string& rawType) {
    std::string baseType;
    std::vector<std::string> dims;
    if (!parseArrayType(rawType, baseType, dims)) {
        return FieldKind::Custom;
    }
    if (!dims.empty()) return FieldKind::Custom;

    const std::string normalized = toLowerCopy(removeWhitespaceCopy(baseType));
    if (normalized == "float") return FieldKind::Float;
    if (normalized == "int") return FieldKind::Int;
    if (normalized == "bool") return FieldKind::Bool;
    if (normalized == "vec3" || normalized == "glm::vec3" ||
        normalized == "vector3") return FieldKind::Vec3;
    if (normalized == "string" || normalized == "std::string") return FieldKind::String;
    if (isListType(baseType)) return FieldKind::ObjectList;
    return FieldKind::Custom;
}

std::string mapScriptTypeToCpp(const std::string& rawType) {
    std::string baseType;
    std::vector<std::string> dims;
    if (!parseArrayType(rawType, baseType, dims)) {
        return mapScriptBaseTypeToCpp(rawType);
    }

    std::string cppType = mapScriptBaseTypeToCpp(baseType);
    for (auto it = dims.rbegin(); it != dims.rend(); ++it) {
        cppType = "std::array<" + cppType + ", " + *it + ">";
    }
    return cppType;
}

bool stripAndParseFieldAnnotations(std::string& declaration, FieldSpec& outField, std::string& error) {
    std::string cleaned;
    cleaned.reserve(declaration.size());

    bool inString = false;
    bool inChar = false;
    bool escaped = false;

    size_t i = 0;
    while (i < declaration.size()) {
        const char c = declaration[i];
        if (inString) {
            cleaned.push_back(c);
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                inString = false;
            }
            ++i;
            continue;
        }
        if (inChar) {
            cleaned.push_back(c);
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '\'') {
                inChar = false;
            }
            ++i;
            continue;
        }

        if (c == '"') {
            inString = true;
            escaped = false;
            cleaned.push_back(c);
            ++i;
            continue;
        }
        if (c == '\'') {
            inChar = true;
            escaped = false;
            cleaned.push_back(c);
            ++i;
            continue;
        }

        if (c != '@') {
            cleaned.push_back(c);
            ++i;
            continue;
        }

        size_t nameStart = i + 1;
        size_t nameEnd = nameStart;
        while (nameEnd < declaration.size()) {
            const unsigned char ch = static_cast<unsigned char>(declaration[nameEnd]);
            if (std::isalnum(ch) == 0 && declaration[nameEnd] != '_') break;
            ++nameEnd;
        }
        if (nameEnd == nameStart || nameEnd >= declaration.size() || declaration[nameEnd] != '(') {
            error = "Invalid field annotation syntax near: " + declaration.substr(i);
            return false;
        }

        const std::string annotationName = declaration.substr(nameStart, nameEnd - nameStart);
        const size_t argsOpen = nameEnd;
        const size_t argsClose = findMatchingParen(declaration, argsOpen);
        if (argsClose == std::string::npos) {
            error = "Field annotation has unmatched '(' near: " + declaration.substr(i);
            return false;
        }

        const std::string args = declaration.substr(argsOpen + 1, argsClose - argsOpen - 1);
        if (annotationName == "range") {
            const std::vector<std::string> parts = splitTopLevel(args, ',');
            if (parts.size() != 2) {
                error = "@range requires two arguments: @range(min, max)";
                return false;
            }
            outField.rangeMinExpr = trimCopy(parts[0]);
            outField.rangeMaxExpr = trimCopy(parts[1]);
            if (outField.rangeMinExpr->empty() || outField.rangeMaxExpr->empty()) {
                error = "@range arguments cannot be empty.";
                return false;
            }
        } else if (annotationName == "step") {
            const std::vector<std::string> parts = splitTopLevel(args, ',');
            if (parts.size() != 1) {
                error = "@step requires one argument: @step(value)";
                return false;
            }
            outField.stepExpr = trimCopy(parts[0]);
            if (outField.stepExpr->empty()) {
                error = "@step argument cannot be empty.";
                return false;
            }
        } else {
            error = "Unsupported field annotation '@" + annotationName +
                    "'. Supported annotations: @range(min, max), @step(value).";
            return false;
        }

        i = argsClose + 1;
    }

    declaration = trimCopy(cleaned);
    return true;
}

bool parseFieldDecl(const std::string& fieldDecl, FieldSpec& outField, std::string& error) {
    std::string trimmed = trimCopy(fieldDecl);
    if (trimmed.empty()) return false;
    if (trimmed.back() == ';') {
        trimmed.pop_back();
        trimmed = trimCopy(trimmed);
    }
    if (trimmed.empty()) return false;

    if (trimmed == "public:" || trimmed == "private:") {
        return false;
    }

    const std::string publicPrefix = "public ";
    const std::string privatePrefix = "private ";
    if (trimmed.rfind(publicPrefix, 0) == 0) {
        outField.visibility = FieldVisibility::Public;
        trimmed = trimCopy(trimmed.substr(publicPrefix.size()));
    } else if (trimmed.rfind(privatePrefix, 0) == 0) {
        outField.visibility = FieldVisibility::Private;
        trimmed = trimCopy(trimmed.substr(privatePrefix.size()));
    } else {
        error = "ModuCPP field is missing 'public' or 'private': " + fieldDecl;
        return false;
    }

    if (!stripAndParseFieldAnnotations(trimmed, outField, error)) {
        return false;
    }

    size_t eqPos = findTopLevelChar(trimmed, '=');
    std::string declPart = (eqPos == std::string::npos) ? trimmed : trimCopy(trimmed.substr(0, eqPos));
    outField.initializer = (eqPos == std::string::npos) ? std::string() : trimCopy(trimmed.substr(eqPos + 1));

    if (declPart.empty()) {
        error = "Invalid field declaration: " + fieldDecl;
        return false;
    }

    size_t nameEnd = declPart.size();
    while (nameEnd > 0 && std::isspace(static_cast<unsigned char>(declPart[nameEnd - 1])) != 0) {
        --nameEnd;
    }
    size_t nameStart = nameEnd;
    while (nameStart > 0) {
        const unsigned char c = static_cast<unsigned char>(declPart[nameStart - 1]);
        if (std::isalnum(c) == 0 && c != '_') {
            break;
        }
        --nameStart;
    }

    if (nameStart >= nameEnd) {
        error = "Unable to parse field name: " + fieldDecl;
        return false;
    }

    outField.name = declPart.substr(nameStart, nameEnd - nameStart);
    outField.rawType = trimCopy(declPart.substr(0, nameStart));
    if (outField.rawType.empty()) {
        error = "Unable to parse field type for: " + outField.name;
        return false;
    }
    if (!parseArrayType(outField.rawType, outField.baseType, outField.arrayDimensions)) {
        error = "Unable to parse array type for: " + outField.name;
        return false;
    }

    outField.kind = publicFieldKindForType(outField.rawType);

    return true;
}

bool lowerParameterDecl(const std::string& rawParam, std::string& outLowered,
                        std::string& outParamName, bool& outIsContext, bool& outContextIsPointer,
                        bool& outIsDelta, std::string& error) {
    std::string param = trimCopy(rawParam);
    outLowered.clear();
    outParamName.clear();
    outIsContext = false;
    outContextIsPointer = false;
    outIsDelta = false;
    if (param.empty()) {
        return true;
    }

    if (param == "MODU_obj") {
        outLowered = "ScriptContext& ctx";
        outParamName = "ctx";
        outIsContext = true;
        return true;
    }

    const std::string lowerParam = toLowerCopy(param);
    if (lowerParam.find("scriptcontext") != std::string::npos) {
        outParamName = extractParamName(param);
        if (outParamName.empty()) {
            error = "Unable to parse ScriptContext parameter name.";
            return false;
        }
        outLowered = param;
        outIsContext = true;
        outContextIsPointer = param.find('*') != std::string::npos;
        return true;
    }

    outIsDelta = looksLikeDeltaTimeParameter(param);

    std::string defaultExpr;
    const size_t eqPos = findTopLevelChar(param, '=');
    if (eqPos != std::string::npos) {
        defaultExpr = trimCopy(param.substr(eqPos + 1));
        param = trimCopy(param.substr(0, eqPos));
    }

    bool isRef = false;
    if (param.rfind("ref ", 0) == 0) {
        isRef = true;
        param = trimCopy(param.substr(4));
    }

    outParamName = extractParamName(param);
    if (outParamName.empty()) {
        outLowered = rawParam;
        return true;
    }

    const size_t namePos = param.rfind(outParamName);
    if (namePos == std::string::npos) {
        outLowered = rawParam;
        return true;
    }

    std::string typePart = trimCopy(param.substr(0, namePos));
    if (typePart.empty()) {
        outLowered = rawParam;
        return true;
    }

    if (isRef) {
        typePart = mapScriptTypeToCpp(typePart) + "&";
    } else {
        typePart = mapScriptTypeToCpp(typePart);
    }

    outLowered = typePart + " " + outParamName;
    if (!defaultExpr.empty()) {
        outLowered += " = " + defaultExpr;
    }
    return true;
}

bool parseMethodDecl(const std::string& signatureDecl, const std::string& body,
                     MethodSpec& outMethod, std::string& error) {
    std::string signature = trimCopy(signatureDecl);
    if (signature.empty()) return false;

    const std::string publicPrefix = "public ";
    const std::string privatePrefix = "private ";
    if (signature.rfind(publicPrefix, 0) == 0) {
        signature = trimCopy(signature.substr(publicPrefix.size()));
    } else if (signature.rfind(privatePrefix, 0) == 0) {
        signature = trimCopy(signature.substr(privatePrefix.size()));
    }

    const size_t openParen = findTopLevelChar(signature, '(');
    if (openParen == std::string::npos) {
        error = "Unsupported ModuCPP method declaration: " + signature;
        return false;
    }
    const size_t closeParen = findMatchingParen(signature, openParen);
    if (closeParen == std::string::npos) {
        error = "Unsupported ModuCPP method declaration (unmatched '('): " + signature;
        return false;
    }
    const std::string trailing = trimCopy(signature.substr(closeParen + 1));
    bool hasExpressionBody = false;
    std::string expressionBody;
    if (!trailing.empty()) {
        if (trailing.rfind("to ", 0) != 0) {
            error = "Unsupported ModuCPP method declaration (trailing tokens): " + signature;
            return false;
        }
        hasExpressionBody = true;
        expressionBody = trimCopy(trailing.substr(3));
        if (expressionBody.empty()) {
            error = "Expression-bodied member is missing its body: " + signature;
            return false;
        }
    }

    const std::string beforeParen = trimCopy(signature.substr(0, openParen));
    if (beforeParen.empty()) {
        error = "Unsupported ModuCPP method declaration (missing return type/name): " + signature;
        return false;
    }

    size_t nameEnd = beforeParen.size();
    while (nameEnd > 0 && std::isspace(static_cast<unsigned char>(beforeParen[nameEnd - 1])) != 0) {
        --nameEnd;
    }
    size_t nameStart = nameEnd;
    while (nameStart > 0) {
        const unsigned char c = static_cast<unsigned char>(beforeParen[nameStart - 1]);
        if (std::isalnum(c) == 0 && c != '_') {
            break;
        }
        --nameStart;
    }
    if (nameStart >= nameEnd) {
        error = "Unsupported ModuCPP method declaration (missing method name): " + signature;
        return false;
    }

    outMethod.name = beforeParen.substr(nameStart, nameEnd - nameStart);
    outMethod.returnType = mapScriptTypeToCpp(trimCopy(beforeParen.substr(0, nameStart)));
    if (outMethod.returnType.empty()) {
        error = "Unsupported ModuCPP method declaration (missing return type): " + signature;
        return false;
    }
    outMethod.originalParams = trimCopy(signature.substr(openParen + 1, closeParen - openParen - 1));
    outMethod.body = body;
    if (hasExpressionBody) {
        outMethod.body = isVoidReturnType(outMethod.returnType)
            ? (expressionBody + ";")
            : ("return " + expressionBody + ";");
    }
    {
        const std::string bodyStripped = stripCommentsPreserveLayout(outMethod.body);
        static const std::regex moduScriptPattern(R"(\bMODU_SCRIPT\s*\()");
        outMethod.hasManualPrelude = std::regex_search(bodyStripped, moduScriptPattern);
    }

    std::vector<std::string> params = splitTopLevel(outMethod.originalParams, ',');
    std::vector<std::string> transpiledParams;
    transpiledParams.reserve(params.size());

    for (const std::string& rawParam : params) {
        std::string loweredParam;
        std::string paramName;
        bool isContext = false;
        bool contextIsPointer = false;
        bool isDelta = false;
        if (!lowerParameterDecl(rawParam, loweredParam, paramName, isContext, contextIsPointer,
                                isDelta, error)) {
            error = "Unable to parse parameter in method '" + outMethod.name + "': " + error;
            return false;
        }
        if (trimCopy(loweredParam).empty()) continue;

        if (isContext) {
            if (outMethod.hasContext) {
                error = "Multiple context parameters found in method: " + outMethod.name;
                return false;
            }
            outMethod.hasContext = true;
            outMethod.contextParamName = paramName.empty() ? "ctx" : paramName;
            outMethod.contextIsPointer = contextIsPointer;
        }
        if (isDelta && !paramName.empty()) {
            outMethod.deltaTimeParamName = paramName;
            outMethod.hasDeltaTimeParam = true;
        }

        transpiledParams.push_back(loweredParam);
    }

    if (isLifecycleMethodName(outMethod.name)) {
        if (!outMethod.hasContext) {
            outMethod.hasContext = true;
            outMethod.contextParamName = "ctx";
            outMethod.contextIsPointer = false;
            outMethod.autoInjectedContext = true;
            transpiledParams.insert(transpiledParams.begin(), "ScriptContext& ctx");
        }

        if (outMethod.name != "Script_OnInspector") {
            bool hasDelta = false;
            for (const std::string& rawParam : params) {
                if (looksLikeDeltaTimeParameter(rawParam)) {
                    hasDelta = true;
                    break;
                }
            }
            if (!hasDelta && outMethod.name != "RenderEditorWindow" &&
                outMethod.name != "ExitRenderEditorWindow") {
                outMethod.autoInjectedDeltaTime = true;
                outMethod.deltaTimeParamName = "dt";
                outMethod.hasDeltaTimeParam = true;
                transpiledParams.push_back("float dt");
            }
        }
    }

    std::ostringstream joined;
    for (size_t i = 0; i < transpiledParams.size(); ++i) {
        if (i > 0) joined << ", ";
        joined << transpiledParams[i];
    }
    outMethod.transpiledParams = joined.str();
    return true;
}

std::string transformEachSyntax(const std::string& body,
                                const std::unordered_set<std::string>& listFields,
                                const std::string& supportNamespace,
                                bool hasContext,
                                std::string& error) {
    static const std::regex eachPattern(
        R"(each\s+([A-Za-z_][A-Za-z0-9_]*)\s*\.state\s*\(\s*([^\)]+?)\s*\)\s*;)");

    std::string out;
    size_t cursor = 0;
    std::smatch match;
    while (std::regex_search(body.cbegin() + static_cast<std::ptrdiff_t>(cursor), body.cend(), match, eachPattern)) {
        const size_t relPos = static_cast<size_t>(match.position());
        const size_t matchPos = cursor + relPos;
        const size_t matchLen = static_cast<size_t>(match.length());

        out += body.substr(cursor, matchPos - cursor);

        const std::string listName = match[1].str();
        const std::string enabledExpr = trimCopy(match[2].str());
        if (listFields.find(listName) == listFields.end()) {
            error = "Unknown list field in each-expression: " + listName;
            return {};
        }
        if (!hasContext) {
            error = "each-expression requires a MODU_obj/ScriptContext parameter.";
            return {};
        }

        const size_t lineStartPos = body.rfind('\n', matchPos);
        const size_t indentStart = (lineStartPos == std::string::npos) ? 0 : lineStartPos + 1;
        size_t indentEnd = indentStart;
        while (indentEnd < matchPos &&
               (body[indentEnd] == ' ' || body[indentEnd] == '\t')) {
            ++indentEnd;
        }
        const std::string indent = body.substr(indentStart, indentEnd - indentStart);

        out += indent + "for (SceneObject* _moduObj : " + supportNamespace +
               "::ResolveObjectList(ctx, " + listName + ")) {\n";
        out += indent + "    if (!_moduObj) continue;\n";
        out += indent + "    " + supportNamespace +
               "::SetResolvedObjectEnabled(ctx, _moduObj, (" + enabledExpr + "));\n";
        out += indent + "}\n";

        cursor = matchPos + matchLen;
    }

    out += body.substr(cursor);
    return out;
}

std::string sanitizeIdentifier(const std::string& input) {
    std::string out;
    out.reserve(input.size() + 8);
    for (char c : input) {
        if (std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_') {
            out.push_back(c);
        } else {
            out.push_back('_');
        }
    }
    if (out.empty() || std::isdigit(static_cast<unsigned char>(out.front())) != 0) {
        out.insert(out.begin(), '_');
    }
    return out;
}

bool isVoidReturnType(const std::string& returnType) {
    const std::string normalized = toLowerCopy(removeWhitespaceCopy(returnType));
    return normalized == "void";
}

std::string rewriteIncludeDirective(const std::string& directive, const fs::path& sourcePath) {
    static const std::regex includePattern(R"(^\s*#include\s*([<"])([^>"]+)[>"]\s*$)");
    std::smatch match;
    if (!std::regex_match(directive, match, includePattern)) {
        return trimCopy(directive);
    }

    const std::string bracket = match[1].str();
    const std::string includeTarget = trimCopy(match[2].str());
    if (includeTarget.empty()) {
        return trimCopy(directive);
    }
    if (includeTarget == "ModuCPP") {
        return "#include \"ModuCPP\"";
    }

    if (bracket == "\"") {
        std::error_code ec;
        fs::path candidate = sourcePath.parent_path() / includeTarget;
        fs::path absolute = fs::absolute(candidate, ec);
        if (!ec && fs::exists(absolute, ec) && !ec) {
            return "#include \"" + absolute.lexically_normal().generic_string() + "\"";
        }
    }

    return "#include " + bracket + includeTarget + ((bracket == "<") ? ">" : "\"");
}

std::string stripIncludeDirectives(const std::string& sourceText) {
    std::istringstream input(sourceText);
    std::ostringstream out;
    std::string line;
    while (std::getline(input, line)) {
        const std::string trimmed = trimCopy(line);
        if (trimmed.rfind("#include", 0) == 0) {
            continue;
        }
        out << line << "\n";
    }
    return out.str();
}

std::string normalizeModuPackageLines(const std::string& sourceText) {
    std::istringstream input(sourceText);
    std::ostringstream out;
    std::string line;
    static const std::regex addPattern(R"(^(\s*)add\s+([A-Za-z_][A-Za-z0-9_:]*)\s*;\s*$)");
    static const std::regex usingPattern(R"(^(\s*)using\s+([A-Za-z_][A-Za-z0-9_:]*)\s*;\s*$)");

    while (std::getline(input, line)) {
        std::smatch match;
        if (std::regex_match(line, match, addPattern)) {
            const std::string indent = match[1].str();
            const std::string packageName = match[2].str();
            out << indent << "#include \"" << packageName << "\"\n";
            continue;
        }
        if (std::regex_match(line, match, usingPattern) &&
            trimCopy(line).rfind("using namespace", 0) != 0) {
            const std::string indent = match[1].str();
            const std::string packageName = match[2].str();
            out << indent << "using namespace " << packageName << ";\n";
            continue;
        }
        out << line << "\n";
    }
    return out.str();
}

std::string convertPublicEnumsToCpp(const std::string& sourceText) {
    const std::string stripped = stripCommentsPreserveLayout(sourceText);
    static const std::regex enumPattern(R"(\bpublic\s+enum\s+([A-Za-z_][A-Za-z0-9_]*)\b)");

    std::string out;
    size_t cursor = 0;
    auto begin = std::sregex_iterator(stripped.begin(), stripped.end(), enumPattern);
    auto end = std::sregex_iterator();
    for (auto it = begin; it != end; ++it) {
        const std::smatch& match = *it;
        const size_t matchPos = static_cast<size_t>(match.position());
        const size_t matchLen = static_cast<size_t>(match.length());
        const std::string enumName = match[1].str();
        const size_t openBrace = stripped.find('{', matchPos + matchLen);
        if (openBrace == std::string::npos) {
            continue;
        }
        const size_t closeBrace = findMatchingBrace(stripped, openBrace);
        if (closeBrace == std::string::npos) {
            continue;
        }

        out += sourceText.substr(cursor, matchPos - cursor);
        out += "enum class " + enumName;
        out += sourceText.substr(matchPos + matchLen, openBrace - (matchPos + matchLen) + 1);
        out += sourceText.substr(openBrace + 1, closeBrace - openBrace - 1);
        out += "}";

        size_t next = closeBrace + 1;
        while (next < sourceText.size() &&
               std::isspace(static_cast<unsigned char>(sourceText[next])) != 0) {
            out.push_back(sourceText[next]);
            ++next;
        }
        if (next >= sourceText.size() || sourceText[next] != ';') {
            out += ";";
        } else {
            out.push_back(';');
            ++next;
        }
        cursor = next;
    }
    out += sourceText.substr(cursor);
    return out;
}

std::string normalizeModuSource(const std::string& sourceText) {
    return convertPublicEnumsToCpp(normalizeModuPackageLines(sourceText));
}

void replaceRegexAll(std::string& text, const std::regex& pattern, const std::string& replacement) {
    text = std::regex_replace(text, pattern, replacement);
}

std::string rewriteSurfaceSyntax(const std::string& sourceText) {
    std::string out = sourceText;

    replaceRegexAll(out, std::regex(R"(\bMath\s*\.)"), "Math::");
    replaceRegexAll(out, std::regex(R"(\b([A-Z][A-Za-z0-9_]*)\s*\.\s*([A-Z][A-Za-z0-9_]*)\b)"),
                    "$1::$2");
    // Fully qualify 'time.' to avoid ambiguity with the C standard library ::time() function
    // when 'using namespace ::ModuCPP' is in scope.
    replaceRegexAll(out, std::regex(R"(\btime\s*\.)"), "::ModuCPP::time.");
    replaceRegexAll(out, std::regex(R"(\bref\s+([A-Za-z_][A-Za-z0-9_]*)\b)"), "$1");
    replaceRegexAll(out, std::regex(R"((^|[^:A-Za-z0-9_])Vector2\b)"), "$1glm::vec2");
    replaceRegexAll(out, std::regex(R"((^|[^:A-Za-z0-9_])Vector3\b)"), "$1glm::vec3");
    replaceRegexAll(out, std::regex(R"((^|[^:A-Za-z0-9_])string\b)"), "$1std::string");
    replaceRegexAll(out,
                    std::regex(R"(\b([A-Za-z_][A-Za-z0-9_\[\]\(\)]*)\s*\.\s*Length\s*\(\s*\))"),
                    "glm::length($1)");
    replaceRegexAll(out,
                    std::regex(R"(\b([A-Za-z_][A-Za-z0-9_\[\]\(\)]*)\s*\.\s*Dot\s*\(\s*([^\)]+?)\s*\))"),
                    "glm::dot($1, $2)");
    replaceRegexAll(out,
                    std::regex(R"(\b([A-Za-z_][A-Za-z0-9_\[\]\(\)]*)\s*\.\s*IsEmpty\s*\(\s*\))"),
                    "$1.empty()");

    static const std::regex arrayDeclPattern(
        R"((^|[;{}\n]\s*)([A-Za-z_][A-Za-z0-9_:<>]*)\s*((?:\[[^\]]+\])+)\s+([A-Za-z_][A-Za-z0-9_]*))");
    std::string rebuilt;
    size_t cursor = 0;
    auto begin = std::sregex_iterator(out.begin(), out.end(), arrayDeclPattern);
    auto end = std::sregex_iterator();
    for (auto it = begin; it != end; ++it) {
        const std::smatch& match = *it;
        const size_t pos = static_cast<size_t>(match.position());
        const size_t len = static_cast<size_t>(match.length());
        rebuilt += out.substr(cursor, pos - cursor);

        const std::string prefix = match[1].str();
        const std::string baseType = match[2].str();
        const std::string dimsText = match[3].str();
        const std::string name = match[4].str();

        std::vector<std::string> dims;
        size_t dimCursor = 0;
        while (dimCursor < dimsText.size()) {
            const size_t open = dimsText.find('[', dimCursor);
            const size_t close = dimsText.find(']', open == std::string::npos ? dimCursor : open);
            if (open == std::string::npos || close == std::string::npos) break;
            dims.push_back(trimCopy(dimsText.substr(open + 1, close - open - 1)));
            dimCursor = close + 1;
        }

        std::string cppType = mapScriptBaseTypeToCpp(baseType);
        for (auto dimIt = dims.rbegin(); dimIt != dims.rend(); ++dimIt) {
            cppType = "std::array<" + cppType + ", " + *dimIt + ">";
        }

        rebuilt += prefix + cppType + " " + name;
        cursor = pos + len;
    }
    rebuilt += out.substr(cursor);
    return rebuilt;
}

bool parseClass(const std::string& sourceText, ClassSpec& outClass, std::string& error) {
    std::istringstream input(sourceText);
    std::string line;
    while (std::getline(input, line)) {
        std::string trimmed = trimCopy(line);
        if (trimmed.rfind("#include", 0) == 0) {
            outClass.includeDirectives.push_back(trimmed);
        }
    }

    const std::string stripped = stripCommentsPreserveLayout(sourceText);
    std::regex classPattern(R"(\bpublic\s+class\s+([A-Za-z_][A-Za-z0-9_]*)\s*:\s*ModuBehaviour\b)");
    std::smatch classMatch;
    if (!std::regex_search(stripped, classMatch, classPattern)) {
        error = "No ModuCPP class found. Expected: public class <Name> : ModuBehaviour";
        return false;
    }

    outClass.name = classMatch[1].str();
    const size_t classDeclPos = static_cast<size_t>(classMatch.position());
    const size_t classDeclEnd = classDeclPos + static_cast<size_t>(classMatch.length());
    const size_t classOpenBrace = stripped.find('{', classDeclEnd);
    if (classOpenBrace == std::string::npos) {
        error = "ModuCPP class body is missing '{'.";
        return false;
    }
    const size_t classCloseBrace = findMatchingBrace(stripped, classOpenBrace);
    if (classCloseBrace == std::string::npos) {
        error = "ModuCPP class body has unmatched '{'.";
        return false;
    }

    size_t classEnd = classCloseBrace + 1;
    while (classEnd < stripped.size() &&
           std::isspace(static_cast<unsigned char>(stripped[classEnd])) != 0) {
        ++classEnd;
    }
    if (classEnd < stripped.size() && stripped[classEnd] == ';') {
        ++classEnd;
    }
    while (classEnd < stripped.size() &&
           std::isspace(static_cast<unsigned char>(stripped[classEnd])) != 0) {
        ++classEnd;
    }

    if (classDeclPos <= sourceText.size()) {
        outClass.passthroughCode += sourceText.substr(0, classDeclPos);
    }
    if (classEnd <= sourceText.size()) {
        outClass.passthroughCode += sourceText.substr(classEnd);
    }

    const size_t bodyStart = classOpenBrace + 1;
    const size_t bodyLen = classCloseBrace - bodyStart;
    const std::string bodyRaw = sourceText.substr(bodyStart, bodyLen);
    const std::string bodyClean = stripped.substr(bodyStart, bodyLen);

    std::set<std::string> fieldNames;
    size_t i = 0;
    while (i < bodyClean.size()) {
        while (i < bodyClean.size() && std::isspace(static_cast<unsigned char>(bodyClean[i])) != 0) {
            ++i;
        }
        if (i >= bodyClean.size()) break;

        size_t nextSemicolon = std::string::npos;
        size_t nextOpenBrace = std::string::npos;
        int parenDepth = 0;
        int angleDepth = 0;
        int bracketDepth = 0;
        int braceDepth = 0;
        bool sawParenInSignature = false;
        bool inString = false;
        bool inChar = false;
        bool escaped = false;

        for (size_t j = i; j < bodyClean.size(); ++j) {
            const char c = bodyClean[j];
            if (inString) {
                if (escaped) {
                    escaped = false;
                } else if (c == '\\') {
                    escaped = true;
                } else if (c == '"') {
                    inString = false;
                }
                continue;
            }
            if (inChar) {
                if (escaped) {
                    escaped = false;
                } else if (c == '\\') {
                    escaped = true;
                } else if (c == '\'') {
                    inChar = false;
                }
                continue;
            }

            if (c == '"') {
                inString = true;
                escaped = false;
                continue;
            }
            if (c == '\'') {
                inChar = true;
                escaped = false;
                continue;
            }

            if (c == '(') {
                if (parenDepth == 0 && angleDepth == 0 && bracketDepth == 0 && braceDepth == 0) {
                    sawParenInSignature = true;
                }
                ++parenDepth;
            }
            else if (c == ')') {
                parenDepth = std::max(0, parenDepth - 1);
                // Once the parameter list closes, any subsequent '<'/'>' are comparison
                // operators (not template brackets), so reset angle depth to avoid
                // mis-tracking expressions like 'clip < ctx.GetSpriteClipCount()'.
                if (sawParenInSignature && parenDepth == 0 && braceDepth == 0) {
                    angleDepth = 0;
                }
            }
            else if (c == '<') ++angleDepth;
            else if (c == '>') angleDepth = std::max(0, angleDepth - 1);
            else if (c == '[') ++bracketDepth;
            else if (c == ']') bracketDepth = std::max(0, bracketDepth - 1);
            else if (c == '{') {
                if (parenDepth == 0 && angleDepth == 0 && bracketDepth == 0 && braceDepth == 0) {
                    const std::string signatureProbe = trimCopy(bodyRaw.substr(i, j - i));
                    if (signatureProbe == "inspector" || sawParenInSignature) {
                        nextOpenBrace = j;
                        break;
                    }
                }
                ++braceDepth;
                continue;
            } else if (c == '}') {
                braceDepth = std::max(0, braceDepth - 1);
            }

            if (parenDepth == 0 && angleDepth == 0 && bracketDepth == 0 && braceDepth == 0) {
                if (c == ';') {
                    nextSemicolon = j;
                    break;
                }
            }
        }

        if (nextSemicolon == std::string::npos && nextOpenBrace == std::string::npos) {
            break;
        }

        if (nextOpenBrace != std::string::npos &&
            (nextSemicolon == std::string::npos || nextOpenBrace < nextSemicolon)) {
            const std::string signatureDecl = bodyRaw.substr(i, nextOpenBrace - i);
            if (std::optional<size_t> missingSemicolonOffset =
                    findLikelyMissingSemicolonBeforeMethod(signatureDecl)) {
                error = formatLocatedParseError(
                    "Missing ';'",
                    sourceText,
                    bodyStart + i + *missingSemicolonOffset);
                return false;
            }

            const size_t methodCloseBrace = findMatchingBrace(bodyClean, nextOpenBrace);
            if (methodCloseBrace == std::string::npos) {
                error = "Method body has unmatched '{' in class: " + outClass.name;
                return false;
            }

            std::string methodBody = bodyRaw.substr(nextOpenBrace + 1, methodCloseBrace - nextOpenBrace - 1);
            const std::string signatureTrimmed = trimCopy(signatureDecl);
            if (signatureTrimmed == "inspector") {
                if (!trimCopy(outClass.inspectorBlock).empty()) {
                    error = "Only one 'inspector { ... }' block is allowed per ModuCPP class.";
                    return false;
                }
                outClass.inspectorBlock = methodBody;
                i = methodCloseBrace + 1;
                if (i < bodyClean.size() && bodyClean[i] == ';') {
                    ++i;
                }
                continue;
            }

            MethodSpec method;
            if (!parseMethodDecl(signatureDecl, methodBody, method, error)) {
                return false;
            }
            outClass.methods.push_back(std::move(method));

            i = methodCloseBrace + 1;
            if (i < bodyClean.size() && bodyClean[i] == ';') {
                ++i;
            }
            continue;
        }

        std::string statementDecl = bodyRaw.substr(i, nextSemicolon - i + 1);
        const std::string statementTrimmed = trimCopy(statementDecl);
        if (findTopLevelChar(statementTrimmed, '(') != std::string::npos) {
            MethodSpec method;
            std::string methodSignature = statementTrimmed;
            if (!methodSignature.empty() && methodSignature.back() == ';') {
                methodSignature.pop_back();
                methodSignature = trimCopy(methodSignature);
            }
            std::string methodError;
            if (parseMethodDecl(methodSignature, std::string(), method, methodError)) {
                outClass.methods.push_back(std::move(method));
                i = nextSemicolon + 1;
                continue;
            }
        }

        if (std::optional<size_t> missingSemicolonOffset = findLikelyMissingSemicolonInFieldDecl(statementDecl)) {
            error = formatLocatedParseError(
                "Missing ';'",
                sourceText,
                bodyStart + i + *missingSemicolonOffset);
            return false;
        }
        FieldSpec field;
        if (parseFieldDecl(statementDecl, field, error)) {
            if (!fieldNames.insert(field.name).second) {
                error = "Duplicate field name in ModuCPP class: " + field.name;
                return false;
            }
            outClass.fields.push_back(std::move(field));
        } else if (!error.empty()) {
            return false;
        }
        i = nextSemicolon + 1;
    }

    if (outClass.methods.empty() && trimCopy(outClass.inspectorBlock).empty()) {
        error = "ModuCPP class has no methods to transpile.";
        return false;
    }
    return true;
}

std::string generateTranspiledSource(const fs::path& sourcePath, const ClassSpec& spec,
                                     std::string& error) {
    const std::string supportNs = "ModuCPPTranspiled_" + sanitizeIdentifier(spec.name);
    const std::string configType = spec.name + "Config";
    const std::string stateType = spec.name + "State";
    const std::string strippedInspector = stripCommentsPreserveLayout(spec.inspectorBlock);

    auto fieldReferencedByInspector = [&](const std::string& fieldName) {
        if (fieldName.empty() || strippedInspector.empty()) return false;
        const std::regex pattern("\\b" + fieldName + "\\b");
        return std::regex_search(strippedInspector, pattern);
    };

    auto fieldPersists = [&](const FieldSpec& field) {
        return field.visibility == FieldVisibility::Public || fieldReferencedByInspector(field.name);
    };

    std::unordered_set<std::string> listFields;
    std::vector<std::string> customPublicFieldNames;
    bool hasAutoInspectorFields = false;
    for (const FieldSpec& field : spec.fields) {
        if (fieldPersists(field) && field.kind == FieldKind::ObjectList) {
            listFields.insert(field.name);
        }
        if (field.visibility == FieldVisibility::Public) {
            hasAutoInspectorFields = true;
        }
        if (field.visibility == FieldVisibility::Public && field.kind == FieldKind::Custom) {
            customPublicFieldNames.push_back(field.name + " : " + field.rawType);
        }
    }
    bool hasInspectorMethod = false;
    for (const MethodSpec& method : spec.methods) {
        if (method.name == "Script_OnInspector") {
            hasInspectorMethod = true;
            break;
        }
    }
    const bool hasInspectorBlock = !trimCopy(spec.inspectorBlock).empty();
    if (hasInspectorMethod && hasInspectorBlock) {
        error = "ModuCPP class '" + spec.name +
                "' cannot declare both Script_OnInspector(...) and inspector { ... }.";
        return {};
    }
    if (!customPublicFieldNames.empty() && !hasInspectorMethod && !hasInspectorBlock) {
        std::ostringstream fields;
        for (size_t i = 0; i < customPublicFieldNames.size(); ++i) {
            if (i > 0) fields << ", ";
            fields << customPublicFieldNames[i];
        }
        error = "ModuCPP class '" + spec.name +
                "' has custom public field type(s) requiring custom inspector authoring. "
                "Add inspector { ... } or Script_OnInspector(...). Fields: " + fields.str();
        return {};
    }

    std::ostringstream out;
    out << "// Generated from \"" << sourcePath.lexically_normal().generic_string() << "\" by ModuCPP transpiler.\n";

    std::unordered_set<std::string> emittedIncludes;
    auto emitInclude = [&](const std::string& includeDirective) {
        const std::string normalized = trimCopy(includeDirective);
        if (normalized.empty()) return;
        if (!emittedIncludes.insert(normalized).second) return;
        out << normalized << "\n";
    };

    emitInclude("#include \"ModuCPP\"");
    for (const std::string& includeDirective : spec.includeDirectives) {
        emitInclude(rewriteIncludeDirective(includeDirective, sourcePath));
    }
    emitInclude("#include <algorithm>");
    emitInclude("#include <array>");
    emitInclude("#include <cctype>");
    emitInclude("#include <cstddef>");
    emitInclude("#include <cstdlib>");
    emitInclude("#include <cstdio>");
    emitInclude("#include <string>");
    emitInclude("#include <vector>");
    out << "\n";
    out << "using namespace ::ModuCPP;\n\n";

    const std::string passthrough = stripIncludeDirectives(spec.passthroughCode);
    if (!trimCopy(passthrough).empty()) {
        out << passthrough << "\n";
    }

    out << "namespace " << supportNs << " {\n\n";
    out << "using namespace ::ModuCPP;\n\n";
    out << "struct " << configType << " {\n";
    for (const FieldSpec& field : spec.fields) {
        if (!fieldPersists(field)) continue;
        if (field.kind == FieldKind::ObjectList) {
            out << "    std::string " << field.name << "Raw";
            if (!field.initializer.empty()) {
                out << " = " << rewriteSurfaceSyntax(field.initializer);
            }
            out << ";\n";
        } else {
            out << "    " << mapScriptTypeToCpp(field.rawType) << " " << field.name;
            if (!field.initializer.empty()) {
                out << " = " << rewriteSurfaceSyntax(field.initializer);
            }
            out << ";\n";
        }
    }
    out << "};\n\n";

    out << "struct " << stateType << " {\n";
    for (const FieldSpec& field : spec.fields) {
        if (fieldPersists(field)) continue;
        out << "    " << mapScriptTypeToCpp(field.rawType) << " " << field.name;
        if (!field.initializer.empty()) {
            out << " = " << rewriteSurfaceSyntax(field.initializer);
        }
        out << ";\n";
    }
    out << "};\n\n";

    out << "inline std::string Trim(const std::string& value) {\n";
    out << "    size_t start = 0;\n";
    out << "    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start])) != 0) {\n";
    out << "        ++start;\n";
    out << "    }\n";
    out << "    size_t end = value.size();\n";
    out << "    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {\n";
    out << "        --end;\n";
    out << "    }\n";
    out << "    return value.substr(start, end - start);\n";
    out << "}\n\n";

    out << "inline bool IsAllDigits(const std::string& value) {\n";
    out << "    if (value.empty()) return false;\n";
    out << "    size_t start = (value[0] == '-' || value[0] == '+') ? 1u : 0u;\n";
    out << "    if (start >= value.size()) return false;\n";
    out << "    for (size_t i = start; i < value.size(); ++i) {\n";
    out << "        if (!std::isdigit(static_cast<unsigned char>(value[i]))) return false;\n";
    out << "    }\n";
    out << "    return true;\n";
    out << "}\n\n";

    out << "inline std::vector<std::string> SplitEscaped(const std::string& value, char delimiter) {\n";
    out << "    std::vector<std::string> fields;\n";
    out << "    std::string current;\n";
    out << "    bool escaped = false;\n";
    out << "    for (char c : value) {\n";
    out << "        if (escaped) {\n";
    out << "            if (c == 'n') current.push_back('\\n');\n";
    out << "            else if (c == 'r') current.push_back('\\r');\n";
    out << "            else current.push_back(c);\n";
    out << "            escaped = false;\n";
    out << "            continue;\n";
    out << "        }\n";
    out << "        if (c == '\\\\') {\n";
    out << "            escaped = true;\n";
    out << "            continue;\n";
    out << "        }\n";
    out << "        if (c == delimiter) {\n";
    out << "            fields.push_back(current);\n";
    out << "            current.clear();\n";
    out << "            continue;\n";
    out << "        }\n";
    out << "        current.push_back(c);\n";
    out << "    }\n";
    out << "    if (escaped) current.push_back('\\\\');\n";
    out << "    fields.push_back(current);\n";
    out << "    return fields;\n";
    out << "}\n\n";

    out << "inline std::string EscapeField(const std::string& value, char delimiter) {\n";
    out << "    std::string outValue;\n";
    out << "    outValue.reserve(value.size() + 8);\n";
    out << "    for (char c : value) {\n";
    out << "        if (c == '\\\\' || c == delimiter || c == '\\n' || c == '\\r') {\n";
    out << "            outValue.push_back('\\\\');\n";
    out << "            if (c == '\\n') outValue.push_back('n');\n";
    out << "            else if (c == '\\r') outValue.push_back('r');\n";
    out << "            else outValue.push_back(c);\n";
    out << "        } else {\n";
    out << "            outValue.push_back(c);\n";
    out << "        }\n";
    out << "    }\n";
    out << "    return outValue;\n";
    out << "}\n\n";

    out << "inline std::string JoinEscaped(const std::vector<std::string>& values, char delimiter) {\n";
    out << "    std::string joined;\n";
    out << "    for (size_t i = 0; i < values.size(); ++i) {\n";
    out << "        joined += EscapeField(values[i], delimiter);\n";
    out << "        if (i + 1 < values.size()) joined.push_back(delimiter);\n";
    out << "    }\n";
    out << "    return joined;\n";
    out << "}\n\n";

    out << "inline std::vector<std::string> DeserializeObjectRefs(const std::string& encoded) {\n";
    out << "    std::vector<std::string> refs;\n";
    out << "    if (encoded.empty()) return refs;\n";
    out << "    const std::vector<std::string> parsed = SplitEscaped(encoded, ';');\n";
    out << "    refs.reserve(parsed.size());\n";
    out << "    for (const std::string& item : parsed) {\n";
    out << "        std::string trimmed = Trim(item);\n";
    out << "        if (!trimmed.empty()) refs.push_back(trimmed);\n";
    out << "    }\n";
    out << "    return refs;\n";
    out << "}\n\n";

    out << "inline std::string SerializeObjectRefs(const std::vector<std::string>& refs) {\n";
    out << "    std::vector<std::string> cleaned;\n";
    out << "    cleaned.reserve(refs.size());\n";
    out << "    for (const std::string& ref : refs) {\n";
    out << "        std::string trimmed = Trim(ref);\n";
    out << "        if (!trimmed.empty()) cleaned.push_back(trimmed);\n";
    out << "    }\n";
    out << "    return JoinEscaped(cleaned, ';');\n";
    out << "}\n\n";

    out << "inline SceneObject* ResolveSceneObjectRef(ScriptContext& ctx, const std::string& objectRef) {\n";
    out << "    std::string trimmed = Trim(objectRef);\n";
    out << "    if (trimmed.empty()) return nullptr;\n";
    out << "    if (SceneObject* resolved = ctx.ResolveObjectRef(trimmed)) {\n";
    out << "        return resolved;\n";
    out << "    }\n";
    out << "    if (IsAllDigits(trimmed)) {\n";
    out << "        return ctx.FindObjectById(std::atoi(trimmed.c_str()));\n";
    out << "    }\n";
    out << "    return ctx.FindObjectByName(trimmed);\n";
    out << "}\n\n";

    out << "inline std::vector<SceneObject*> ResolveObjectList(ScriptContext& ctx, const std::string& encodedRefs) {\n";
    out << "    std::vector<SceneObject*> resolved;\n";
    out << "    const std::vector<std::string> refs = DeserializeObjectRefs(encodedRefs);\n";
    out << "    resolved.reserve(refs.size());\n";
    out << "    for (const std::string& ref : refs) {\n";
    out << "        resolved.push_back(ResolveSceneObjectRef(ctx, ref));\n";
    out << "    }\n";
    out << "    return resolved;\n";
    out << "}\n\n";

    out << "inline void SetResolvedObjectEnabled(ScriptContext& ctx, SceneObject* obj, bool enabled) {\n";
    out << "    if (!obj) return;\n";
    out << "    bool changed = false;\n";
    out << "    if (obj->enabled != enabled) {\n";
    out << "        obj->enabled = enabled;\n";
    out << "        changed = true;\n";
    out << "    }\n";
    out << "    if (obj->hasCollider && obj->collider.enabled != enabled) {\n";
    out << "        obj->collider.enabled = enabled;\n";
    out << "        changed = true;\n";
    out << "    }\n";
    out << "    if (obj->hasCollider2D && obj->collider2D.enabled != enabled) {\n";
    out << "        obj->collider2D.enabled = enabled;\n";
    out << "        changed = true;\n";
    out << "    }\n";
    out << "    if (changed) {\n";
    out << "        ctx.MarkDirty();\n";
    out << "    }\n";
    out << "}\n\n";

    out << "inline bool DrawObjectRefListEditor(ScriptContext& ctx, const char* label,\n";
    out << "                                   std::vector<std::string>& refs) {\n";
    out << "    bool changed = false;\n";
    out << "    if (!ImGui::TreeNode(label)) {\n";
    out << "        return false;\n";
    out << "    }\n";
    out << "    for (size_t i = 0; i < refs.size(); ++i) {\n";
    out << "        ImGui::PushID(static_cast<int>(i));\n";
    out << "        std::vector<char> buffer(256, '\\0');\n";
    out << "        std::snprintf(buffer.data(), buffer.size(), \"%s\", refs[i].c_str());\n";
    out << "        ImGui::SetNextItemWidth(-80.0f);\n";
    out << "        if (ImGui::InputText(\"##ref\", buffer.data(), buffer.size())) {\n";
    out << "            refs[i] = buffer.data();\n";
    out << "            changed = true;\n";
    out << "        }\n";
    out << "        ImGui::SameLine();\n";
    out << "        if (ImGui::SmallButton(\"X\")) {\n";
    out << "            refs.erase(refs.begin() + static_cast<std::ptrdiff_t>(i));\n";
    out << "            changed = true;\n";
    out << "            ImGui::PopID();\n";
    out << "            --i;\n";
    out << "            continue;\n";
    out << "        }\n";
    out << "        if (SceneObject* resolved = ResolveSceneObjectRef(ctx, refs[i])) {\n";
    out << "            ImGui::TextDisabled(\"%s (id=%d)\", resolved->name.c_str(), resolved->id);\n";
    out << "        }\n";
    out << "        if (ImGui::BeginDragDropTarget()) {\n";
    out << "            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(\"SCENE_OBJECT\")) {\n";
    out << "                if (payload->Data && payload->DataSize == static_cast<int>(sizeof(int))) {\n";
    out << "                    int droppedId = *static_cast<const int*>(payload->Data);\n";
    out << "                    refs[i] = std::string(\"Object.ID-\") + std::to_string(droppedId);\n";
    out << "                    changed = true;\n";
    out << "                }\n";
    out << "            }\n";
    out << "            ImGui::EndDragDropTarget();\n";
    out << "        }\n";
    out << "        ImGui::PopID();\n";
    out << "    }\n";
    out << "    if (ImGui::Button(\"Add Reference\")) {\n";
    out << "        refs.emplace_back();\n";
    out << "        changed = true;\n";
    out << "    }\n";
    out << "    ImGui::SameLine();\n";
    out << "    if (ImGui::Button(\"Add Selected\")) {\n";
    out << "        int selectedId = ctx.GetSelectedObjectId();\n";
    out << "        if (selectedId >= 0) {\n";
    out << "            refs.push_back(std::string(\"Object.ID-\") + std::to_string(selectedId));\n";
    out << "            changed = true;\n";
    out << "        }\n";
    out << "    }\n";
    out << "    if (ImGui::BeginDragDropTarget()) {\n";
    out << "        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(\"SCENE_OBJECT\")) {\n";
    out << "            if (payload->Data && payload->DataSize == static_cast<int>(sizeof(int))) {\n";
    out << "                int droppedId = *static_cast<const int*>(payload->Data);\n";
    out << "                refs.push_back(std::string(\"Object.ID-\") + std::to_string(droppedId));\n";
    out << "                changed = true;\n";
    out << "            }\n";
    out << "        }\n";
    out << "        ImGui::EndDragDropTarget();\n";
    out << "    }\n";
    out << "    ImGui::TreePop();\n";
    out << "    return changed;\n";
    out << "}\n\n";

    out << "inline void BindConfig(ScriptContext& ctx, " << configType << "& config) {\n";
    bool hasPersistedFields = false;
    for (const FieldSpec& field : spec.fields) {
        if (!fieldPersists(field)) continue;
        hasPersistedFields = true;
        if (field.kind == FieldKind::Custom) {
            if (field.arrayDimensions.size() == 1) {
                out << "    ::ModuCPP::BindArray(ctx, \"" << field.name << "\", config." << field.name << ");\n";
            } else if (field.arrayDimensions.size() == 2) {
                out << "    ::ModuCPP::BindArray2D(ctx, \"" << field.name << "\", config." << field.name << ");\n";
            }
            continue;
        }
        if (field.kind == FieldKind::ObjectList) {
            out << "    ::ModuCPP::BindSetting(ctx, \"" << field.name << "\", config." << field.name << "Raw);\n";
        } else {
            out << "    ::ModuCPP::BindSetting(ctx, \"" << field.name << "\", config." << field.name << ");\n";
        }
    }
    if (!hasPersistedFields) {
        out << "    (void)ctx;\n";
        out << "    (void)config;\n";
    }
    out << "}\n\n";
    out << "} // namespace " << supportNs << "\n\n";

    auto findPersistedFieldByName = [&](const std::string& fieldName) -> const FieldSpec* {
        for (const FieldSpec& field : spec.fields) {
            if (fieldPersists(field) && field.name == fieldName) {
                return &field;
            }
        }
        return nullptr;
    };

    auto emitObjectListEditorCode = [&](const std::string& fieldExpr, const std::string& uiLabel,
                                        const std::string& tempRefsName, const std::string& indent) {
        out << indent << "{\n";
        out << indent << "    std::vector<std::string> " << tempRefsName << " = " << supportNs
            << "::DeserializeObjectRefs(" << fieldExpr << ");\n";
        out << indent << "    if (" << supportNs << "::DrawObjectRefListEditor(ctx, \""
            << escapeCStringLiteral(uiLabel) << "\", " << tempRefsName << ")) {\n";
        out << indent << "        " << fieldExpr << " = " << supportNs
            << "::SerializeObjectRefs(" << tempRefsName << ");\n";
        out << indent << "        changed = true;\n";
        out << indent << "    }\n";
        out << indent << "}\n";
    };

    for (const MethodSpec& method : spec.methods) {
        out << method.returnType << " " << method.name << "(" << method.transpiledParams << ");\n";
    }
    if (!spec.methods.empty()) {
        out << "\n";
    }

    if (hasInspectorBlock) {
        out << "extern \"C\" MODULARITY_SCRIPT_EXPORT void Script_OnInspector(ScriptContext& ctx) {\n";
        out << "    MODU_SCRIPT(ctx);\n";
        if (!spec.fields.empty()) {
            out << "    auto& config = ::ModuCPP::Config<" << supportNs << "::" << configType << ">();\n";
            out << "    " << supportNs << "::BindConfig(ctx, config);\n";
            out << "    auto& state = ::ModuCPP::State<" << supportNs << "::" << stateType << ">();\n";
            for (const FieldSpec& field : spec.fields) {
                if (!fieldPersists(field)) continue;
                if (field.kind == FieldKind::ObjectList) {
                    out << "    auto& " << field.name << " = config." << field.name << "Raw;\n";
                } else {
                    out << "    auto& " << field.name << " = config." << field.name << ";\n";
                }
            }
            for (const FieldSpec& field : spec.fields) {
                if (fieldPersists(field)) continue;
                out << "    auto& " << field.name << " = state." << field.name << ";\n";
            }
        }
        out << "    bool changed = false;\n";

        int inspectorTempCounter = 0;
        bool hasExplicitInspectorSave = false;
        auto nextInspectorTemp = [&](const std::string& prefix) {
            ++inspectorTempCounter;
            return "_modu" + prefix + std::to_string(inspectorTempCounter);
        };

        std::function<bool(const std::string&, const std::vector<std::string>&, const std::string&)> emitInspectorCall;
        emitInspectorCall = [&](const std::string& callName, const std::vector<std::string>& args,
                                const std::string& indent) -> bool {
            auto requireArgs = [&](size_t count, const char* usage) -> bool {
                if (args.size() != count) {
                    error = std::string("inspector ") + usage;
                    return false;
                }
                return true;
            };

            auto emitAutoLabel = [&](const std::string& expr) {
                return escapeCStringLiteral(inspectorLabelFromExpression(expr));
            };
            auto parseLabelValue = [&](const std::vector<std::string>& widgetArgs,
                                       const std::string& usage,
                                       std::string& outLabelLiteral,
                                       std::string& outExpr) -> bool {
                if (widgetArgs.size() == 1) {
                    outExpr = trimCopy(widgetArgs[0]);
                    outLabelLiteral = "\"" + emitAutoLabel(outExpr) + "\"";
                    return true;
                }
                if (widgetArgs.size() == 2) {
                    outLabelLiteral = trimCopy(widgetArgs[0]);
                    outExpr = trimCopy(widgetArgs[1]);
                    return true;
                }
                error = "inspector " + usage;
                return false;
            };

            if (callName == "Config") {
                if (!requireArgs(2, "Config(Type, varName) requires exactly two arguments.")) return false;
                const std::string typeExpr = trimCopy(args[0]);
                const std::string varExpr = trimCopy(args[1]);
                if (typeExpr.empty() || varExpr.empty()) {
                    error = "inspector Config(Type, varName) requires non-empty type and variable.";
                    return false;
                }
                out << indent << typeExpr << " " << varExpr << " = loadConfig(ctx);\n";
                return true;
            }
            if (callName == "AutoSave" || callName == "Save") {
                if (!requireArgs(1, "AutoSave(configVar) requires exactly one argument.")) return false;
                const std::string expr = trimCopy(args[0]);
                if (expr.empty()) {
                    error = "inspector AutoSave(configVar) requires non-empty argument.";
                    return false;
                }
                out << indent << "if (changed) {\n";
                out << indent << "    saveConfig(ctx, " << expr << ");\n";
                out << indent << "}\n";
                hasExplicitInspectorSave = true;
                return true;
            }

            if (callName == "Header") {
                if (!requireArgs(1, "Header(title) requires exactly one argument.")) return false;
                out << indent << "ImGui::TextUnformatted(" << trimCopy(args[0]) << ");\n";
                out << indent << "ImGui::Separator();\n";
                return true;
            }
            if (callName == "Run") {
                if (!requireArgs(1, "Run(expression) requires exactly one argument.")) return false;
                out << indent << trimCopy(args[0]) << ";\n";
                return true;
            }
            if (callName == "Separator") {
                if (!args.empty()) {
                    error = "inspector Separator() does not take arguments.";
                    return false;
                }
                out << indent << "ImGui::Separator();\n";
                return true;
            }
            if (callName == "Toggle") {
                std::string labelExpr;
                std::string expr;
                if (!parseLabelValue(args, "Toggle(value) or Toggle(label, value) expected.",
                                     labelExpr, expr)) return false;
                out << indent << "changed |= ImGui::Checkbox(" << labelExpr
                    << ", &" << expr << ");\n";
                return true;
            }
            if (callName == "Slider") {
                std::string labelExpr;
                std::string expr;
                std::string minExpr;
                std::string maxExpr;
                if (args.size() == 3) {
                    expr = trimCopy(args[0]);
                    minExpr = trimCopy(args[1]);
                    maxExpr = trimCopy(args[2]);
                    labelExpr = "\"" + emitAutoLabel(expr) + "\"";
                } else if (args.size() == 4) {
                    labelExpr = trimCopy(args[0]);
                    expr = trimCopy(args[1]);
                    minExpr = trimCopy(args[2]);
                    maxExpr = trimCopy(args[3]);
                } else {
                    error = "inspector Slider(value,min,max) or Slider(label,value,min,max) expected.";
                    return false;
                }

                const FieldSpec* field = findPersistedFieldByName(expr);
                if (field && field->kind == FieldKind::Float) {
                    const std::string stepExpr = field->stepExpr.value_or("0.01f");
                    out << indent << "changed |= ImGui::DragFloat(" << labelExpr
                        << ", &" << expr << ", " << stepExpr << ", "
                        << minExpr << ", " << maxExpr << ");\n";
                    return true;
                }
                if (field && field->kind == FieldKind::Int) {
                    out << indent << "changed |= ImGui::SliderInt(" << labelExpr
                        << ", &" << expr << ", static_cast<int>(" << minExpr
                        << "), static_cast<int>(" << maxExpr << "));\n";
                    return true;
                }
                out << indent << "{\n";
                out << indent << "    auto& _moduSliderValue = " << expr << ";\n";
                out << indent << "    using _ModuSliderType = std::remove_reference_t<decltype(_moduSliderValue)>;\n";
                out << indent << "    if constexpr (std::is_floating_point_v<_ModuSliderType>) {\n";
                out << indent << "        changed |= ImGui::DragFloat(" << labelExpr
                    << ", &_moduSliderValue, 0.01f, " << minExpr << ", " << maxExpr << ");\n";
                out << indent << "    } else if constexpr (std::is_integral_v<_ModuSliderType>) {\n";
                out << indent << "        int _moduSliderInt = static_cast<int>(_moduSliderValue);\n";
                out << indent << "        if (ImGui::SliderInt(" << labelExpr
                    << ", &_moduSliderInt, static_cast<int>(" << minExpr
                    << "), static_cast<int>(" << maxExpr << "))) {\n";
                out << indent << "            _moduSliderValue = static_cast<_ModuSliderType>(_moduSliderInt);\n";
                out << indent << "            changed = true;\n";
                out << indent << "        }\n";
                out << indent << "    }\n";
                out << indent << "}\n";
                return true;
            }
            if (callName == "Number") {
                std::string labelExpr;
                std::string expr;
                if (!parseLabelValue(args, "Number(value) or Number(label, value) expected.",
                                     labelExpr, expr)) return false;
                out << indent << "{\n";
                out << indent << "    auto& _moduNumberValue = " << expr << ";\n";
                out << indent << "    using _ModuNumberType = std::remove_reference_t<decltype(_moduNumberValue)>;\n";
                out << indent << "    if constexpr (std::is_floating_point_v<_ModuNumberType>) {\n";
                out << indent << "        changed |= ImGui::DragFloat(" << labelExpr
                    << ", &_moduNumberValue, 0.01f);\n";
                out << indent << "    } else if constexpr (std::is_integral_v<_ModuNumberType>) {\n";
                out << indent << "        int _moduNumberInt = static_cast<int>(_moduNumberValue);\n";
                out << indent << "        if (ImGui::InputInt(" << labelExpr
                    << ", &_moduNumberInt)) {\n";
                out << indent << "            _moduNumberValue = static_cast<_ModuNumberType>(_moduNumberInt);\n";
                out << indent << "            changed = true;\n";
                out << indent << "        }\n";
                out << indent << "    }\n";
                out << indent << "}\n";
                return true;
            }
            if (callName == "String") {
                std::string labelExpr;
                std::string expr;
                if (!parseLabelValue(args, "String(value) or String(label, value) expected.",
                                     labelExpr, expr)) return false;
                out << indent << "changed |= DrawStdStringInput(" << labelExpr
                    << ", " << expr << ", 512);\n";
                return true;
            }
            if (callName == "ObjectRef") {
                std::string labelExpr;
                std::string expr;
                if (!parseLabelValue(args, "ObjectRef(value) or ObjectRef(label, value) expected.",
                                     labelExpr, expr)) return false;
                out << indent << "changed |= DrawObjectRefInput(ctx, " << labelExpr
                    << ", " << expr << ");\n";
                return true;
            }
            if (callName == "AudioClip") {
                std::string labelExpr;
                std::string expr;
                if (!parseLabelValue(args, "AudioClip(value) or AudioClip(label, value) expected.",
                                     labelExpr, expr)) return false;
                out << indent << "changed |= DrawAudioClipInput(" << labelExpr
                    << ", " << expr << ", 512);\n";
                return true;
            }
            if (callName == "ObjectList") {
                std::string labelExpr;
                std::string expr;
                if (!parseLabelValue(args, "ObjectList(value) or ObjectList(label, value) expected.",
                                     labelExpr, expr)) return false;
                const FieldSpec* field = findPersistedFieldByName(expr);
                if (field && field->kind == FieldKind::ObjectList) {
                    std::string uiLabel = inspectorLabelFromExpression(expr);
                    if (args.size() == 2) {
                        uiLabel = trimCopy(args[0]);
                        if (uiLabel.size() >= 2 && uiLabel.front() == '"' && uiLabel.back() == '"') {
                            uiLabel = uiLabel.substr(1, uiLabel.size() - 2);
                        }
                    }
                    emitObjectListEditorCode(expr, uiLabel,
                                             nextInspectorTemp("Refs"), indent);
                } else {
                    out << indent << "changed |= DrawObjectRefListEditor(ctx, " << labelExpr << ", "
                        << expr << ");\n";
                }
                return true;
            }
            if (callName == "Enum") {
                std::string labelExpr;
                std::string expr;
                if (!parseLabelValue(args, "Enum(value) or Enum(label, value) expected.",
                                     labelExpr, expr)) return false;
                if (identifierTailFromExpression(expr) == "currentLanguage") {
                    out << indent << "changed |= DrawLanguageCombo(" << labelExpr
                        << ", " << expr << ");\n";
                    return true;
                }
                out << indent << "{\n";
                const std::string tempVar = nextInspectorTemp("Enum");
                out << indent << "    int " << tempVar << " = static_cast<int>(" << expr << ");\n";
                out << indent << "    if (ImGui::InputInt(" << labelExpr
                    << ", &" << tempVar << ")) {\n";
                out << indent << "        using _ModuEnumType = std::remove_reference_t<decltype(" << expr << ")>;\n";
                out << indent << "        " << expr << " = static_cast<_ModuEnumType>(" << tempVar << ");\n";
                out << indent << "        changed = true;\n";
                out << indent << "    }\n";
                out << indent << "}\n";
                return true;
            }
            if (callName == "DialogueLines") {
                std::string labelExpr;
                std::string expr;
                if (!parseLabelValue(args, "DialogueLines(lines) or DialogueLines(label, lines) expected.",
                                     labelExpr, expr)) return false;
                const std::string idVar = nextInspectorTemp("ObjId");
                const std::string selVar = nextInspectorTemp("SelectedLine");
                out << indent << "{\n";
                out << indent << "    ImGui::TextUnformatted(" << labelExpr << ");\n";
                out << indent << "    const int " << idVar << " = ctx.object ? ctx.object->id : -1;\n";
                out << indent << "    int& " << selVar << " = g_selectedLineByObject[" << idVar << "];\n";
                out << indent << "    changed |= drawDialogueLineEditor(ctx, " << expr
                    << ", " << selVar << ");\n";
                out << indent << "}\n";
                return true;
            }
            if (callName == "InteractionOptions") {
                std::string labelExpr;
                std::string expr;
                if (!parseLabelValue(args, "InteractionOptions(options) or InteractionOptions(label, options) expected.",
                                     labelExpr, expr)) return false;
                const std::string idVar = nextInspectorTemp("ObjId");
                const std::string optVar = nextInspectorTemp("SelectedOption");
                const std::string lineVar = nextInspectorTemp("SelectedLine");
                out << indent << "{\n";
                out << indent << "    ImGui::TextUnformatted(" << labelExpr << ");\n";
                out << indent << "    const int " << idVar << " = ctx.object ? ctx.object->id : -1;\n";
                out << indent << "    int& " << optVar << " = g_selectedOptionEditorIndex[" << idVar << "];\n";
                out << indent << "    int& " << lineVar << " = g_selectedDialogueLineEditorIndex[" << idVar << "];\n";
                out << indent << "    changed |= drawInteractionOptionsEditor(ctx, " << expr
                    << ", " << optVar << ", " << lineVar << ");\n";
                out << indent << "}\n";
                return true;
            }
            if (callName == "MenuActions") {
                std::string labelExpr;
                std::string menuRefsExpr;
                std::string actionsExpr;
                if (args.size() == 2) {
                    menuRefsExpr = trimCopy(args[0]);
                    actionsExpr = trimCopy(args[1]);
                    labelExpr = "\"" + emitAutoLabel(actionsExpr) + "\"";
                } else if (args.size() == 3) {
                    labelExpr = trimCopy(args[0]);
                    menuRefsExpr = trimCopy(args[1]);
                    actionsExpr = trimCopy(args[2]);
                } else {
                    error = "inspector MenuActions(menuItemRefs, actions) or MenuActions(label, menuItemRefs, actions) expected.";
                    return false;
                }
                const std::string idVar = nextInspectorTemp("ObjId");
                const std::string selVar = nextInspectorTemp("SelectedAction");
                out << indent << "{\n";
                out << indent << "    ImGui::TextUnformatted(" << labelExpr << ");\n";
                out << indent << "    const int " << idVar << " = ctx.object ? ctx.object->id : -1;\n";
                out << indent << "    int& " << selVar << " = g_selectedActionByObject[" << idVar << "];\n";
                out << indent << "    changed |= drawMenuActionsEditor(ctx, " << menuRefsExpr
                    << ", " << actionsExpr << ", " << selVar << ");\n";
                out << indent << "}\n";
                return true;
            }
            if (callName == "RuntimeDialogueStatus") {
                if (!args.empty()) {
                    error = "inspector RuntimeDialogueStatus() does not take arguments.";
                    return false;
                }
                const std::string idVar = nextInspectorTemp("ObjId");
                const std::string itVar = nextInspectorTemp("StateIt");
                out << indent << "{\n";
                out << indent << "    const int " << idVar << " = ctx.object ? ctx.object->id : -1;\n";
                out << indent << "    auto " << itVar << " = g_runtimeStates.find(" << idVar << ");\n";
                out << indent << "    drawRuntimeStatus(" << itVar << " != g_runtimeStates.end() ? &"
                    << itVar << "->second : nullptr);\n";
                out << indent << "}\n";
                return true;
            }
            if (callName == "RuntimeInteractableStatus") {
                if (!args.empty()) {
                    error = "inspector RuntimeInteractableStatus() does not take arguments.";
                    return false;
                }
                const std::string idVar = nextInspectorTemp("ObjId");
                const std::string itVar = nextInspectorTemp("StateIt");
                out << indent << "{\n";
                out << indent << "    const int " << idVar << " = ctx.object ? ctx.object->id : -1;\n";
                out << indent << "    auto " << itVar << " = g_runtimeStates.find(" << idVar << ");\n";
                out << indent << "    drawRuntimeStatus(config, " << itVar << " != g_runtimeStates.end() ? &"
                    << itVar << "->second : nullptr);\n";
                out << indent << "}\n";
                return true;
            }
            if (callName == "RuntimeMenuStatus") {
                if (!args.empty()) {
                    error = "inspector RuntimeMenuStatus() does not take arguments.";
                    return false;
                }
                const std::string idVar = nextInspectorTemp("ObjId");
                const std::string itVar = nextInspectorTemp("StateIt");
                out << indent << "{\n";
                out << indent << "    const int " << idVar << " = ctx.object ? ctx.object->id : -1;\n";
                out << indent << "    auto " << itVar << " = g_runtimeStates.find(" << idVar << ");\n";
                out << indent << "    drawRuntimeStatus(config, " << itVar << " != g_runtimeStates.end() ? &"
                    << itVar << "->second : nullptr);\n";
                out << indent << "}\n";
                return true;
            }
            if (callName == "TextEffectFlags") {
                std::string labelExpr;
                std::string expr;
                if (!parseLabelValue(args, "TextEffectFlags(effect) or TextEffectFlags(label, effect) expected.",
                                     labelExpr, expr)) return false;
                const std::string beforeVar = nextInspectorTemp("BeforeEffect");
                out << indent << "{\n";
                out << indent << "    ImGui::TextUnformatted(" << labelExpr << ");\n";
                out << indent << "    auto " << beforeVar << " = " << expr << ";\n";
                out << indent << "    drawTextEffectEditor(" << expr << ");\n";
                out << indent << "    changed |= (" << expr << " != " << beforeVar << ");\n";
                out << indent << "}\n";
                return true;
            }
            if (callName == "ClipGrid") {
                if (!requireArgs(2, "ClipGrid(idleClips, walkClips) requires exactly two arguments.")) return false;
                out << indent << "changed |= EditDirectionalClipGrid(" << trimCopy(args[0])
                    << ", " << trimCopy(args[1]) << ");\n";
                return true;
            }
            if (callName == "SoundSet") {
                if (!requireArgs(2, "SoundSet(label, sounds) requires exactly two arguments.")) return false;
                out << indent << "changed |= EditSoundSet(" << trimCopy(args[0])
                    << ", " << trimCopy(args[1]) << ");\n";
                return true;
            }

            error = "Unsupported inspector statement: " + callName;
            return false;
        };

        std::function<bool(const std::string&, const std::string&, bool)> emitInspectorScope;
        emitInspectorScope = [&](const std::string& scopeText, const std::string& indent,
                                 bool insideTabs) -> bool {
            size_t i = 0;
            while (i < scopeText.size()) {
                while (i < scopeText.size() &&
                       std::isspace(static_cast<unsigned char>(scopeText[i])) != 0) {
                    ++i;
                }
                if (i >= scopeText.size()) break;

                if (!(std::isalpha(static_cast<unsigned char>(scopeText[i])) != 0 ||
                      scopeText[i] == '_')) {
                    error = "Unsupported inspector syntax near: " + trimCopy(scopeText.substr(i, 32));
                    return false;
                }

                const size_t nameStart = i;
                ++i;
                while (i < scopeText.size()) {
                    const unsigned char c = static_cast<unsigned char>(scopeText[i]);
                    if (std::isalnum(c) == 0 && scopeText[i] != '_') break;
                    ++i;
                }
                const std::string callName = scopeText.substr(nameStart, i - nameStart);

                while (i < scopeText.size() &&
                       std::isspace(static_cast<unsigned char>(scopeText[i])) != 0) {
                    ++i;
                }

                std::vector<std::string> args;
                if (i < scopeText.size() && scopeText[i] == '(') {
                    const size_t closeParen = findMatchingParen(scopeText, i);
                    if (closeParen == std::string::npos) {
                        error = "Unmatched '(' in inspector statement: " + callName;
                        return false;
                    }
                    args = splitTopLevel(scopeText.substr(i + 1, closeParen - i - 1), ',');
                    if (args.size() == 1 && trimCopy(args[0]).empty()) {
                        args.clear();
                    }
                    i = closeParen + 1;
                    while (i < scopeText.size() &&
                           std::isspace(static_cast<unsigned char>(scopeText[i])) != 0) {
                        ++i;
                    }
                }

                if (i < scopeText.size() && scopeText[i] == '{') {
                    const size_t closeBrace = findMatchingBrace(scopeText, i);
                    if (closeBrace == std::string::npos) {
                        error = "Unmatched '{' in inspector container: " + callName;
                        return false;
                    }
                    const std::string inner = scopeText.substr(i + 1, closeBrace - i - 1);

                    if (callName == "Tabs") {
                        if (!args.empty()) {
                            error = "inspector Tabs does not take arguments.";
                            return false;
                        }
                        out << indent << "if (ImGui::BeginTabBar(\"##ModuCPPInspectorTabs"
                            << ++inspectorTempCounter << "\")) {\n";
                        if (!emitInspectorScope(inner, indent + "    ", true)) {
                            return false;
                        }
                        out << indent << "    ImGui::EndTabBar();\n";
                        out << indent << "}\n";
                    } else if (callName == "Tab") {
                        if (args.size() != 1) {
                            error = "inspector Tab(title) requires exactly one argument.";
                            return false;
                        }
                        if (!insideTabs) {
                            error = "inspector Tab(...) must be inside Tabs { ... }.";
                            return false;
                        }
                        out << indent << "if (ImGui::BeginTabItem(" << trimCopy(args[0]) << ")) {\n";
                        if (!emitInspectorScope(inner, indent + "    ", false)) {
                            return false;
                        }
                        out << indent << "    ImGui::EndTabItem();\n";
                        out << indent << "}\n";
                    } else if (callName == "Section") {
                        if (args.size() != 1) {
                            error = "inspector Section(title) requires exactly one argument.";
                            return false;
                        }
                        out << indent << "ImGui::TextUnformatted(" << trimCopy(args[0]) << ");\n";
                        out << indent << "ImGui::Separator();\n";
                        if (!emitInspectorScope(inner, indent, insideTabs)) {
                            return false;
                        }
                    } else if (callName == "Group") {
                        if (!args.empty()) {
                            out << indent << "ImGui::TextUnformatted(" << trimCopy(args[0]) << ");\n";
                        }
                        out << indent << "ImGui::BeginGroup();\n";
                        if (!emitInspectorScope(inner, indent + "    ", insideTabs)) {
                            return false;
                        }
                        out << indent << "ImGui::EndGroup();\n";
                    } else if (callName == "Foldout") {
                        if (args.size() != 1) {
                            error = "inspector Foldout(title) requires exactly one argument.";
                            return false;
                        }
                        out << indent << "if (ImGui::CollapsingHeader(" << trimCopy(args[0]) << ")) {\n";
                        if (!emitInspectorScope(inner, indent + "    ", insideTabs)) {
                            return false;
                        }
                        out << indent << "}\n";
                    } else {
                        error = "Unsupported inspector container: " + callName;
                        return false;
                    }

                    i = closeBrace + 1;
                    while (i < scopeText.size() &&
                           std::isspace(static_cast<unsigned char>(scopeText[i])) != 0) {
                        ++i;
                    }
                    if (i < scopeText.size() && scopeText[i] == ';') {
                        ++i;
                    }
                    continue;
                }

                if (i >= scopeText.size() || scopeText[i] != ';') {
                    error = "Missing ';' after inspector statement: " + callName;
                    return false;
                }
                ++i;

                if (!emitInspectorCall(callName, args, indent)) {
                    return false;
                }
            }
            return true;
        };

        const std::string inspectorDsl = stripCommentsPreserveLayout(spec.inspectorBlock);
        if (!emitInspectorScope(inspectorDsl, "    ", false)) {
            return {};
        }

        if (!hasExplicitInspectorSave) {
            out << "    if (changed) {\n";
            out << "        ctx.SaveAutoSettings();\n";
            out << "    }\n";
        }
        out << "}\n\n";
    } else if (hasAutoInspectorFields && !hasInspectorMethod) {
        out << "extern \"C\" MODULARITY_SCRIPT_EXPORT void Script_OnInspector(ScriptContext& ctx) {\n";
        out << "    MODU_SCRIPT(ctx);\n";
        out << "    auto& config = ::ModuCPP::Config<" << supportNs << "::" << configType << ">();\n";
        out << "    " << supportNs << "::BindConfig(ctx, config);\n";
        out << "    bool changed = false;\n";
        for (const FieldSpec& field : spec.fields) {
            if (field.visibility != FieldVisibility::Public) continue;
            const std::string label = inspectorLabelFromFieldName(field.name);
            if (field.kind == FieldKind::Float) {
                const std::string stepExpr = field.stepExpr.value_or("0.01f");
                const std::string minExpr = field.rangeMinExpr.value_or("0.0f");
                const std::string maxExpr = field.rangeMaxExpr.value_or("0.0f");
                out << "    changed |= ImGui::DragFloat(\"" << escapeCStringLiteral(label)
                    << "\", &config." << field.name << ", " << stepExpr
                    << ", " << minExpr << ", " << maxExpr << ");\n";
            } else if (field.kind == FieldKind::Int) {
                if (field.rangeMinExpr.has_value() && field.rangeMaxExpr.has_value()) {
                    out << "    changed |= ImGui::SliderInt(\"" << escapeCStringLiteral(label)
                        << "\", &config." << field.name << ", static_cast<int>("
                        << field.rangeMinExpr.value() << "), static_cast<int>("
                        << field.rangeMaxExpr.value() << "));\n";
                } else {
                    out << "    changed |= ImGui::InputInt(\"" << escapeCStringLiteral(label)
                        << "\", &config." << field.name << ");\n";
                }
            } else if (field.kind == FieldKind::Bool) {
                out << "    changed |= ImGui::Checkbox(\"" << escapeCStringLiteral(label)
                    << "\", &config." << field.name << ");\n";
            } else if (field.kind == FieldKind::Vec3) {
                const std::string stepExpr = field.stepExpr.value_or("0.01f");
                const std::string minExpr = field.rangeMinExpr.value_or("-1000.0f");
                const std::string maxExpr = field.rangeMaxExpr.value_or("1000.0f");
                out << "    changed |= ImGui::DragFloat3(\"" << escapeCStringLiteral(label)
                    << "\", &config." << field.name << ".x, " << stepExpr
                    << ", " << minExpr << ", " << maxExpr << ", \"%.3f\");\n";
            } else if (field.kind == FieldKind::String) {
                out << "    {\n";
                out << "        std::vector<char> buffer(512, '\\0');\n";
                out << "        std::snprintf(buffer.data(), buffer.size(), \"%s\", config." << field.name << ".c_str());\n";
                out << "        if (ImGui::InputText(\"" << escapeCStringLiteral(label) << "\", buffer.data(), buffer.size())) {\n";
                out << "            config." << field.name << " = buffer.data();\n";
                out << "            changed = true;\n";
                out << "        }\n";
                out << "    }\n";
            } else if (field.kind == FieldKind::ObjectList) {
                emitObjectListEditorCode("config." + field.name + "Raw", label, "_moduRefs_" + field.name,
                                         "    ");
            }
        }
        out << "    if (changed) {\n";
        out << "        ctx.SaveAutoSettings();\n";
        out << "    }\n";
        out << "}\n\n";
    }

    // Forward-declare all methods so they can call each other regardless of definition order.
    for (const MethodSpec& method : spec.methods) {
        out << method.returnType << " " << method.name << "(" << method.transpiledParams << ");\n";
    }
    out << "\n";

    for (const MethodSpec& method : spec.methods) {
        std::string rewrittenBody = rewriteSurfaceSyntax(method.body);
        std::string transformedBody = transformEachSyntax(rewrittenBody, listFields, supportNs,
                                                          method.hasContext, error);
        if (!error.empty()) {
            return {};
        }

        out << method.returnType << " " << method.name << "(" << method.transpiledParams << ") {\n";
        if (method.hasContext) {
            const bool manualPreludeMode = method.hasManualPrelude && spec.fields.empty();
            if (method.hasDeltaTimeParam) {
                out << "    ::ModuCPP::time.deltaTime = " << method.deltaTimeParamName << ";\n";
            }
            if (!manualPreludeMode) {
                if (method.contextIsPointer) {
                    if (isVoidReturnType(method.returnType)) {
                        out << "    if (!" << method.contextParamName << ") return;\n";
                    } else {
                        out << "    if (!" << method.contextParamName << ") return {};\n";
                    }
                    out << "    ScriptContext& ctx = *" << method.contextParamName << ";\n";
                } else if (method.contextParamName != "ctx") {
                    out << "    ScriptContext& ctx = " << method.contextParamName << ";\n";
                }
                out << "    MODU_SCRIPT(ctx);\n";
                if (!spec.fields.empty()) {
                    out << "    auto& config = ::ModuCPP::Config<" << supportNs << "::" << configType << ">();\n";
                    out << "    " << supportNs << "::BindConfig(ctx, config);\n";
                    out << "    auto& state = ::ModuCPP::State<" << supportNs << "::" << stateType << ">();\n";
                    for (const FieldSpec& field : spec.fields) {
                        if (fieldPersists(field)) {
                            if (field.kind == FieldKind::ObjectList) {
                                out << "    auto& " << field.name << " = config." << field.name << "Raw;\n";
                            } else {
                                out << "    auto& " << field.name << " = config." << field.name << ";\n";
                            }
                        } else {
                            out << "    auto& " << field.name << " = state." << field.name << ";\n";
                        }
                    }
                }
            }
        } else {
            out << "    ScriptContext* _moduCtxPtr = ::ModuCPP::ctxPtr();\n";
            if (isVoidReturnType(method.returnType)) {
                out << "    if (!_moduCtxPtr) return;\n";
            } else {
                out << "    if (!_moduCtxPtr) return {};\n";
            }
            out << "    ScriptContext& ctx = *_moduCtxPtr;\n";
            if (method.hasDeltaTimeParam) {
                out << "    ::ModuCPP::time.deltaTime = " << method.deltaTimeParamName << ";\n";
            }
            out << "    MODU_SCRIPT(ctx);\n";
            if (!spec.fields.empty()) {
                out << "    auto& config = ::ModuCPP::Config<" << supportNs << "::" << configType << ">();\n";
                out << "    " << supportNs << "::BindConfig(ctx, config);\n";
                out << "    auto& state = ::ModuCPP::State<" << supportNs << "::" << stateType << ">();\n";
                for (const FieldSpec& field : spec.fields) {
                    if (fieldPersists(field)) {
                        if (field.kind == FieldKind::ObjectList) {
                            out << "    auto& " << field.name << " = config." << field.name << "Raw;\n";
                        } else {
                            out << "    auto& " << field.name << " = config." << field.name << ";\n";
                        }
                    } else {
                        out << "    auto& " << field.name << " = state." << field.name << ";\n";
                    }
                }
            }
        }
        // Wrap the user body in a nested scope when parameters were auto-injected
        // (e.g. 'float dt') so that re-declarations in the body (e.g. 'float dt = time.deltaTime')
        // don't shadow the injected parameters and cause a compile error.
        const bool needsBodyScope = method.autoInjectedContext || method.autoInjectedDeltaTime;
        if (needsBodyScope) {
            out << "    {\n" << transformedBody << "\n    }\n";
        } else {
            out << transformedBody << "\n";
        }
        out << "}\n\n";
    }

    return out.str();
}

} // namespace

bool ModuCPPTranspiler::shouldTranspile(const fs::path& sourcePath, const std::string& sourceText) {
    std::string ext = toLowerCopy(sourcePath.extension().string());
    if (ext == ".moducpp") {
        return true;
    }

    // Allow high-level ModuCPP syntax in .cpp-like files without impacting normal C++ scripts.
    std::regex classPattern(R"(\bpublic\s+class\s+[A-Za-z_][A-Za-z0-9_]*\s*:\s*ModuBehaviour\b)");
    const std::string stripped = stripCommentsPreserveLayout(sourceText);
    return std::regex_search(stripped, classPattern);
}

bool ModuCPPTranspiler::transpile(const fs::path& sourcePath, const std::string& sourceText,
                                  ModuCPPTranspileResult& outResult, std::string& error) const {
    error.clear();
    outResult = ModuCPPTranspileResult{};

    const std::string ext = toLowerCopy(sourcePath.extension().string());
    const std::string normalizedSource = normalizeModuSource(sourceText);
    const std::string stripped = stripCommentsPreserveLayout(normalizedSource);
    const std::regex classPattern(R"(\bpublic\s+class\s+([A-Za-z_][A-Za-z0-9_]*)\s*:\s*ModuBehaviour\b)");
    const bool hasHighLevelClass = std::regex_search(stripped, classPattern);

    // Allow .moducpp as a thin frontend type for legacy/native C++ scripts during migration.
    // When high-level class syntax is absent, pass the source through unchanged.
    if (ext == ".moducpp" && !hasHighLevelClass) {
        outResult.generatedSource = normalizedSource;
        outResult.className = sourcePath.stem().string();
        return true;
    }

    ClassSpec parsed;
    if (!parseClass(normalizedSource, parsed, error)) {
        return false;
    }

    std::string generated = generateTranspiledSource(sourcePath, parsed, error);
    if (!error.empty()) {
        return false;
    }
    if (generated.empty()) {
        error = "ModuCPP transpiler generated empty output.";
        return false;
    }

    outResult.generatedSource = std::move(generated);
    outResult.className = parsed.name;
    return true;
}
