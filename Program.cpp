// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.
// See the LICENSE file in the project root for more information.

// Standard headers
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <iostream>

// Provided by the AppHost NuGet package and installed as an SDK pack
#include <nethost.h>

// Header files copied from https://github.com/dotnet/core-setup
#include <coreclr_delegates.h>
#include <hostfxr.h>

#include <Windows.h>

#define DIR_SEPARATOR L'\\'

using string_t = std::basic_string<char_t>;

namespace
{
	// Globals to hold hostfxr exports
	hostfxr_initialize_for_runtime_config_fn init_fptr;
	hostfxr_get_runtime_delegate_fn get_delegate_fptr;
	hostfxr_close_fn close_fptr;

	// Forward declarations
	bool load_hostfxr();
	load_assembly_and_get_function_pointer_fn get_dotnet_load_assembly(const char_t *assembly);
}

bool FileExists(string_t szPath)
{
	DWORD dwAttrib = GetFileAttributes(szPath.c_str());

	return (dwAttrib != INVALID_FILE_ATTRIBUTES &&
		!(dwAttrib & FILE_ATTRIBUTE_DIRECTORY));
}

int __cdecl wmain(int argc, wchar_t *argv[])
{
	// Get the current executable's directory
	// This sample assumes the managed assembly to load and its runtime configuration file are next to the host
	char_t host_path[MAX_PATH];

	auto size = ::GetFullPathNameW(argv[0], sizeof(host_path) / sizeof(char_t), host_path, nullptr);
	assert(size != 0);

	string_t root_path = host_path;
	auto pos = root_path.find_last_of(DIR_SEPARATOR);
	assert(pos != string_t::npos);
	root_path = root_path.substr(0, pos + 1);

	//
	// STEP 1: Load HostFxr and get exported hosting functions
	//
	if (!load_hostfxr())
	{
		assert(false && "Failure: load_hostfxr()");
		return EXIT_FAILURE;
	}

	//
	// STEP 2: Initialize and start the .NET Core runtime
	//
	const string_t config_path = root_path + L"CefSharp.BrowserSubProcess.NetCore.runtimeconfig.json";
	load_assembly_and_get_function_pointer_fn load_assembly_and_get_function_pointer = nullptr;
	load_assembly_and_get_function_pointer = get_dotnet_load_assembly(config_path.c_str());
	assert(load_assembly_and_get_function_pointer != nullptr && "Failure: get_dotnet_load_assembly()");

	//
	// STEP 3: Load managed assembly and get function pointer to a managed method
	//
	const char_t *dotnet_type = L"CefSharp.BrowserSubprocess.BrowserSubprocessExecutable, CefSharp.BrowserSubprocess.Core";
	const char_t *dotnet_type_method = L"MainNetCore";
	string_t dotnetlib_path = root_path + L"CefSharp.BrowserSubprocess.Core.dll";

	//If the dll isn't next to our exe then it's likely we've been deployed
	//via the Nuget package and are in a folder a few levels depper
	//So we must go up the folder tree in search of our dll
	// e.g. runtimes\win-x64\native
	if (!FileExists(dotnetlib_path))
	{
		dotnetlib_path = root_path + L"..\\..\\..\\CefSharp.BrowserSubprocess.Core.dll";
	}

	// Function pointer to managed delegate
	component_entry_point_fn mainNetCore = nullptr;
	int rc = load_assembly_and_get_function_pointer(
		dotnetlib_path.c_str(),
		dotnet_type,
		dotnet_type_method,
		nullptr /*delegate_type_name*/,
		nullptr,
		(void**)&mainNetCore);
	assert(rc == 0 && mainNetCore != nullptr && "Failure: load_assembly_and_get_function_pointer()");

	int exitCode = mainNetCore(0, 0);

	return exitCode;
}

/********************************************************************************************
 * Function used to load and activate .NET Core
 ********************************************************************************************/

namespace
{
	// Forward declarations
	void *load_library(const char_t *);
	void *get_export(void *, const char *);

	void *load_library(const char_t *path)
	{
		HMODULE h = ::LoadLibraryW(path);
		assert(h != nullptr);
		return (void*)h;
	}
	void *get_export(void *h, const char *name)
	{
		void *f = ::GetProcAddress((HMODULE)h, name);
		assert(f != nullptr);
		return f;
	}

	// Using the nethost library, discover the location of hostfxr and get exports
	bool load_hostfxr()
	{
		// Pre-allocate a large buffer for the path to hostfxr
		char_t buffer[MAX_PATH];
		size_t buffer_size = sizeof(buffer) / sizeof(char_t);
		int rc = get_hostfxr_path(buffer, &buffer_size, nullptr);
		if (rc != 0)
			return false;

		// Load hostfxr and get desired exports
		void *lib = load_library(buffer);
		init_fptr = (hostfxr_initialize_for_runtime_config_fn)get_export(lib, "hostfxr_initialize_for_runtime_config");
		get_delegate_fptr = (hostfxr_get_runtime_delegate_fn)get_export(lib, "hostfxr_get_runtime_delegate");
		close_fptr = (hostfxr_close_fn)get_export(lib, "hostfxr_close");

		return (init_fptr && get_delegate_fptr && close_fptr);
	}

	// Load and initialize .NET Core and get desired function pointer for scenario
	load_assembly_and_get_function_pointer_fn get_dotnet_load_assembly(const char_t *config_path)
	{
		// Load .NET Core
		void *load_assembly_and_get_function_pointer = nullptr;
		hostfxr_handle cxt = nullptr;
		int rc = init_fptr(config_path, nullptr, &cxt);
		if (rc != 0 || cxt == nullptr)
		{
			std::cerr << "Init failed: " << std::hex << std::showbase << rc << std::endl;
			close_fptr(cxt);
			return nullptr;
		}

		// Get the load assembly function pointer
		rc = get_delegate_fptr(
			cxt,
			hdt_load_assembly_and_get_function_pointer,
			&load_assembly_and_get_function_pointer);
		if (rc != 0 || load_assembly_and_get_function_pointer == nullptr)
			std::cerr << "Get delegate failed: " << std::hex << std::showbase << rc << std::endl;

		close_fptr(cxt);
		return (load_assembly_and_get_function_pointer_fn)load_assembly_and_get_function_pointer;
	}
}
