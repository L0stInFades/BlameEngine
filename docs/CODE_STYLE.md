# Blame Engine Code Style Guide

This document outlines the coding standards and style guidelines for the Blame Engine codebase.
(Code symbols still use the `NEXT`/`Next` token — see brand-vs-symbol note in [`ENGINEERING_HANDBOOK.md`](ENGINEERING_HANDBOOK.md).)

## File Naming

- **Headers**: `.h` extension, use snake_case for directories
  - Example: `next/foundation/logger.h`
- **Source**: `.cpp` extension, use snake_case for directories
  - Example: `engine/foundation/src/logger.cpp`

## Naming Conventions

### Classes and Structs
- Use **PascalCase** for class and struct names
```cpp
class AssetManager { };
struct JobHandle { };
```

### Functions and Methods
- Use **PascalCase** for public methods
```cpp
void Initialize();
bool LoadPackage(const std::string& path);
```

### Variables
- Use **camelCase** for local variables and parameters
```cpp
int workerCount;
std::string assetName;
```

- Use **snake_case_** for private member variables (with trailing underscore)
```cpp
class Example {
private:
    int activeWorkers_;
    std::string filePath_;
};
```

### Constants
- Use **PascalCase** for constants
```cpp
const int MaxBufferSize = 4096;
const float DefaultTimeout = 30.0f;
```

### Enums
- Use **PascalCase** for enum types
- Use **PascalCase** for enum values
```cpp
enum class JobPriority {
    High,
    Normal,
    Low
};
```

### Macros
- Use **SCREAMING_SNAKE_CASE** for macros
```cpp
#define NEXT_LOG_INFO(fmt, ...) ...
#define MAX_WORKER_THREADS 16
```

## Code Organization

### Include Guards
Use `#pragma once` for header guards:
```cpp
#pragma once

// Header contents
```

### Include Order
1. Corresponding header (if .cpp file)
2. System/STL headers
3. Third-party library headers
4. Project headers (grouped by module)

```cpp
#include "next/foundation/logger.h"

#include <cstdio>
#include <string>

#include "external/library.h"

#include "next/module1/header1.h"
#include "next/module2/header2.h"
```

## Formatting

### Indentation
- Use **4 spaces** for indentation (no tabs)

### Braces
- Use **Allman style** (opening brace on new line) for functions/classes
```cpp
void Function()
{
    // Code here
}
```

- Use **K&R style** (opening brace on same line) for control structures
```cpp
if (condition) {
    // Code here
} else {
    // Code here
}
```

### Spacing
- Space after keywords: `if (`, `while (`, `for (`
- Space around operators: `a = b + c`
- No space inside parentheses: `func(a, b)` not `func( a, b )`

### Line Length
- Maximum line length: **120 characters**
- Break long lines at logical points

## Documentation

### Public APIs
All public APIs must have Doxygen-style documentation:
```cpp
/**
 * @brief Brief description
 *
 * Detailed description if needed.
 *
 * @param paramName Description
 * @return Description
 */
void Function(int paramName);
```

### Inline Comments
- Use `//` for single-line comments
- Place comments above the code they describe
```cpp
// Check if the buffer is large enough
if (bufferSize < requiredSize) {
    return false;
}
```

## Best Practices

### Memory Management
- Prefer RAII and smart pointers over raw pointers
```cpp
// Good
std::unique_ptr<Data> data = std::make_unique<Data>();

// Avoid
Data* data = new Data();
```

### Error Handling
- Use return values for expected failures
- Use assertions for programmer errors
- Log errors before returning failure

### Const Correctness
- Mark variables `const` when they should not be modified
- Use `const` references for read-only parameters
```cpp
void Process(const std::string& input);  // Good
void Process(std::string input);         // Avoid (unnecessary copy)
```

## Language Features

### Modern C++
- Use `nullptr` instead of `NULL`
- Use `override` keyword on overridden virtual functions
- Use `enum class` instead of raw `enum`
- Prefer range-based for loops
```cpp
// Good
for (const auto& item : items) {
    // Process item
}
```

### What to Avoid
- Do not use `using namespace std;` in headers
- Do not use C-style casts
- Do not use raw pointers for ownership
- Do not use `#define` for constants (use `constexpr`)

## System-Specific Guidelines

### Singletons
Use the Instance() pattern for singletons:
```cpp
class System {
public:
    static System& Instance();
    // ...
private:
    System() = default;
    ~System() = default;
    System(const System&) = delete;
    System& operator=(const System&) = delete;
};
```

### Initialization
All systems should follow this pattern:
```cpp
bool Initialize();  // Returns true on success
void Shutdown();    // Cleanup and release resources
```

## Platform-Specific Code

Use platform preprocessor directives sparingly:
```cpp
#ifdef _WIN32
    // Windows-specific code
#else
    // Other platforms
#endif
```

Document any platform-specific behavior clearly.

## Testing and Quality

- Write unit tests for critical functionality
- Use static analysis tools regularly
- Address compiler warnings promptly
- Conduct code reviews for significant changes

---

**Note**: This guide is a living document. Update it as the project evolves and new patterns emerge.
