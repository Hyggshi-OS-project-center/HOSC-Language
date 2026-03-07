import re
import sys
import subprocess
from pathlib import Path

from PyQt5.QtCore import Qt, QStringListModel
from PyQt5.QtGui import QFont, QKeySequence, QTextCharFormat, QTextCursor, QColor
from PyQt5.QtWidgets import (
    QApplication,
    QMainWindow,
    QFileDialog,
    QTextEdit,
    QTabWidget,
    QSplitter,
    QListWidget,
    QAction,
    QMessageBox,
    QAbstractItemView,
    QShortcut,
    QCompleter,
    QInputDialog,
)


DEFAULT_TEMPLATE = """package main

func main() {
    var x = 10
    print(x + 20)
}
"""

BASE_COMPLETIONS = [
    "package",
    "func",
    "var",
    "let",
    "if",
    "else",
    "while",
    "for",
    "return",
    "print",
    "true",
    "false",
    "break",
    "continue",
    "window",
    "menu_notepad",
    "button",
    "input",
    "textarea",
    "textarea_set",
    "label",
    "text",
    "color",
    "font",
    "clear",
    "loop",
    "scroll_range",
    "scroll_y",
    "open_file_dialog",
    "save_file_dialog",
    "file_read",
    "file_read_line",
    "file_write",
    "exec",
    "nl",
    "mouse_x",
    "mouse_y",
    "mouse_down",
    "mouse_click",
    "key_down",
    "delta",
    "layout_reset",
    "layout_next",
]


class CodeEditor(QTextEdit):
    def __init__(self, completion_words):
        super().__init__()
        self.file_path = None
        self.error_lines = []

        self.setAcceptRichText(False)
        self.setFont(QFont("Consolas", 11))

        self.completer_model = QStringListModel(self)
        self.completer = QCompleter(self.completer_model, self)
        self.completer.setWidget(self)
        self.completer.setCaseSensitivity(Qt.CaseInsensitive)
        self.completer.setFilterMode(Qt.MatchStartsWith)
        self.completer.activated[str].connect(self.insert_completion)
        self.set_completion_words(completion_words)

    def set_completion_words(self, words):
        unique = sorted(set(words), key=lambda x: x.lower())
        self.completer_model.setStringList(unique)

    def word_under_cursor(self):
        cursor = self.textCursor()
        cursor.select(QTextCursor.WordUnderCursor)
        return cursor.selectedText()

    def insert_completion(self, completion):
        cursor = self.textCursor()
        prefix = self.completer.completionPrefix()
        extra = completion[len(prefix):]
        cursor.insertText(extra)
        self.setTextCursor(cursor)

    def show_completion_popup(self, force=False):
        prefix = self.word_under_cursor()
        if not force and len(prefix) < 2:
            self.completer.popup().hide()
            return

        self.completer.setCompletionPrefix(prefix)
        popup = self.completer.popup()
        popup.setCurrentIndex(self.completer.completionModel().index(0, 0))

        cr = self.cursorRect()
        width = popup.sizeHintForColumn(0) + popup.verticalScrollBar().sizeHint().width() + 14
        cr.setWidth(max(220, width))
        self.completer.complete(cr)

    def keyPressEvent(self, event):
        if self.completer.popup().isVisible():
            if event.key() in (Qt.Key_Enter, Qt.Key_Return, Qt.Key_Escape, Qt.Key_Tab, Qt.Key_Backtab):
                event.ignore()
                return

        if event.key() == Qt.Key_Space and event.modifiers() & Qt.ControlModifier:
            self.show_completion_popup(force=True)
            return

        super().keyPressEvent(event)

        text = event.text()
        if text and (text.isalnum() or text == "_"):
            self.show_completion_popup(force=False)
        elif event.key() in (Qt.Key_Backspace, Qt.Key_Delete):
            self.show_completion_popup(force=False)


