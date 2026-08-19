#include "eml_builder.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <sstream>

namespace {

std::string SanitizeHeader(std::string value) {
  value.erase(std::remove(value.begin(), value.end(), '\r'), value.end());
  value.erase(std::remove(value.begin(), value.end(), '\n'), value.end());
  return value;
}

void WriteAddressHeader(std::ostringstream* output, const char* name,
                        const std::vector<std::string>& addresses) {
  *output << name << ':';
  size_t line_length = std::char_traits<char>::length(name) + 1;
  for (size_t index = 0; index < addresses.size(); ++index) {
    const std::string address = SanitizeHeader(addresses[index]);
    if (index == 0) {
      *output << ' ';
      ++line_length;
    } else if (line_length + 2 + address.size() > 78) {
      *output << ",\r\n ";
      line_length = 1;
    } else {
      *output << ", ";
      line_length += 2;
    }
    *output << address;
    line_length += address.size();
  }
  *output << "\r\n";
}

std::string WideToUtf8(const std::wstring& value) {
  if (value.empty()) return {};
  const int length = WideCharToMultiByte(CP_UTF8, 0, value.data(),
                                         static_cast<int>(value.size()),
                                         nullptr, 0, nullptr, nullptr);
  std::string converted(length, '\0');
  WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                      converted.data(), length, nullptr, nullptr);
  return converted;
}

bool WriteBytes(HANDLE file, const char* data, size_t size,
                DWORD* native_error) {
  size_t offset = 0;
  while (offset < size) {
    const DWORD chunk = static_cast<DWORD>(
        std::min<size_t>(size - offset, MAXDWORD));
    DWORD written = 0;
    const BOOL write_succeeded =
        WriteFile(file, data + offset, chunk, &written, nullptr);
    if (!write_succeeded || written == 0) {
      *native_error =
          write_succeeded ? ERROR_WRITE_FAULT : GetLastError();
      return false;
    }
    offset += written;
  }
  return true;
}

bool WriteString(HANDLE file, const std::string& value, DWORD* native_error) {
  return WriteBytes(file, value.data(), value.size(), native_error);
}

