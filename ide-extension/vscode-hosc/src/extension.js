const vscode = require('vscode');
const cp = require('child_process');
const path = require('path');
const fs = require('fs');

let runTerminal;

const FALLBACK_KEYWORDS = [
  'package', 'import', 'func', 'var', 'let', 'if', 'else', 'while', 'for', 'return', 'print'
];

const FALLBACK_BUILTINS = [
  { name: 'window', detail: 'window(title)' },
  { name: 'text', detail: 'text(x, y, message)' },
  { name: 'loop', detail: 'loop()' },
  { name: 'button', detail: 'button(label)' },
  { name: 'input', detail: 'input(x, y, w, id)' },
  { name: 'warn', detail: 'warn(message)' }
];

function normalizeCliPath(rawValue) {
  const value = String(rawValue || 'hosc').trim();
  if (
    (value.startsWith('"') && value.endsWith('"')) ||
    (value.startsWith("'") && value.endsWith("'"))
  ) {
    return value.slice(1, -1).trim();
  }
  return value;
}

function getCwd() {
  return vscode.workspace.workspaceFolders?.[0]?.uri.fsPath;
}

function fileExists(filePath) {
  try {
    return fs.statSync(filePath).isFile();
  } catch {
    return false;
  }
}

function resolveCliPath(configValue) {
  const cwd = getCwd();
  const cli = normalizeCliPath(configValue);

  if (path.isAbsolute(cli)) {
    return cli;
  }

  if ((cli.includes('/') || cli.includes('\\')) && cwd) {
    return path.resolve(cwd, cli);
  }

  if (cli.toLowerCase() === 'hosc' && cwd) {
    const localCli = path.join(cwd, 'tools', 'bin', process.platform === 'win32' ? 'hosc.exe' : 'hosc');
    if (fileExists(localCli)) {
      return localCli;
    }
  }

  return cli;
}

function getCliPath() {
  const raw = vscode.workspace.getConfiguration('hosc').get('cliPath', 'hosc');
  return resolveCliPath(raw);
}

function quoteArg(value) {
  const str = String(value);
  if (process.platform === 'win32') {
    return `"${str.replace(/"/g, '""')}"`;
  }
  return `'${str.replace(/'/g, "'\\''")}'`;
}

function getRunTerminal() {
  if (!runTerminal || runTerminal.exitStatus !== undefined) {
    runTerminal = vscode.window.createTerminal('HOSC');
  }
  return runTerminal;
}

function runCli(args, cwd) {
  return new Promise((resolve) => {
    const cliPath = getCliPath();
    cp.execFile(cliPath, args, { cwd }, (error, stdout, stderr) => {
      let errText = stderr || '';
      if (error && error.code === 'ENOENT') {
        errText = `${errText}\nHOSC CLI not found: ${cliPath}\nSet setting \"hosc.cliPath\" or build ./tools/build.ps1.`.trim();
      }
      resolve({ error, stdout: stdout || '', stderr: errText, cliPath, args });
    });
  });
}

function parseDiagnostics(text, doc) {
  const items = [];
  const lines = text.split(/\r?\n/);
  for (const line of lines) {
    if (!line) continue;
    const m = line.match(/(.+):(\d+):(\d+):\s*(error|warning):\s*(.+)$/i);
    if (!m) continue;

    const [, filePath, l, c, severityText, message] = m;

    if (doc && filePath) {
      const normalizedFilePath = path.resolve(filePath);
      const normalizedDocPath = doc.uri.fsPath;
      if (normalizedFilePath !== normalizedDocPath && !normalizedDocPath.endsWith(filePath)) {
        continue;
      }
    }

    const lineNum = Math.max(0, Number(l) - 1);
    const colNum = Math.max(0, Number(c) - 1);
    const range = new vscode.Range(lineNum, colNum, lineNum, colNum + 1);
    const severity = severityText.toLowerCase() === 'warning'
      ? vscode.DiagnosticSeverity.Warning
      : vscode.DiagnosticSeverity.Error;

    items.push(new vscode.Diagnostic(range, message, severity));
  }
  return items;
}

