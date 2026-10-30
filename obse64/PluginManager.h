#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <unordered_map>

#include "obse64/PluginAPI.h"
#include "obse64_common/Types.h"

#include <Windows.h>

class PluginManager
{
public:
	PluginManager();
	~PluginManager();

	enum
	{
		kPhase_Preload = 0,
		kPhase_Load,

		kPhase_Num,
	};

	void	init();
	void	installPlugins(u32 phase);
	void	loadComplete();
	void	deinit();

	const PluginInfo *	infoByName(const char * name) const;

	const char *	pluginNameFromHandle(PluginHandle handle) const;
	PluginHandle	lookupHandleFromName(const char * pluginName) const;

	// interface handlers
	static void *				queryInterface(u32 id);
	static PluginHandle			getPluginHandle();
	static u32					getReleaseIndex();
	static const PluginInfo *	getPluginInfo(const char* name);
	static const char*			getSaveFolderName();

	static bool dispatchMessage(PluginHandle sender, u32 messageType, void * data, u32 dataLen, const char* receiver);
	static bool	registerListener(PluginHandle listener, const char* sender, OBSEMessagingInterface::EventCallback handler);

private:
	struct LoadedPlugin
	{
		LoadedPlugin();

		std::string dllName;

		HMODULE		handle = 0;
		PluginInfo	info;
		u32			internalHandle = 0;

		OBSEPluginVersionData	version;

		_OBSEPlugin_Load	load[kPhase_Num] = { nullptr };

		const char		* errorState = nullptr;
		u32				errorCode = 0;

		bool	hasLoad = false;
		bool	hasPreload = false;
	};

	bool	findPluginDirectory(void);
	void	scanPlugins(void);

	const char *	safeCallLoadPlugin(LoadedPlugin * plugin, const OBSEInterface * obse64, u32 phase);

	void			sanitize(OBSEPluginVersionData * version);
	const char *	checkPluginCompatibility(const OBSEPluginVersionData & version);
	const char *	checkAddressLibrary(void);

	void			logPluginLoadError(const LoadedPlugin & plugin, const char * errStr, u32 errCode = 0, bool isError = true);
	void			reportPluginErrors();
	void			updateAddressLibraryPrompt();

	// takes a plugin's message handlers out of every sender's list, before it's unloaded
	static void		removeListeners(PluginHandle listener);

	typedef std::vector <LoadedPlugin>	LoadedPluginList;

	std::string			m_pluginDirectory;
	LoadedPluginList	m_plugins;

	LoadedPluginList	m_erroredPlugins;

	// A plugin keeps the handle it's given here, in scan order, for good: one that fails to load
	// leaves the list, so its handle is no guide to where the others are in it.
	PluginHandle		m_nextHandle = 1;	// 0 is OBSE's own

	bool				m_oldAddressLibrary = false;

	static PluginHandle		s_currentPluginHandle;
};

class BranchTrampolineManager
{
public:
	BranchTrampolineManager(BranchTrampoline& trampoline) :
		m_trampoline(trampoline)
	{}

	void* allocate(PluginHandle plugin, size_t size);

private:
	BranchTrampoline& m_trampoline;
	std::mutex m_lock;
	std::unordered_map<PluginHandle, size_t> m_stats;
};

extern BranchTrampolineManager g_branchTrampolineManager;
extern BranchTrampolineManager g_localTrampolineManager;

void * AllocateFromOBSEBranchPool(PluginHandle plugin, size_t size);
void * AllocateFromOBSELocalPool(PluginHandle plugin, size_t size);

extern PluginManager	g_pluginManager;
