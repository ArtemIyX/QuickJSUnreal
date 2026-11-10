<div align="center">
  <img src="Resources/Icon128.png" alt="QuickJSUnreal plugin icon" width="112" />
  <h1>QuickJSUnreal</h1>
  <p><strong>Run JavaScript from Unreal Engine C++.</strong></p>
  <p>A small runtime host for <a href="https://github.com/quickjs-ng/quickjs">QuickJS-NG</a>.</p>
  <p>
    <img src="https://img.shields.io/badge/Unreal_Engine-5.7-0E1128?style=for-the-badge&logo=unrealengine&logoColor=white" alt="Unreal Engine 5.7" />
    <img src="https://img.shields.io/badge/Platform-Win64-0078D4?style=for-the-badge&logo=windows&logoColor=white" alt="Win64" />
    <img src="https://img.shields.io/badge/API-C%2B%2B-00599C?style=for-the-badge&logo=cplusplus&logoColor=white" alt="C++ API" />
    <a href="https://github.com/quickjs-ng/quickjs"><img src="https://img.shields.io/badge/Engine-QuickJS--NG-F7DF1E?style=for-the-badge&logo=javascript&logoColor=black" alt="Powered by QuickJS-NG" /></a>
  </p>
</div>

---

> **Current status:** The plugin provides a C++ runtime host and shared-library integration for Win64. It targets the UE 5.7 project in which it was developed; the wrapper, automated tests, packaged builds, and other engine versions still need verification. It does not provide Blueprint nodes or a browser.

## Overview

QuickJSUnreal connects Unreal C++ to an embedded QuickJS-NG runtime. Create a host, evaluate trusted JavaScript, call functions in either direction, and process pending promise jobs when your application chooses.

- **One host, one context:** each `FQuickJSRuntime` owns its JavaScript runtime and context.
- **C++ access:** use convenience methods or the exposed QuickJS C API for native bindings and values.
- **Explicit scheduling:** pump JavaScript jobs from your own game-thread code; the plugin does not create a ticker.
- **Shared engine:** Unreal modules use one staged `qjs.dll` and its matching import library.

The plugin is independent of NativeWeb and AffineUI. It can be used as a building block for that integration, but it does not implement Vue rendering or a DOM bridge.

## Requirements

| Requirement | Details |
| --- | --- |
| Unreal Engine | UE 5.7 is the development target. Other versions have not been verified. |
| Platform | Windows x64 (`Win64`). Linux and other platforms are not packaged yet. |
| Project | A C++ Unreal project, or a plugin/module that can depend on `QuickJSUnreal`. |
| QuickJS binaries | The matching `qjs.dll` and `qjs-import.lib` described below. |
| Source builds | CMake, Git, and an installed Visual Studio C++ toolchain, only if building QuickJS-NG yourself. |

## Installation

1. Copy the complete `QuickJSUnreal` plugin into your project's `Plugins` folder:

   ```text
   MyProject/
   └── Plugins/
       └── QuickJSUnreal/
           ├── QuickJSUnreal.uplugin
           ├── Binaries/ThirdParty/QuickJS/Win64/qjs.dll
           └── Source/
               ├── QuickJSUnreal/
               └── ThirdParty/QuickJS/
                   ├── include/quickjs.h
                   ├── lib/Win64/qjs-import.lib
                   ├── LICENSE
                   └── REVISION.txt
   ```

2. Enable **QuickJSUnreal** in **Edit → Plugins**, or add it to your `.uproject`:

   ```json
   {
     "Plugins": [
       { "Name": "QuickJSUnreal", "Enabled": true }
     ]
   }
   ```

   If your project already has a `Plugins` array, add the entry to that array instead of creating a second one.

3. In the `.Build.cs` for the module that uses JavaScript, add:

   ```csharp
   PrivateDependencyModuleNames.Add("QuickJSUnreal");
   ```

   Use `PublicDependencyModuleNames` instead if your module's **public headers** expose QuickJSUnreal or QuickJS types.

4. Regenerate project files if needed and build the project. The plugin links the import library and stages `qjs.dll` as a runtime dependency.