function computeFallbackDiagnostics(doc) {
  const items = [];
  const lines = doc.getText().split(/\r?\n/);
  const bracketStack = [];
  let firstNonEmptyLine = -1;
  let packageCount = 0;

  const push = (line, start, end, message, severity = vscode.DiagnosticSeverity.Error) => {
    const s = Math.max(0, start);
    const e = Math.max(s + 1, end);
    const range = new vscode.Range(line, s, line, e);
    items.push(new vscode.Diagnostic(range, message, severity));
  };

  const stripLineComment = (line) => {
    let inString = false;
    for (let i = 0; i < line.length; i++) {
      const ch = line[i];
      const next = i + 1 < line.length ? line[i + 1] : '';
      if (ch === '"' && line[i - 1] !== '\\') {
        inString = !inString;
        continue;
      }
      if (!inString && ch === '/' && next === '/') {
        return line.slice(0, i);
      }
    }
    return line;
  };

  for (let lineIndex = 0; lineIndex < lines.length; lineIndex++) {
    const rawLine = lines[lineIndex];
    const content = stripLineComment(rawLine).trim();
    if (!content) continue;

    if (firstNonEmptyLine === -1) firstNonEmptyLine = lineIndex;

    if (content.includes(':=')) {
      const col = rawLine.indexOf(':=');
      push(lineIndex, col, col + 2, 'Unsupported operator ":="');
    }

    if (/^\s*package\b/.test(rawLine)) {
      packageCount++;
      const firstPkg = rawLine.indexOf('package');
      const tokens = content.split(/\s+/);

      if (tokens.length < 2) {
        push(lineIndex, firstPkg, firstPkg + 7, 'Expected package name after "package"');
      } else if (tokens[1] === 'package') {
        const secondPkg = rawLine.indexOf('package', firstPkg + 1);
        push(lineIndex, Math.max(0, secondPkg), Math.max(1, secondPkg + 7), 'Unexpected "package" keyword');
      } else if (tokens.length > 2) {
        const t = tokens[2];
        const at = rawLine.indexOf(t);
        push(lineIndex, Math.max(0, at), Math.max(1, at + t.length), `Unexpected token "${t}" in package declaration`);
      }

      if (packageCount > 1) {
        push(lineIndex, Math.max(0, firstPkg), Math.max(1, firstPkg + 7), 'Duplicate package declaration');
      }
    }

    let inString = false;
    for (let i = 0; i < rawLine.length; i++) {
      const ch = rawLine[i];
      const next = i + 1 < rawLine.length ? rawLine[i + 1] : '';

      if (!inString && ch === '/' && next === '/') break;
      if (ch === '"' && rawLine[i - 1] !== '\\') {
        inString = !inString;
        continue;
      }
      if (inString) continue;

      if (ch === '{' || ch === '(' || ch === '[') {
        bracketStack.push({ ch, line: lineIndex, col: i });
      } else if (ch === '}' || ch === ')' || ch === ']') {
        const open = bracketStack.length > 0 ? bracketStack[bracketStack.length - 1] : null;
        const expectedOpen = ch === '}' ? '{' : (ch === ')' ? '(' : '[');
        if (!open || open.ch !== expectedOpen) {
          push(lineIndex, i, i + 1, `Unexpected "${ch}"`);
        } else {
          bracketStack.pop();
        }
      }
    }

    if (inString) {
      push(lineIndex, Math.max(0, rawLine.length - 1), rawLine.length, 'Unclosed string literal');
    }
  }

  if (packageCount === 0 && firstNonEmptyLine >= 0) {
    push(firstNonEmptyLine, 0, 1, 'Missing package declaration (expected "package main")', vscode.DiagnosticSeverity.Warning);
  } else if (firstNonEmptyLine >= 0) {
    const firstContent = stripLineComment(lines[firstNonEmptyLine]).trim();
    if (!firstContent.startsWith('package')) {
      push(firstNonEmptyLine, 0, 1, 'Package declaration should be the first statement', vscode.DiagnosticSeverity.Warning);
    }
  }

  const closeFor = { '{': '}', '(': ')', '[': ']' };
  for (const unclosed of bracketStack) {
    push(unclosed.line, unclosed.col, unclosed.col + 1, `Unclosed "${unclosed.ch}", expected "${closeFor[unclosed.ch]}"`);
  }

  return items;
}

function resolveTarget(uri) {
  if (uri && uri.fsPath) {
    if (path.extname(uri.fsPath).toLowerCase() !== '.hosc') {
      vscode.window.showWarningMessage('Selected file is not a .hosc file');
      return null;
    }
    return { filePath: uri.fsPath, doc: undefined };
  }

  const editor = vscode.window.activeTextEditor;
  if (!editor) {
    vscode.window.showWarningMessage('No active editor');
    return null;
  }

  const doc = editor.document;
  if (doc.languageId !== 'hosc') {
    vscode.window.showWarningMessage('Active file is not a .hosc file');
    return null;
  }

  return { filePath: doc.uri.fsPath, doc };
}

function summarizeError(result) {
  const text = (result.stderr || result.error?.message || '').trim();
  if (!text) return 'unknown error';
  return text.split(/\r?\n/)[0];
}

