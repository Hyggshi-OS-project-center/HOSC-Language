import * as vscode from 'vscode';
import { ALL_SYMBOLS, DOTTED_CALLS, KEYWORDS, KEYWORD_NAMES } from './hoscLanguageData';
import { parseHoscDiagnosticsOutput, resolveHoscExecutable, runHoscCheck, toVsCodeDiagnostics } from './hoscCompiler';

const HOSC_SELECTOR: vscode.DocumentSelector = { language: 'hosc' };

// ---------------------------------------------------------------------------
// Shared helper: index `func` declarations (the only declaration kind that
// exists in this grammar — no struct/interface, see hoscLanguageData.ts).
// ---------------------------------------------------------------------------

interface HoscFuncDecl {
    name: string;
    range: vscode.Range;
    nameRange: vscode.Range;
}

const FUNC_DECL_RE = /\bfunc\s+([a-zA-Z_][a-zA-Z0-9_]*)/g;

function indexFuncDeclarations(document: vscode.TextDocument): HoscFuncDecl[] {
    const text = document.getText();
    const decls: HoscFuncDecl[] = [];
    let match: RegExpExecArray | null;
    FUNC_DECL_RE.lastIndex = 0;
    while ((match = FUNC_DECL_RE.exec(text)) !== null) {
        const name = match[1];
        const nameStart = match.index + match[0].indexOf(name, 4); // after "func"
        const startPos = document.positionAt(match.index);
        const nameStartPos = document.positionAt(nameStart);
        const nameEndPos = document.positionAt(nameStart + name.length);
        decls.push({
            name,
            range: new vscode.Range(startPos, nameEndPos),
            nameRange: new vscode.Range(nameStartPos, nameEndPos),
        });
    }
    return decls;
}

// ---------------------------------------------------------------------------
// Completion: keywords + the one documented dotted-call (audio.play) +
// func names declared in the current file.
// ---------------------------------------------------------------------------

class HoscCompletionProvider implements vscode.CompletionItemProvider {
    provideCompletionItems(document: vscode.TextDocument): vscode.ProviderResult<vscode.CompletionItem[]> {
        const items: vscode.CompletionItem[] = [];

        for (const kw of KEYWORDS) {
            const item = new vscode.CompletionItem(kw.name, vscode.CompletionItemKind.Keyword);
            item.detail = kw.detail;
            item.documentation = new vscode.MarkdownString(kw.documentation);
            if (kw.snippet) {
                item.insertText = new vscode.SnippetString(kw.snippet);
            }
            items.push(item);
        }

        for (const d of DOTTED_CALLS) {
            const item = new vscode.CompletionItem(d.name, vscode.CompletionItemKind.Method);
            item.detail = d.detail;
            item.documentation = new vscode.MarkdownString(d.documentation);
            if (d.snippet) {
                item.insertText = new vscode.SnippetString(d.snippet);
            }
            items.push(item);
        }

        for (const decl of indexFuncDeclarations(document)) {
            const item = new vscode.CompletionItem(decl.name, vscode.CompletionItemKind.Function);
            item.detail = `func ${decl.name}`;
            items.push(item);
        }

        return items;
    }
}

// ---------------------------------------------------------------------------
// Hover
// ---------------------------------------------------------------------------

class HoscHoverProvider implements vscode.HoverProvider {
    provideHover(document: vscode.TextDocument, position: vscode.Position): vscode.ProviderResult<vscode.Hover> {
        const range = document.getWordRangeAtPosition(position, /[a-zA-Z_][a-zA-Z0-9_]*/);
        if (!range) return undefined;
        const word = document.getText(range);

        const known = ALL_SYMBOLS.find(s => s.name === word);
        if (known) {
            const md = new vscode.MarkdownString();
            md.appendCodeblock(known.detail, 'hosc');
            md.appendMarkdown(known.documentation);
            return new vscode.Hover(md, range);
        }

        const decl = indexFuncDeclarations(document).find(d => d.name === word);
        if (decl) {
            const md = new vscode.MarkdownString();
            md.appendCodeblock(`func ${decl.name}`, 'hosc');
            md.appendMarkdown('User-defined function declared in this file.');
            return new vscode.Hover(md, range);
        }

        return undefined;
    }
}

