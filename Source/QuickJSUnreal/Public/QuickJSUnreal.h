#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FQuickJSRuntime;

/** Loads the shared engine and closes live hosts before unloading it. */
class QUICKJSUNREAL_API FQuickJSUnrealModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	friend class FQuickJSRuntime;
	bool IsLibraryReady() const { return LibraryHandle != nullptr && !bShuttingDown; }
	void RegisterRuntime(FQuickJSRuntime* Runtime);
	void UnregisterRuntime(FQuickJSRuntime* Runtime);

	void* LibraryHandle = nullptr;
	bool bShuttingDown = false;
	TSet<FQuickJSRuntime*> LiveRuntimes;
};