async function checkDocument(doc, diagnosticCollection, output) {
  if (!doc || doc.languageId !== 'hosc') return;

  const result = await runCli(['check', doc.uri.fsPath], getCwd());
  const combined = [result.stdout, result.stderr].filter(Boolean).join('\n');
  const parsed = parseDiagnostics(combined, doc);

  if (result.error) {
    if (parsed.length > 0) {
      diagnosticCollection.set(doc.uri, parsed);
    } else {
      const fallback = computeFallbackDiagnostics(doc);
      if (fallback.length > 0) {
        diagnosticCollection.set(doc.uri, fallback);
      } else {
        const range = new vscode.Range(0, 0, 0, 1);
        diagnosticCollection.set(doc.uri, [
          new vscode.Diagnostic(range, combined || 'hosc check failed', vscode.DiagnosticSeverity.Error)
        ]);
      }
    }
    output.appendLine(`[check] ${doc.uri.fsPath}`);
    if (combined.trim()) output.appendLine(combined.trim());
    return;
  }

  diagnosticCollection.delete(doc.uri);
}

async function checkTarget(uri, diagnosticCollection, output) {
  const target = resolveTarget(uri);
  if (!target) return;

  if (target.doc) {
    if (target.doc.isDirty) {
      await target.doc.save();
    }
    await checkDocument(target.doc, diagnosticCollection, output);
    return;
  }

  const result = await runCli(['check', target.filePath], getCwd());
  const combined = [result.stdout, result.stderr].filter(Boolean).join('\n');
  output.show(true);
  output.appendLine(`[check] ${target.filePath}`);
  if (combined.trim()) output.appendLine(combined.trim());
  if (result.error) {
    vscode.window.showErrorMessage(`hosc check failed: ${summarizeError(result)}`);
  }
}

async function runTargetFile(command, uri, argsBuilder, output) {
  const target = resolveTarget(uri);
  if (!target) return;

  if (target.doc && target.doc.isDirty) {
    await target.doc.save();
  }

  const args = argsBuilder(target.filePath);
  const result = await runCli(args, getCwd());

  output.show(true);
  output.appendLine(`[${command}] ${quoteArg(result.cliPath)} ${args.map(quoteArg).join(' ')}`);
  if (result.stdout.trim()) output.appendLine(result.stdout.trim());
  if (result.stderr.trim()) output.appendLine(result.stderr.trim());

  if (result.error) {
    vscode.window.showErrorMessage(`hosc ${command} failed: ${summarizeError(result)}`);
  }
}

async function runTargetFileInTerminal(uri) {
  const target = resolveTarget(uri);
  if (!target) return;

  if (target.doc && target.doc.isDirty) {
    await target.doc.save();
  }

  const terminal = getRunTerminal();
  const cliPath = getCliPath();
  terminal.show(true);

  if (process.platform === 'win32') {
    terminal.sendText(`& ${quoteArg(cliPath)} run ${quoteArg(target.filePath)}`);
  } else {
    terminal.sendText(`${quoteArg(cliPath)} run ${quoteArg(target.filePath)}`);
  }
}

function registerFallbackIntelliSense(context) {
  const completionProvider = vscode.languages.registerCompletionItemProvider(
    { language: 'hosc' },
    {
      provideCompletionItems() {
        const keywordItems = FALLBACK_KEYWORDS.map((keyword) => {
          const item = new vscode.CompletionItem(keyword, vscode.CompletionItemKind.Keyword);
          return item;
        });

        const builtinItems = FALLBACK_BUILTINS.map((builtin) => {
          const item = new vscode.CompletionItem(builtin.name, vscode.CompletionItemKind.Function);
          item.detail = builtin.detail;
          return item;
        });

        return [...keywordItems, ...builtinItems];
      }
    },
    '.',
    '('
  );

  const hoverProvider = vscode.languages.registerHoverProvider({ language: 'hosc' }, {
    provideHover(document, position) {
      const range = document.getWordRangeAtPosition(position);
      if (!range) return null;
      const word = document.getText(range);
      const builtin = FALLBACK_BUILTINS.find((b) => b.name === word);
      if (!builtin) return null;
      return new vscode.Hover(`**${builtin.name}**\n\n${builtin.detail}`);
    }
  });

  context.subscriptions.push(completionProvider, hoverProvider);
}

function startLanguageClient(context, output) {
  const enabled = vscode.workspace.getConfiguration('hosc').get('lsp.enabled', true);
  if (!enabled) {
    output.appendLine('[lsp] disabled by setting hosc.lsp.enabled=false');
    return null;
  }

  let LanguageClient;
  let TransportKind;
  try {
    ({ LanguageClient, TransportKind } = require('vscode-languageclient/node'));
  } catch {
    output.appendLine('[lsp] vscode-languageclient not installed. Run: npm install in ide-extension/vscode-hosc');
    return null;
  }

  const serverModule = path.join(context.extensionPath, 'src', 'lsp', 'server.js');
  const serverOptions = {
    run: { module: serverModule, transport: TransportKind.ipc },
    debug: { module: serverModule, transport: TransportKind.ipc, options: { execArgv: ['--nolazy', '--inspect=6011'] } }
  };

  const clientOptions = {
    documentSelector: [{ scheme: 'file', language: 'hosc' }],
    synchronize: {
      fileEvents: vscode.workspace.createFileSystemWatcher('**/*.hosc')
    }
  };

  const client = new LanguageClient('hoscLanguageServer', 'HOSC Language Server', serverOptions, clientOptions);
  context.subscriptions.push(client.start());
  output.appendLine('[lsp] started');
  return client;
}

