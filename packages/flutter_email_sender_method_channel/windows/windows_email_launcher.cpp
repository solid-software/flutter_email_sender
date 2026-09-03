#include "windows_email_launcher.h"

#include <windows.h>
#include <objbase.h>
#include <shellapi.h>

namespace {

constexpr ULONGLONG kDraftLifetime =
    24ULL * 60ULL * 60ULL * 10'000'000ULL;  // 24 hours in 100 ns units.

std::wstring TempDirectory() {
  wchar_t directory[MAX_PATH + 1]{};
  const DWORD length = GetTempPathW(MAX_PATH, directory);
  if (length == 0 || length > MAX_PATH) return {};
  std::wstring plugin_directory =
      std::wstring(directory) + L"flutter_email_sender\\";
  if (!CreateDirectoryW(plugin_directory.c_str(), nullptr) &&
      GetLastError() != ERROR_ALREADY_EXISTS)
    return {};
  return plugin_directory;
}

void CleanupStaleEmlDrafts(const std::wstring& directory) {
  FILETIME current_time{};
  GetSystemTimeAsFileTime(&current_time);
  ULARGE_INTEGER current{};
  current.LowPart = current_time.dwLowDateTime;
  current.HighPart = current_time.dwHighDateTime;

  WIN32_FIND_DATAW entry{};
  HANDLE search = FindFirstFileW((directory + L"*.eml").c_str(), &entry);
  if (search == INVALID_HANDLE_VALUE) return;
  do {
    if ((entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) continue;
    ULARGE_INTEGER modified{};
    modified.LowPart = entry.ftLastWriteTime.dwLowDateTime;
    modified.HighPart = entry.ftLastWriteTime.dwHighDateTime;
    if (current.QuadPart > modified.QuadPart &&
        current.QuadPart - modified.QuadPart > kDraftLifetime) {
      DeleteFileW((directory + entry.cFileName).c_str());
    }
  } while (FindNextFileW(search, &entry));
  FindClose(search);
}

}  // namespace

TemporaryEmlResult CreateTemporaryEmlPath() {
  const std::wstring directory = TempDirectory();
  if (directory.empty()) return {{}, static_cast<int>(GetLastError())};
  CleanupStaleEmlDrafts(directory);

  for (int attempt = 0; attempt < 10; ++attempt) {
    GUID guid{};
    if (FAILED(CoCreateGuid(&guid))) return {};
    wchar_t guid_text[39]{};
    if (StringFromGUID2(guid, guid_text, 39) == 0) return {};
    const std::wstring eml_path =
        directory + L"draft-" + guid_text + L".eml";
    HANDLE file = CreateFileW(eml_path.c_str(), GENERIC_WRITE, 0, nullptr,
                              CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
      CloseHandle(file);
      return {eml_path};
    }
    const DWORD error = GetLastError();
    if (error != ERROR_FILE_EXISTS && error != ERROR_ALREADY_EXISTS)
      return {{}, static_cast<int>(error)};
  }
  return {};
}

EmailLaunchResult OpenEmlDraft(const std::wstring& path) {
  const HRESULT com_result =
      CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
  const auto launch_result = reinterpret_cast<INT_PTR>(ShellExecuteW(
      nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
  if (SUCCEEDED(com_result)) CoUninitialize();
  if (launch_result <= 32) {
    DeleteFileW(path.c_str());
    return {false, "Could not open the email draft.",
            static_cast<int>(launch_result)};
  }
  return {true, {}};
}