std::string Base64Line(const uint8_t* data, size_t size) {
  static constexpr char alphabet[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string encoded;
  encoded.reserve(((size + 2) / 3) * 4 + 2);
  for (size_t offset = 0; offset < size; offset += 3) {
    const uint32_t block = static_cast<uint32_t>(data[offset]) << 16 |
                           (offset + 1 < size
                                ? static_cast<uint32_t>(data[offset + 1]) << 8
                                : 0) |
                           (offset + 2 < size ? data[offset + 2] : 0);
    encoded.push_back(alphabet[(block >> 18) & 0x3f]);
    encoded.push_back(alphabet[(block >> 12) & 0x3f]);
    encoded.push_back(offset + 1 < size ? alphabet[(block >> 6) & 0x3f] : '=');
    encoded.push_back(offset + 2 < size ? alphabet[block & 0x3f] : '=');
  }
  encoded += "\r\n";
  return encoded;
}

bool WriteBase64(HANDLE output, const uint8_t* data, size_t size,
                 DWORD* native_error) {
  if (size == 0) return WriteString(output, "\r\n", native_error);
  for (size_t offset = 0; offset < size; offset += 57) {
    const size_t chunk = std::min<size_t>(57, size - offset);
    if (!WriteString(output, Base64Line(data + offset, chunk), native_error))
      return false;
  }
  return true;
}

bool WriteBase64File(HANDLE output, const std::wstring& path,
                     DWORD* native_error, bool* attachment_error) {
  HANDLE input = CreateFileW(
      path.c_str(), GENERIC_READ,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (input == INVALID_HANDLE_VALUE) {
    *native_error = GetLastError();
    *attachment_error = true;
    return false;
  }
  std::array<uint8_t, 57> buffer{};
  bool succeeded = true;
  bool had_data = false;
  while (true) {
    DWORD read = 0;
    if (!ReadFile(input, buffer.data(), static_cast<DWORD>(buffer.size()),
                  &read, nullptr)) {
      *native_error = GetLastError();
      *attachment_error = true;
      succeeded = false;
      break;
    }
    if (read == 0) {
      if (!had_data)
        succeeded = WriteString(output, "\r\n", native_error);
      break;
    }
    had_data = true;
    if (!WriteString(output, Base64Line(buffer.data(), read), native_error)) {
      succeeded = false;
      break;
    }
  }
  CloseHandle(input);
  return succeeded;
}

std::string EncodeWordChunk(const std::string& value, size_t offset,
                            size_t size) {
  std::string encoded = Base64Line(
      reinterpret_cast<const uint8_t*>(value.data() + offset), size);
  encoded.resize(encoded.size() - 2);
  return "=?UTF-8?B?" + encoded + "?=";
}

std::string EncodedWords(const std::string& value) {
  constexpr size_t kMaximumRawBytes = 45;
  std::string result;
  for (size_t offset = 0; offset < value.size();) {
    size_t end = (std::min)(value.size(), offset + kMaximumRawBytes);
    while (end < value.size() && end > offset &&
           (static_cast<unsigned char>(value[end]) & 0xC0) == 0x80)
      --end;
    if (end == offset)
      end = (std::min)(value.size(), offset + kMaximumRawBytes);
    if (!result.empty()) result += "\r\n ";
    result += EncodeWordChunk(value, offset, end - offset);
    offset = end;
  }
  return result;
}

std::string PercentEncodeParameter(const std::string& value) {
  static constexpr char hexadecimal[] = "0123456789ABCDEF";
  std::string encoded;
  for (const unsigned char character : value) {
    const bool attribute_character =
        (character >= 'a' && character <= 'z') ||
        (character >= 'A' && character <= 'Z') ||
        (character >= '0' && character <= '9') || character == '!' ||
        character == '#' || character == '$' || character == '&' ||
        character == '+' || character == '-' || character == '.' ||
        character == '^' || character == '_' || character == '`' ||
        character == '|' || character == '~';
    if (attribute_character) {
      encoded.push_back(static_cast<char>(character));
    } else {
      encoded.push_back('%');
      encoded.push_back(hexadecimal[character >> 4]);
      encoded.push_back(hexadecimal[character & 0x0F]);
    }
  }
  return encoded;
}

std::string AsciiFilenameFallback(const std::string& value) {
  std::string fallback;
  fallback.reserve(value.size());
  for (const unsigned char character : value) {
    fallback.push_back(character >= 0x20 && character <= 0x7E &&
                               character != '"' && character != '\\'
                           ? static_cast<char>(character)
                           : '_');
  }
  return fallback.empty() ? "attachment" : fallback;
}

}  // namespace

EmlBuildResult WriteEmlDraft(const EmailMessage& email,
                             const std::wstring& path) {
  HANDLE output = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                              TRUNCATE_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (output == INVALID_HANDLE_VALUE) {
    const DWORD native_error = GetLastError();
    DeleteFileW(path.c_str());
    return {"Could not create the email draft file.",
            static_cast<int>(native_error)};
  }

  const std::string boundary =
      "----flutter-email-sender-" + std::to_string(GetTickCount64());
  std::ostringstream headers;
  headers << "X-Unsent: 1\r\nMIME-Version: 1.0\r\n";
  WriteAddressHeader(&headers, "To", email.recipients);
  if (!email.cc.empty()) WriteAddressHeader(&headers, "Cc", email.cc);
  if (!email.bcc.empty()) WriteAddressHeader(&headers, "Bcc", email.bcc);
  if (!email.subject.empty())
    headers << "Subject:\r\n " << EncodedWords(SanitizeHeader(email.subject))
            << "\r\n";
  headers << "Content-Type: multipart/mixed; boundary=\"" << boundary
          << "\"\r\n\r\n--" << boundary << "\r\n";
  headers << "Content-Type: " << (email.is_html ? "text/html" : "text/plain")
          << "; charset=UTF-8\r\nContent-Transfer-Encoding: base64\r\n\r\n";

  DWORD native_error = ERROR_SUCCESS;
  bool attachment_error = false;
  bool succeeded = WriteString(output, headers.str(), &native_error) &&
                   WriteBase64(output,
                               reinterpret_cast<const uint8_t*>(email.body.data()),
                               email.body.size(), &native_error);
  std::string error;
  for (const auto& attachment : email.attachment_paths) {
    if (!succeeded) break;
    const auto separator = attachment.find_last_of(L"\\/");
    const std::wstring wide_name = separator == std::wstring::npos
                                       ? attachment
                                       : attachment.substr(separator + 1);
    const std::string utf8_name = WideToUtf8(wide_name);
    const std::string fallback_name = AsciiFilenameFallback(utf8_name);
    const std::string encoded_name = PercentEncodeParameter(utf8_name);
    std::ostringstream attachment_headers;
    attachment_headers << "--" << boundary << "\r\n";
    attachment_headers << "Content-Type: application/octet-stream;\r\n"
                       << " name=\"" << fallback_name << "\";\r\n"
                       << " name*=UTF-8''" << encoded_name << "\r\n";
    attachment_headers << "Content-Disposition: attachment;\r\n"
                       << " filename=\"" << fallback_name << "\";\r\n"
                       << " filename*=UTF-8''" << encoded_name << "\r\n";
    attachment_headers << "Content-Transfer-Encoding: base64\r\n\r\n";
    succeeded =
        WriteString(output, attachment_headers.str(), &native_error);
    if (succeeded && !WriteBase64File(output, attachment, &native_error,
                                      &attachment_error)) {
      succeeded = false;
      if (attachment_error)
        error = "Could not read attachment: " + WideToUtf8(attachment);
    }
  }
  if (succeeded)
    succeeded = WriteString(output, "--" + boundary + "--\r\n",
                            &native_error);
  CloseHandle(output);

  if (!succeeded) {
    DeleteFileW(path.c_str());
    return {error.empty() ? "Could not write the email draft file." : error,
            static_cast<int>(native_error), attachment_error};
  }
  return {{}};
}
