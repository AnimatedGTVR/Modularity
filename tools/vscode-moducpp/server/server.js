"use strict";

const cp = require("child_process");
const fs = require("fs");
const net = require("net");
const os = require("os");
const path = require("path");
const { URL } = require("url");

const documents = new Map();
const validateTimers = new Map();
const logClients = new Set();
let nextRequestId = 1;
let settings = {
    workspaceFolder: process.cwd(),
    moducpp: {
        diagnostics: {
            command: "",
            cwd: "${workspaceFolder}",
            enableEditorChecks: false
        }
    }
};

function send(message) {
    const body = JSON.stringify(message);
    process.stdout.write(`Content-Length: ${Buffer.byteLength(body, "utf8")}\r\n\r\n${body}`);
}

function respond(id, result) {
    send({ jsonrpc: "2.0", id, result });
}

function notify(method, params) {
    send({ jsonrpc: "2.0", method, params });
}

function nowIso() {
    return new Date().toISOString();
}

function log(entry) {
    const fullEntry = {
        timestamp: nowIso(),
        type: entry.type || entry.label || "INFO",
        label: entry.label || entry.type || "INFO",
        sourceFile: entry.sourceFile || "",
        generatedCppFile: entry.generatedCppFile || "",
        line: entry.line || 0,
        column: entry.column || 0,
        message: entry.message || "",
        rawMessage: entry.rawMessage || "",
        translatedMessage: entry.translatedMessage || "",
        suggestedFix: entry.suggestedFix || ""
    };
    const json = JSON.stringify(fullEntry) + "\n";
    for (const client of logClients) {
        client.write(json);
    }
}

const logServer = net.createServer(socket => {
    logClients.add(socket);
    socket.on("close", () => logClients.delete(socket));
    socket.on("error", () => logClients.delete(socket));
});
logServer.on("error", error => {
    process.stderr.write(`ModuCPP LSP log server unavailable on port 8956: ${error.message}\n`);
});
logServer.listen(8956, "127.0.0.1", () => {
    log({ type: "INFO", message: "ModuCPP LSP log server listening on TCP port 8956." });
});

function uriToPath(uri) {
    try {
        const parsed = new URL(uri);
        if (parsed.protocol !== "file:") {
            return "";
        }
        return decodeURIComponent(parsed.pathname);
    } catch {
        return "";
    }
}

function pathToUri(filePath) {
    let resolved = path.resolve(filePath).replace(/\\/g, "/");
    if (!resolved.startsWith("/")) {
        resolved = "/" + resolved;
    }
    return "file://" + encodeURI(resolved);
}

function trim(value) {
    return String(value || "").trim();
}

function lower(value) {
    return String(value || "").toLowerCase();
}

function extractQuoted(text) {
    const single = /'([^']+)'/.exec(text);
    if (single) return single[1];
    const double = /"([^"]+)"/.exec(text);
    return double ? double[1] : "";
}

function cleanOriginal(rawMessage) {
    const lines = String(rawMessage || "")
        .split(/\r?\n/)
        .map(line => line.trimEnd())
        .filter(Boolean);
    if (lines.length <= 4) {
        return lines.join("\n");
    }
    return lines.slice(0, 4).join("\n") + "\n...";
}

function visualHint(sourceLine, hintStartColumn, hintSpanLength, hintMessage) {
    if (!sourceLine || !hintMessage) {
        return "";
    }
    const startColumn = Math.max(1, hintStartColumn || 1);
    const spanLength = Math.max(1, hintSpanLength || 1);
    const prefix = sourceLine.slice(0, startColumn - 1).replace(/[^\t]/g, " ");
    const underline = "_".repeat(spanLength) + "|";
    return `${sourceLine}\n${prefix}${underline} ${hintMessage}`;
}

function diagnosticMessage(code, line, column, translatedMessage, rawMessage, hintBlock) {
    if (!translatedMessage) {
        translatedMessage = "This ModuCPP code could not be validated.";
    }
    const original = cleanOriginal(rawMessage);
    const location = `ModuCPP(${code || "MD013"}) [Ln ${line || 1}, Col ${column || 1}]`;
    const hint = hintBlock ? `\n\n${hintBlock}` : "";
    if (!original) {
        return `${location}\n${translatedMessage}${hint}`;
    }
    return `${location}\n${translatedMessage}${hint}\n\nOriginal C++:\n${original}`;
}

function severityNumber(severity) {
    const value = lower(severity);
    if (value.includes("warn")) return 2;
    if (value.includes("note") || value.includes("info")) return 3;
    return 1;
}

function lineRange(line, column, text, spanLength = 1) {
    const zeroLine = Math.max(0, (line || 1) - 1);
    const zeroColumn = Math.max(0, (column || 1) - 1);
    const sourceLine = text.split(/\r?\n/)[zeroLine] || "";
    const unclampedEnd = zeroColumn + Math.max(1, spanLength);
    const endColumn = sourceLine
        ? Math.max(zeroColumn + 1, Math.min(sourceLine.length, unclampedEnd))
        : unclampedEnd;
    return {
        start: { line: zeroLine, character: zeroColumn },
        end: { line: zeroLine, character: endColumn }
    };
}

