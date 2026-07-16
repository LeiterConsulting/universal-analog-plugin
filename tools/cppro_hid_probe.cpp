#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cfgmgr32.h>
#include <hidsdi.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <iomanip>
#include <iostream>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace
{
constexpr USHORT FINALMOUSE_VID = 0x361D;
constexpr USHORT CPPRO_MCU_PID = 0x0200;

std::vector<std::wstring> get_hid_paths()
{
	GUID hid_guid;
	HidD_GetHidGuid(&hid_guid);

	ULONG chars = 0;
	if (CM_Get_Device_Interface_List_SizeW(&chars, &hid_guid, nullptr, CM_GET_DEVICE_INTERFACE_LIST_PRESENT) != CR_SUCCESS)
	{
		return {};
	}

	std::vector<wchar_t> paths(chars);
	if (CM_Get_Device_Interface_ListW(&hid_guid, nullptr, paths.data(), chars, CM_GET_DEVICE_INTERFACE_LIST_PRESENT) != CR_SUCCESS)
	{
		return {};
	}

	std::vector<std::wstring> result;
	for (const wchar_t* path = paths.data(); *path != L'\0'; path += std::wcslen(path) + 1)
	{
		result.emplace_back(path);
	}
	return result;
}

std::wstring get_product_name(HANDLE device)
{
	wchar_t buffer[256]{};
	if (HidD_GetProductString(device, buffer, sizeof(buffer)))
	{
		return buffer;
	}
	return L"(unknown)";
}

std::set<UCHAR> get_report_ids(HANDLE device, HIDP_REPORT_TYPE type, USHORT report_length)
{
	std::set<UCHAR> ids;
	PHIDP_PREPARSED_DATA preparsed = nullptr;
	if (!HidD_GetPreparsedData(device, &preparsed))
	{
		return ids;
	}

	std::vector<char> report(report_length);
	for (unsigned id = 0; id <= 0xFF; ++id)
	{
		if (HidP_InitializeReportForID(type, static_cast<UCHAR>(id), preparsed,
			report.data(), static_cast<ULONG>(report.size())) == HIDP_STATUS_SUCCESS)
		{
			ids.emplace(static_cast<UCHAR>(id));
		}
	}
	HidD_FreePreparsedData(preparsed);
	return ids;
}

void print_report_ids(HANDLE device, HIDP_REPORT_TYPE type, USHORT report_length, const char* label)
{
	std::cout << label << " report IDs:";
	if (report_length != 0)
	{
		for (const UCHAR id : get_report_ids(device, type, report_length))
		{
			std::cout << " 0x" << std::hex << static_cast<unsigned>(id) << std::dec;
		}
	}
	std::cout << '\n';
}

void print_value_caps(PHIDP_PREPARSED_DATA preparsed, HIDP_REPORT_TYPE type, USHORT count, const char* label)
{
	if (count == 0)
	{
		return;
	}
	std::vector<HIDP_VALUE_CAPS> values(count);
	USHORT actual = count;
	if (HidP_GetValueCaps(type, values.data(), &actual, preparsed) != HIDP_STATUS_SUCCESS)
	{
		return;
	}
	for (USHORT i = 0; i < actual; ++i)
	{
		const auto& value = values[i];
		std::cout << label << " value: id=0x" << std::hex << static_cast<unsigned>(value.ReportID)
			<< " usage_page=0x" << value.UsagePage << " usage=0x";
		if (value.IsRange)
		{
			std::cout << value.Range.UsageMin << "..0x" << value.Range.UsageMax;
		}
		else
		{
			std::cout << value.NotRange.Usage;
		}
		std::cout << std::dec << " bits=" << value.BitSize << " count=" << value.ReportCount
			<< " logical=" << value.LogicalMin << ".." << value.LogicalMax << '\n';
	}
}

void print_hex(const std::vector<std::uint8_t>& report, DWORD size)
{
	for (DWORD i = 0; i < size; ++i)
	{
		if (i != 0)
		{
			std::cout << ' ';
		}
		std::cout << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned>(report[i]);
	}
	std::cout << std::dec << '\n';
}

bool request_key_reports(HANDLE device, USHORT output_report_length)
{
	// This is the read-only CMD_KEY_REPORTS request used by Finalmouse XPanel:
	// report ID 3, framed length 2, command marker 0xF0, command ID 0x1D.
	std::vector<std::uint8_t> report(output_report_length);
	report[0] = 0x03;
	report[1] = 0x02;
	report[2] = 0xF0;
	report[3] = 0x1D;

	OVERLAPPED overlapped{};
	overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
	if (overlapped.hEvent == nullptr)
	{
		return false;
	}

	DWORD bytes_written = 0;
	BOOL ok = WriteFile(device, report.data(), static_cast<DWORD>(report.size()), &bytes_written, &overlapped);
	if (!ok && GetLastError() == ERROR_IO_PENDING)
	{
		ok = GetOverlappedResult(device, &overlapped, &bytes_written, TRUE);
	}
	CloseHandle(overlapped.hEvent);
	return ok && bytes_written == report.size();
}

void capture_reports(HANDLE device, USHORT report_length, int seconds)
{
	std::vector<std::uint8_t> report(report_length);
	std::vector<std::uint8_t> previous;
	OVERLAPPED overlapped{};
	overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
	if (overlapped.hEvent == nullptr)
	{
		return;
	}

	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
	std::cout << "Capturing changed input reports for " << seconds << " seconds...\n";
	while (std::chrono::steady_clock::now() < deadline)
	{
		ResetEvent(overlapped.hEvent);
		DWORD bytes_read = 0;
		const BOOL immediate = ReadFile(device, report.data(), static_cast<DWORD>(report.size()), &bytes_read, &overlapped);
		if (!immediate && GetLastError() != ERROR_IO_PENDING)
		{
			std::cerr << "ReadFile failed: " << GetLastError() << '\n';
			break;
		}

		const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
		const DWORD wait_ms = static_cast<DWORD>(std::max<std::int64_t>(0, remaining.count()));
		if (!immediate && WaitForSingleObject(overlapped.hEvent, wait_ms) != WAIT_OBJECT_0)
		{
			CancelIoEx(device, &overlapped);
			break;
		}
		if (!immediate && !GetOverlappedResult(device, &overlapped, &bytes_read, FALSE))
		{
			std::cerr << "GetOverlappedResult failed: " << GetLastError() << '\n';
			break;
		}

		if (bytes_read != 0 && (previous.size() != bytes_read || !std::equal(report.begin(), report.begin() + bytes_read, previous.begin())))
		{
			print_hex(report, bytes_read);
			previous.assign(report.begin(), report.begin() + bytes_read);
		}
	}
	CloseHandle(overlapped.hEvent);
}
}

