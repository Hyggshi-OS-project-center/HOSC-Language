/**
 * HOSC language metadata (keywords, known dotted-calls, diagnostic codes)
 * used by the completion, hover, and diagnostics providers.
 *
 * This is sourced directly from the project's own
 * `docs/HOSC_SYNTAX_REFERENCE.md` / `docs/api.md` (v2.0, 2026-07-31),
 * which was rewritten against the actual lexer/parser source. In
 * particular:
 *   - There are exactly 22 keywords. No `struct`, no `interface`, no
 *     `function`, no `println`, no compound assignment, no `null`.
 *   - There is no fixed "built-in function" list like `sleep`/`len`/
 *     `input`/`typeof`/`exit`/`random` — those do not exist in this
 *     grammar. The only documented working dotted-call is `audio.play`.
 *   - `window`/`text` are statement keywords, but using them anywhere in
 *     a file makes `hosc_compile_memory()` reject it with H003 before
 *     parsing even starts (they're for the separate GUI framework runner).
 * If the grammar changes, update this file to match — don't re-guess.
 */

export interface HoscSymbolDoc {
    name: string;
    kind: 'keyword' | 'dotted-call';
    detail: string;
    documentation: string;
    snippet?: string;
}

export const KEYWORDS: HoscSymbolDoc[] = [
    { name: 'package', kind: 'keyword', detail: 'package <name>', documentation: 'Required header. A HOSC file is a `package` declaration followed by top-level statements and `func` declarations.' },
    { name: 'import', kind: 'keyword', detail: 'import "path.hosc";', documentation: 'Imports another module. Only a quoted path is supported at the statement level.', snippet: 'import "${1:module.hosc}";' },
    { name: 'func', kind: 'keyword', detail: 'func name(params) { }', documentation: 'Declares a function. A `func main()` entry point is required in every program.', snippet: 'func ${1:name}(${2:params}) {\n\t$0\n}' },
    { name: 'if', kind: 'keyword', detail: 'if condition { }', documentation: 'Conditional branch. Parentheses around the condition are **not** required (though `(...)` is legal as a grouped expression).', snippet: 'if ${1:condition} {\n\t$0\n}' },
    { name: 'else', kind: 'keyword', detail: 'else { }  /  else if condition { }', documentation: 'Alternate branch for an if statement.' },
    { name: 'while', kind: 'keyword', detail: 'while condition { }', documentation: 'Loop while a condition is true. No parentheses required around the condition.', snippet: 'while ${1:condition} {\n\t$0\n}' },
    { name: 'for', kind: 'keyword', detail: 'for (init; cond; step) { }  or  for cond { }', documentation: 'C-style form requires parens and the init clause must use `var` (so it can be reassigned in the step clause). A bare-condition form with no parens/init/step is also accepted, like `for isRunning { }`.', snippet: 'for (${1:var i = 0}; ${2:i < n}; ${3:i = i + 1}) {\n\t$0\n}' },
    { name: 'switch', kind: 'keyword', detail: 'switch (value) { case X: ... default: ... }', documentation: 'Multi-way branch. No fallthrough keyword — each `case`/`default` body runs until the next `case`/`default`/`}`.', snippet: 'switch (${1:value}) {\n\tcase ${2:1}:\n\t\t$0\n\tdefault:\n\t\tbreak;\n}' },
    { name: 'case', kind: 'keyword', detail: 'case value:', documentation: 'A branch inside a switch statement.' },
    { name: 'default', kind: 'keyword', detail: 'default:', documentation: 'The fallback branch inside a switch statement.' },
    { name: 'return', kind: 'keyword', detail: 'return [expr];', documentation: 'Returns from the current function. The value is optional (`return;` is valid).' },
    { name: 'break', kind: 'keyword', detail: 'break;', documentation: 'Exits the nearest loop or switch.' },
    { name: 'continue', kind: 'keyword', detail: 'continue;', documentation: 'Skips to the next iteration of the nearest loop.' },
    { name: 'var', kind: 'keyword', detail: 'var name = value;', documentation: "Declares the only mutable binding — `var` is the sole kind of declaration that can be reassigned. (Note: this is the reverse of the let/var convention in JavaScript.)" },
    { name: 'let', kind: 'keyword', detail: 'let name = value;', documentation: 'Declares an immutable binding. Reassigning a `let` produces diagnostic H203, same as reassigning a `const`.' },
    { name: 'const', kind: 'keyword', detail: 'const name = value;', documentation: 'Declares a constant. Functionally identical to `let` for reassignment purposes (both produce H203 if reassigned).' },
    { name: 'window', kind: 'keyword', detail: 'window("Title");', documentation: 'Framework GUI statement keyword. Using it anywhere in a file makes the compiler reject the whole file with H003 (framework-script detection runs before parsing). Run framework scripts through `hosc_framework`, not `hosc run`.' },
    { name: 'text', kind: 'keyword', detail: 'text(x, y, "Label");', documentation: 'Framework GUI statement keyword — same H003 framework-script detection as `window` applies.' },
    { name: 'print', kind: 'keyword', detail: 'print expr;', documentation: 'Prints an expression with a trailing newline. Parentheses are optional: `print "hi";` and `print("hi");` are both valid.', snippet: 'print ${1:expression};' },
    { name: 'prints', kind: 'keyword', detail: 'prints[`literal text`];', documentation: "Prints a literal, raw (backtick-delimited) string verbatim — the only construct that can span multiple lines, so it's used for ASCII art. Must be exactly `prints[` + backtick raw string + `]`; anything else right after the closing backtick is diagnostic H104.", snippet: 'prints[`\n$0\n`];' },
    { name: 'true', kind: 'keyword', detail: 'boolean literal', documentation: 'The boolean literal true.' },
    { name: 'false', kind: 'keyword', detail: 'boolean literal', documentation: 'The boolean literal false.' },
];

// The only dotted-call built-in documented as actually working through the
// normal compile pipeline (it parses as a plain identifier + call, not a
// keyword). `window(...)`/`text(...)` are NOT here — see the KEYWORDS
// entries above, since they take the framework-rejection path instead.
export const DOTTED_CALLS: HoscSymbolDoc[] = [
    {
        name: 'audio.play',
        kind: 'dotted-call',
        detail: 'audio.play("file.mp3")  or  audio.play({ file: "...", loop: true })',
        documentation: 'Plays an audio file. `audio` is an ordinary identifier (not a keyword), so this is a regular dotted function call, not framework-rejected like `window`/`text`.',
        snippet: 'audio.play(${1:"file.mp3"})',
    },
];

export const ALL_SYMBOLS: HoscSymbolDoc[] = [...KEYWORDS, ...DOTTED_CALLS];

export const KEYWORD_NAMES = new Set(KEYWORDS.map(k => k.name));

/**
 * The real compiler's diagnostic codes (from `docs/api.md`, produced by
 * `hosc check <file>` and `hosc build`/`hosc run`). This extension's
 * diagnostics provider shells out to the real `hosc` executable and
 * parses this output rather than re-guessing errors client-side — see
 * languageFeatures.ts for why.
 */
export const DIAGNOSTIC_CODES: Record<string, string> = {
    H000: 'File read failure, or import resolution failure',
    H001: 'Missing func main() entry point',
    H002: 'Syntax parse error (generic/unclassified)',
    H003: 'Framework GUI script detected — run via hosc_framework, not hosc run',
    H104: 'Unknown keyword or keyword typo (or a malformed prints[...] closing token)',
    H205: 'Unknown function or identifier call',
    H900: 'Bytecode emission failure (e.g. the % operator currently fails to compile)',
};