function makeDiagnostic({
    sourceFile,
    generatedCppFile = "",
    generatedLine = 0,
    line = 1,
    column = 1,
    severity = "error",
    code = "MD013",
    source = "ModuCPP",
    translatedMessage,
    rawMessage,
    suggestedFix = "",
    hintMessage = "",
    hintStartColumn = column,
    hintSpanLength = 1,
    endLine = 0,
    endColumn = 0,
    snippet = "",
    text = ""
}) {
    const sourceLine = snippet || sourceLineAt(text, line);
    const hintBlock = visualHint(sourceLine, hintStartColumn, hintSpanLength, hintMessage || suggestedFix);
    const range = lineRange(line, column, text || sourceLine, hintSpanLength);
    if (endLine > 0 || endColumn > 0) {
        range.end.line = Math.max(0, (endLine || line || 1) - 1);
        range.end.character = Math.max(range.start.character + 1, (endColumn || column + hintSpanLength || column + 1) - 1);
    }
    const diagnostic = {
        range,
        severity: severityNumber(severity),
        code,
        source,
        message: diagnosticMessage(code, line, column, translatedMessage, rawMessage, hintBlock),
        data: {
            filePath: sourceFile || "",
            line,
            column,
            rawMessage: rawMessage || "",
            translatedMessage: translatedMessage || "",
            suggestedFix,
            visualHint: hintBlock,
            snippet: sourceLine,
            generatedFile: generatedCppFile,
            generatedLine
        }
    };
    log({
        type: "LSP_DIAGNOSTIC",
        sourceFile,
        generatedCppFile,
        line,
        column,
        message: diagnostic.message,
        rawMessage,
        translatedMessage,
        suggestedFix
    });
    return diagnostic;
}

const knownImports = [
    "ModuCPP",
    "ModuEngine",
    "ModuInput",
    "RMeshBuilder",
    "ModuCPP.Experimental"
];

const knownAttributes = [
    "Header",
    "Slider",
    "ObjectRef",
    "ObjectList",
    "DialogueLines",
    "ClipGridPair",
    "Separator",
    "SoundSet"
];

function levenshtein(a, b) {
    if (a === b) return 0;
    if (!a) return b.length;
    if (!b) return a.length;
    const previous = Array.from({ length: b.length + 1 }, (_, i) => i);
    const current = new Array(b.length + 1);
    for (let i = 1; i <= a.length; i++) {
        current[0] = i;
        for (let j = 1; j <= b.length; j++) {
            const cost = a[i - 1].toLowerCase() === b[j - 1].toLowerCase() ? 0 : 1;
            current[j] = Math.min(
                current[j - 1] + 1,
                previous[j] + 1,
                previous[j - 1] + cost
            );
        }
        for (let j = 0; j <= b.length; j++) {
            previous[j] = current[j];
        }
    }
    return previous[b.length];
}

function bestSuggestion(value, candidates) {
    const token = trim(value);
    if (token.length < 3) {
        return "";
    }
    let best = "";
    let bestDistance = Infinity;
    const unique = [...new Set(candidates.filter(Boolean))];
    for (const candidate of unique) {
        if (candidate === token) continue;
        const lengthDelta = Math.abs(candidate.length - token.length);
        if (lengthDelta > 3) continue;
        const distance = levenshtein(token, candidate);
        const allowed = token.length <= 5 ? 1 : token.length >= 8 ? 3 : 2;
        if (distance <= allowed && distance < bestDistance) {
            best = candidate;
            bestDistance = distance;
        }
    }
    return best;
}