int main(int argc, char** argv)
{
	const int capture_seconds = argc >= 2 ? std::max(0, std::atoi(argv[1])) : 0;
	bool found = false;

	for (const auto& path : get_hid_paths())
	{
		HANDLE device = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
			FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
		if (device == INVALID_HANDLE_VALUE)
		{
			continue;
		}

		HIDD_ATTRIBUTES attributes{};
		attributes.Size = sizeof(attributes);
		PHIDP_PREPARSED_DATA preparsed = nullptr;
		HIDP_CAPS caps{};
		if (HidD_GetAttributes(device, &attributes)
			&& attributes.VendorID == FINALMOUSE_VID
			&& attributes.ProductID == CPPRO_MCU_PID
			&& HidD_GetPreparsedData(device, &preparsed)
			&& HidP_GetCaps(preparsed, &caps) == HIDP_STATUS_SUCCESS)
		{
			found = true;
			std::wcout << L"Product: " << get_product_name(device) << L'\n'
				<< L"Path: " << path << L'\n';
			std::cout << "Usage: 0x" << std::hex << caps.UsagePage << ":0x" << caps.Usage << std::dec << '\n'
				<< "Report lengths (input/output/feature): " << caps.InputReportByteLength << '/'
				<< caps.OutputReportByteLength << '/' << caps.FeatureReportByteLength << '\n';
			print_report_ids(device, HidP_Input, caps.InputReportByteLength, "Input");
			print_report_ids(device, HidP_Output, caps.OutputReportByteLength, "Output");
			print_report_ids(device, HidP_Feature, caps.FeatureReportByteLength, "Feature");
			print_value_caps(preparsed, HidP_Input, caps.NumberInputValueCaps, "Input");
			print_value_caps(preparsed, HidP_Output, caps.NumberOutputValueCaps, "Output");
			print_value_caps(preparsed, HidP_Feature, caps.NumberFeatureValueCaps, "Feature");
			std::cout << '\n';

			if (capture_seconds > 0 && caps.UsagePage >= 0xFF00)
			{
				if (!request_key_reports(device, caps.OutputReportByteLength))
				{
					std::cerr << "Could not request CPPRO key reports.\n";
				}
				capture_reports(device, caps.InputReportByteLength, capture_seconds);
			}
		}

		if (preparsed != nullptr)
		{
			HidD_FreePreparsedData(preparsed);
		}
		CloseHandle(device);
	}

	if (!found)
	{
		std::cerr << "No readable Finalmouse Centerpiece Pro MCU HID collections found.\n";
		return 1;
	}
	return 0;
}