// ---------------------------------------------------------------------------
// Go to Definition: jump to `func name` (current file, then other .hosc
// files in the workspace).
// ---------------------------------------------------------------------------

class HoscDefinitionProvider implements vscode.DefinitionProvider {
    async provideDefinition(
        document: vscode.TextDocument,
        position: vscode.Position,
        token: vscode.CancellationToken
    ): Promise<vscode.Definition | undefined> {
        const range = document.getWordRangeAtPosition(position, /[a-zA-Z_][a-zA-Z0-9_]*/);
        if (!range) return undefined;
        const word = document.getText(range);
        if (KEYWORD_NAMES.has(word)) return undefined;

        const localDecl = indexFuncDeclarations(document).find(d => d.name === word);
        if (localDecl) {
            return new vscode.Location(document.uri, localDecl.nameRange);
        }

        const files = await vscode.workspace.findFiles('**/*.hosc', '**/node_modules/**', 200);
        for (const uri of files) {
            if (token.isCancellationRequested) return undefined;
            if (uri.toString() === document.uri.toString()) continue;

            let text: string;
            try {
                const bytes = await vscode.workspace.fs.readFile(uri);
                text = Buffer.from(bytes).toString('utf8');
            } catch {
                continue;
            }
            const re = new RegExp(`\\bfunc\\s+(${escapeRegExp(word)})\\b`);
            const m = re.exec(text);
            if (m) {
                const doc = await vscode.workspace.openTextDocument(uri);
                const nameStart = m.index + m[0].indexOf(word, 4);
                const pos = doc.positionAt(nameStart);
                const end = doc.positionAt(nameStart + word.length);
                return new vscode.Location(uri, new vscode.Range(pos, end));
            }
        }
        return undefined;
    }
}

function escapeRegExp(s: string): string {
    return s.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
}

// ---------------------------------------------------------------------------
// Outline: func declarations only (no struct/interface in this grammar).
// ---------------------------------------------------------------------------

class HoscDocumentSymbolProvider implements vscode.DocumentSymbolProvider {
    provideDocumentSymbols(document: vscode.TextDocument): vscode.ProviderResult<vscode.DocumentSymbol[]> {
        return indexFuncDeclarations(document).map(decl => {
            const fullRange = expandToBlockEnd(document, decl.range);
            return new vscode.DocumentSymbol(decl.name, 'func', vscode.SymbolKind.Function, fullRange, decl.nameRange);
        });
    }
}

function expandToBlockEnd(document: vscode.TextDocument, declRange: vscode.Range): vscode.Range {
    const text = document.getText();
    const startOffset = document.offsetAt(declRange.end);
    const braceStart = text.indexOf('{', startOffset);
    if (braceStart === -1) return declRange;
    let depth = 0;
    for (let i = braceStart; i < text.length; i++) {
        if (text[i] === '{') depth++;
        else if (text[i] === '}') {
            depth--;
            if (depth === 0) {
                return new vscode.Range(declRange.start, document.positionAt(i + 1));
            }
        }
    }
    return declRange;
}

// ---------------------------------------------------------------------------
// Semantic highlighting: functions + the one mutable-declaration kind
// worth distinguishing (var vs let/const — since HOSC's mutability rule is
// the reverse of JS, coloring `var` names differently is genuinely useful).
// ---------------------------------------------------------------------------

export const SEMANTIC_TOKEN_TYPES = ['function', 'variable'];
const legend = new vscode.SemanticTokensLegend(SEMANTIC_TOKEN_TYPES);