The DLL and import library must come from the **same QuickJS-NG revision** as `quickjs.h`. If they are missing, follow [Building QuickJS-NG](#building-quickjs-ng) before building Unreal.

## Quick start

Run this example on the **game thread**, for example from a C++ actor's `BeginPlay`:

```cpp
#include "QuickJSRuntime.h"

void RunQuickJSExample()
{
    FString CreateError;
    TUniquePtr<FQuickJSRuntime> Host = FQuickJSRuntime::Create(CreateError);
    if (!Host)
    {
        UE_LOG(LogTemp, Error, TEXT("QuickJS creation failed: %s"), *CreateError);
        return;
    }

    FQuickJSError Error;
    JSValue Result = JS_UNDEFINED;
    if (Host->Evaluate(TEXT("20 + 22"), TEXT("example.js"),
                       EQuickJSSourceType::Script, Result, Error))
    {
        JSContext* Context = Host->GetContext();
        int32_t Number = 0;
        if (JS_ToInt32(Context, &Number, Result) == 0)
        {
            UE_LOG(LogTemp, Display, TEXT("JavaScript result: %d"), Number);
        }
        else if (Host->TakeException(Error))
        {
            UE_LOG(LogTemp, Error, TEXT("Conversion failed: %s"), *Error.Message);
        }
        JS_FreeValue(Context, Result);
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("JavaScript error: %s\n%s"),
               *Error.Message, *Error.Stack);
    }

    Host->Close();
}
```

After a successful build, the expected log output is `JavaScript result: 42`. `Evaluate` also accepts `EQuickJSSourceType::Module` for a self-contained ES module. The source name is used in error and stack information.

## C++ API

Include `QuickJSRuntime.h` in your module. The host exposes `quickjs.h` so C++ callers can use the [QuickJS-NG C API](https://quickjs-ng.github.io/quickjs/developer-guide/intro/) directly.

| API | Purpose |
| --- | --- |
| `FQuickJSRuntime::Create(OutError)` | Create a host, runtime, and context. Returns null with an error if creation fails. |
| `GetContext()` | Get a **borrowed** `JSContext*` for raw QuickJS calls. |
| `Evaluate(...)` | Evaluate script or module source. |
| `Call(...)` | Call a JavaScript function with borrowed QuickJS arguments. |
| `TakeException(...)` | Consume exception details after a raw QuickJS API call fails. |
| `ExecutePendingJobs(...)` | Run a caller-limited number of pending JavaScript jobs. |
| `IsOpen()` / `Close()` | Check the host and release its context and runtime. `Close()` is idempotent. |

### Native functions

Register native functions through QuickJS using the host's context. For example, the callback below can be registered as a JavaScript global:

```cpp
static JSValue NativeDouble(JSContext* Context, JSValueConst ThisValue,
                            int ArgumentCount, JSValueConst* Arguments)
{
    if (ArgumentCount < 1)
    {
        return JS_ThrowTypeError(Context, "Expected one number");
    }

    int32_t Number = 0;
    if (JS_ToInt32(Context, &Number, Arguments[0]) < 0)
    {
        return JS_EXCEPTION;
    }
    return JS_NewFloat64(Context, static_cast<double>(Number) * 2.0);
}

// Host is an open FQuickJSRuntime, used on the game thread.
JSContext* Context = Host->GetContext();
JSValue Global = JS_GetGlobalObject(Context);
JSValue Function = JS_NewCFunction(Context, NativeDouble, "nativeDouble", 1);

// JS_SetPropertyStr consumes Function, including on failure.
const int SetResult = JS_SetPropertyStr(Context, Global, "nativeDouble", Function);
JS_FreeValue(Context, Global);
if (SetResult < 0)
{
    FQuickJSError Error;
    Host->TakeException(Error);
    UE_LOG(LogTemp, Error, TEXT("Native registration failed: %s"), *Error.Message);
}
```

JavaScript can then call `nativeDouble(21)`. This example omits allocation-failure handling for `JS_GetGlobalObject` and `JS_NewCFunction`; production bindings should check for `JS_EXCEPTION` before using returned values. Native callback parameters are borrowed; return a newly owned value or a QuickJS exception as required by the C API.

### Promise jobs

QuickJS does not automatically advance jobs through an Unreal ticker. Call the host on the game thread when your application is ready to process them:

```cpp
int32 Executed = 0;
bool bHasMoreJobs = false;
FQuickJSError Error;

if (!Host->ExecutePendingJobs(32, Executed, bHasMoreJobs, Error))
{
    UE_LOG(LogTemp, Error, TEXT("QuickJS job failed: %s"), *Error.Message);
}
// If bHasMoreJobs is true, the caller can pump again later.
```

The limit bounds how many queued jobs are attempted in that call; `0` only checks whether work is pending. A limit does **not** interrupt a single long-running script or job. A rejected promise is not necessarily a failed job, and unhandled-rejection tracking is not implemented.

### Ownership and threading

- Create, use, close, and destroy each host on the **game thread**. The same rule applies to calls made directly through its raw QuickJS context.
- A successful `Evaluate` or `Call` returns an **owned** `JSValue`. Free it with `JS_FreeValue` before reusing the output variable or closing the host. Free QuickJS strings with the matching QuickJS function.
- `GetContext()` is borrowed: do not free it, keep it after `Close()`, or exchange its values with another runtime.
- Release all owned QuickJS values and strings before closing. Destroy client-owned host objects before the Unreal module unloads. Module shutdown closes any still-open hosts before unloading the DLL.
- Don’t close or destroy a host from one of its active native callbacks. The wrapper rejects reentrant evaluation, calls, exception extraction, and job pumping; direct C API calls remain the caller's responsibility.

## Building QuickJS-NG

If your plugin copy already includes the matching DLL and import library, you can skip this section. To produce them yourself, close Unreal and use a clean QuickJS-NG checkout at the commit in `REVISION.txt`.

From PowerShell, run the supplied script, replacing the paths with yours:

```powershell
& "C:\Path\To\MyProject\Plugins\QuickJSUnreal\BuildScripts\BuildQuickJSWin64.ps1" `
  -SourceRoot "C:\Path\To\quickjs"
```

The script defaults to the `Visual Studio 18 2026` CMake generator. You can pass `-Generator` for another installed Visual Studio generator and `-Toolset` to select an installed toolset, for example `-Toolset "version=14.51.36231"`.

It verifies the checkout revision and the bundled header and license, builds the `qjs` target as an x64 shared library with the release dynamic MSVC runtime (`/MD`), then installs `qjs.dll` and `qjs-import.lib` into the plugin paths shown above. QuickJS-NG's CMake configuration selects the MSVC C11 atomics option when supported.

> **Static vs import library:** A static `qjs.lib` from an earlier build is not the DLL import library. QuickJSUnreal deliberately links the distinct `qjs-import.lib` produced by the script.

If your repository ignores `.lib`, `.dll`, or plugin `Binaries` paths, explicitly include both generated binaries in your distribution or source-control workflow. Do not mix a header, import library, and DLL from different QuickJS-NG revisions.

## Testing

After building the shared binaries and Unreal project, open **Tools → Test Automation** and look for tests under `QuickJSUnreal.Runtime`. The included tests cover script evaluation, exception details, native functions, C++ calls into JavaScript, self-contained modules, bounded promise-job processing, job failures, and repeated close.

The tests have not yet been run for this implementation. Also verify module unload with all client-owned JavaScript values released and test a packaged Win64 build before distributing the plugin.

## Troubleshooting

| Problem | Check |
| --- | --- |
| Build reports a missing import library or DLL | Run the build script and check the two paths in [Installation](#installation). A static `qjs.lib` alone is insufficient. |
| Plugin loads but runtime creation fails | Check `LogQuickJSUnreal` for the DLL path and confirm the DLL matches the header and import library. |
| `Cannot open include file: quickjs.h` | Add `QuickJSUnreal` to the consuming module's dependencies and regenerate project files if needed. |
| A promise callback doesn’t run | Call `ExecutePendingJobs` on the game thread. There is no automatic ticker. |
| A script expects `console`, timers, `window`, or `document` | These host or browser APIs are not provided by this plugin. Add the specific integration your application needs. |
| A module import cannot be resolved | External module loading is not configured. Use a self-contained bundle or provide a loader in a later integration. |

## Scope and license

QuickJSUnreal hosts the JavaScript engine; it does not ship the `qjs` command-line application, QuickJS's optional `std`/`os` modules, a browser, DOM, JavaScript timers, Vue renderer, or Unreal UI integration. There is no implicit script-file loading. Treat scripts as trusted application content; this is not a security sandbox.

QuickJSUnreal is available under the [MIT License](LICENSE), including for commercial projects. Retain the applicable copyright and license notices when distributing copies or substantial portions of the software.

Bundled third-party software has its own copyright notices and license terms. The QuickJS-NG MIT license is included at [Source/ThirdParty/QuickJS/LICENSE](Source/ThirdParty/QuickJS/LICENSE). See the [QuickJS-NG project](https://github.com/quickjs-ng/quickjs) for upstream documentation and notices.