function translateCompilerMessage(rawMessage, sourceLine) {
    const raw = trim(rawMessage);
    const text = lower(raw);
    const quoted = extractQuoted(raw);
    const code = { code: "MD013", message: "This line is not a valid ModuCPP statement.", fix: "Check the syntax near this line." };

    if (text.includes("expected ';'") || text.includes("; expected") || text.includes("missing ';'")) {
        return { code: "MD001", message: "Missing ';' at the end of this statement.", fix: "Add ';' at the end of the line." };
    }
    if (text.includes("expected '}'") || text.includes("unmatched '{'") || text.includes("missing '}'")) {
        return { code: "MD012", message: "A code block was opened but not closed.", fix: "Add the missing '}'." };
    }
    if (text.includes("expected ')'") || text.includes("unmatched '('") || text.includes("missing ')'")) {
        return { code: "MD010", message: "A function call or condition is missing ')'.", fix: "Close the parenthesized expression." };
    }
    if (text.includes("missing terminating") || text.includes("unterminated string")) {
        return { code: "MD011", message: "A string was opened but not closed.", fix: "Add the closing quote." };
    }
    if (text.includes("base operand of '->'") || sourceLine.includes("->")) {
        return { code: "MD033", message: "Use '.' for ModuCPP script-facing member access here.", fix: "Replace '->' with '.' unless you are intentionally writing raw C++ pointer code." };
    }
    if (text.includes("request for member") || text.includes("no member named") || text.includes("is not a member of")) {
        const name = quoted || "that member";
        return {
            code: "MD034",
            message: `Cannot find field or method "${name}" on this object.`,
            fix: "Check the member name and the module import that defines it."
        };
    }
    if (text.includes("was not declared in this scope") || text.includes("undeclared identifier") || text.includes("identifier not found")) {
        const name = quoted || "this symbol";
        return {
            code: "MD007",
            message: `Cannot find "${name}" in this ModuCPP scope.`,
            fix: "Declare it first, fix the spelling, or add the module that provides it."
        };
    }
    if (text.includes("no matching function") || text.includes("candidate function not viable") || text.includes("cannot convert argument")) {
        return { code: "MD015", message: "This function call does not match any available ModuCPP overload.", fix: "Check the argument count, order, and types." };
    }
    if (text.includes("too few arguments") || text.includes("not enough arguments")) {
        return { code: "MD016", message: "This function call is missing required arguments.", fix: "Add all required arguments." };
    }
    if (text.includes("too many arguments")) {
        return { code: "MD017", message: "This function call has too many arguments.", fix: "Remove the extra arguments." };
    }
    if (text.includes("cannot convert") || text.includes("invalid conversion") || text.includes("no viable conversion") || text.includes("cannot initialize")) {
        return { code: "MD009", message: "This value has the wrong type for the place it is being used.", fix: "Use a value with the expected type or add an explicit supported conversion." };
    }
    if (text.includes("invalid cast") || text.includes("static_cast")) {
        return { code: "MD035", message: "This cast is not valid for these ModuCPP types.", fix: "Cast between compatible types or use a helper conversion function." };
    }
    if (text.includes("control reaches end of non-void function") || text.includes("must return") || text.includes("return-statement")) {
        return { code: "MD018", message: "This method must return a value of the declared return type.", fix: "Add a return value or change the method return type to void." };
    }
    if (text.includes("redefinition") || text.includes("already defined") || text.includes("duplicate")) {
        return { code: "MD008", message: "This name is already defined in the current scope.", fix: "Rename one definition or remove the duplicate." };
    }
    if (text.includes("no such file or directory") || text.includes("file not found") || text.includes("cannot open include file")) {
        return { code: "MD021", message: "A required include or ModuCPP module could not be found.", fix: "Check the add/include name and configured script include paths." };
    }
    if (text.includes("undefined reference") || text.includes("unresolved external symbol") || text.includes("ld returned") || text.includes("linker command failed")) {
        return { code: "MD105", message: "The script compiled, but linking failed.", fix: "Check exported functions, linked libraries, and unresolved symbols." };
    }
    if (text.includes("template") || text.includes("instantiation")) {
        return { code: "MD036", message: "A template or generated helper could not be instantiated for these types.", fix: "Check the types used in this call or inspector field." };
    }
    if (text.includes("autofields")) {
        return { code: "MD037", message: "AutoFields could not expose one or more fields.", fix: "Use existing fields and supported inspector field types." };
    }
    if (text.includes("inspector")) {
        return { code: "MD038", message: "An inspector-exposed field or inspector statement is not valid.", fix: "Use a supported inspector field type or inspector DSL statement." };
    }
    if (text.includes("namespace") || text.includes("not a class") || text.includes("not a namespace")) {
        return { code: "MD039", message: "This name is being used in the wrong namespace or class scope.", fix: "Check the module name, class name, and scope qualifier." };
    }
    return code;
}

function generatedSourceFor(rawFile, fallbackSource) {
    const file = rawFile || "";
    if (file.endsWith(".moducpp.gen.cpp")) {
        return file.slice(0, -".gen.cpp".length);
    }
    if ((file.endsWith(".wrap.cpp") || file.endsWith(".capi.wrap.cpp")) && fallbackSource) {
        return fallbackSource;
    }
    return file;
}

function sourceLineAt(text, line) {
    return text.split(/\r?\n/)[Math.max(0, line - 1)] || "";
}

