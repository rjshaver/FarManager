﻿#ifndef PLCLASS_HPP_E324EC16_24F2_4402_BA87_74212799246D
#define PLCLASS_HPP_E324EC16_24F2_4402_BA87_74212799246D
#pragma once

/*
plclass.hpp
*/
/*
Copyright © 1996 Eugene Roshal
Copyright © 2000 Far Group
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:
1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.
3. The name of the authors may not be used to endorse or promote products
   derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "bitflags.hpp"
#include "windowsfwd.hpp"
#include "farexcpt.hpp"
#include "exception.hpp"

enum class lng : int;
class PluginManager;
class Plugin;
class language;
class PluginsCacheConfig;

std::exception_ptr& GlobalExceptionPtr();

namespace detail
{
	template<typename callable, typename fallback>
	auto invoke_and_store_exception(callable&& Callable, fallback&& Fallback)
	{
		try
		{
			return Callable();
		}
		CATCH_AND_SAVE_EXCEPTION_TO(GlobalExceptionPtr())
		return Fallback();
	}
}

template<typename callable>
auto invoke_and_store_exception(callable&& Callable)
{
	return detail::invoke_and_store_exception(FWD(Callable), [] {});
}

template<typename callable, typename fallback>
auto invoke_and_store_exception(fallback Result, callable&& Callable)
{
	using result_type = std::common_type_t<std::result_of_t<callable()>, fallback>;
	return detail::invoke_and_store_exception(
		FWD(Callable),
		[&]() -> result_type { return Result; }
	);
}

enum EXPORTS_ENUM
{
	iGetGlobalInfo,
	iSetStartupInfo,
	iOpen,
	iClosePanel,
	iGetPluginInfo,
	iGetOpenPanelInfo,
	iGetFindData,
	iFreeFindData,
	iGetVirtualFindData,
	iFreeVirtualFindData,
	iSetDirectory,
	iGetFiles,
	iPutFiles,
	iDeleteFiles,
	iMakeDirectory,
	iProcessHostFile,
	iSetFindList,
	iConfigure,
	iExitFAR,
	iProcessPanelInput,
	iProcessPanelEvent,
	iProcessEditorEvent,
	iCompare,
	iProcessEditorInput,
	iProcessViewerEvent,
	iProcessDialogEvent,
	iProcessSynchroEvent,
	iProcessConsoleInput,
	iAnalyse,
	iCloseAnalyse,
	iGetContentFields,
	iGetContentData,
	iFreeContentData,

	iOpenFilePlugin,
	iGetMinFarVersion,

	ExportsCount
};

enum PLUGINWORKFLAGS
{
	PIWF_LOADED        = bit(0), // DLL загружена
	PIWF_CACHED        = bit(1), // кешируется
	PIWF_PRELOADED     = bit(2), //
	PIWF_DONTLOADAGAIN = bit(3), // не загружать плагин снова, ставится в результате проверки требуемой версии фара
	PIWF_DATALOADED    = bit(4), // LoadData успешно выполнилась
};

extern PluginStartupInfo NativeInfo;
extern FarStandardFunctions NativeFSF;

class i_plugin_module
{
public:
	virtual ~i_plugin_module() = default;
	virtual void* get_opaque() const = 0;
};

class plugin_factory
{
public:
	explicit plugin_factory(PluginManager* owner);
	using plugin_module_ptr = std::unique_ptr<i_plugin_module>;
	using function_address = void*;
	using exports_array = std::array<std::pair<function_address, bool>, ExportsCount>;

	struct export_name
	{
		string_view UName;
		basic_string_view<char> AName;
	};

	virtual ~plugin_factory() = default;

	virtual std::unique_ptr<Plugin> CreatePlugin(const string& filename);

	virtual bool IsPlugin(const string& filename) const = 0;
	virtual plugin_module_ptr Create(const string& filename) = 0;
	virtual bool Destroy(plugin_module_ptr& module) = 0;
	virtual function_address GetFunction(const plugin_module_ptr& Instance, const export_name& Name) = 0;

	virtual void ProcessError(const string_view& Function) const {}

	auto GetOwner() const { return m_owner; }
	const auto& ExportsNames() const { return m_ExportsNames; }

protected:
	range<const export_name*> m_ExportsNames;
	PluginManager* const m_owner;
};

using plugin_factory_ptr = std::unique_ptr<plugin_factory>;

class native_plugin_module: public i_plugin_module
{
public:
	NONCOPYABLE(native_plugin_module);
	explicit native_plugin_module(const string& Name): m_Module(Name, true) {}
	virtual void* get_opaque() const override { return nullptr; }

	os::rtdl::module m_Module;
};

class native_plugin_factory : public plugin_factory
{
public:
	NONCOPYABLE(native_plugin_factory);
	using plugin_factory::plugin_factory;

	virtual bool IsPlugin(const string& filename) const override;
	virtual plugin_module_ptr Create(const string& filename) override;
	virtual bool Destroy(plugin_module_ptr& module) override;
	virtual function_address GetFunction(const plugin_module_ptr& Instance, const export_name& Name) override;

private:
	// the rest shouldn't be here, just an optimization for OEM plugins
	bool IsPlugin2(const void* Module) const;
	virtual bool FindExport(const basic_string_view<char>& ExportName) const;
};

template<EXPORTS_ENUM id, bool Native>
struct prototype;

template<EXPORTS_ENUM id, bool Native>
using prototype_t = typename prototype<id, Native>::type;

namespace detail
{
	struct ExecuteStruct
	{
		void operator=(intptr_t value) { Result = value; }
		void operator=(HANDLE value) { Result = reinterpret_cast<intptr_t>(value); }
		operator intptr_t() const { return Result; }
		operator void*() const { return ToPtr(Result); }
		EXPORTS_ENUM id;
		intptr_t Result;
	};

	template<typename function, typename... args, REQUIRES(std::is_void_v<std::result_of_t<function(args...)>>)>
	void ExecuteFunctionImpl(ExecuteStruct&, const function& Function, args&&... Args)
	{
		Function(FWD(Args)...);
	}

	template<typename function, typename... args, REQUIRES(!std::is_void_v<std::result_of_t<function(args...)>>)>
	void ExecuteFunctionImpl(ExecuteStruct& es, const function& Function, args&&... Args)
	{
		es = Function(FWD(Args)...);
	}
}

class Plugin: noncopyable
{
public:
	NONCOPYABLE(Plugin);
	Plugin(plugin_factory* Factory, const string& ModuleName);
	virtual ~Plugin() = default;

	virtual bool GetGlobalInfo(GlobalInfo *Info);
	virtual bool SetStartupInfo(PluginStartupInfo *Info);
	virtual void* Open(OpenInfo* Info);
	virtual void ClosePanel(ClosePanelInfo* Info);
	virtual bool GetPluginInfo(PluginInfo *pi);
	virtual void GetOpenPanelInfo(OpenPanelInfo *Info);
	virtual int GetFindData(GetFindDataInfo* Info);
	virtual void FreeFindData(FreeFindDataInfo* Info);
	virtual int GetVirtualFindData(GetVirtualFindDataInfo* Info);
	virtual void FreeVirtualFindData(FreeFindDataInfo* Info);
	virtual int SetDirectory(SetDirectoryInfo* Info);
	virtual int GetFiles(GetFilesInfo* Info);
	virtual int PutFiles(PutFilesInfo* Info);
	virtual int DeleteFiles(DeleteFilesInfo* Info);
	virtual int MakeDirectory(MakeDirectoryInfo* Info);
	virtual int ProcessHostFile(ProcessHostFileInfo* Info);
	virtual int SetFindList(SetFindListInfo* Info);
	virtual int Configure(ConfigureInfo* Info);
	virtual void ExitFAR(ExitInfo *Info);
	virtual int ProcessPanelInput(ProcessPanelInputInfo* Info);
	virtual int ProcessPanelEvent(ProcessPanelEventInfo* Info);
	virtual int ProcessEditorEvent(ProcessEditorEventInfo* Info);
	virtual int Compare(CompareInfo* Info);
	virtual int ProcessEditorInput(ProcessEditorInputInfo* Info);
	virtual int ProcessViewerEvent(ProcessViewerEventInfo* Info);
	virtual int ProcessDialogEvent(ProcessDialogEventInfo* Info);
	virtual int ProcessSynchroEvent(ProcessSynchroEventInfo* Info);
	virtual int ProcessConsoleInput(ProcessConsoleInputInfo *Info);
	virtual void* Analyse(AnalyseInfo *Info);
	virtual void CloseAnalyse(CloseAnalyseInfo* Info);

	virtual int GetContentFields(GetContentFieldsInfo *Info);
	virtual int GetContentData(GetContentDataInfo *Info);
	virtual void FreeContentData(GetContentDataInfo *Info);

	virtual void* OpenFilePlugin(const wchar_t *Name, const unsigned char *Data, size_t DataSize, int OpMode);
	virtual bool CheckMinFarVersion();


#ifndef NO_WRAPPER
	virtual bool IsOemPlugin() const { return false; }
#endif // NO_WRAPPER
	virtual const string& GetHotkeyName() const { return m_strGuid; }

	virtual bool InitLang(const string& Path);
	void CloseLang();

	bool has(EXPORTS_ENUM id) const { return Exports[id].second; }
	bool has(const detail::ExecuteStruct& es) const { return has(es.id); }

	const string& GetModuleName() const { return m_strModuleName; }
	const string& GetCacheName() const  { return m_strCacheName; }
	const string& GetTitle() const { return strTitle; }
	const string& GetDescription() const { return strDescription; }
	const string& GetAuthor() const { return strAuthor; }
	const VersionInfo& GetVersion() const { return PluginVersion; }
	const VersionInfo& GetMinFarVersion() const { return MinFarVersion; }
	const string& GetVersionString() const { return VersionString; }
	const GUID& GetGUID() const { return m_Guid; }
	bool IsPendingRemove() const { return bPendingRemove; }
	const wchar_t *GetMsg(intptr_t Id) const;

	bool CheckWorkFlags(DWORD flags) const { return WorkFlags.Check(flags); }

	bool Load();
	int Unload(bool bExitFAR = false);
	bool LoadData();
	bool LoadFromCache(const os::fs::find_data &FindData);
	bool SaveToCache();
	bool IsPanelPlugin();
	bool Active() const {return Activity != 0;}
	void AddDialog(const window_ptr& Dlg);
	bool RemoveDialog(const window_ptr& Dlg);
	auto keep_activity() { return make_raii_wrapper(this, [](Plugin* p){ ++p->Activity; }, [](Plugin* p){ --p->Activity; });  }

protected:
	template<EXPORTS_ENUM ExportId, bool Native = true>
	struct ExecuteStruct: detail::ExecuteStruct
	{
		explicit ExecuteStruct(intptr_t FallbackValue = 0)
		{
			id = ExportId;
			Result = FallbackValue;
		}
		using export_id = std::integral_constant<EXPORTS_ENUM, ExportId>;
		using native = std::integral_constant<bool, Native>;
	};

	template<typename T, class... args>
	void ExecuteFunctionSeh(T& es, args&&... Args)
	{
		Prologue(); ++Activity;
		SCOPE_EXIT{ --Activity; Epilogue(); };

		const auto& ProcessException = [&](const auto& Handler, auto&&... ProcArgs)
		{
			Handler(FWD(ProcArgs)..., m_Factory->ExportsNames()[T::export_id::value].UName, this)? HandleFailure(T::export_id::value) : throw;
		};

		try
		{
			detail::ExecuteFunctionImpl(es, reinterpret_cast<prototype_t<T::export_id::value, T::native::value>>(Exports[T::export_id::value].first), FWD(Args)...);
			RethrowIfNeeded(GlobalExceptionPtr());
			m_Factory->ProcessError(m_Factory->ExportsNames()[T::export_id::value].UName);
		}
		catch (const std::exception& e)
		{
			ProcessException(ProcessStdException, e);
		}
		catch (...)
		{
			ProcessException(ProcessUnknownException);
		}
	}

	template<typename T, typename... args>
	void ExecuteFunction(T& es, args&&... Args)
	{
		seh_invoke_with_ui(
		[&]
		{
			// No FWD macro here - gcc 5.1 crash
			ExecuteFunctionSeh(es, std::forward<args>(Args)...);
		},
		[this]
		{
			HandleFailure(T::export_id::value);
		},
		m_Factory->ExportsNames()[T::export_id::value].UName, this);
	}

	void HandleFailure(EXPORTS_ENUM id);

	virtual void Prologue() {}
	virtual void Epilogue() {}

	plugin_factory::exports_array Exports;

	std::unordered_set<window_ptr> m_dialogs;
	plugin_factory* m_Factory;
	plugin_factory::plugin_module_ptr m_Instance;
	std::unique_ptr<language> PluginLang;
	size_t Activity;
	bool bPendingRemove;

private:
	friend class PluginManager;

	void InitExports();
	void ClearExports();
	void SetGuid(const GUID& Guid);

	template<typename T>
	void SetInstance(T* Object) const
	{
		Object->Instance = m_Instance->get_opaque();
	}

	string strTitle;
	string strDescription;
	string strAuthor;

	string m_strModuleName;
	string m_strCacheName;

	BitFlags WorkFlags;      // рабочие флаги текущего плагина

	VersionInfo MinFarVersion;
	VersionInfo PluginVersion;

	string VersionString;

	GUID m_Guid;
	string m_strGuid;
};

plugin_factory_ptr CreateCustomPluginFactory(PluginManager* Owner, const string& Filename);

#define DECLARE_GEN_PLUGIN_FUNCTION(name, is_native, signature) template<> struct prototype<name, is_native>  { using type = signature; };
#define WA(string) { L##string##_sv, string##_sv }

#endif // PLCLASS_HPP_E324EC16_24F2_4402_BA87_74212799246D