class HoscSemanticTokensProvider implements vscode.DocumentSemanticTokensProvider {
    provideDocumentSemanticTokens(document: vscode.TextDocument): vscode.ProviderResult<vscode.SemanticTokens> {
        const builder = new vscode.SemanticTokensBuilder(legend);
        const lines = document.getText().split(/\r\n|\r|\n/);

        const funcDeclRe = /\b(func)\s+([a-zA-Z_][a-zA-Z0-9_]*)/g;
        const varDeclRe = /\b(var)\s+([a-zA-Z_][a-zA-Z0-9_]*)/g;
        const callRe = /\b([a-zA-Z_][a-zA-Z0-9_]*)\s*(?=\()/g;
        const controlWords = new Set(['if', 'while', 'for', 'switch', 'func']);

        let inRawString = false;

        for (let lineNum = 0; lineNum < lines.length; lineNum++) {
            const line = lines[lineNum];

            if (inRawString) {
                if (line.includes('`')) inRawString = false;
                continue; // never tokenize raw-string content as code
            }

            const codePart = line.split('//')[0];

            funcDeclRe.lastIndex = 0;
            let m: RegExpExecArray | null;
            while ((m = funcDeclRe.exec(codePart)) !== null) {
                const nameIdx = m.index + m[0].lastIndexOf(m[2]);
                builder.push(new vscode.Range(lineNum, nameIdx, lineNum, nameIdx + m[2].length), 'function');
            }

            varDeclRe.lastIndex = 0;
            while ((m = varDeclRe.exec(codePart)) !== null) {
                const nameIdx = m.index + m[0].lastIndexOf(m[2]);
                builder.push(new vscode.Range(lineNum, nameIdx, lineNum, nameIdx + m[2].length), 'variable');
            }

            callRe.lastIndex = 0;
            while ((m = callRe.exec(codePart)) !== null) {
                const name = m[1];
                if (controlWords.has(name)) continue;
                builder.push(new vscode.Range(lineNum, m.index, lineNum, m.index + name.length), 'function');
            }

            // Enter raw-string mode if this line opens one that doesn't
            // also close on the same line (prints[` ... multi-line ... `];).
            const backtickCount = (codePart.match(/`/g) || []).length;
            if (backtickCount % 2 === 1) {
                inRawString = true;
            }
        }

        return builder.build();
    }
}

// ---------------------------------------------------------------------------
// Diagnostics: real compiler output (`hosc check <file>`), not a guesser.
//
// The previous version of this extension tried to emulate H104/H205 with
// regexes and got both the meaning of the codes AND basic things like
// "semicolons are required" and "prints(...) takes parens" wrong, because
// none of that was ever confirmed against the real grammar. `docs/api.md`
// says `hosc check` is implemented and this is what the project's own
// lsp/src/diagnostics.ts already shells out to, so this does the same
// thing instead of re-inventing it.
// ---------------------------------------------------------------------------

function registerDiagnostics(context: vscode.ExtensionContext): void {
    const collection = vscode.languages.createDiagnosticCollection('hosc');
    const output = vscode.window.createOutputChannel('HOSC');
    context.subscriptions.push(collection, output);

    let warnedNoCompiler = false;
    let debounceTimer: NodeJS.Timeout | undefined;

    const check = async (document: vscode.TextDocument) => {
        if (document.languageId !== 'hosc' || document.uri.scheme !== 'file') return;

        const executable = resolveHoscExecutable(document);
        if (!executable) return;

        const folder = vscode.workspace.getWorkspaceFolder(document.uri);
        const { output: raw, failed } = await runHoscCheck(executable, document.uri.fsPath, folder?.uri.fsPath);

        if (failed) {
            collection.delete(document.uri);
            if (!warnedNoCompiler) {
                warnedNoCompiler = true;
                output.appendLine(
                    `Could not run "${executable} check" (executable not found). ` +
                        `Set "hosc.executablePath" in settings to point at your built hosc(.exe), ` +
                        `or build it via tools/build.ps1. Diagnostics from the real compiler are ` +
                        `disabled until then — this extension does not guess errors client-side.`
                );
            }
            return;
        }

        output.appendLine(`$ ${executable} check ${document.uri.fsPath}`);
        if (raw.trim().length > 0) output.appendLine(raw.trim());

        const hoscDiags = parseHoscDiagnosticsOutput(raw);
        collection.set(document.uri, toVsCodeDiagnostics(document, hoscDiags));
    };

    const scheduleCheck = (document: vscode.TextDocument) => {
        if (document.languageId !== 'hosc') return;
        if (debounceTimer) clearTimeout(debounceTimer);
        debounceTimer = setTimeout(() => check(document), 400);
    };

    vscode.workspace.textDocuments.forEach(doc => check(doc));

    context.subscriptions.push(
        vscode.workspace.onDidOpenTextDocument(check),
        vscode.workspace.onDidSaveTextDocument(check),
        vscode.workspace.onDidChangeTextDocument(e => scheduleCheck(e.document)),
        vscode.workspace.onDidCloseTextDocument(doc => collection.delete(doc.uri))
    );
}

// ---------------------------------------------------------------------------
// Formatter: brace-based reindentation that leaves `prints[` raw-string
// content completely untouched (it's ASCII art — reindenting it corrupts
// the art), since the real `hosc fmt` is currently a stub
// ("formatter not implemented in bootstrap build", per
// docs/troubleshooting.md) and this fills that gap client-side.
// ---------------------------------------------------------------------------

class HoscFormattingProvider implements vscode.DocumentFormattingEditProvider {
    provideDocumentFormattingEdits(
        document: vscode.TextDocument,
        options: vscode.FormattingOptions
    ): vscode.ProviderResult<vscode.TextEdit[]> {
        const indentUnit = options.insertSpaces ? ' '.repeat(options.tabSize) : '\t';
        const lines = document.getText().split(/\r\n|\r|\n/);
        const formatted: string[] = [];
        let depth = 0;
        let inRawString = false;

        for (const rawLine of lines) {
            if (inRawString) {
                // Pass raw-string content through completely unmodified.
                formatted.push(rawLine);
                if (rawLine.includes('`')) inRawString = false;
                continue;
            }

            const trimmed = rawLine.trim();
            if (trimmed.length === 0) {
                formatted.push('');
                continue;
            }

            const startsWithClose = /^[}\])]/.test(trimmed);
            const printDepth = startsWithClose ? Math.max(depth - 1, 0) : depth;
            formatted.push(indentUnit.repeat(printDepth) + trimmed);

            const codeForBraces = trimmed.split('//')[0];
            let delta = 0;
            for (const ch of codeForBraces) {
                if (ch === '{' || ch === '[' || ch === '(') delta++;
                else if (ch === '}' || ch === ']' || ch === ')') delta--;
            }
            depth = Math.max(depth + delta, 0);

            // If this line opened an unterminated backtick raw string
            // (odd number of backticks), everything until the closing
            // backtick is passed through verbatim on the next iterations.
            const backtickCount = (codeForBraces.match(/`/g) || []).length;
            if (backtickCount % 2 === 1) {
                inRawString = true;
            }
        }

        const fullRange = new vscode.Range(document.positionAt(0), document.positionAt(document.getText().length));
        return [vscode.TextEdit.replace(fullRange, formatted.join('\n'))];
    }
}

// ---------------------------------------------------------------------------
// Registration entry point
// ---------------------------------------------------------------------------

export function registerLanguageFeatures(context: vscode.ExtensionContext): void {
    context.subscriptions.push(
        vscode.languages.registerCompletionItemProvider(HOSC_SELECTOR, new HoscCompletionProvider()),
        vscode.languages.registerHoverProvider(HOSC_SELECTOR, new HoscHoverProvider()),
        vscode.languages.registerDefinitionProvider(HOSC_SELECTOR, new HoscDefinitionProvider()),
        vscode.languages.registerDocumentSymbolProvider(HOSC_SELECTOR, new HoscDocumentSymbolProvider()),
        vscode.languages.registerDocumentSemanticTokensProvider(HOSC_SELECTOR, new HoscSemanticTokensProvider(), legend),
        vscode.languages.registerDocumentFormattingEditProvider(HOSC_SELECTOR, new HoscFormattingProvider())
    );

    registerDiagnostics(context);
}
