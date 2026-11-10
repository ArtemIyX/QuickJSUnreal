#include "QuickJSRuntime.h"

#include "Modules/ModuleManager.h"
#include "QuickJSUnreal.h"
#include "Templates/UnrealTemplate.h"

DEFINE_LOG_CATEGORY_STATIC(LogQuickJSRuntime, Log, All);

namespace
{
FString CopyQuickJSString(JSContext* Context, JSValueConst Value)
{
	size_t Length = 0;
	const char* Text = JS_ToCStringLen(Context, &Length, Value);
	if (!Text)
	{
		// Converting an exception or its stack can itself throw.
		if (JS_HasException(Context)) { JS_FreeValue(Context, JS_GetException(Context)); }
		return FString();
	}
	const int32 SafeLength = static_cast<int32>(FMath::Min<size_t>(Length, MAX_int32));
	const FUTF8ToTCHAR Converted(Text, SafeLength);
	FString Result(Converted.Length(), Converted.Get());
	JS_FreeCString(Context, Text);
	return Result;
}
}

TUniquePtr<FQuickJSRuntime> FQuickJSRuntime::Create(FString& OutError)
{
	OutError.Reset();
	if (!IsInGameThread()) { OutError = TEXT("QuickJS hosts must be created on the game thread."); return nullptr; }
	FQuickJSUnrealModule* Module = FModuleManager::LoadModulePtr<FQuickJSUnrealModule>(TEXT("QuickJSUnreal"));
	if (!Module || !Module->IsLibraryReady())
	{
		OutError = TEXT("QuickJS-NG DLL is unavailable or the module is shutting down.");
		return nullptr;
	}
	TUniquePtr<FQuickJSRuntime> Host(new FQuickJSRuntime());
	Host->Runtime = JS_NewRuntime();
	if (!Host->Runtime)
	{
		OutError = TEXT("Could not create the QuickJS runtime.");
		return nullptr;
	}
	Host->Context = JS_NewContext(Host->Runtime);
	if (!Host->Context)
	{
		OutError = TEXT("Could not create the QuickJS context.");
		return nullptr;
	}
	Host->Owner = Module;
	Module->RegisterRuntime(Host.Get());
	return Host;
}

FQuickJSRuntime::~FQuickJSRuntime()
{
	if (!IsInGameThread()) { UE_LOG(LogQuickJSRuntime, Fatal, TEXT("A QuickJS host must be destroyed on the game thread.")); }
	if (bExecuting) { UE_LOG(LogQuickJSRuntime, Fatal, TEXT("A QuickJS host cannot be destroyed from its active callback.")); }
	Close();
}

JSContext* FQuickJSRuntime::GetContext() const
{
	if (!IsInGameThread()) { UE_LOG(LogQuickJSRuntime, Error, TEXT("QuickJS context access requires the game thread.")); return nullptr; }
	return Context;
}

bool FQuickJSRuntime::IsOpen() const
{
	if (!IsInGameThread()) { return false; }
	return Runtime != nullptr && Context != nullptr;
}

bool FQuickJSRuntime::Evaluate(const FString& Source, const FString& SourceName,
	EQuickJSSourceType Type, JSValue& OutValue, FQuickJSError& OutError)
{
	OutValue = JS_UNDEFINED;
	OutError = {};
	if (!IsInGameThread()) { OutError.Message = TEXT("QuickJS evaluation requires the game thread."); return false; }
	if (!IsOpen()) { OutError.Message = TEXT("QuickJS runtime is closed."); return false; }
	if (bExecuting) { OutError.Message = TEXT("Reentrant QuickJS host execution is not supported."); return false; }
	TGuardValue<bool> ExecutionGuard(bExecuting, true);
	const FTCHARToUTF8 Utf8Source(*Source, Source.Len());
	// JS_Eval requires a trailing NUL even though its length is passed separately.
	TArray<ANSICHAR> SourceBuffer;
	if (Utf8Source.Length() > 0) { SourceBuffer.Append(Utf8Source.Get(), Utf8Source.Length()); }
	SourceBuffer.Add('\0');
	const FString Filename = SourceName.IsEmpty() ? TEXT("<script>") : SourceName;
	const FTCHARToUTF8 Utf8Filename(*Filename);
	const int Flags = Type == EQuickJSSourceType::Module ? JS_EVAL_TYPE_MODULE : JS_EVAL_TYPE_GLOBAL;
	JSValue Value = JS_Eval(Context, SourceBuffer.GetData(), static_cast<size_t>(Utf8Source.Length()), Utf8Filename.Get(), Flags);
	if (JS_IsException(Value))
	{
		ConsumeException(Context, OutError);
		return false;
	}
	OutValue = Value;
	return true;
}

