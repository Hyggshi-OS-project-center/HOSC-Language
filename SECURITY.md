# Security Policy

HOSC is a bootstrap-stage compiler, VM, runtime, framework, and editor tooling
repository. Security reports are welcome, especially for memory safety issues in
native code and unsafe handling of untrusted source or bytecode.

## Supported Versions

There is no stable public release line in this workspace snapshot. Report
vulnerabilities against the current default development branch unless a release
branch is explicitly published.

## Scope

In scope:

- Compiler crashes or memory-safety bugs triggered by `.hosc` source.
- VM crashes, stack corruption, object lifetime issues, or bytecode loader bugs.
- Runtime embedding API issues.
- Framework runtime crashes from malformed framework scripts or assets.
- CI or build script behavior that could execute unintended commands.
- VS Code extension or LSP behavior that mishandles workspace files.

Out of scope:

- Running untrusted `.hbc` bytecode in a privileged production environment.
- Issues in third-party tools such as MinGW, Node.js, npm packages, or VS Code.
- Social engineering or account compromise unrelated to repository code.

## Reporting

Do not open a public issue for suspected vulnerabilities. Send a private report
to the maintainer contact listed by the repository owner, or use GitHub private
vulnerability reporting if enabled.

Include:

- Affected component
- Reproduction steps
- Minimal source or bytecode sample when possible
- Expected and actual behavior
- Crash logs, sanitizer output, or debugger notes
- Suggested severity and impact

## Handling Expectations

- Critical memory-safety or code-execution issues should be triaged first.
- Denial-of-service issues in compiler, VM, or LSP should include a minimal
  reproducer.
- Documentation-only security issues can be handled through normal PRs unless
  they expose sensitive information.

## Hardening Backlog

- Add sanitizer-enabled native test jobs.
- Fuzz lexer, parser, bytecode loader, and VM dispatch.
- Add bytecode validation before execution.
- Avoid checking generated binaries into public release branches unless there is
  a signed release process.
