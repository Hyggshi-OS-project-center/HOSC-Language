# 🗂️ **HOSC Language Project Structure**

## 📁 **Reorganized Directory Structure**

```
hosc-language/
├── 📁 framework/                    # HOSC Runtime Framework
│   ├── 📁 bin/                      # Framework executables
│   │   └── hosc_framework.exe       # Complete runtime platform
│   ├── 📁 build/                   # Build artifacts
│   ├── 📁 docs/                    # Framework documentation
│   │   ├── README_FRAMEWORK.md     # Complete framework docs
│   │   └── HOW_TO_RUN_FRAMEWORK.md # Usage guide
│   ├── 📁 examples/                # Framework examples
│   │   ├── simple_demo.hosc
│   │   ├── var_decl.hosc
│   │   ├── func_decl.hosc
│   │   ├── debug_print.hosc
│   │   ├── error.hosc
│   │   ├── info.hosc
│   │   ├── warning.hosc
│   │   ├── window.hosc
│   │   ├── sleep.hosc
│   │   ├── ask.hosc
│   │   ├── demo.hosc
│   │   ├── print.hosc
│   │   ├── file.hosc
│   │   └── comprehensive_demo.hosc
│   ├── 📁 include/                 # Framework headers
│   │   └── hosc_runtime.h          # Runtime framework header
│   ├── 📁 src/                     # Framework source code
│   │   ├── hosc_framework.c        # Framework demonstration
│   │   ├── hosc_runtime.c          # Runtime implementation
│   │   └── hosc_modules.c          # Built-in modules
│   └── Makefile.framework          # Framework build system
│
├── 📁 hosc-language/               # Original HOSC Language
│   ├── 📁 bin/                     # HOSC executables
│   │   ├── hosc-cli.exe            # Command line interface
│   │   ├── hosc_engine.exe         # Standalone engine
│   │   ├── hosc_example.exe        # Library example
│   │   └── hosc_lib.dll            # Shared library
│   ├── 📁 build/                   # Build artifacts
│   ├── 📁 docs/                    # HOSC documentation
│   │   ├── README.md               # Original README
│   │   └── README_NEW.md           # New features README
│   ├── 📁 examples/                # HOSC examples (empty)
│   ├── 📁 include/                 # HOSC headers
│   │   ├── codegen.h
│   │   ├── lexer.h
│   │   ├── parser.h
│   │   ├── runtime.h
│   │   └── token.h
│   ├── 📁 src/                     # HOSC source code
│   │   ├── ast.h                   # Abstract Syntax Tree
│   │   ├── codegen.c               # Code generator
│   │   ├── core.c                  # Runtime core
│   │   ├── executor.c              # AST executor
│   │   ├── lexer.c                 # Lexical analyzer
│   │   └── parser.c                # Syntax parser
│   ├── 📁 tests/                   # Test files
│   │   ├── assert_helpers.h
│   │   └── test_compiler.c
│   ├── Makefile                    # Original build system
│   └── Makefile.new               # New build system
│
├── 📁 docs/                        # Project documentation
├── 📁 bin/                         # Root executables
├── 📁 src/                         # Root source files
└── 📁 examples/                    # Root examples
```

## 🎯 **Project Components**

### **1. HOSC Runtime Framework** (`framework/`)
- **Complete Runtime Platform** - The "soul" of HOSC language
- **Unified Runtime Environment** - Single context for all components
- **Memory Management** - Automatic tracking and statistics
- **Module System** - Built-in modules (Core, IO, Math, String, Win32)
- **API Framework** - Dynamic function registration
- **Error Handling** - Centralized error reporting
- **Logging System** - Multi-level logging with timestamps
- **Standard Library** - Core data types and collections

### **2. Original HOSC Language** (`hosc-language/`)
- **Compiler Components** - Lexer, Parser, Code Generator
- **Runtime Execution** - Direct AST execution
- **CLI Interface** - Command-line interface
- **Standalone Engine** - Non-CLI implementation
- **Shared Library** - Embeddable HOSC functionality
- **Test Suite** - Comprehensive testing

## 🚀 **How to Use Each Component**

### **Framework (Complete Runtime Platform)**
```bash
cd framework
make -f Makefile.framework framework
./bin/hosc_framework.exe
```

### **Original HOSC Language**
```bash
cd hosc-language
make -f Makefile.new all
./bin/hosc-cli.exe run examples/simple_demo.hosc
```

## 📚 **Documentation Structure**

- **`framework/docs/`** - Complete framework documentation
- **`hosc-language/docs/`** - Original HOSC language documentation
- **`docs/`** - Project-wide documentation

## 🔧 **Build Systems**

- **`framework/Makefile.framework`** - Framework build system
- **`hosc-language/Makefile`** - Original build system
- **`hosc-language/Makefile.new`** - New build system

## 🎉 **Benefits of This Organization**

1. **Clear Separation** - Framework vs Original Language
2. **Modular Structure** - Each component has its own directory
3. **Easy Navigation** - Logical file organization
4. **Independent Development** - Can work on components separately
5. **Clean Builds** - Separate build systems for each component
6. **Comprehensive Documentation** - Organized documentation structure

---

**The HOSC Language Project is now perfectly organized! 🎯**
