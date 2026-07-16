#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace
{
constexpr std::uint16_t CPPRO_VENDOR_ID = 0x361D;
constexpr std::uint16_t CPPRO_PRODUCT_ID = 0x0200;
constexpr std::uint64_t CPPRO_DEVICE_ID =
	(static_cast<std::uint64_t>(CPPRO_VENDOR_ID) << 16) | CPPRO_PRODUCT_ID;

enum class DeviceEventType : int { Connected = 1, Disconnected = 2 };
enum class DeviceType : int { Keyboard = 1, Keypad = 2, Other = 3 };

struct DeviceInfo
{
	std::uint16_t vendor_id;
	std::uint16_t product_id;
	const char* manufacturer_name;
	const char* device_name;
	std::uint64_t device_id;
	DeviceType device_type;
};

using EventHandler = void (*)(void*, DeviceEventType, void*);
using NameFn = const char* (*)();
using IsInitialisedFn = bool (*)();
using InitialiseFn = int (*)(void*, EventHandler);
using DeviceInfoFn = int (*)(void**, std::uint32_t);
using ReadFullBufferFn = int (*)(std::uint16_t*, float*, std::uint32_t, std::uint64_t);
using UnloadFn = void (*)();

void eventHandler(void*, DeviceEventType, void*) {}

template <typename T>
T getExport(HMODULE module, const char* name)
{
	auto* address = GetProcAddress(module, name);
	if (address == nullptr)
	{
		throw std::runtime_error(std::string("Missing export: ") + name);
	}
	return reinterpret_cast<T>(address);
}
}

int main(int argc, char** argv)
{
	if (argc < 3 || argc > 4)
	{
		std::cerr << "Usage: release_dll_smoke <plugin.dll> <capture-seconds> [--require-input]\n";
		return 2;
	}

	const auto capture_seconds = std::stoi(argv[2]);
	if (capture_seconds < 0)
	{
		throw std::runtime_error("Capture duration cannot be negative");
	}
	const bool require_input = argc == 4 && std::strcmp(argv[3], "--require-input") == 0;

	HMODULE module = LoadLibraryA(argv[1]);
	if (module == nullptr)
	{
		std::cerr << "LoadLibrary failed with Windows error " << GetLastError() << '\n';
		return 1;
	}

	UnloadFn unload = nullptr;
	bool initialised = false;
	try
	{
		const auto* abi_version = getExport<const std::uint32_t*>(module, "ANALOG_SDK_PLUGIN_ABI_VERSION");
		if (*abi_version > 1)
		{
			throw std::runtime_error("Unsupported plugin ABI version");
		}

		const bool abi1 = *abi_version == 1;
		const auto name = getExport<NameFn>(module, abi1 ? "name" : "_name");
		const auto is_initialised = getExport<IsInitialisedFn>(module, "is_initialised");
		const auto initialise = getExport<InitialiseFn>(module, abi1 ? "initialise" : "_initialise");
		const auto device_info = getExport<DeviceInfoFn>(module, abi1 ? "device_info" : "_device_info");
		const auto read_full_buffer = getExport<ReadFullBufferFn>(module, abi1 ? "read_full_buffer" : "_read_full_buffer");
		unload = getExport<UnloadFn>(module, "unload");

		if (std::strcmp(name(), "Universal Analog Plugin") != 0 || !is_initialised())
		{
			throw std::runtime_error("Plugin identity or initialisation status export is invalid");
		}

		const int initial_devices = initialise(nullptr, eventHandler);
		initialised = true;
		if (initial_devices < 1)
		{
			throw std::runtime_error("No supported analog keyboard was discovered");
		}

		void* devices[32]{};
		const int device_count = device_info(devices, static_cast<std::uint32_t>(std::size(devices)));
		if (device_count < 1 || device_count > initial_devices)
		{
			throw std::runtime_error("device_info returned an invalid device count");
		}

		if (abi1)
		{
			bool found_cppro = false;
			for (int i = 0; i < device_count; ++i)
			{
				const auto* info = static_cast<const DeviceInfo*>(devices[i]);
				if (info->vendor_id == CPPRO_VENDOR_ID && info->product_id == CPPRO_PRODUCT_ID)
				{
					found_cppro = info->device_id == CPPRO_DEVICE_ID
						&& info->device_type == DeviceType::Keyboard
						&& info->device_name != nullptr
						&& std::strstr(info->device_name, "Centerpiece") != nullptr;
				}
			}
			if (!found_cppro)
			{
				throw std::runtime_error("ABI 1 metadata did not identify the connected CPPRO");
			}
		}

		std::uint64_t samples = 0;
		float maximum = 0.0f;
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(capture_seconds);
		do
		{
			std::uint16_t codes[256]{};
			float analogues[256]{};
			const int count = read_full_buffer(codes, analogues, static_cast<std::uint32_t>(std::size(codes)), CPPRO_DEVICE_ID);
			if (count < 0 || count > static_cast<int>(std::size(codes)))
			{
				throw std::runtime_error("read_full_buffer returned an invalid sample count");
			}
			for (int i = 0; i < count; ++i)
			{
				if (!std::isfinite(analogues[i]) || analogues[i] < 0.0f || analogues[i] > 1.0f)
				{
					throw std::runtime_error("read_full_buffer returned an out-of-range analog value");
				}
				if (analogues[i] > 0.0f)
				{
					++samples;
					maximum = std::max(maximum, analogues[i]);
				}
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		while (std::chrono::steady_clock::now() < deadline);

		if (require_input && samples == 0)
		{
			throw std::runtime_error("No non-zero CPPRO analog samples were captured");
		}

		unload();
		initialised = false;
		std::cout << argv[1] << ": ABI " << *abi_version
			<< ", devices " << device_count
			<< ", non-zero samples " << samples
			<< ", maximum " << maximum << " - PASS\n";
		FreeLibrary(module);
		return 0;
	}
	catch (const std::exception& error)
	{
		if (initialised && unload != nullptr)
		{
			unload();
		}
		FreeLibrary(module);
		std::cerr << argv[1] << ": " << error.what() << '\n';
		return 1;
	}
}
