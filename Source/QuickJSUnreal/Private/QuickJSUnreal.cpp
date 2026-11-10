#include "QuickJSUnreal.h"

#include "QuickJSRuntime.h"
#include "HAL/PlatformProcess.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogQuickJSUnreal, Log, All);

void FQuickJSUnrealModule::StartupModule()
{
	if (!IsInGameThread()) { UE_LOG(LogQuickJSUnreal, Fatal, TEXT("QuickJSUnreal must start on the game thread.")); }
	bShuttingDown = false;
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("QuickJSUnreal"));
	if (!Plugin)
	{
		UE_LOG(LogQuickJSUnreal, Error, TEXT("QuickJSUnreal plugin directory could not be found."));
		return;
	}
	const FString LibraryPath = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Binaries/ThirdParty/QuickJS/Win64/qjs.dll"));
	LibraryHandle = FPlatformProcess::GetDllHandle(*LibraryPath);
	if (!LibraryHandle)
	{
		UE_LOG(LogQuickJSUnreal, Error, TEXT("Could not load QuickJS-NG DLL: %s"), *LibraryPath);
	}
}

void FQuickJSUnrealModule::ShutdownModule()
{
	if (!IsInGameThread()) { UE_LOG(LogQuickJSUnreal, Fatal, TEXT("QuickJSUnreal must shut down on the game thread.")); }
	bShuttingDown = true;
	while (!LiveRuntimes.IsEmpty())
	{
		FQuickJSRuntime* Runtime = *LiveRuntimes.CreateIterator();
		if (Runtime->bExecuting)
		{
			UE_LOG(LogQuickJSUnreal, Fatal, TEXT("Cannot unload QuickJSUnreal from an active QuickJS callback."));
		}
		Runtime->Close();
	}
	if (LibraryHandle)
	{
		FPlatformProcess::FreeDllHandle(LibraryHandle);
		LibraryHandle = nullptr;
	}
}

void FQuickJSUnrealModule::RegisterRuntime(FQuickJSRuntime* Runtime)
{
	check(IsInGameThread());
	check(IsLibraryReady());
	LiveRuntimes.Add(Runtime);
}

void FQuickJSUnrealModule::UnregisterRuntime(FQuickJSRuntime* Runtime)
{
	check(IsInGameThread());
	LiveRuntimes.Remove(Runtime);
}

IMPLEMENT_MODULE(FQuickJSUnrealModule, QuickJSUnreal)
