#include "flutter_email_sender_method_channel_plugin.h"

#include <flutter/standard_method_codec.h>
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <exception>
#include <future>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "email_message.h"
#include "eml_builder.h"
#include "windows_email_launcher.h"

namespace {

bool IsStorageFull(int native_error) {
  return native_error == ERROR_DISK_FULL ||
         native_error == ERROR_HANDLE_DISK_FULL ||
         native_error == ERROR_DISK_QUOTA_EXCEEDED;
}

const flutter::EncodableValue* Argument(const flutter::EncodableMap& arguments,
                                        const char* name) {
  const auto iterator = arguments.find(flutter::EncodableValue(name));
  return iterator == arguments.end() ? nullptr : &iterator->second;
}

std::string StringArgument(const flutter::EncodableMap& arguments,
                           const char* name) {
  const auto* value = Argument(arguments, name);
  if (value == nullptr) return {};
  const auto* string = std::get_if<std::string>(value);
  return string == nullptr ? std::string() : *string;
}

std::vector<std::string> StringListArgument(
    const flutter::EncodableMap& arguments, const char* name) {
  const auto* value = Argument(arguments, name);
  const auto* list = value == nullptr
                         ? nullptr
                         : std::get_if<flutter::EncodableList>(value);
  std::vector<std::string> strings;
  if (list == nullptr) return strings;
  strings.reserve(list->size());
  for (const auto& item : *list) {
    const auto* string = std::get_if<std::string>(&item);
    if (string != nullptr) strings.push_back(*string);
  }
  return strings;
}

std::wstring Utf8ToWide(const std::string& value) {
  if (value.empty()) return {};
  const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                          value.data(),
                                          static_cast<int>(value.size()),
                                          nullptr, 0);
  if (length == 0) return {};
  std::wstring converted(length, L'\0');
  MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                      static_cast<int>(value.size()), converted.data(), length);
  return converted;
}

EmailMessage ParseEmail(const flutter::EncodableMap& arguments) {
  EmailMessage email;
  email.subject = StringArgument(arguments, "subject");
  email.recipients = StringListArgument(arguments, "recipients");
  email.cc = StringListArgument(arguments, "cc");
  email.bcc = StringListArgument(arguments, "bcc");
  email.body = StringArgument(arguments, "body");
  for (const auto& path : StringListArgument(arguments, "attachment_paths")) {
    email.attachment_paths.push_back(Utf8ToWide(path));
  }
  const auto* html = Argument(arguments, "is_html");
  const auto* is_html =
      html == nullptr ? nullptr : std::get_if<bool>(html);
  email.is_html = is_html != nullptr && *is_html;
  return email;
}

}  // namespace

FlutterEmailSenderMethodChannelPlugin::FlutterEmailSenderMethodChannelPlugin() =
    default;
FlutterEmailSenderMethodChannelPlugin::~FlutterEmailSenderMethodChannelPlugin() =
    default;

void FlutterEmailSenderMethodChannelPlugin::RegisterWithRegistrar(
    flutter::PluginRegistrarWindows* registrar) {
  auto channel = std::make_unique<flutter::MethodChannel<flutter::EncodableValue>>(
      registrar->messenger(), "flutter_email_sender",
      &flutter::StandardMethodCodec::GetInstance());
  auto plugin = std::make_unique<FlutterEmailSenderMethodChannelPlugin>();
  channel->SetMethodCallHandler(
      [plugin_pointer = plugin.get()](const auto& call, auto result) {
        plugin_pointer->HandleMethodCall(call, std::move(result));
      });
  registrar->AddPlugin(std::move(plugin));
}

void FlutterEmailSenderMethodChannelPlugin::HandleMethodCall(
    const flutter::MethodCall<flutter::EncodableValue>& method_call,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {
  if (method_call.method_name() == "getCapabilities") {
    // Packaged email clients may handle .eml through ShellExecuteW without
    // exposing a classic executable association. Let send perform the actual
    // launch and report association errors from ShellExecuteW.
    result->Success(flutter::EncodableMap{{flutter::EncodableValue("canSend"),
                                          flutter::EncodableValue(true)}});
    return;
  }
  if (method_call.method_name() != "send") {
    result->NotImplemented();
    return;
  }

  const auto* arguments =
      std::get_if<flutter::EncodableMap>(method_call.arguments());
  if (arguments == nullptr) {
    result->Error("error", "Arguments are not a map.");
    return;
  }
  EmailMessage email = ParseEmail(*arguments);
  auto shared_result =
      std::shared_ptr<flutter::MethodResult<flutter::EncodableValue>>(
          std::move(result));
  workers_.erase(
      std::remove_if(workers_.begin(), workers_.end(), [](auto& worker) {
        return worker.wait_for(std::chrono::seconds(0)) ==
               std::future_status::ready;
      }),
      workers_.end());
  try {
    workers_.emplace_back(std::async(
        std::launch::async,
        [email = std::move(email), result = shared_result]() mutable {
          try {
            const TemporaryEmlResult temporary = CreateTemporaryEmlPath();
            if (temporary.path.empty()) {
              result->Error(IsStorageFull(temporary.native_error)
                                ? "storage_full"
                                : "error",
                            "Could not reserve an email draft file.",
                            flutter::EncodableValue(temporary.native_error));
              return;
            }
            const EmlBuildResult eml = WriteEmlDraft(email, temporary.path);
            if (!eml.succeeded()) {
              const char* code = eml.attachment_error
                                     ? "attachment_error"
                                     : IsStorageFull(eml.native_error)
                                           ? "storage_full"
                                           : "error";
              result->Error(code, eml.error,
                            flutter::EncodableValue(eml.native_error));
              return;
            }
            const EmailLaunchResult launch = OpenEmlDraft(temporary.path);
            if (!launch.succeeded) {
              result->Error(
                  launch.native_error == SE_ERR_NOASSOC ||
                          launch.native_error == SE_ERR_ASSOCINCOMPLETE
                      ? "not_available"
                      : "error",
                  launch.error,
                  flutter::EncodableValue(launch.native_error));
              return;
            }
            result->Success();
          } catch (const std::exception& exception) {
            result->Error("error",
                          std::string("Failed to create email draft: ") +
                              exception.what());
          } catch (...) {
            result->Error("error", "Failed to create email draft.");
          }
        }));
  } catch (const std::exception& exception) {
    shared_result->Error("error",
                         std::string("Could not start email draft worker: ") +
                             exception.what());
  } catch (...) {
    shared_result->Error("error", "Could not start email draft worker.");
  }
}
