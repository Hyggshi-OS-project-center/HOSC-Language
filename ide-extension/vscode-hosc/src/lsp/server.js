const {
  createConnection,
  TextDocuments,
  ProposedFeatures,
  CompletionItemKind,
  DiagnosticSeverity,
  TextDocumentSyncKind,
  SymbolKind,
  Location
} = require('vscode-languageserver/node');
const { TextDocument } = require('vscode-languageserver-textdocument');

const connection = createConnection(ProposedFeatures.all);
const documents = new TextDocuments(TextDocument);

const KEYWORDS = [
  'package', 'import', 'func', 'var', 'let', 'if', 'else', 'while', 'for', 'return', 'print'
];

const BUILTINS = {
  window: {
    signature: 'window(title)',
    docs: 'Create a window with title.'
  },
  text: {
    signature: 'text(x, y, message)',
    docs: 'Draw text at coordinates.'
  },
  loop: {
    signature: 'loop()',
    docs: 'Start GUI event/render loop.'
  },
  button: {
    signature: 'button(label)',
    docs: 'Returns true when button is clicked.'
  },
  input: {
    signature: 'input(x, y, w, id)',
    docs: 'Draw and manage input widget.'
  },
  warn: {
    signature: 'warn(message)',
    docs: 'Show warning message.'
  },
  print: {
    signature: 'print(value)',
    docs: 'Print a value to output.'
  }
};

function getWordAt(text, offset) {
  let start = offset;
  let end = offset;
  while (start > 0 && /[A-Za-z0-9_]/.test(text[start - 1])) start--;
  while (end < text.length && /[A-Za-z0-9_]/.test(text[end])) end++;
  if (start === end) return '';
  return text.slice(start, end);
}

function pushDiagnostic(diagnostics, line, start, end, message, severity = DiagnosticSeverity.Error) {
  const safeStart = Math.max(0, start);
  const safeEnd = Math.max(safeStart + 1, end);
  diagnostics.push({
    severity,
    range: {
      start: { line, character: safeStart },
      end: { line, character: safeEnd }
    },
    message,
    source: 'hosc-lsp'
  });
}

function stripLineComment(line) {
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
}

function firstTokenIndex(line, token) {
  const r = new RegExp(`\\b${token}\\b`);
  const m = line.match(r);
  return m ? m.index : 0;
}

function scanBracketErrors(text, diagnostics) {
  const stack = [];
  const lines = text.split(/\r?\n/);
  const openToClose = { '{': '}', '(': ')', '[': ']' };
  const closeToOpen = { '}': '{', ')': '(', ']': '[' };

  for (let lineIndex = 0; lineIndex < lines.length; lineIndex++) {
    const line = lines[lineIndex];
    let inString = false;

    for (let i = 0; i < line.length; i++) {
      const ch = line[i];
      const next = i + 1 < line.length ? line[i + 1] : '';

      if (!inString && ch === '/' && next === '/') {
        break;
      }

      if (ch === '"' && line[i - 1] !== '\\') {
        inString = !inString;
        continue;
      }
      if (inString) continue;

      if (openToClose[ch]) {
        stack.push({ ch, line: lineIndex, character: i });
        continue;
      }

      if (closeToOpen[ch]) {
        const top = stack.length > 0 ? stack[stack.length - 1] : null;
        if (!top || top.ch !== closeToOpen[ch]) {
          pushDiagnostic(diagnostics, lineIndex, i, i + 1, `Unexpected "${ch}"`);
        } else {
          stack.pop();
        }
      }
    }

    if (inString) {
      pushDiagnostic(diagnostics, lineIndex, Math.max(0, line.length - 1), line.length, 'Unclosed string literal');
    }
  }

  for (const unclosed of stack) {
    const expected = openToClose[unclosed.ch];
    pushDiagnostic(
      diagnostics,
      unclosed.line,
      unclosed.character,
      unclosed.character + 1,
      `Unclosed "${unclosed.ch}", expected "${expected}"`
    );
  }
}

function computeDiagnostics(document) {
  const text = document.getText();
  const diagnostics = [];
  const lines = text.split(/\r?\n/);
  let packageCount = 0;
  let firstNonEmptyLine = -1;

  for (let i = 0; i < lines.length; i++) {
    const raw = lines[i];
    const content = stripLineComment(raw).trim();
    if (!content) continue;

    if (firstNonEmptyLine === -1) firstNonEmptyLine = i;

    if (content.includes(':=')) {
      const col = raw.indexOf(':=');
      pushDiagnostic(diagnostics, i, col, col + 2, 'Unsupported operator ":="');
    }

    if (/^\s*package\b/.test(raw)) {
      packageCount++;
      const pkgIndex = firstTokenIndex(raw, 'package');
      const tokens = content.split(/\s+/);

      if (tokens.length < 2) {
        pushDiagnostic(diagnostics, i, pkgIndex, pkgIndex + 'package'.length, 'Expected package name after "package"');
        continue;
      }

      if (tokens[1] === 'package') {
        const secondIndex = raw.indexOf('package', pkgIndex + 1);
        pushDiagnostic(
          diagnostics,
          i,
          Math.max(0, secondIndex),
          Math.max(1, secondIndex + 'package'.length),
          'Unexpected "package" keyword'
        );
      }

      if (tokens.length > 2) {
        const unexpected = tokens[2];
        const tokenIndex = raw.indexOf(unexpected);
        pushDiagnostic(
          diagnostics,
          i,
          Math.max(0, tokenIndex),
          Math.max(1, tokenIndex + unexpected.length),
          `Unexpected token "${unexpected}" in package declaration`
        );
      }

      if (packageCount > 1) {
        pushDiagnostic(diagnostics, i, pkgIndex, pkgIndex + 'package'.length, 'Duplicate package declaration');
      }
    }
  }

  if (packageCount === 0 && firstNonEmptyLine >= 0) {
    pushDiagnostic(
      diagnostics,
      firstNonEmptyLine,
      0,
      1,
      'Missing package declaration (expected "package main")',
      DiagnosticSeverity.Warning
    );
  } else if (firstNonEmptyLine >= 0) {
    const firstContent = stripLineComment(lines[firstNonEmptyLine]).trim();
    if (!firstContent.startsWith('package')) {
      pushDiagnostic(
        diagnostics,
        firstNonEmptyLine,
        0,
        1,
        'Package declaration should be the first statement',
        DiagnosticSeverity.Warning
      );
    }
  }

  scanBracketErrors(text, diagnostics);

  return diagnostics;
}

