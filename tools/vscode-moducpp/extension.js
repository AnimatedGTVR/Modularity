const vscode = require("vscode");
const cp = require("child_process");
const path = require("path");

let serverProcess = null;
let nextId = 1;

function send(message) {
    if (!serverProcess || !serverProcess.stdin.writable) {
        return;
    }
    const body = JSON.stringify(message);
    serverProcess.stdin.write(`Content-Length: ${Buffer.byteLength(body, "utf8")}\r\n\r\n${body}`);
}

function sendRequest(method, params) {
    send({ jsonrpc: "2.0", id: nextId++, method, params });
}

function sendNotification(method, params) {
    send({ jsonrpc: "2.0", method, params });
}

function uriFromDocument(document) {
    return document.uri.toString();
}

function syncSettings() {
    const config = vscode.workspace.getConfiguration("moducpp");
    const workspaceFolder = vscode.workspace.workspaceFolders?.[0]?.uri.fsPath || "";
    sendNotification("workspace/didChangeConfiguration", {
        settings: {
            moducpp: {
                diagnostics: {
                    command: config.get("diagnostics.command", ""),
                    cwd: config.get("diagnostics.cwd", "${workspaceFolder}"),
                    enableEditorChecks: config.get("diagnostics.enableEditorChecks", false)
                }
            },
            workspaceFolder
        }
    });
}

function activate(context) {
    const serverPath = path.join(context.extensionPath, "server", "server.js");
    serverProcess = cp.spawn(process.execPath, [serverPath], {
        cwd: context.extensionPath,
        stdio: ["pipe", "pipe", "pipe"]
    });

    let buffer = Buffer.alloc(0);
    serverProcess.stdout.on("data", chunk => {
        buffer = Buffer.concat([buffer, chunk]);
        while (true) {
            const headerEnd = buffer.indexOf("\r\n\r\n");
            if (headerEnd < 0) {
                break;
            }
            const header = buffer.slice(0, headerEnd).toString("utf8");
            const match = /Content-Length:\s*(\d+)/i.exec(header);
            if (!match) {
                buffer = buffer.slice(headerEnd + 4);
                continue;
            }
            const length = Number(match[1]);
            const messageStart = headerEnd + 4;
            const messageEnd = messageStart + length;
            if (buffer.length < messageEnd) {
                break;
            }
            const body = buffer.slice(messageStart, messageEnd).toString("utf8");
            buffer = buffer.slice(messageEnd);
            const message = JSON.parse(body);
            if (message.method === "textDocument/publishDiagnostics") {
                publishDiagnostics(message.params);
            }
        }
    });
    serverProcess.stderr.on("data", chunk => console.warn(chunk.toString()));

    const diagnosticCollections = new Map();
    function collectionFor(uri) {
        let collection = diagnosticCollections.get(uri);
        if (!collection) {
            collection = vscode.languages.createDiagnosticCollection("moducpp");
            diagnosticCollections.set(uri, collection);
            context.subscriptions.push(collection);
        }
        return collection;
    }

    function publishDiagnostics(params) {
        const uri = vscode.Uri.parse(params.uri);
        const diagnostics = (params.diagnostics || []).map(item => {
            const range = new vscode.Range(
                item.range.start.line,
                item.range.start.character,
                item.range.end.line,
                item.range.end.character
            );
            const severity =
                item.severity === 1 ? vscode.DiagnosticSeverity.Error :
                item.severity === 2 ? vscode.DiagnosticSeverity.Warning :
                item.severity === 3 ? vscode.DiagnosticSeverity.Information :
                vscode.DiagnosticSeverity.Hint;
            const diagnostic = new vscode.Diagnostic(range, item.message, severity);
            diagnostic.code = item.code;
            diagnostic.source = item.source || "ModuCPP";
            diagnostic.data = item.data;
            diagnostic.relatedInformation = [];
            if (item.data?.generatedFile) {
                diagnostic.relatedInformation.push(new vscode.DiagnosticRelatedInformation(
                    new vscode.Location(vscode.Uri.file(item.data.generatedFile), new vscode.Position(Math.max(0, (item.data.generatedLine || 1) - 1), 0)),
                    "Generated C++ location"
                ));
            }
            return diagnostic;
        });
        collectionFor(params.uri).set(uri, diagnostics);
    }

    sendRequest("initialize", {
        processId: process.pid,
        rootUri: vscode.workspace.workspaceFolders?.[0]?.uri.toString() || null,
        capabilities: {}
    });
    sendNotification("initialized", {});
    syncSettings();

    for (const document of vscode.workspace.textDocuments) {
        if (document.languageId === "moducpp" || document.fileName.endsWith(".moducpp")) {
            sendNotification("textDocument/didOpen", {
                textDocument: {
                    uri: uriFromDocument(document),
                    languageId: "moducpp",
                    version: document.version,
                    text: document.getText()
                }
            });
        }
    }

    context.subscriptions.push(vscode.workspace.onDidOpenTextDocument(document => {
        if (document.languageId === "moducpp" || document.fileName.endsWith(".moducpp")) {
            sendNotification("textDocument/didOpen", {
                textDocument: {
                    uri: uriFromDocument(document),
                    languageId: "moducpp",
                    version: document.version,
                    text: document.getText()
                }
            });
        }
    }));

    context.subscriptions.push(vscode.workspace.onDidChangeTextDocument(event => {
        const document = event.document;
        if (document.languageId === "moducpp" || document.fileName.endsWith(".moducpp")) {
            sendNotification("textDocument/didChange", {
                textDocument: {
                    uri: uriFromDocument(document),
                    version: document.version
                },
                contentChanges: [{ text: document.getText() }]
            });
        }
    }));

    context.subscriptions.push(vscode.workspace.onDidSaveTextDocument(document => {
        if (document.languageId === "moducpp" || document.fileName.endsWith(".moducpp")) {
            sendNotification("textDocument/didSave", {
                textDocument: { uri: uriFromDocument(document) },
                text: document.getText()
            });
        }
    }));

    context.subscriptions.push(vscode.workspace.onDidChangeConfiguration(event => {
        if (event.affectsConfiguration("moducpp")) {
            syncSettings();
        }
    }));
}

function deactivate() {
    if (serverProcess) {
        serverProcess.kill();
        serverProcess = null;
    }
}

module.exports = { activate, deactivate };