bool FQuickJSRuntime::Call(JSValueConst Function, JSValueConst ThisValue, int32 ArgumentCount,
	JSValueConst* Arguments, JSValue& OutValue, FQuickJSError& OutError)
{
	OutValue = JS_UNDEFINED;
	OutError = {};
	if (!IsInGameThread()) { OutError.Message = TEXT("QuickJS calls require the game thread."); return false; }
	if (!IsOpen()) { OutError.Message = TEXT("QuickJS runtime is closed."); return false; }
	if (ArgumentCount < 0 || (ArgumentCount > 0 && !Arguments))
	{
		OutError.Message = TEXT("Invalid JavaScript function arguments.");
		return false;
	}
	if (bExecuting) { OutError.Message = TEXT("Reentrant QuickJS host execution is not supported."); return false; }
	TGuardValue<bool> ExecutionGuard(bExecuting, true);
	JSValue Value = JS_Call(Context, Function, ThisValue, ArgumentCount, Arguments);
	if (JS_IsException(Value))
	{
		ConsumeException(Context, OutError);
		return false;
	}
	OutValue = Value;
	return true;
}

bool FQuickJSRuntime::TakeException(FQuickJSError& OutError)
{
	OutError = {};
	if (!IsInGameThread()) { OutError.Message = TEXT("QuickJS exception access requires the game thread."); return false; }
	if (!IsOpen()) { OutError.Message = TEXT("QuickJS runtime is closed."); return false; }
	if (bExecuting) { OutError.Message = TEXT("Reentrant QuickJS host execution is not supported."); return false; }
	TGuardValue<bool> ExecutionGuard(bExecuting, true);
	if (!JS_HasException(Context)) { return false; }
	ConsumeException(Context, OutError);
	return true;
}

bool FQuickJSRuntime::ExecutePendingJobs(int32 MaxJobs, int32& OutExecuted,
	bool& bOutHasPendingJobs, FQuickJSError& OutError)
{
	OutExecuted = 0;
	bOutHasPendingJobs = false;
	OutError = {};
	if (!IsInGameThread()) { OutError.Message = TEXT("QuickJS job pumping requires the game thread."); return false; }
	if (!IsOpen()) { OutError.Message = TEXT("QuickJS runtime is closed."); return false; }
	if (MaxJobs < 0) { OutError.Message = TEXT("MaxJobs cannot be negative."); return false; }
	if (bExecuting) { OutError.Message = TEXT("Reentrant QuickJS host execution is not supported."); return false; }
	TGuardValue<bool> ExecutionGuard(bExecuting, true);
	while (OutExecuted < MaxJobs)
	{
		JSContext* JobContext = nullptr;
		const int Result = JS_ExecutePendingJob(Runtime, &JobContext);
		if (Result == 0) { break; }
		++OutExecuted;
		if (Result < 0)
		{
			ConsumeException(JobContext ? JobContext : Context, OutError);
			bOutHasPendingJobs = JS_IsJobPending(Runtime);
			return false;
		}
	}
	bOutHasPendingJobs = JS_IsJobPending(Runtime);
	return true;
}

void FQuickJSRuntime::ConsumeException(JSContext* InContext, FQuickJSError& OutError)
{
	OutError = {};
	if (!InContext || !JS_HasException(InContext))
	{
		OutError.Message = TEXT("QuickJS reported an exception without exception details.");
		return;
	}
	JSValue Exception = JS_GetException(InContext);
	OutError.Message = CopyQuickJSString(InContext, Exception);
	if (OutError.Message.IsEmpty()) { OutError.Message = TEXT("JavaScript exception (message unavailable)."); }
	if (JS_IsError(Exception))
	{
		JSValue Stack = JS_GetPropertyStr(InContext, Exception, "stack");
		if (JS_IsException(Stack))
		{
			if (JS_HasException(InContext)) { JS_FreeValue(InContext, JS_GetException(InContext)); }
		}
		else
		{
			if (!JS_IsUndefined(Stack) && !JS_IsNull(Stack)) { OutError.Stack = CopyQuickJSString(InContext, Stack); }
			JS_FreeValue(InContext, Stack);
		}
	}
	JS_FreeValue(InContext, Exception);
}

void FQuickJSRuntime::Close()
{
	if (!IsInGameThread())
	{
		UE_LOG(LogQuickJSRuntime, Error, TEXT("QuickJS hosts must be closed on the game thread."));
		return;
	}
	if (bExecuting)
	{
		UE_LOG(LogQuickJSRuntime, Error, TEXT("Cannot close a QuickJS host during an active callback; close it after execution returns."));
		return;
	}
	// Detach first so reentrant cleanup cannot free the same handles twice.
	JSContext* ContextToFree = Context; Context = nullptr;
	JSRuntime* RuntimeToFree = Runtime; Runtime = nullptr;
	FQuickJSUnrealModule* PreviousOwner = Owner; Owner = nullptr;
	if (PreviousOwner) { PreviousOwner->UnregisterRuntime(this); }
	if (ContextToFree) { JS_FreeContext(ContextToFree); }
	if (RuntimeToFree) { JS_FreeRuntime(RuntimeToFree); }
}