function validateTextDocument(document) {
  const diagnostics = computeDiagnostics(document);
  connection.sendDiagnostics({ uri: document.uri, diagnostics });
}

connection.onInitialize(() => {
  return {
    capabilities: {
      textDocumentSync: TextDocumentSyncKind.Incremental,
      completionProvider: {
        resolveProvider: false,
        triggerCharacters: ['.', '(']
      },
      hoverProvider: true,
      signatureHelpProvider: {
        triggerCharacters: ['(']
      },
      documentSymbolProvider: true,
      definitionProvider: true
    }
  };
});

connection.onCompletion(() => {
  const keywordItems = KEYWORDS.map((keyword) => ({
    label: keyword,
    kind: CompletionItemKind.Keyword
  }));

  const builtinItems = Object.entries(BUILTINS).map(([name, info]) => ({
    label: name,
    kind: CompletionItemKind.Function,
    detail: info.signature,
    documentation: info.docs
  }));

  return [...keywordItems, ...builtinItems];
});

connection.onHover((params) => {
  const doc = documents.get(params.textDocument.uri);
  if (!doc) return null;
  const offset = doc.offsetAt(params.position);
  const word = getWordAt(doc.getText(), offset);

  if (BUILTINS[word]) {
    return {
      contents: {
        kind: 'markdown',
        value: `**${word}**\n\n\`${BUILTINS[word].signature}\`\n\n${BUILTINS[word].docs}`
      }
    };
  }

  if (KEYWORDS.includes(word)) {
    return {
      contents: {
        kind: 'markdown',
        value: `keyword: **${word}**`
      }
    };
  }

  return null;
});

connection.onSignatureHelp((params) => {
  const doc = documents.get(params.textDocument.uri);
  if (!doc) return null;

  const lineText = doc.getText({
    start: { line: params.position.line, character: 0 },
    end: params.position
  });

  const match = lineText.match(/([A-Za-z_][A-Za-z0-9_]*)\s*\([^()]*$/);
  if (!match) return null;

  const fn = match[1];
  if (!BUILTINS[fn]) return null;

  return {
    signatures: [
      {
        label: BUILTINS[fn].signature,
        documentation: BUILTINS[fn].docs
      }
    ],
    activeSignature: 0,
    activeParameter: 0
  };
});

connection.onDocumentSymbol((params) => {
  const doc = documents.get(params.textDocument.uri);
  if (!doc) return [];
  const text = doc.getText();

  const symbols = [];
  const funcRegex = /\bfunc\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(/g;
  const varRegex = /\b(?:var|let)\s+([A-Za-z_][A-Za-z0-9_]*)/g;

  let match;
  while ((match = funcRegex.exec(text))) {
    const start = doc.positionAt(match.index);
    const end = doc.positionAt(match.index + match[0].length);
    symbols.push({
      name: match[1],
      kind: SymbolKind.Function,
      location: { uri: doc.uri, range: { start, end } }
    });
  }

  while ((match = varRegex.exec(text))) {
    const start = doc.positionAt(match.index);
    const end = doc.positionAt(match.index + match[0].length);
    symbols.push({
      name: match[1],
      kind: SymbolKind.Variable,
      location: { uri: doc.uri, range: { start, end } }
    });
  }

  return symbols;
});

connection.onDefinition((params) => {
  const doc = documents.get(params.textDocument.uri);
  if (!doc) return null;

  const offset = doc.offsetAt(params.position);
  const word = getWordAt(doc.getText(), offset);
  if (!word) return null;

  const regex = new RegExp(`\\bfunc\\s+${word}\\s*\\(`, 'g');
  const text = doc.getText();
  const match = regex.exec(text);
  if (!match) return null;

  const start = doc.positionAt(match.index);
  const end = doc.positionAt(match.index + match[0].length);
  return Location.create(doc.uri, { start, end });
});

documents.onDidOpen((e) => validateTextDocument(e.document));
documents.onDidChangeContent((e) => validateTextDocument(e.document));
documents.onDidClose((e) => {
  connection.sendDiagnostics({ uri: e.document.uri, diagnostics: [] });
});

documents.listen(connection);
connection.listen();