class HOSCIDEMain(QMainWindow):
    def __init__(self):
        super().__init__()

        self.setWindowTitle("HOSC VSCode Mini (Qt5)")
        self.resize(1280, 820)

        self.project_root = Path(__file__).resolve().parents[1]
        self.examples_dirs = [
            self.project_root / "framework" / "examples",
            self.project_root / "examples",
        ]
        self.compiler = self.project_root / "tools" / "bin" / "hosc-compiler.exe"
        self.runtime = self.project_root / "tools" / "bin" / "hvm.exe"
        self.cli = self.project_root / "tools" / "bin" / "hosc.exe"
        self.build_dir = self.project_root / "build"

        self.explorer_paths = []
        self.untitled_count = 1
        self.last_find_text = ""

        self.init_ui()
        self.apply_style()
        self.refresh_explorer()
        self.new_file()

    def init_ui(self):
        splitter = QSplitter(Qt.Horizontal)

        self.file_list = QListWidget()
        self.file_list.setSelectionMode(QAbstractItemView.SingleSelection)
        self.file_list.itemDoubleClicked.connect(self.open_file_from_item)

        self.tabs = QTabWidget()
        self.tabs.setTabsClosable(True)
        self.tabs.tabCloseRequested.connect(self.close_tab)
        self.tabs.currentChanged.connect(self.on_current_tab_changed)

        self.output = QTextEdit()
        self.output.setReadOnly(True)
        self.output.setFont(QFont("Consolas", 10))

        right_splitter = QSplitter(Qt.Vertical)
        right_splitter.addWidget(self.tabs)
        right_splitter.addWidget(self.output)
        right_splitter.setSizes([560, 220])

        splitter.addWidget(self.file_list)
        splitter.addWidget(right_splitter)
        splitter.setSizes([260, 1000])

        self.setCentralWidget(splitter)

        self.init_menu()
        self.init_shortcuts()
        self.statusBar().showMessage("Ready")

    def apply_style(self):
        self.setStyleSheet(
            """
            QMainWindow { background: #1f2329; color: #d7dae0; }
            QMenuBar { background: #181b20; color: #d7dae0; }
            QMenuBar::item:selected { background: #2b313b; }
            QMenu { background: #252a33; color: #d7dae0; border: 1px solid #333a46; }
            QMenu::item:selected { background: #3a4250; }

            QListWidget {
                background: #252a33;
                color: #d7dae0;
                border: 1px solid #333a46;
                padding: 4px;
            }
            QListWidget::item:selected { background: #355070; }

            QTabWidget::pane { border: 1px solid #333a46; }
            QTabBar::tab {
                background: #252a33;
                color: #cfd7e6;
                padding: 8px 14px;
                border: 1px solid #333a46;
                border-bottom: none;
                margin-right: 1px;
            }
            QTabBar::tab:selected {
                background: #1f2329;
                color: #ffffff;
            }

            QTextEdit {
                background: #1f2329;
                color: #d7dae0;
                border: 1px solid #333a46;
                selection-background-color: #3b5170;
            }
            QStatusBar {
                background: #181b20;
                color: #d7dae0;
                border-top: 1px solid #333a46;
            }
            """
        )

    def init_menu(self):
        menubar = self.menuBar()

        file_menu = menubar.addMenu("File")
        edit_menu = menubar.addMenu("Edit")
        build_menu = menubar.addMenu("Build")
        view_menu = menubar.addMenu("View")

        self.new_action = QAction("New", self)
        self.new_action.setShortcut("Ctrl+N")
        self.new_action.triggered.connect(self.new_file)

        self.open_action = QAction("Open", self)
        self.open_action.setShortcut("Ctrl+O")
        self.open_action.triggered.connect(self.open_file_dialog)

        self.save_action = QAction("Save", self)
        self.save_action.setShortcut("Ctrl+S")
        self.save_action.triggered.connect(self.save_file)

        self.save_as_action = QAction("Save As", self)
        self.save_as_action.setShortcut("Ctrl+Shift+S")
        self.save_as_action.triggered.connect(self.save_file_as)

        self.find_action = QAction("Find", self)
        self.find_action.setShortcut("Ctrl+F")
        self.find_action.triggered.connect(self.find_text)

        self.find_next_action = QAction("Find Next", self)
        self.find_next_action.setShortcut("F3")
        self.find_next_action.triggered.connect(self.find_next)

        self.build_action = QAction("Build (.hbc)", self)
        self.build_action.setShortcut("Ctrl+B")
        self.build_action.triggered.connect(self.build_file)

        self.build_exe_action = QAction("Build EXE", self)
        self.build_exe_action.setShortcut("Ctrl+Shift+E")
        self.build_exe_action.triggered.connect(self.build_exe_file)

        self.check_action = QAction("Check", self)
        self.check_action.setShortcut("Ctrl+Shift+B")
        self.check_action.triggered.connect(self.check_file)

        self.run_action = QAction("Run", self)
        self.run_action.setShortcut("F5")
        self.run_action.triggered.connect(self.run_file)

        self.run_exe_action = QAction("Run EXE", self)
        self.run_exe_action.setShortcut("F6")
        self.run_exe_action.triggered.connect(self.run_exe_file)

        self.fmt_action = QAction("Format", self)
        self.fmt_action.setShortcut("Alt+Shift+F")
        self.fmt_action.triggered.connect(self.format_file)

        self.refresh_action = QAction("Refresh Explorer", self)
        self.refresh_action.triggered.connect(self.refresh_explorer)

        file_menu.addAction(self.new_action)
        file_menu.addAction(self.open_action)
        file_menu.addAction(self.save_action)
        file_menu.addAction(self.save_as_action)

        edit_menu.addAction(self.find_action)
        edit_menu.addAction(self.find_next_action)

        build_menu.addAction(self.check_action)
        build_menu.addAction(self.build_action)
        build_menu.addAction(self.build_exe_action)
        build_menu.addAction(self.run_action)
        build_menu.addAction(self.run_exe_action)
        build_menu.addAction(self.fmt_action)

        view_menu.addAction(self.refresh_action)

    def init_shortcuts(self):
        QShortcut(QKeySequence("Ctrl+W"), self, activated=self.close_current_tab)
        QShortcut(QKeySequence("Ctrl+Space"), self, activated=self.force_complete)

    def force_complete(self):
        editor = self.current_editor(silent=True)
        if editor is not None:
            editor.show_completion_popup(force=True)

    def append_output(self, text):
        if text:
            self.output.append(text.rstrip("\n"))

    def append_log(self, text):
        self.output.append(text)

    def current_editor(self, silent=False):
        editor = self.tabs.currentWidget()
        if editor is None and not silent:
            QMessageBox.information(self, "HOSC IDE", "No active editor tab.")
        return editor

    def editor_display_name(self, editor):
        if getattr(editor, "file_path", None):
            return Path(editor.file_path).name
        idx = self.tabs.indexOf(editor) + 1
        if idx <= 0:
            idx = 1
        return f"untitled{idx}.hosc"

    def tab_title_for(self, editor):
        title = self.editor_display_name(editor)
        if editor.document().isModified():
            title = title + " *"
        return title

    def update_tab_title(self, editor):
        idx = self.tabs.indexOf(editor)
        if idx >= 0:
            self.tabs.setTabText(idx, self.tab_title_for(editor))

    def update_window_title(self):
        editor = self.current_editor(silent=True)
        if editor is None:
            self.setWindowTitle("HOSC VSCode Mini (Qt5)")
            return

        if getattr(editor, "file_path", None):
            title = str(Path(editor.file_path).name)
        else:
            title = self.editor_display_name(editor)

        if editor.document().isModified():
            title = "*" + title

        self.setWindowTitle(f"HOSC VSCode Mini (Qt5) - {title}")

    def update_status(self, editor=None):
        if editor is None:
            editor = self.current_editor(silent=True)

        if editor is None:
            self.statusBar().showMessage("Ready")
            return

        cursor = editor.textCursor()
        line = cursor.blockNumber() + 1
        col = cursor.columnNumber() + 1

        path = getattr(editor, "file_path", None)
        name = str(Path(path).name) if path else self.editor_display_name(editor)
        state = "modified" if editor.document().isModified() else "saved"
        self.statusBar().showMessage(f"{name} | Ln {line}, Col {col} | {state}")

    def on_current_tab_changed(self, _index):
        self.update_window_title()
        self.update_status()

    def on_editor_modified(self, editor):
        self.update_tab_title(editor)
        self.update_window_title()
        self.update_status(editor)

    def create_editor(self):
        editor = CodeEditor(BASE_COMPLETIONS)
        editor.textChanged.connect(lambda e=editor: self.on_editor_text_changed(e))
        editor.cursorPositionChanged.connect(lambda e=editor: self.update_status(e))
        editor.document().modificationChanged.connect(lambda _m, e=editor: self.on_editor_modified(e))
        return editor

    def new_file(self):
        editor = self.create_editor()
        editor.setPlainText(DEFAULT_TEMPLATE)
        editor.document().setModified(False)

        title = f"untitled{self.untitled_count}.hosc"
        self.untitled_count += 1

        index = self.tabs.addTab(editor, title)
        self.tabs.setCurrentIndex(index)
        self.on_editor_text_changed(editor)
        self.update_tab_title(editor)
        self.update_window_title()
        self.update_status(editor)

    def find_open_tab_by_path(self, path):
        target = str(Path(path).resolve())
        for i in range(self.tabs.count()):
            w = self.tabs.widget(i)
            if getattr(w, "file_path", None):
                if str(Path(w.file_path).resolve()) == target:
                    return i
        return -1

    def open_path(self, path):
        path = Path(path)
        if not path.exists():
            QMessageBox.warning(self, "Open", f"File not found:\n{path}")
            return

        existing = self.find_open_tab_by_path(path)
        if existing >= 0:
            self.tabs.setCurrentIndex(existing)
            return

        try:
            text = path.read_text(encoding="utf-8")
        except UnicodeDecodeError:
            text = path.read_text(encoding="cp1252", errors="replace")

        editor = self.create_editor()
        editor.setPlainText(text)
        editor.file_path = str(path)
        editor.document().setModified(False)

        index = self.tabs.addTab(editor, path.name)
        self.tabs.setCurrentIndex(index)
        self.refresh_explorer()
        self.select_in_explorer(path)
        self.on_editor_text_changed(editor)
        self.update_tab_title(editor)
        self.update_window_title()
        self.update_status(editor)

    def default_open_dir(self):
        for d in self.examples_dirs:
            if d.exists():
                return d
        return self.project_root

    def open_file_dialog(self):
        start_dir = str(self.default_open_dir())
        path, _ = QFileDialog.getOpenFileName(self, "Open", start_dir, "HOSC (*.hosc);;All files (*.*)")
        if path:
            self.open_path(path)

    def open_file_from_item(self, item):
        row = self.file_list.row(item)
        if row < 0 or row >= len(self.explorer_paths):
            return
        self.open_path(self.explorer_paths[row])

    def save_file(self):
        editor = self.current_editor()
        if editor is None:
            return False

        path = getattr(editor, "file_path", None)
        if not path:
            return self.save_file_as()

        try:
            Path(path).write_text(editor.toPlainText(), encoding="utf-8", newline="")
        except OSError as exc:
            QMessageBox.critical(self, "Save failed", str(exc))
            return False

        editor.document().setModified(False)
        self.update_tab_title(editor)
        self.update_window_title()
        self.update_status(editor)
        self.refresh_explorer()
        self.select_in_explorer(path)
        return True

    def save_file_as(self):
        editor = self.current_editor()
        if editor is None:
            return False

        start_dir = str(self.default_open_dir())
        path, _ = QFileDialog.getSaveFileName(self, "Save", start_dir, "HOSC (*.hosc);;All files (*.*)")
        if not path:
            return False

        editor.file_path = path
        ok = self.save_file()
        if ok:
            self.update_tab_title(editor)
        return ok

    def maybe_save_editor(self, editor):
        if editor is None or not editor.document().isModified():
            return True

        name = self.editor_display_name(editor)
        answer = QMessageBox.question(
            self,
            "Unsaved changes",
            f"Save changes to {name}?",
            QMessageBox.Save | QMessageBox.Discard | QMessageBox.Cancel,
            QMessageBox.Save,
        )

        if answer == QMessageBox.Cancel:
            return False

        if answer == QMessageBox.Discard:
            return True

        previous_index = self.tabs.currentIndex()
        target_index = self.tabs.indexOf(editor)
        if target_index >= 0:
            self.tabs.setCurrentIndex(target_index)

        ok = self.save_file()

        if previous_index >= 0 and previous_index < self.tabs.count():
            self.tabs.setCurrentIndex(previous_index)
        return ok

    def ensure_toolchain(self):
        missing = []
        if not self.compiler.exists():
            missing.append(str(self.compiler))
        if not self.runtime.exists():
            missing.append(str(self.runtime))
        if missing:
            QMessageBox.critical(
                self,
                "Toolchain missing",
                "Missing binaries. Build first with tools/build.ps1:\n\n" + "\n".join(missing),
            )
            return False
        return True

    def ensure_cli(self):
        if not self.cli.exists():
            QMessageBox.critical(
                self,
                "CLI missing",
                "Missing hosc.exe. Build first with tools/build.ps1:\n\n" + str(self.cli),
            )
            return False
        return True

    def current_source_path(self):
        editor = self.current_editor()
        if editor is None:
            return None

        if not getattr(editor, "file_path", None):
            if not self.save_file_as():
                return None
        else:
            if not self.save_file():
                return None

        return Path(editor.file_path)

    def parse_error_lines_from_output(self, output_text):
        lines = set()
        patterns = [
            r"\.hosc:(\d+)(?::\d+)?",
            r"line\s+(\d+)",
            r"\((\d+)\)",
        ]
        for pattern in patterns:
            for m in re.finditer(pattern, output_text, flags=re.IGNORECASE):
                try:
                    n = int(m.group(1))
                    if n > 0:
                        lines.add(n)
                except (TypeError, ValueError):
                    pass
        return sorted(lines)

    def lint_error_lines(self, text):
        lines = text.splitlines()
        error_lines = []

        brace_stack = []
        paren_stack = []

        for idx, line in enumerate(lines, start=1):
            stripped = line.strip()
            if not stripped:
                continue

            quote_count = 0
            escaped = False
            for ch in line:
                if ch == "\\" and not escaped:
                    escaped = True
                    continue
                if ch == '"' and not escaped:
                    quote_count += 1
                escaped = False
            if quote_count % 2 == 1:
                error_lines.append(idx)

            line_no_strings = re.sub(r'"(?:\\.|[^"\\])*"', '""', line)

            for ch in line_no_strings:
                if ch == "{":
                    brace_stack.append(idx)
                elif ch == "}":
                    if brace_stack:
                        brace_stack.pop()
                    else:
                        error_lines.append(idx)
                elif ch == "(":
                    paren_stack.append(idx)
                elif ch == ")":
                    if paren_stack:
                        paren_stack.pop()
                    else:
                        error_lines.append(idx)

            if re.match(r"^(var|let)\s+[A-Za-z_][A-Za-z0-9_]*\s*=\s*$", stripped):
                error_lines.append(idx)

            if re.search(r"[+\-*/%]$", stripped):
                error_lines.append(idx)

        error_lines.extend(brace_stack)
        error_lines.extend(paren_stack)

        return sorted(set(error_lines))

    def apply_error_underlines(self, editor, lines):
        editor.error_lines = sorted(set(lines))
        selections = []

        for line_no in editor.error_lines:
            block = editor.document().findBlockByLineNumber(line_no - 1)
            if not block.isValid():
                continue

            cursor = QTextCursor(block)
            cursor.select(QTextCursor.LineUnderCursor)

            fmt = QTextCharFormat()
            fmt.setUnderlineStyle(QTextCharFormat.WaveUnderline)
            fmt.setUnderlineColor(QColor("#ff5f56"))

            sel = QTextEdit.ExtraSelection()
            sel.cursor = cursor
            sel.format = fmt
            selections.append(sel)

        editor.setExtraSelections(selections)

    def on_editor_text_changed(self, editor):
        text = editor.toPlainText()

        words = set(BASE_COMPLETIONS)
        words.update(re.findall(r"\b[A-Za-z_][A-Za-z0-9_]*\b", text))
        editor.set_completion_words(words)

        lint_lines = self.lint_error_lines(text)
        self.apply_error_underlines(editor, lint_lines)
        self.update_status(editor)

    def run_subprocess_logged(self, cmd):
        self.append_log(f"$ {' '.join(cmd)}")
        result = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            cwd=str(self.project_root),
        )
        self.append_output(result.stdout or "")
        self.append_output(result.stderr or "")
        return result

    def build_file(self):
        if not self.ensure_toolchain():
            return False

        src = self.current_source_path()
        if src is None:
            return False

        editor = self.current_editor(silent=True)
        if editor is None:
            return False

        out = src.with_suffix(".hbc")

        cmd = [str(self.compiler), str(src), "-b", str(out)]
        result = self.run_subprocess_logged(cmd)

        combined = ((result.stdout or "") + "\n" + (result.stderr or "")).strip()

        if result.returncode != 0:
            parsed_lines = self.parse_error_lines_from_output(combined)
            if not parsed_lines:
                parsed_lines = self.lint_error_lines(editor.toPlainText())
            self.apply_error_underlines(editor, parsed_lines)

            if parsed_lines:
                self.append_log(f"Underlined error lines: {', '.join(str(x) for x in parsed_lines)}")
            QMessageBox.warning(self, "Build", f"Build failed (exit {result.returncode}).")
            return False

        self.apply_error_underlines(editor, [])
        self.append_log(f"Build success: {out}")
        return True

    def build_exe_file(self):
        if not self.ensure_cli():
            return None

        src = self.current_source_path()
        if src is None:
            return None

        editor = self.current_editor(silent=True)
        if editor is None:
            return None

        exe_path = src.with_suffix(".exe")
        cmd = [str(self.cli), "build", str(src), "-o", str(exe_path)]
        result = self.run_subprocess_logged(cmd)
        combined = ((result.stdout or "") + "\n" + (result.stderr or "")).strip()

        if result.returncode != 0:
            parsed_lines = self.parse_error_lines_from_output(combined)
            if not parsed_lines:
                parsed_lines = self.lint_error_lines(editor.toPlainText())
            self.apply_error_underlines(editor, parsed_lines)
            QMessageBox.warning(self, "Build EXE", f"Build EXE failed (exit {result.returncode}).")
            return None

        self.apply_error_underlines(editor, [])
        self.append_log(f"Build EXE success: {exe_path}")
        return exe_path
    def check_file(self):
        if not self.ensure_cli():
            return False

        editor = self.current_editor()
        if editor is None:
            return False

        if not getattr(editor, "file_path", None):
            if not self.save_file_as():
                return False
        else:
            if not self.save_file():
                return False

        src = Path(editor.file_path)
        cmd = [str(self.cli), "check", str(src)]
        result = self.run_subprocess_logged(cmd)
        combined = ((result.stdout or "") + "\n" + (result.stderr or "")).strip()

        if result.returncode != 0:
            parsed_lines = self.parse_error_lines_from_output(combined)
            if not parsed_lines:
                parsed_lines = self.lint_error_lines(editor.toPlainText())
            self.apply_error_underlines(editor, parsed_lines)
            QMessageBox.warning(self, "Check", f"Check failed (exit {result.returncode}).")
            return False

        self.apply_error_underlines(editor, [])
        self.append_log("Check success.")
        return True

    def format_file(self):
        if not self.ensure_cli():
            return False

        editor = self.current_editor()
        if editor is None:
            return False

        if not getattr(editor, "file_path", None):
            if not self.save_file_as():
                return False
        else:
            if not self.save_file():
                return False

        src = Path(editor.file_path)
        cmd = [str(self.cli), "fmt", str(src)]
        result = self.run_subprocess_logged(cmd)

        if result.returncode != 0:
            QMessageBox.warning(self, "Format", f"Format failed (exit {result.returncode}).")
            return False

        try:
            text = src.read_text(encoding="utf-8")
        except UnicodeDecodeError:
            text = src.read_text(encoding="cp1252", errors="replace")

        cursor = editor.textCursor()
        pos = cursor.position()

        editor.blockSignals(True)
        editor.setPlainText(text)
        editor.blockSignals(False)

        new_cursor = editor.textCursor()
        new_cursor.setPosition(min(pos, len(text)))
        editor.setTextCursor(new_cursor)
        editor.document().setModified(False)

        self.on_editor_text_changed(editor)
        self.update_tab_title(editor)
        self.update_window_title()
        self.update_status(editor)
        self.append_log("Format success.")
        return True

    def run_file(self):
        if not self.ensure_cli():
            return

        src = self.current_source_path()
        if src is None:
            return

        if not self.check_file():
            return

        cmd = [str(self.cli), "run", str(src)]
        self.append_log(f"$ {' '.join(cmd)}")

        try:
            subprocess.Popen(cmd, cwd=str(self.project_root))
            self.append_log("Run started in external process.")
        except OSError as exc:
            QMessageBox.critical(self, "Run failed", str(exc))

    def run_exe_file(self):
        exe_path = self.build_exe_file()
        if not exe_path:
            return

        cmd = [str(exe_path)]
        self.append_log(f"$ {' '.join(cmd)}")

        try:
            subprocess.Popen(cmd, cwd=str(exe_path.parent))
            self.append_log("EXE started in external process.")
        except OSError as exc:
            QMessageBox.critical(self, "Run EXE failed", str(exc))

    def find_text(self):
        editor = self.current_editor(silent=True)
        if editor is None:
            return

        text, ok = QInputDialog.getText(self, "Find", "Find:", text=self.last_find_text)
        if not ok or not text:
            return

        self.last_find_text = text
        self.find_next(reset_to_start=False)

    def find_next(self, reset_to_start=False):
        editor = self.current_editor(silent=True)
        if editor is None:
            return

        if not self.last_find_text:
            self.find_text()
            return

        if reset_to_start:
            cursor = editor.textCursor()
            cursor.movePosition(QTextCursor.Start)
            editor.setTextCursor(cursor)

        if editor.find(self.last_find_text):
            self.update_status(editor)
            return

        cursor = editor.textCursor()
        cursor.movePosition(QTextCursor.Start)
        editor.setTextCursor(cursor)

        if not editor.find(self.last_find_text):
            QMessageBox.information(self, "Find", f"Cannot find: {self.last_find_text}")

    def refresh_explorer(self):
        files = []
        for d in self.examples_dirs:
            if d.exists():
                files.extend(sorted(d.glob("*.hosc")))

        files.extend(sorted(self.project_root.glob("*.hosc")))

        for i in range(self.tabs.count()):
            w = self.tabs.widget(i)
            p = getattr(w, "file_path", None)
            if p:
                files.append(Path(p))

        seen = set()
        unique = []
        for f in files:
            key = str(f.resolve())
            if key not in seen:
                seen.add(key)
                unique.append(f)

        unique.sort(key=lambda p: str(p).lower())

        self.explorer_paths = unique
        self.file_list.clear()
        for path in self.explorer_paths:
            try:
                label = str(path.resolve().relative_to(self.project_root.resolve())).replace("\\", "/")
            except ValueError:
                label = str(path)
            self.file_list.addItem(label)

    def select_in_explorer(self, path):
        target = str(Path(path).resolve())
        for i, p in enumerate(self.explorer_paths):
            if str(Path(p).resolve()) == target:
                self.file_list.setCurrentRow(i)
                break

    def close_tab(self, index):
        widget = self.tabs.widget(index)
        if widget is None:
            self.tabs.removeTab(index)
            return

        if not self.maybe_save_editor(widget):
            return

        widget.deleteLater()
        self.tabs.removeTab(index)

        if self.tabs.count() == 0:
            self.new_file()
        else:
            self.on_current_tab_changed(self.tabs.currentIndex())

    def close_current_tab(self):
        if self.tabs.count() == 0:
            return
        self.close_tab(self.tabs.currentIndex())

    def closeEvent(self, event):
        for i in range(self.tabs.count()):
            editor = self.tabs.widget(i)
            if editor is None:
                continue

            if not self.maybe_save_editor(editor):
                event.ignore()
                return

        event.accept()


def main():
    app = QApplication(sys.argv)
    window = HOSCIDEMain()

    if len(sys.argv) > 1:
        arg = Path(sys.argv[1]).expanduser()
        if not arg.is_absolute():
            arg = (Path.cwd() / arg).resolve()
        window.open_path(arg)

    window.show()
    sys.exit(app.exec_())


if __name__ == "__main__":
    main()