class HoscDebugConfigurationProvider {
  provideDebugConfigurations() {
    return [
      {
        type: 'hosc',
        request: 'launch',
        name: 'Debug Current HOSC File',
        program: '${file}',
        cwd: '${workspaceFolder}',
        stopOnEntry: false
      }
    ];
  }

  resolveDebugConfiguration(folder, config) {
    const resolved = { ...config };

    if (!resolved.type) resolved.type = 'hosc';
    if (!resolved.request) resolved.request = 'launch';
    if (!resolved.name) resolved.name = 'Debug HOSC File';

    if (!resolved.program) {
      const editor = vscode.window.activeTextEditor;
      if (editor && editor.document.languageId === 'hosc') {
        resolved.program = editor.document.uri.fsPath;
      }
    }

    if (!resolved.program) {
      vscode.window.showErrorMessage('No HOSC file selected for debugging');
      return undefined;
    }

    resolved.cliPath = resolved.cliPath || getCliPath();
    resolved.cwd = resolved.cwd || folder?.uri?.fsPath || getCwd();
    return resolved;
  }
}

class HoscDebugAdapterDescriptorFactory {
  constructor(context) {
    this.context = context;
  }

  createDebugAdapterDescriptor() {
    const adapterPath = path.join(this.context.extensionPath, 'src', 'hoscDebugAdapter.js');
    return new vscode.DebugAdapterExecutable(process.execPath, [adapterPath]);
  }
}

async function debugTargetFile(uri) {
  const target = resolveTarget(uri);
  if (!target) return;

  if (target.doc && target.doc.isDirty) {
    await target.doc.save();
  }

  const folder = vscode.workspace.getWorkspaceFolder(vscode.Uri.file(target.filePath));
  await vscode.debug.startDebugging(folder, {
    type: 'hosc',
    request: 'launch',
    name: `Debug ${path.basename(target.filePath)}`,
    program: target.filePath,
    cwd: folder?.uri?.fsPath || getCwd(),
    cliPath: getCliPath(),
    stopOnEntry: vscode.workspace.getConfiguration('hosc').get('debug.stopOnEntry', false)
  });
}

function activate(context) {
  const output = vscode.window.createOutputChannel('HOSC');
  const diagnostics = vscode.languages.createDiagnosticCollection('hosc');

  context.subscriptions.push(output, diagnostics);

  registerFallbackIntelliSense(context);
  startLanguageClient(context, output);

  const debugConfigProvider = new HoscDebugConfigurationProvider();
  const debugAdapterFactory = new HoscDebugAdapterDescriptorFactory(context);
  context.subscriptions.push(vscode.debug.registerDebugConfigurationProvider('hosc', debugConfigProvider));
  context.subscriptions.push(vscode.debug.registerDebugAdapterDescriptorFactory('hosc', debugAdapterFactory));

  context.subscriptions.push(vscode.commands.registerCommand('hosc.runFile', async (uri) => {
    await runTargetFileInTerminal(uri);
  }));

  context.subscriptions.push(vscode.commands.registerCommand('hosc.run', async (uri) => {
    await runTargetFile('run', uri, (file) => ['run', file], output);
  }));

  context.subscriptions.push(vscode.commands.registerCommand('hosc.build', async (uri) => {
    await runTargetFile('build', uri, (file) => ['build', file], output);
  }));

  context.subscriptions.push(vscode.commands.registerCommand('hosc.check', async (uri) => {
    await checkTarget(uri, diagnostics, output);
  }));

  context.subscriptions.push(vscode.commands.registerCommand('hosc.debugFile', async (uri) => {
    await debugTargetFile(uri);
  }));

  context.subscriptions.push(vscode.workspace.onDidSaveTextDocument(async (doc) => {
    const runOnSave = vscode.workspace.getConfiguration('hosc').get('runOnSaveCheck', true);
    if (!runOnSave) return;
    await checkDocument(doc, diagnostics, output);
  }));

  context.subscriptions.push(vscode.workspace.onDidOpenTextDocument(async (doc) => {
    if (doc.languageId === 'hosc') {
      await checkDocument(doc, diagnostics, output);
    }
  }));
}

function deactivate() {}

module.exports = {
  activate,
  deactivate,
};