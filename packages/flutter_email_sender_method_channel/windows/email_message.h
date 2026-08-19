#ifndef FLUTTER_PLUGIN_EMAIL_MESSAGE_H_
#define FLUTTER_PLUGIN_EMAIL_MESSAGE_H_

#include <string>
#include <vector>

struct EmailMessage {
  std::string subject;
  std::vector<std::string> recipients;
  std::vector<std::string> cc;
  std::vector<std::string> bcc;
  std::string body;
  std::vector<std::wstring> attachment_paths;
  bool is_html = false;
};

#endif
