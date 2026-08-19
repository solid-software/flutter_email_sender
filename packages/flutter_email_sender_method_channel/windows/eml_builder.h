#ifndef FLUTTER_PLUGIN_EML_BUILDER_H_
#define FLUTTER_PLUGIN_EML_BUILDER_H_

#include <string>

#include "email_message.h"

struct EmlBuildResult {
  std::string error;
  int native_error = 0;
  bool attachment_error = false;

  bool succeeded() const { return error.empty(); }
};

EmlBuildResult WriteEmlDraft(const EmailMessage& email,
                             const std::wstring& path);

#endif
