# ModuCPP Language Tools

This folder contains a dependency-free VSCode/VSCodium extension and LSP server for ModuCPP diagnostics.

The server validates opened and saved `.moducpp` files by running the configured ModuCPP engine diagnostics command, then publishes the engine/transpiler/compiler diagnostics into the editor. It also starts a JSON-lines debug log stream on TCP port `8956`.

## Install in VSCodium

`codium --install-extension` installs packaged `.vsix` files or marketplace extension ids. It does not install an unpacked extension folder directly.

From this folder:

```bash
cd /home/anemunt/Git-base/Modularity/tools/vscode-moducpp
npx --yes @vscode/vsce package
codium --install-extension moducpp-language-tools-0.1.0.vsix
```

Then reload VSCodium and open a `.moducpp` file.

For development without packaging, launch VSCodium with the folder as an extension development path:

```bash
codium --extensionDevelopmentPath=/home/anemunt/Git-base/Modularity/tools/vscode-moducpp /home/anemunt/Git-base/Modularity
```

Optional settings:

```json
{
  "moducpp.diagnostics.command": "./your-script-compiler --check ${file}",
  "moducpp.diagnostics.cwd": "${workspaceFolder}",
  "moducpp.diagnostics.enableEditorChecks": false
}
```

The command should prefer JSON-lines diagnostics emitted by the ModuCPP engine/transpiler/compiler. Normal GCC/Clang/MSVC output is still accepted as a fallback.

Example diagnostic JSON line:

```json
{"file":"/path/to/DialogueSystem.moducpp","line":98,"column":9,"endLine":98,"endColumn":27,"severity":"error","code":"MD014","source":"ModuCPP","message":"Unknown function-like symbol near 'TryAutoOpenOnBegin'.","hint":"Did you mean 'autoOpenOnBegin'?","snippet":"        TryAutoOpenOnBegin();","underlineStart":8,"underlineLength":18,"originalBackend":"Unknown function-like symbol near 'TryAutoOpenOnBegin'."}
```

Clean results can be reported with:

```json
{"file":"/path/to/DialogueSystem.moducpp","diagnostics":[]}
```

Full raw messages are kept in each diagnostic's `data.rawMessage`; the editor-facing message keeps the ModuCPP explanation first and labels the original backend text below it.