function normalizeEngineDiagnostic(payload, fallbackSource, fallbackText) {
    if (!payload || typeof payload !== "object") {
        return null;
    }

    const sourceFile = payload.file || payload.sourceFile || payload.path || fallbackSource;
    const line = Number(payload.line || payload.startLine || 1) || 1;
    const column = Number(payload.column || payload.startColumn || 1) || 1;
    const endLine = Number(payload.endLine || 0) || 0;
    const endColumn = Number(payload.endColumn || 0) || 0;
    const snippet = payload.snippet || payload.sourceLine || sourceLineAt(fallbackText, line);
    const underlineStart =
        payload.underlineStart !== undefined
            ? (Number(payload.underlineStart) || 0) + 1
            : Number(payload.hintStartColumn || column) || column;
    const underlineLength = Number(payload.underlineLength || payload.hintSpanLength || Math.max(1, (endColumn || column + 1) - column)) || 1;
    const originalBackend = payload.originalBackend || payload.originalCpp || payload.originalCxx || payload.rawMessage || payload.raw || "";
    const message = payload.message || payload.translatedMessage || "ModuCPP diagnostic.";
    const code = payload.code || "MD000";
    const severity = payload.severity || "error";

    return makeDiagnostic({
        sourceFile,
        generatedCppFile: payload.generatedCppFile || payload.generatedFile || "",
        generatedLine: Number(payload.generatedLine || 0) || 0,
        line,
        column,
        endLine,
        endColumn,
        severity,
        code,
        source: payload.source || "ModuCPP",
        translatedMessage: message,
        rawMessage: originalBackend,
        suggestedFix: payload.suggestedFix || payload.hint || "",
        hintMessage: payload.hint || payload.suggestedFix || "",
        hintStartColumn: underlineStart + (underlineStart === 0 ? 1 : 0),
        hintSpanLength: underlineLength,
        snippet,
        text: fallbackText
    });
}

function engineDiagnosticItemsFromObject(value) {
    if (Array.isArray(value)) {
        return value;
    }
    if (!value || typeof value !== "object") {
        return [];
    }
    if (Array.isArray(value.diagnostics)) {
        return value.diagnostics;
    }
    if (value.type === "diagnostic" || value.kind === "diagnostic" || value.file || value.sourceFile) {
        return [value];
    }
    return [];
}

function parseStructuredDiagnostics(logText, fallbackSource, fallbackText) {
    const diagnostics = [];
    let sawStructured = false;
    let sawClean = false;
    const lines = String(logText || "").split(/\r?\n/);

    for (const rawLine of lines) {
        let line = trim(rawLine);
        if (!line) {
            continue;
        }
        line = line.replace(/^MODUCPP_DIAGNOSTIC\s*:?\s*/i, "");
        line = line.replace(/^MODUCPP_DIAGNOSTICS\s*:?\s*/i, "");

        if (!line.startsWith("{") && !line.startsWith("[")) {
            continue;
        }

        let parsed;
        try {
            parsed = JSON.parse(line);
        } catch {
            continue;
        }

        sawStructured = true;
        if (parsed && typeof parsed === "object" && !Array.isArray(parsed)) {
            const clean =
                parsed.clean === true ||
                parsed.ok === true ||
                parsed.status === "clean" ||
                parsed.clear === true ||
                (Array.isArray(parsed.diagnostics) && parsed.diagnostics.length === 0);
            if (clean) {
                sawClean = true;
            }
        }

        for (const item of engineDiagnosticItemsFromObject(parsed)) {
            const inheritedItem =
                parsed && !Array.isArray(parsed) && parsed.file && item && typeof item === "object" && !item.file && !item.sourceFile
                    ? { ...item, file: parsed.file }
                    : item;
            const diagnostic = normalizeEngineDiagnostic(inheritedItem, fallbackSource, fallbackText);
            if (diagnostic) {
                diagnostics.push(diagnostic);
            }
        }
    }

    return { diagnostics, sawStructured, sawClean };
}

function parseCompilerOutput(logText, fallbackSource, fallbackText) {
    const diagnostics = [];
    const lines = String(logText || "").split(/\r?\n/);
    const gccClang = /^(.+?):(\d+)(?::(\d+))?:\s*(fatal error|error|warning|note):\s*(.+)$/i;
    const msvc = /^(.+?)\((\d+)(?:,(\d+))?\)\s*:\s*(fatal error|error|warning|note)\s+(?:[A-Z]{1,4}\d+):\s*(.+)$/i;
    const linker = /(undefined reference|unresolved external symbol|ld returned|collect2: error|linker command failed)/i;

    for (const lineText of lines) {
        const line = trim(lineText);
        if (!line) continue;

        let match = gccClang.exec(line);
        let rawFile = "";
        let lineNumber = 1;
        let column = 1;
        let severity = "error";
        let message = "";
        if (match) {
            rawFile = trim(match[1]);
            lineNumber = Number(match[2]) || 1;
            column = Number(match[3]) || 1;
            severity = match[4];
            message = trim(match[5]);
        } else {
            match = msvc.exec(line);
            if (match) {
                rawFile = trim(match[1]);
                lineNumber = Number(match[2]) || 1;
                column = Number(match[3]) || 1;
                severity = match[4];
                message = trim(match[5]);
            } else if (linker.test(line)) {
                rawFile = fallbackSource;
                message = line;
            } else {
                continue;
            }
        }

        const generated = rawFile.endsWith(".moducpp.gen.cpp") || rawFile.endsWith(".wrap.cpp") || rawFile.endsWith(".capi.wrap.cpp");
        const sourceFile = generated ? generatedSourceFor(rawFile, fallbackSource) : (rawFile || fallbackSource);
        const targetText = sourceFile === fallbackSource ? fallbackText : readFileText(sourceFile);
        const translated = translateCompilerMessage(message, sourceLineAt(targetText, lineNumber));
        diagnostics.push(makeDiagnostic({
            sourceFile,
            generatedCppFile: generated ? rawFile : "",
            generatedLine: generated ? lineNumber : 0,
            line: generated && sourceFile !== rawFile ? 1 : lineNumber,
            column,
            severity,
            code: translated.code,
            translatedMessage: translated.message,
            rawMessage: line,
            suggestedFix: translated.fix,
            hintMessage: translated.fix,
            text: targetText || fallbackText
        }));
        log({
            type: generated ? "CPP_ERROR" : "TRANSPILER_ERROR",
            sourceFile,
            generatedCppFile: generated ? rawFile : "",
            line: lineNumber,
            column,
            message,
            rawMessage: line,
            translatedMessage: translated.message,
            suggestedFix: translated.fix
        });
    }
    return diagnostics;
}

