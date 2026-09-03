#include "eml_builder.h"

#include <windows.h>

#include <algorithm>
#include <cassert>
#include <cctype>
#include <fstream>
#include <regex>
#include <string>
#include <vector>

namespace {

std::wstring TemporaryPath(const wchar_t* extension) {
  wchar_t directory[MAX_PATH + 1]{};
  assert(GetTempPathW(MAX_PATH, directory) != 0);
  GUID guid{};
  assert(SUCCEEDED(CoCreateGuid(&guid)));
  wchar_t text[39]{};
  assert(StringFromGUID2(guid, text, 39) != 0);
  return std::wstring(directory) + text + extension;
}

void WriteFixture(const std::wstring& path,
                  const std::vector<unsigned char>& bytes) {
  std::ofstream output(path, std::ios::binary);
  assert(output.good());
  output.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

std::string ReadFile(const std::wstring& path) {
  std::ifstream input(path, std::ios::binary);
  assert(input.good());
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

std::vector<unsigned char> DecodeBase64(std::string value) {
  static const std::string alphabet =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  value.erase(std::remove_if(value.begin(), value.end(),
                             [](unsigned char c) { return std::isspace(c); }),
              value.end());
  std::vector<unsigned char> result;
  unsigned int buffer = 0;
  int bits = 0;
  for (const char character : value) {
    if (character == '=') break;
    const auto position = alphabet.find(character);
    assert(position != std::string::npos);
    buffer = (buffer << 6) | static_cast<unsigned int>(position);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      result.push_back(static_cast<unsigned char>((buffer >> bits) & 0xff));
    }
  }
  return result;
}

std::string HeaderValue(const std::string& eml, const std::string& name) {
  const auto begin = eml.find(name + ": ");
  assert(begin != std::string::npos);
  const auto value_begin = begin + name.size() + 2;
  return eml.substr(value_begin, eml.find("\r\n", value_begin) - value_begin);
}

void TestCompleteMultipartDraft() {
  const std::vector<unsigned char> pdf = {0x25, 0x50, 0x44, 0x46, 0x00, 0xff};
  const std::vector<unsigned char> other = {1, 2, 3, 4, 5};
  const std::wstring pdf_path = TemporaryPath(L"-invoice.pdf");
  const std::wstring unicode_path = TemporaryPath(L"-żółć.bin");
  const std::wstring eml_path = TemporaryPath(L".eml");
  WriteFixture(pdf_path, pdf);
  WriteFixture(unicode_path, other);
  WriteFixture(eml_path, {});

  EmailMessage email;
  email.subject = "Unicode \xc5\xbc\xc3\xb3\xc5\x82\xc4\x87";
  email.body = "<p>Hello</p>";
  email.is_html = true;
  email.attachment_paths = {pdf_path, unicode_path};
  assert(WriteEmlDraft(email, eml_path).succeeded());
  const std::string eml = ReadFile(eml_path);

  assert(std::regex_search(
      eml, std::regex("Date: [A-Z][a-z]{2}, [0-9]{2} [A-Z][a-z]{2} "
                      "[0-9]{4} [0-9]{2}:[0-9]{2}:[0-9]{2} \\+0000\\r\\n")));
  const std::string message_id = HeaderValue(eml, "Message-ID");
  assert(message_id.find("@flutter-email-sender.invalid>") !=
         std::string::npos);
  assert(eml.find("Content-Type: application/pdf;") != std::string::npos);
  assert(eml.find("Content-Type: application/octet-stream;") !=
         std::string::npos);
  assert(eml.find("filename*=UTF-8''") != std::string::npos);
  assert(eml.find("%C5%BC%C3%B3%C5%82%C4%87.bin") != std::string::npos);

  const std::string boundary =
      HeaderValue(eml, "Content-Type")
          .substr(HeaderValue(eml, "Content-Type").find("boundary=\"") + 10);
  const std::string clean_boundary = boundary.substr(0, boundary.size() - 1);
  const std::string delimiter = "--" + clean_boundary;
  assert(eml.rfind(delimiter + "--\r\n") == eml.size() - delimiter.size() - 4);
  assert(std::count(eml.begin(), eml.end(), '\n') ==
         std::count(eml.begin(), eml.end(), '\r'));

  const auto pdf_header = eml.find("Content-Type: application/pdf;");
  const auto payload_begin = eml.find("\r\n\r\n", pdf_header) + 4;
  const auto payload_end = eml.find("\r\n" + delimiter, payload_begin);
  assert(DecodeBase64(eml.substr(payload_begin, payload_end - payload_begin)) ==
         pdf);

  const std::wstring second_eml_path = TemporaryPath(L".eml");
  WriteFixture(second_eml_path, {});
  assert(WriteEmlDraft(email, second_eml_path).succeeded());
  assert(HeaderValue(ReadFile(second_eml_path), "Message-ID") != message_id);

  DeleteFileW(pdf_path.c_str());
  DeleteFileW(unicode_path.c_str());
  DeleteFileW(eml_path.c_str());
  DeleteFileW(second_eml_path.c_str());
}

void TestEmptyPlainTextBodyAndAsciiFilename() {
  const std::vector<unsigned char> bytes = {'t', 'e', 's', 't'};
  const std::wstring attachment_path = TemporaryPath(L"-report.txt");
  const std::wstring eml_path = TemporaryPath(L".eml");
  WriteFixture(attachment_path, bytes);
  WriteFixture(eml_path, {});
  EmailMessage email;
  email.attachment_paths = {attachment_path};
  assert(WriteEmlDraft(email, eml_path).succeeded());
  const std::string eml = ReadFile(eml_path);
  assert(eml.find("Content-Type: text/plain; charset=UTF-8") !=
         std::string::npos);
  assert(eml.find("filename=\"") != std::string::npos);
  assert(eml.find("-report.txt\"") != std::string::npos);
  DeleteFileW(attachment_path.c_str());
  DeleteFileW(eml_path.c_str());
}

}  // namespace

int main() {
  TestCompleteMultipartDraft();
  TestEmptyPlainTextBodyAndAsciiFilename();
  return 0;
}
