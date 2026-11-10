#if WITH_DEV_AUTOMATION_TESTS

#include "QuickJSRuntime.h"
#include "Misc/AutomationTest.h"

namespace
{
JSValue NativeDouble(JSContext* Context, JSValueConst ThisValue, int ArgumentCount, JSValueConst* Arguments)
{
	(void)ThisValue;
	if (ArgumentCount < 1) { return JS_ThrowTypeError(Context, "Expected one argument"); }
	int32_t Input = 0;
	if (JS_ToInt32(Context, &Input, Arguments[0]) < 0) { return JS_EXCEPTION; }
	return JS_NewInt32(Context, Input * 2);
}

JSValue FailingJob(JSContext* Context, int ArgumentCount, JSValueConst* Arguments)
{
	(void)ArgumentCount;
	(void)Arguments;
	return JS_ThrowTypeError(Context, "job failure");
}

JSValue NativeTryReentry(JSContext* Context, JSValueConst ThisValue, int ArgumentCount, JSValueConst* Arguments)
{
	(void)ThisValue;
	(void)ArgumentCount;
	(void)Arguments;
	FQuickJSRuntime* Host = static_cast<FQuickJSRuntime*>(JS_GetContextOpaque(Context));
	if (!Host) { return JS_ThrowInternalError(Context, "Missing test host"); }
	FQuickJSError Error;
	JSValue Value = JS_UNDEFINED;
	const bool bSucceeded = Host->Evaluate(TEXT("1"), TEXT("nested.js"), EQuickJSSourceType::Script, Value, Error);
	if (bSucceeded) { JS_FreeValue(Context, Value); }
	return JS_NewBool(Context, !bSucceeded && Error.Message.Contains(TEXT("Reentrant")));
}

bool ReadInt(JSContext* Context, JSValueConst Value, int32& OutNumber)
{
	int32_t Number = 0;
	if (JS_ToInt32(Context, &Number, Value) < 0) { return false; }
	OutNumber = static_cast<int32>(Number);
	return true;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuickJSRuntimeEvaluationTest,
	"QuickJSUnreal.Runtime.EvaluationAndNativeCalls",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FQuickJSRuntimeEvaluationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FString CreateError;
	TUniquePtr<FQuickJSRuntime> Host = FQuickJSRuntime::Create(CreateError);
	if (!TestTrue(TEXT("Runtime is created"), Host.IsValid())) { AddError(CreateError); return false; }
	JSContext* Context = Host->GetContext();
	FQuickJSError Error;
	JSValue Value = JS_UNDEFINED;
	int32 Number = 0;
	if (TestTrue(TEXT("Arithmetic evaluates"), Host->Evaluate(TEXT("20 + 22"), TEXT("arithmetic.js"), EQuickJSSourceType::Script, Value, Error)))
	{
		TestTrue(TEXT("Arithmetic result converts"), ReadInt(Context, Value, Number));
		TestEqual(TEXT("Arithmetic result"), Number, 42);
		JS_FreeValue(Context, Value);
	}
	TestFalse(TEXT("Thrown error is reported"), Host->Evaluate(TEXT("throw new Error('expected failure')"), TEXT("error-test.js"), EQuickJSSourceType::Script, Value, Error));
	TestTrue(TEXT("Exception message is preserved"), Error.Message.Contains(TEXT("expected failure")));
	TestTrue(TEXT("Exception stack includes the source name"), Error.Stack.Contains(TEXT("error-test.js")));
	if (TestTrue(TEXT("UTF-8 source evaluates"), Host->Evaluate(TEXT("'Привет'"), TEXT("unicode.js"), EQuickJSSourceType::Script, Value, Error)))
	{
		const char* Text = JS_ToCString(Context, Value);
		TestNotNull(TEXT("JavaScript string converts"), Text);
		if (Text)
		{
			TestEqual(TEXT("Unicode round trip"), FString(UTF8_TO_TCHAR(Text)), FString(TEXT("Привет")));
			JS_FreeCString(Context, Text);
		}
		JS_FreeValue(Context, Value);
	}

	JSValue Global = JS_GetGlobalObject(Context);
	JSValue Native = JS_NewCFunction(Context, NativeDouble, "nativeDouble", 1);
	const int RegistrationResult = JS_SetPropertyStr(Context, Global, "nativeDouble", Native); // Consumes Native.
	JS_FreeValue(Context, Global);
	if (!TestTrue(TEXT("Native function registers"), RegistrationResult >= 0)) { return false; }
	if (TestTrue(TEXT("JavaScript calls the native function"), Host->Evaluate(TEXT("nativeDouble(21)"), TEXT("native-test.js"), EQuickJSSourceType::Script, Value, Error)))
	{
		TestTrue(TEXT("Native result converts"), ReadInt(Context, Value, Number));
		TestEqual(TEXT("Native function result"), Number, 42);
		JS_FreeValue(Context, Value);
	}
	JS_SetContextOpaque(Context, Host.Get());
	Global = JS_GetGlobalObject(Context);
	Native = JS_NewCFunction(Context, NativeTryReentry, "nativeTryReentry", 0);
	const int ReentryRegistration = JS_SetPropertyStr(Context, Global, "nativeTryReentry", Native);
	JS_FreeValue(Context, Global);
	if (TestTrue(TEXT("Reentry test function registers"), ReentryRegistration >= 0) &&
		TestTrue(TEXT("Reentry test executes"), Host->Evaluate(TEXT("nativeTryReentry()"), TEXT("reentry.js"), EQuickJSSourceType::Script, Value, Error)))
	{
		TestEqual(TEXT("Nested host evaluation is rejected"), JS_ToBool(Context, Value), 1);
		JS_FreeValue(Context, Value);
	}
	JSValue Function = JS_UNDEFINED;
	if (TestTrue(TEXT("Function is created"), Host->Evaluate(TEXT("(function(x) { return x + 1; })"), TEXT("function-test.js"), EQuickJSSourceType::Script, Function, Error)))
	{
		JSValue Argument = JS_NewInt32(Context, 41);
		if (TestTrue(TEXT("C++ calls JavaScript"), Host->Call(Function, JS_UNDEFINED, 1, &Argument, Value, Error)))
		{
			TestTrue(TEXT("Call result converts"), ReadInt(Context, Value, Number));
			TestEqual(TEXT("Call result"), Number, 42);
			JS_FreeValue(Context, Value);
		}
		JS_FreeValue(Context, Argument);
		JS_FreeValue(Context, Function);
	}
	if (TestTrue(TEXT("Self-contained module evaluates"), Host->Evaluate(TEXT("globalThis.moduleRan = 1;"), TEXT("module-test.mjs"), EQuickJSSourceType::Module, Value, Error)))
	{
		JS_FreeValue(Context, Value);
	}
	TestFalse(TEXT("External module imports are not configured"), Host->Evaluate(TEXT("import 'missing-module';"), TEXT("missing.mjs"), EQuickJSSourceType::Module, Value, Error));
	TestFalse(TEXT("Missing module reports an exception"), Error.Message.IsEmpty());
	const char RawSource[] = "throw new Error('raw exception')";
	JSValue RawResult = JS_Eval(Context, RawSource, sizeof(RawSource) - 1, "raw.js", JS_EVAL_TYPE_GLOBAL);
	TestTrue(TEXT("Raw evaluation throws"), JS_IsException(RawResult));
	TestTrue(TEXT("Raw exception can be consumed"), Host->TakeException(Error));
	TestTrue(TEXT("Raw exception message is preserved"), Error.Message.Contains(TEXT("raw exception")));
	TestFalse(TEXT("No exception remains"), Host->TakeException(Error));
	Host->Close();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuickJSRuntimeJobsTest,
	"QuickJSUnreal.Runtime.PendingJobs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FQuickJSRuntimeJobsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FString CreateError;
	TUniquePtr<FQuickJSRuntime> Host = FQuickJSRuntime::Create(CreateError);
	if (!TestTrue(TEXT("Runtime is created"), Host.IsValid())) { AddError(CreateError); return false; }
	JSContext* Context = Host->GetContext();
	FQuickJSError Error;
	JSValue Value = JS_UNDEFINED;
	if (!TestTrue(TEXT("Promise callbacks can be queued"), Host->Evaluate(
		TEXT("globalThis.jobCount = 0; Promise.resolve().then(() => jobCount++); Promise.resolve().then(() => jobCount++);"),
		TEXT("jobs.js"), EQuickJSSourceType::Script, Value, Error))) { return false; }
	JS_FreeValue(Context, Value);
	int32 Executed = 0;
	bool bPending = false;
	TestFalse(TEXT("Negative job budget is rejected"), Host->ExecutePendingJobs(-1, Executed, bPending, Error));
	TestTrue(TEXT("Zero budget reports pending jobs"), Host->ExecutePendingJobs(0, Executed, bPending, Error));
	TestEqual(TEXT("Zero budget executes no jobs"), Executed, 0);
	TestTrue(TEXT("Jobs are pending"), bPending);
	if (TestTrue(TEXT("Read state before pumping"), Host->Evaluate(TEXT("jobCount"), TEXT("read.js"), EQuickJSSourceType::Script, Value, Error)))
	{
		int32 Number = -1;
		TestTrue(TEXT("Initial value converts"), ReadInt(Context, Value, Number));
		TestEqual(TEXT("Promise has not run automatically"), Number, 0);
		JS_FreeValue(Context, Value);
	}
	TestTrue(TEXT("First bounded pump succeeds"), Host->ExecutePendingJobs(1, Executed, bPending, Error));
	TestEqual(TEXT("First pump executes one job"), Executed, 1);
	TestTrue(TEXT("One job remains"), bPending);
	TestTrue(TEXT("Second bounded pump succeeds"), Host->ExecutePendingJobs(1, Executed, bPending, Error));
	TestEqual(TEXT("Second pump executes one job"), Executed, 1);
	TestFalse(TEXT("Queue is empty"), bPending);
	if (TestTrue(TEXT("Read state after pumping"), Host->Evaluate(TEXT("jobCount"), TEXT("read.js"), EQuickJSSourceType::Script, Value, Error)))
	{
		int32 Number = -1;
		TestTrue(TEXT("Updated value converts"), ReadInt(Context, Value, Number));
		TestEqual(TEXT("Both promise callbacks ran"), Number, 2);
		JS_FreeValue(Context, Value);
	}
	TestTrue(TEXT("Failing job is enqueued"), JS_EnqueueJob(Context, FailingJob, 0, nullptr) >= 0);
	TestFalse(TEXT("Job exception is returned"), Host->ExecutePendingJobs(1, Executed, bPending, Error));
	TestEqual(TEXT("Failing job counts toward the budget"), Executed, 1);
	TestTrue(TEXT("Job exception message is preserved"), Error.Message.Contains(TEXT("job failure")));
	Host->Close();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuickJSRuntimeLifecycleTest,
	"QuickJSUnreal.Runtime.Lifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FQuickJSRuntimeLifecycleTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	for (int32 Index = 0; Index < 3; ++Index)
	{
		FString CreateError;
		TUniquePtr<FQuickJSRuntime> Host = FQuickJSRuntime::Create(CreateError);
		if (!TestTrue(TEXT("Runtime is created"), Host.IsValid())) { AddError(CreateError); return false; }
		TestTrue(TEXT("Runtime is open"), Host->IsOpen());
		Host->Close();
		Host->Close();
		TestFalse(TEXT("Runtime is closed"), Host->IsOpen());
		TestNull(TEXT("Closed runtime has no context"), Host->GetContext());
		FQuickJSError Error;
		JSValue Value = JS_UNDEFINED;
		TestFalse(TEXT("Closed runtime rejects evaluation"), Host->Evaluate(TEXT("1"), TEXT("closed.js"), EQuickJSSourceType::Script, Value, Error));
		TestFalse(TEXT("Closed runtime rejects calls"), Host->Call(JS_UNDEFINED, JS_UNDEFINED, 0, nullptr, Value, Error));
		int32 Executed = -1;
		bool bPending = true;
		TestFalse(TEXT("Closed runtime rejects job pumping"), Host->ExecutePendingJobs(1, Executed, bPending, Error));
	}
	return true;
}

#endif
