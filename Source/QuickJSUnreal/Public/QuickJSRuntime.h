#pragma once

#include "CoreMinimal.h"
#include "Templates/UniquePtr.h"
#include "quickjs.h"

class FQuickJSUnrealModule;

enum class EQuickJSSourceType : uint8
{
	Script,
	Module
};

struct FQuickJSError
{
	FString Message;
	FString Stack;
};

/**
 * Owns one QuickJS runtime and context. Create, use, Close, and destroy on the
 * game thread. The raw C API is subject to the same rule. Release every owned
 * JSValue and JS string before Close; never use raw pointers after Close.
 */
class QUICKJSUNREAL_API FQuickJSRuntime final
{
public:
	static TUniquePtr<FQuickJSRuntime> Create(FString& OutError);
	~FQuickJSRuntime();
	FQuickJSRuntime(const FQuickJSRuntime&) = delete;
	FQuickJSRuntime& operator=(const FQuickJSRuntime&) = delete;

	/** Borrowed; valid only until Close. The caller must not free it. */
	JSContext* GetContext() const;
	bool IsOpen() const;

	/** On success OutValue is owned by the caller and must be freed with JS_FreeValue. */
	bool Evaluate(const FString& Source, const FString& SourceName, EQuickJSSourceType Type,
		JSValue& OutValue, FQuickJSError& OutError);
	/** Function, ThisValue, and Arguments are borrowed. OutValue is owned on success. */
	bool Call(JSValueConst Function, JSValueConst ThisValue, int32 ArgumentCount,
		JSValueConst* Arguments, JSValue& OutValue, FQuickJSError& OutError);
	/** Consume the current exception after a raw C API operation reports JS_EXCEPTION. */
	bool TakeException(FQuickJSError& OutError);
	/** Run up to MaxJobs jobs; zero only checks whether work is pending. */
	bool ExecutePendingJobs(int32 MaxJobs, int32& OutExecuted, bool& bOutHasPendingJobs, FQuickJSError& OutError);
	void Close();

private:
	friend class FQuickJSUnrealModule;
	FQuickJSRuntime() = default;
	static void ConsumeException(JSContext* Context, FQuickJSError& OutError);

	FQuickJSUnrealModule* Owner = nullptr;
	JSRuntime* Runtime = nullptr;
	JSContext* Context = nullptr;
	bool bExecuting = false;
};
