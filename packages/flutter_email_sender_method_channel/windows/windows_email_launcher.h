#ifndef FLUTTER_PLUGIN_WINDOWS_EMAIL_LAUNCHER_H_
#define FLUTTER_PLUGIN_WINDOWS_EMAIL_LAUNCHER_H_

#include <string>

struct EmailLaunchResult {
  bool succeeded;
  std::string error;
  int native_error = 0;
};

struct TemporaryEmlResult {
  std::wstring path;
  int native_error = 0;
};

TemporaryEmlResult CreateTemporaryEmlPath();
EmailLaunchResult OpenEmlDraft(const std::wstring& path);

#endif
