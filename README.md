# riskyapi

A small Windows x64 command-line tool that recursively scans C/C++ source trees
for calls to potentially risky Win32 APIs (process injection, anti-debugging,
service creation) and reports where each one appears.

It is a review aid: it flags every textual occurrence of the watched names so a
human can inspect them. It does not decide whether a use is malicious.

## Watched APIs

| API | Typical risk |
|-----|--------------|
| `CreateRemoteThread` | Remote thread / code injection |
| `WriteProcessMemory` | Writing into another process |
| `VirtualAllocEx` | Allocating memory in another process |
| `QueueUserAPC` | APC injection |
| `NtUnmapViewOfSection` | Process hollowing |
| `IsDebuggerPresent` | Anti-debugging |
| `CheckRemoteDebuggerPresent` | Anti-debugging |
| `NtQueryInformationProcess` | Anti-debugging / process introspection |
| `CreateService` | Service installation (persistence) |

The `CreateServiceA` / `CreateServiceW` ANSI/Unicode variants are matched too and
reported under the base name `CreateService`.

## Building

Requires a C++17 compiler. Native Windows x64.

**MSVC** (from a "x64 Native Tools Command Prompt for VS", or after running
`vcvars64.bat`):

```
cl /std:c++17 /EHsc /Fe:riskyapi.exe riskyapi.cpp
```

**MinGW-w64:**

```
g++ -std=c++17 riskyapi.cpp -o riskyapi.exe
```

## Usage

```
riskyapi <root-dir> [--csv | --json] [--selftest]
```

- `<root-dir>` — directory to scan recursively. Defaults to `.` if omitted.
- `--csv` — emit CSV instead of the grouped text report.
- `--json` — emit a JSON array instead of the grouped text report.
- `--selftest` — run the built-in matcher self-check and exit.

Scanned file extensions: `.c .cpp .cc .cxx .h .hpp .hxx .hh`.
Files with no hits are omitted from all output modes.

### Exit codes

| Code | Meaning |
|------|---------|
| `0` | Scan completed, no hits found |
| `1` | Scan completed, one or more hits found |
| `2` | Bad arguments or path not found |

The `0`/`1` split lets you gate a CI step on "no risky calls" — a non-zero exit
means something was flagged.

## Output

### Grouped report (default)

```
src\inject.cpp
  L2     C16   VirtualAllocEx             |     LPVOID p = VirtualAllocEx(h, ...);
  L3     C5    WriteProcessMemory         |     WriteProcessMemory(h, p, ...);

Summary: 1 file(s), 2 hit(s).
```

`Ln` is the 1-based line number, `Cn` the 1-based column, followed by the API
name and the source line.

### CSV (`--csv`)

```
file,line,col,api,linetext
"src\inject.cpp",2,16,VirtualAllocEx,"    LPVOID p = VirtualAllocEx(h, ...);"
```

Fields are quoted; embedded quotes are doubled.

### JSON (`--json`)

A single array of hit objects:

```json
[
  {"file":"src\\inject.cpp","line":2,"col":16,"api":"VirtualAllocEx","linetext":"    LPVOID p = VirtualAllocEx(h, ...);"}
]
```

## Matching notes

- Matching is **whole-word**: `MyVirtualAllocEx` and `VirtualAllocExtra` are not
  flagged, but `VirtualAllocEx` and `CreateServiceA` are.
- Matches inside comments and string literals **are** reported by design — a
  review tool should surface every occurrence.
- CRLF line endings and a leading UTF-8 BOM are handled so column numbers are
  accurate.

To change the watched list, edit the `APIS[]` array at the top of
`riskyapi.cpp` and rebuild.
