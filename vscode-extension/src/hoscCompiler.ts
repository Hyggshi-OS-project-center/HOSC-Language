import * as vscode from 'vscode';
import * as fs from 'fs';
import * as path from 'path';
import { execFile } from 'child_process';

/**
 * Real diagnostics come from the actual `hosc` compiler's `check`
 * subcommand (`hosc check <file.hosc>` — "Implemented", per docs/api.md),
 * not from a client-side regex guesser. The repo already has an official
 * `lsp/` that works this way (`lsp/src/diagnostics.ts`:
 * parseHoscDiagnosticsLine / parseHoscDiagnosticsOutput /
 * toLanguageServerDiagnostics, shelling out to hosc.exe) — this mirrors
 * that approach so this extension doesn't drift from the real grammar
 * the way an invented linter inevitably will.
 *
 * Output format (docs/api.md):
 *   <file>:<line>:<col>: <severity> <code>:
 *   <message>
 *
 *   <source_line>
 *   ^~~~~
 */

export interface HoscDiagnostic {
    line: number; // 1-based, as printed by the compiler
    col: number; // 1-based
    severity: 'error' | 'warning';
    code: string;
    message: string;
}

const HEADER_RE = /^(.*):(\d+):(\d+):\s+(error|warning)\s+(H\d+):\s*$/;

export function parseHoscDiagnosticsOutput(output: string): HoscDiagnostic[] {
    const lines = output.split(/\r\n|\r|\n/);
    const diagnostics: HoscDiagnostic[] = [];

    for (let i = 0; i < lines.length; i++) {
        const m = HEADER_RE.exec(lines[i].trim());
        if (!m) continue;
        const [, , lineStr, colStr, severity, code] = m;

        // The message is the next non-empty line after the header.
        let j = i + 1;
        while (j < lines.length && lines[j].trim().length === 0) j++;
        const message = j < lines.length ? lines[j].trim() : '';

        diagnostics.push({
            line: parseInt(lineStr, 10),
            col: parseInt(colStr, 10),
            severity: severity as 'error' | 'warning',
            code,
            message,
        });
    }

    return diagnostics;
}

export function toVsCodeDiagnostics(document: vscode.TextDocument, hoscDiags: HoscDiagnostic[]): vscode.Diagnostic[] {
    return hoscDiags.map(d => {
        const line = Math.max(d.line - 1, 0);
        const col = Math.max(d.col - 1, 0);
        const lineText = line < document.lineCount ? document.lineAt(line).text : '';
        const endCol = Math.max(col + 1, Math.min(col + 1, lineText.length || col + 1));
        const range = new vscode.Range(line, col, line, Math.max(endCol, col + 1));
        const diag = new vscode.Diagnostic(
            range,
            d.message || `${d.code}`,
            d.severity === 'warning' ? vscode.DiagnosticSeverity.Warning : vscode.DiagnosticSeverity.Error
        );
        diag.code = d.code;
        diag.source = 'hosc';
        return diag;
    });
}

/**
 * Resolves a usable path to the `hosc` executable, trying (in order):
 *   1. The `hosc.executablePath` setting, if set (resolved relative to
 *      the workspace folder when not absolute).
 *   2. `${workspaceFolder}/tools/bin/hosc.exe` and `.../tools/bin/hosc`
 *      (where `tools/build.ps1` places it, per docs/troubleshooting.md).
 *   3. `hosc` / `hosc.exe` on PATH.
 * Returns undefined if nothing resolvable is found.
 */
export function resolveHoscExecutable(document: vscode.TextDocument): string | undefined {
    const folder = vscode.workspace.getWorkspaceFolder(document.uri);
    const folderPath = folder?.uri.fsPath;

    const config = vscode.workspace.getConfiguration('hosc', document.uri);
    const configured = config.get<string>('executablePath');
    if (configured && configured.trim().length > 0) {
        const resolved = path.isAbsolute(configured)
            ? configured
            : folderPath
            ? path.join(folderPath, configured)
            : configured;
        if (fs.existsSync(resolved)) {
            return resolved;
        }
        // Configured but not found — still return it so the error surfaces
        // to the user via the failed execFile call rather than silently
        // falling through to a different binary.
        return resolved;
    }

    if (folderPath) {
        const candidates = [
            path.join(folderPath, 'tools', 'bin', 'hosc.exe'),
            path.join(folderPath, 'tools', 'bin', 'hosc'),
        ];
        for (const c of candidates) {
            if (fs.existsSync(c)) return c;
        }
    }

    // Fall back to PATH lookup; execFile will fail with ENOENT if missing,
    // which callers treat as "no compiler available" rather than an error.
    return process.platform === 'win32' ? 'hosc.exe' : 'hosc';
}

export function runHoscCheck(executable: string, filePath: string, cwd: string | undefined): Promise<{ output: string; failed: boolean }> {
    return new Promise(resolve => {
        execFile(executable, ['check', filePath], { cwd, timeout: 10000 }, (error, stdout, stderr) => {
            if (error && (error as NodeJS.ErrnoException).code === 'ENOENT') {
                resolve({ output: '', failed: true });
                return;
            }
            resolve({ output: `${stdout}\n${stderr}`, failed: false });
        });
    });
}