function parseStageError(stageError, sourceFile, text) {
    const raw = trim(stageError);
    if (!raw) return [];
    const line = /(?:^|[^A-Za-z])line\s+(\d+)/i.exec(raw);
    const column = /(?:^|[^A-Za-z])column\s+(\d+)/i.exec(raw);
    const clean = raw.replace(/^ModuCPP transpile failed:\s*/i, "");
    const translated = translateCompilerMessage(clean, sourceLineAt(text, Number(line?.[1]) || 1));
    return [makeDiagnostic({
        sourceFile,
        line: Number(line?.[1]) || 1,
        column: Number(column?.[1]) || 1,
        severity: "error",
        code: translated.code,
        translatedMessage: translated.message,
        rawMessage: raw,
        suggestedFix: translated.fix,
        hintMessage: translated.fix,
        text
    })];
}

function readFileText(filePath) {
    try {
        return fs.readFileSync(filePath, "utf8");
    } catch {
        return "";
    }
}

function staticDiagnostics(sourceFile, text) {
    const diagnostics = [];
    const lines = text.split(/\r?\n/);
    const stack = [];
    for (let i = 0; i < lines.length; i++) {
        const line = lines[i];
        const trimmed = line.trim();
        if (!trimmed || trimmed.startsWith("//")) continue;

        if (/^add\b/.test(trimmed) && !trimmed.endsWith(";")) {
            const translated = translateCompilerMessage("expected ';'", line);
            diagnostics.push(makeDiagnostic({
                sourceFile,
                line: i + 1,
                column: line.length + 1,
                severity: "error",
                code: translated.code,
                translatedMessage: "Import statements must end with ';'.",
                rawMessage: "ModuCPP import statement is missing ';'.",
                suggestedFix: "Write the import as: add ModuleName;",
                hintMessage: "Add this: ';'",
                hintStartColumn: Math.max(1, line.search(/\S/) + 1),
                hintSpanLength: Math.max(1, trimmed.length),
                text
            }));
        }

        const importMatch = /^(\s*add\s+)([A-Za-z_][A-Za-z0-9_.]*)\s*;/.exec(line);
        if (importMatch && !knownImports.includes(importMatch[2])) {
            const suggestion = bestSuggestion(importMatch[2], knownImports);
            diagnostics.push(makeDiagnostic({
                sourceFile,
                line: i + 1,
                column: importMatch[1].length + 1,
                severity: "error",
                code: "MD021",
                translatedMessage: `Unknown ModuCPP module '${importMatch[2]}'.`,
                rawMessage: `Unknown ModuCPP import '${importMatch[2]}'.`,
                suggestedFix: suggestion ? `Did you mean '${suggestion}'?` : "Check the module name.",
                hintMessage: suggestion ? `Did you mean '${suggestion}'?` : "Check this module name.",
                hintStartColumn: importMatch[1].length + 1,
                hintSpanLength: importMatch[2].length,
                text
            }));
        }

        const attributeMatch = /^(\s*)\[\s*([A-Za-z_][A-Za-z0-9_]*)/.exec(line);
        if (attributeMatch && !knownAttributes.includes(attributeMatch[2])) {
            const suggestion = bestSuggestion(attributeMatch[2], knownAttributes);
            diagnostics.push(makeDiagnostic({
                sourceFile,
                line: i + 1,
                column: attributeMatch[1].length + 2,
                severity: "error",
                code: "MD010",
                translatedMessage: `Unknown ModuCPP attribute '${attributeMatch[2]}'.`,
                rawMessage: `Unknown annotation or macro near '${attributeMatch[2]}'.`,
                suggestedFix: suggestion ? `Did you mean '${suggestion}'?` : "Check the attribute name.",
                hintMessage: suggestion ? `Did you mean '${suggestion}'?` : "Check this attribute name.",
                hintStartColumn: attributeMatch[1].length + 2,
                hintSpanLength: attributeMatch[2].length,
                text
            }));
        }

        const arrowColumn = line.indexOf("->");
        if (arrowColumn >= 0) {
            const translated = translateCompilerMessage("base operand of '->' is not a pointer", line);
            diagnostics.push(makeDiagnostic({
                sourceFile,
                line: i + 1,
                column: arrowColumn + 1,
                severity: "error",
                code: translated.code,
                translatedMessage: translated.message,
                rawMessage: "ModuCPP source uses '->'.",
                suggestedFix: translated.fix,
                hintMessage: "Use '.' here.",
                hintStartColumn: arrowColumn + 1,
                hintSpanLength: 2,
                text
            }));
        }

        const stringAssignment = /^(\s*(?:public|private)?\s*string\s+[A-Za-z_][A-Za-z0-9_]*\s*=\s*)([^";]+)\s*;/.exec(line);
        if (stringAssignment) {
            const value = trim(stringAssignment[2]);
            const isNumericOrBool = /^(?:true|false|\d+(?:\.\d+)?f?)$/.test(value);
            const looksLikeUnquotedText = /^[A-Za-z][A-Za-z0-9 _-]*$/.test(value) && /\s/.test(value);
            if (isNumericOrBool || looksLikeUnquotedText) {
                diagnostics.push(makeDiagnostic({
                    sourceFile,
                    line: i + 1,
                    column: stringAssignment[1].length + 1,
                    severity: "error",
                    code: isNumericOrBool ? "MD009" : "MD011",
                    translatedMessage: isNumericOrBool
                        ? "This field expects a string, but the assigned value is not a string."
                        : "This looks like a string value without quotes.",
                    rawMessage: `String assignment uses unquoted or non-string value '${value}'.`,
                    suggestedFix: `Use quotes if this is text: "${value}".`,
                    hintMessage: `Use quotes if this is text: "${value}".`,
                    hintStartColumn: stringAssignment[1].length + 1,
                    hintSpanLength: value.length,
                    text
                }));
            }
        }

        for (let c = 0; c < line.length; c++) {
            if (line[c] === "{" || line[c] === "(" || line[c] === "[") stack.push({ char: line[c], line: i + 1, column: c + 1 });
            if (line[c] === "}") {
                const open = stack.pop();
                if (!open || open.char !== "{") {
                    diagnostics.push(makeDiagnostic({
                        sourceFile,
                        line: i + 1,
                        column: c + 1,
                        severity: "error",
                        code: "MD012",
                        translatedMessage: "This closing brace does not match an open code block.",
                        rawMessage: "Unmatched '}' in ModuCPP source.",
                        suggestedFix: "Remove this brace or add the missing opening brace.",
                        hintMessage: "This brace has no matching '{'.",
                        hintSpanLength: 1,
                        text
                    }));
                }
            }
            if (line[c] === ")") {
                const open = stack.pop();
                if (!open || open.char !== "(") {
                    diagnostics.push(makeDiagnostic({
                        sourceFile,
                        line: i + 1,
                        column: c + 1,
                        severity: "error",
                        code: "MD010",
                        translatedMessage: "This closing parenthesis does not match an open parenthesis.",
                        rawMessage: "Unmatched ')' in ModuCPP source.",
                        suggestedFix: "Remove this parenthesis or add the missing opening parenthesis.",
                        hintMessage: "This parenthesis has no matching '('.",
                        hintSpanLength: 1,
                        text
                    }));
                }
            }
            if (line[c] === "]") {
                const open = stack.pop();
                if (!open || open.char !== "[") {
                    diagnostics.push(makeDiagnostic({
                        sourceFile,
                        line: i + 1,
                        column: c + 1,
                        severity: "error",
                        code: "MD010",
                        translatedMessage: "This closing bracket does not match an open bracket.",
                        rawMessage: "Unmatched ']' in ModuCPP source.",
                        suggestedFix: "Remove this bracket or add the missing opening bracket.",
                        hintMessage: "This bracket has no matching '['.",
                        hintSpanLength: 1,
                        text
                    }));
                }
            }
        }

        const looksLikeStatement = /^(public|private)?\s*([A-Za-z_][A-Za-z0-9_:<>\s*&]+)\s+[A-Za-z_][A-Za-z0-9_]*\s*(=.+)?$/.test(trimmed);
        const isControlOrBlock = /^(add\b|if|for|while|switch|else|public class|inspector\b)/.test(trimmed) || trimmed.endsWith("{") || trimmed.endsWith("}") || trimmed.endsWith(";") || trimmed.includes("(");
        if (looksLikeStatement && !isControlOrBlock) {
            const translated = translateCompilerMessage("expected ';'", line);
            diagnostics.push(makeDiagnostic({
                sourceFile,
                line: i + 1,
                column: line.length + 1,
                severity: "error",
                code: translated.code,
                translatedMessage: translated.message,
                rawMessage: "ModuCPP source statement appears to be missing ';'.",
                suggestedFix: translated.fix,
                hintMessage: "Add this: ';'",
                hintStartColumn: Math.max(1, line.search(/\S/) + 1),
                hintSpanLength: Math.max(1, trimmed.length),
                text
            }));
        }
    }

    for (const open of stack.slice(-3)) {
        const message =
            open.char === "{" ? "A code block was opened but not closed." :
            open.char === "(" ? "A parenthesized expression was opened but not closed." :
            "A bracketed expression or attribute was opened but not closed.";
        diagnostics.push(makeDiagnostic({
            sourceFile,
            line: open.line,
            column: open.column,
            severity: "error",
            code: open.char === "{" ? "MD012" : "MD010",
            translatedMessage: message,
            rawMessage: `Unclosed '${open.char}' in ModuCPP source.`,
            suggestedFix:
                open.char === "{" ? "Add the missing '}'." :
                open.char === "(" ? "Add the missing ')'." :
                "Add the missing ']'.",
            hintMessage:
                open.char === "{" ? "Add the matching '}'." :
                open.char === "(" ? "Add the matching ')'." :
                "Add the matching ']'.",
            hintSpanLength: 1,
            text
        }));
    }
    return diagnostics;
}

function expandPlaceholders(value, sourceFile) {
    const workspaceFolder = settings.workspaceFolder || process.cwd();
    return String(value || "")
        .replace(/\$\{file\}/g, sourceFile)
        .replace(/\$\{workspaceFolder\}/g, workspaceFolder);
}

function extensionStatusDiagnostic(sourceFile, text, message, rawMessage = "") {
    return makeDiagnostic({
        sourceFile,
        line: 1,
        column: 1,
        severity: "warning",
        code: "MDLSP001",
        translatedMessage: message,
        rawMessage,
        suggestedFix: "Configure moducpp.diagnostics.command to run the ModuCPP engine diagnostics pipeline.",
        text
    });
}

function runDiagnosticCommand(sourceFile, text) {
    const command = settings.moducpp?.diagnostics?.command || "";
    if (!trim(command)) {
        return Promise.resolve({
            diagnostics: [extensionStatusDiagnostic(
                sourceFile,
                text,
                "ModuCPP engine diagnostics are not configured.",
                "No moducpp.diagnostics.command setting was provided."
            )],
            engineUnavailable: true
        });
    }
    const cwdSetting = settings.moducpp?.diagnostics?.cwd || "${workspaceFolder}";
    const cwd = expandPlaceholders(cwdSetting, sourceFile);
    const expandedCommand = expandPlaceholders(command, sourceFile);
    return new Promise(resolve => {
        cp.exec(expandedCommand, { cwd, timeout: 30000, maxBuffer: 1024 * 1024 * 4 }, (error, stdout, stderr) => {
            const commandError = error ? error.message : "";
            const output = [stdout, stderr].filter(Boolean).join(os.EOL);
            const structured = parseStructuredDiagnostics(output, sourceFile, text);
            if (structured.sawStructured) {
                resolve({
                    diagnostics: structured.diagnostics,
                    clean: structured.sawClean && structured.diagnostics.length === 0,
                    engineUnavailable: false
                });
                return;
            }

            const compilerDiagnostics = parseCompilerOutput(output, sourceFile, text);
            if (compilerDiagnostics.length > 0) {
                resolve({ diagnostics: compilerDiagnostics, engineUnavailable: false });
                return;
            }

            if (error) {
                resolve({
                    diagnostics: [extensionStatusDiagnostic(
                        sourceFile,
                        text,
                        "ModuCPP engine diagnostics command failed before reporting diagnostics.",
                        commandError
                    )],
                    engineUnavailable: true
                });
                return;
            }

            resolve({ diagnostics: [], clean: true, engineUnavailable: false });
        });
    });
}

function publish(uri, diagnostics) {
    notify("textDocument/publishDiagnostics", { uri, diagnostics });
}

function scheduleValidate(uri, delay = 150) {
    clearTimeout(validateTimers.get(uri));
    validateTimers.set(uri, setTimeout(() => validate(uri), delay));
}

async function validate(uri) {
    const document = documents.get(uri);
    if (!document) return;
    const sourceFile = uriToPath(uri);
    const commandResult = await runDiagnosticCommand(sourceFile, document.text);
    const editorDiagnostics =
        settings.moducpp?.diagnostics?.enableEditorChecks === true && !commandResult.engineUnavailable
            ? staticDiagnostics(sourceFile, document.text)
            : [];
    publish(uri, dedupeDiagnostics(editorDiagnostics.concat(commandResult.diagnostics || []), sourceFile));
}

function dedupeDiagnostics(diagnostics, sourceFile) {
    const seen = new Set();
    const out = [];
    for (const diagnostic of diagnostics) {
        if (diagnostic.data?.filePath && path.resolve(diagnostic.data.filePath) !== path.resolve(sourceFile)) {
            continue;
        }
        const key = `${diagnostic.code}|${diagnostic.range.start.line}|${diagnostic.range.start.character}|${diagnostic.data?.translatedMessage}`;
        if (seen.has(key)) continue;
        seen.add(key);
        out.push(diagnostic);
    }
    return out.slice(0, 100);
}

function handlePipelineOutput(params) {
    const uri = params.uri || pathToUri(params.sourceFile || "");
    const sourceFile = uriToPath(uri) || params.sourceFile || "";
    const text = documents.get(uri)?.text || readFileText(sourceFile);
    if (params.clear === true || params.clean === true || params.status === "clean") {
        publish(uri, []);
        return;
    }

    const diagnostics = [];
    for (const item of engineDiagnosticItemsFromObject(params)) {
        const inheritedItem =
            params.file && item && typeof item === "object" && !item.file && !item.sourceFile
                ? { ...item, file: params.file }
                : item;
        const diagnostic = normalizeEngineDiagnostic(inheritedItem, sourceFile, text);
        if (diagnostic) {
            diagnostics.push(diagnostic);
        }
    }
    if (diagnostics.length > 0 || Array.isArray(params.diagnostics)) {
        publish(uri, dedupeDiagnostics(diagnostics, sourceFile));
        return;
    }

    diagnostics.push(...parseStageError(params.stageError || "", sourceFile, text));
    diagnostics.push(...parseCompilerOutput(params.compileLog || "", sourceFile, text));
    diagnostics.push(...parseCompilerOutput(params.linkLog || "", sourceFile, text));
    publish(uri, dedupeDiagnostics(diagnostics, sourceFile));
}

let inputBuffer = Buffer.alloc(0);
process.stdin.on("data", chunk => {
    inputBuffer = Buffer.concat([inputBuffer, chunk]);
    while (true) {
        const headerEnd = inputBuffer.indexOf("\r\n\r\n");
        if (headerEnd < 0) return;
        const header = inputBuffer.slice(0, headerEnd).toString("utf8");
        const lengthMatch = /Content-Length:\s*(\d+)/i.exec(header);
        if (!lengthMatch) {
            inputBuffer = inputBuffer.slice(headerEnd + 4);
            continue;
        }
        const length = Number(lengthMatch[1]);
        const start = headerEnd + 4;
        const end = start + length;
        if (inputBuffer.length < end) return;
        const raw = inputBuffer.slice(start, end).toString("utf8");
        inputBuffer = inputBuffer.slice(end);
        handleMessage(JSON.parse(raw));
    }
});

function handleMessage(message) {
    switch (message.method) {
        case "initialize":
            if (message.params?.rootUri) {
                settings.workspaceFolder = uriToPath(message.params.rootUri);
            }
            respond(message.id, {
                capabilities: {
                    textDocumentSync: {
                        openClose: true,
                        change: 1,
                        save: { includeText: true }
                    }
                },
                serverInfo: {
                    name: "ModuCPP Language Server",
                    version: "0.1.0"
                }
            });
            break;
        case "shutdown":
            respond(message.id, null);
            break;
        case "exit":
            process.exit(0);
            break;
        case "initialized":
            log({ type: "INFO", message: "ModuCPP language server initialized." });
            break;
        case "workspace/didChangeConfiguration":
            settings = {
                ...settings,
                ...message.params?.settings,
                moducpp: {
                    ...settings.moducpp,
                    ...message.params?.settings?.moducpp
                }
            };
            break;
        case "textDocument/didOpen":
            documents.set(message.params.textDocument.uri, {
                version: message.params.textDocument.version || 0,
                text: message.params.textDocument.text || ""
            });
            scheduleValidate(message.params.textDocument.uri, 0);
            break;
        case "textDocument/didChange":
            documents.set(message.params.textDocument.uri, {
                version: message.params.textDocument.version || 0,
                text: message.params.contentChanges?.[0]?.text || ""
            });
            scheduleValidate(message.params.textDocument.uri);
            break;
        case "textDocument/didSave": {
            const uri = message.params.textDocument.uri;
            const existing = documents.get(uri) || { version: 0, text: "" };
            documents.set(uri, { ...existing, text: message.params.text || existing.text });
            scheduleValidate(uri, 0);
            break;
        }
        case "textDocument/didClose":
            documents.delete(message.params.textDocument.uri);
            publish(message.params.textDocument.uri, []);
            break;
        case "moducpp/pipelineOutput":
            handlePipelineOutput(message.params || {});
            break;
        default:
            if (Object.prototype.hasOwnProperty.call(message, "id")) {
                respond(message.id, null);
            }
            break;
    }
}

process.on("uncaughtException", error => {
    log({ type: "ERROR", message: error.message, rawMessage: error.stack || error.message });
});
