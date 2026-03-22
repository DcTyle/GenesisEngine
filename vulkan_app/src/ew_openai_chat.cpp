#include "ew_openai_chat.hpp"

#include <windows.h>
#include <winhttp.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <vector>

namespace ewv {

namespace {

static std::wstring ew_utf8_to_wide_local(const std::string& s) {
    if (s.empty()) return std::wstring();
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring out((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), out.data(), n);
    return out;
}

static bool ew_resolve_api_key_file_w(std::wstring* out_path_w) {
    if (out_path_w) out_path_w->clear();
    wchar_t exe_buf[MAX_PATH] = {};
    const DWORD n = GetModuleFileNameW(nullptr, exe_buf, MAX_PATH);
    if (n == 0u || n >= MAX_PATH) return false;

    std::error_code ec;
    std::filesystem::path probe = std::filesystem::path(exe_buf).parent_path();
    while (!probe.empty()) {
        const std::filesystem::path candidate = probe / "chatgptAPI.txt";
        if (std::filesystem::exists(candidate, ec) && std::filesystem::is_regular_file(candidate, ec)) {
            if (out_path_w) *out_path_w = candidate.wstring();
            return true;
        }
        const std::filesystem::path parent = probe.parent_path();
        if (parent == probe) break;
        probe = parent;
    }
    return false;
}

static bool ew_extract_api_key_token(const std::string& raw_utf8, std::string* out_key_utf8) {
    if (out_key_utf8) out_key_utf8->clear();
    const size_t pos = raw_utf8.find("sk-");
    if (pos == std::string::npos) return false;
    size_t end = pos;
    while (end < raw_utf8.size()) {
        const unsigned char c = (unsigned char)raw_utf8[end];
        const bool ok = (c >= 'a' && c <= 'z') ||
                        (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') ||
                        c == '-' || c == '_';
        if (!ok) break;
        ++end;
    }
    if (end <= pos + 3u) return false;
    if (out_key_utf8) *out_key_utf8 = raw_utf8.substr(pos, end - pos);
    return true;
}

static bool ew_load_api_key_from_file(const std::wstring& path_w, std::string* out_key_utf8, std::string* out_err) {
    if (out_key_utf8) out_key_utf8->clear();
    if (out_err) out_err->clear();
    std::ifstream f(std::filesystem::path(path_w), std::ios::in | std::ios::binary);
    if (!f.good()) {
        if (out_err) *out_err = "chatgptAPI.txt could not be opened";
        return false;
    }
    std::string raw((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (!ew_extract_api_key_token(raw, out_key_utf8)) {
        if (out_err) *out_err = "chatgptAPI.txt does not contain an sk- API key token";
        return false;
    }
    return true;
}

static void ew_json_escape_append(std::string& out, const std::string& s) {
    for (unsigned char c : s) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20u) {
                    char buf[7];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", (unsigned)c);
                    out += buf;
                } else {
                    out.push_back((char)c);
                }
                break;
        }
    }
}

struct EwOpenAiSource {
    std::string title_utf8;
    std::string url_utf8;
};

static std::string ew_build_request_body_json(const EwOpenAiTextRequest& request) {
    std::string json;
    json.reserve(request.instructions_utf8.size() + request.input_utf8.size() + 512u);
    json += "{\"model\":\"";
    ew_json_escape_append(json, request.model_utf8.empty() ? std::string(ew_openai_default_model_utf8()) : request.model_utf8);
    json += "\",\"instructions\":\"";
    ew_json_escape_append(json, request.instructions_utf8);
    json += "\",\"input\":\"";
    ew_json_escape_append(json, request.input_utf8);
    json += "\",\"max_output_tokens\":";
    json += std::to_string((unsigned long long)request.max_output_tokens_u32);
    if (request.enable_web_search) {
        json += ",\"tools\":[{\"type\":\"web_search\"}],\"include\":[\"web_search_call.action.sources\"]";
    }
    json += "}";
    return json;
}

static void ew_skip_json_ws(const std::string& s, size_t* io_pos) {
    while (*io_pos < s.size()) {
        const unsigned char c = (unsigned char)s[*io_pos];
        if (c != ' ' && c != '\r' && c != '\n' && c != '\t') break;
        ++(*io_pos);
    }
}

static bool ew_parse_json_string(const std::string& s, size_t quote_pos, std::string* out, size_t* out_next) {
    if (out) out->clear();
    if (quote_pos >= s.size() || s[quote_pos] != '"') return false;
    std::string acc;
    size_t i = quote_pos + 1u;
    while (i < s.size()) {
        const char c = s[i++];
        if (c == '"') {
            if (out) *out = acc;
            if (out_next) *out_next = i;
            return true;
        }
        if (c != '\\') {
            acc.push_back(c);
            continue;
        }
        if (i >= s.size()) return false;
        const char esc = s[i++];
        switch (esc) {
            case '"': acc.push_back('"'); break;
            case '\\': acc.push_back('\\'); break;
            case '/': acc.push_back('/'); break;
            case 'b': acc.push_back('\b'); break;
            case 'f': acc.push_back('\f'); break;
            case 'n': acc.push_back('\n'); break;
            case 'r': acc.push_back('\r'); break;
            case 't': acc.push_back('\t'); break;
            case 'u':
                if (i + 4u <= s.size()) {
                    const std::string hex = s.substr(i, 4u);
                    i += 4u;
                    const unsigned code = (unsigned)std::strtoul(hex.c_str(), nullptr, 16);
                    if (code < 0x80u) acc.push_back((char)code);
                    else acc.push_back('?');
                } else {
                    return false;
                }
                break;
            default:
                acc.push_back(esc);
                break;
        }
    }
    return false;
}

static bool ew_find_json_string_value_after_key(const std::string& json, std::string_view key, size_t start, size_t end, std::string* out) {
    size_t pos = start;
    while (pos < end) {
        pos = json.find(key, pos);
        if (pos == std::string::npos || pos >= end) return false;
        size_t i = pos + key.size();
        ew_skip_json_ws(json, &i);
        if (i >= json.size() || json[i] != ':') {
            pos += key.size();
            continue;
        }
        ++i;
        ew_skip_json_ws(json, &i);
        if (i >= json.size() || json[i] != '"') {
            pos += key.size();
            continue;
        }
        return ew_parse_json_string(json, i, out, nullptr);
    }
    return false;
}

static bool ew_extract_output_text_from_response_json(const std::string& json, std::string* out_text_utf8) {
    if (out_text_utf8) out_text_utf8->clear();
    size_t pos = 0u;
    while (true) {
        pos = json.find("\"type\"", pos);
        if (pos == std::string::npos) break;
        std::string type_value;
        if (!ew_find_json_string_value_after_key(json, "\"type\"", pos, json.size(), &type_value)) break;
        if (type_value == "output_text") {
            const size_t window_end = (pos + 16384u < json.size()) ? (pos + 16384u) : json.size();
            if (ew_find_json_string_value_after_key(json, "\"text\"", pos, window_end, out_text_utf8)) {
                return true;
            }
        }
        pos += 6u;
    }
    return ew_find_json_string_value_after_key(json, "\"output_text\"", 0u, json.size(), out_text_utf8);
}

static std::string ew_extract_error_message_from_response_json(const std::string& json) {
    std::string msg;
    if (ew_find_json_string_value_after_key(json, "\"message\"", 0u, json.size(), &msg)) return msg;
    return std::string();
}

static void ew_append_unique_source(std::vector<EwOpenAiSource>* out_sources, EwOpenAiSource source) {
    if (!out_sources || source.url_utf8.empty()) return;
    if (source.title_utf8.empty()) source.title_utf8 = source.url_utf8;
    for (const EwOpenAiSource& existing : *out_sources) {
        if (existing.url_utf8 == source.url_utf8) return;
    }
    out_sources->push_back(std::move(source));
}

static void ew_extract_sources_from_response_json(const std::string& json, std::vector<EwOpenAiSource>* out_sources) {
    if (!out_sources) return;
    out_sources->clear();

    size_t pos = 0u;
    while (true) {
        pos = json.find("\"type\"", pos);
        if (pos == std::string::npos) break;
        std::string type_value;
        if (!ew_find_json_string_value_after_key(json, "\"type\"", pos, json.size(), &type_value)) {
            pos += 6u;
            continue;
        }
        if (type_value == "url_citation") {
            const size_t window_end = (pos + 4096u < json.size()) ? (pos + 4096u) : json.size();
            EwOpenAiSource source;
            (void)ew_find_json_string_value_after_key(json, "\"title\"", pos, window_end, &source.title_utf8);
            (void)ew_find_json_string_value_after_key(json, "\"url\"", pos, window_end, &source.url_utf8);
            ew_append_unique_source(out_sources, std::move(source));
        }
        pos += 6u;
    }

    if (!out_sources->empty()) return;

    pos = 0u;
    while (true) {
        pos = json.find("\"url\"", pos);
        if (pos == std::string::npos) break;
        size_t colon = pos + 5u;
        ew_skip_json_ws(json, &colon);
        if (colon >= json.size() || json[colon] != ':') {
            pos += 5u;
            continue;
        }
        ++colon;
        ew_skip_json_ws(json, &colon);
        if (colon >= json.size() || json[colon] != '"') {
            pos += 5u;
            continue;
        }
        EwOpenAiSource source;
        size_t next = colon;
        if (!ew_parse_json_string(json, colon, &source.url_utf8, &next)) {
            pos += 5u;
            continue;
        }
        const size_t window_end = (next + 1024u < json.size()) ? (next + 1024u) : json.size();
        (void)ew_find_json_string_value_after_key(json, "\"title\"", pos, window_end, &source.title_utf8);
        ew_append_unique_source(out_sources, std::move(source));
        pos = next;
    }
}

static void ew_append_sources_to_output_text(std::string* io_text_utf8, const std::vector<EwOpenAiSource>& sources) {
    if (!io_text_utf8 || sources.empty()) return;
    if (!io_text_utf8->empty()) {
        if (io_text_utf8->back() != '\n') *io_text_utf8 += "\n";
        *io_text_utf8 += "\n";
    }
    *io_text_utf8 += "Sources:\n";
    const size_t shown = (sources.size() < 6u) ? sources.size() : 6u;
    for (size_t i = 0; i < shown; ++i) {
        io_text_utf8->push_back('-');
        io_text_utf8->push_back(' ');
        if (!sources[i].title_utf8.empty() && sources[i].title_utf8 != sources[i].url_utf8) {
            *io_text_utf8 += sources[i].title_utf8;
            *io_text_utf8 += " - ";
        }
        *io_text_utf8 += sources[i].url_utf8;
        *io_text_utf8 += "\n";
    }
}

static bool ew_read_response_body(HINTERNET request, std::string* out_body_utf8) {
    if (out_body_utf8) out_body_utf8->clear();
    std::string body;
    for (;;) {
        DWORD avail = 0u;
        if (!WinHttpQueryDataAvailable(request, &avail)) return false;
        if (avail == 0u) break;
        std::vector<char> chunk((size_t)avail);
        DWORD read = 0u;
        if (!WinHttpReadData(request, chunk.data(), avail, &read)) return false;
        body.append(chunk.data(), chunk.data() + read);
    }
    if (out_body_utf8) *out_body_utf8 = std::move(body);
    return true;
}

} // namespace

const char* ew_openai_default_model_utf8() {
    return "gpt-5-mini";
}

bool ew_openai_chat_available(std::wstring* out_key_file_w, std::string* out_err) {
    if (out_key_file_w) out_key_file_w->clear();
    if (out_err) out_err->clear();
    std::wstring path_w;
    if (!ew_resolve_api_key_file_w(&path_w)) {
        if (out_err) *out_err = "chatgptAPI.txt was not found in the application root chain";
        return false;
    }
    std::string key;
    if (!ew_load_api_key_from_file(path_w, &key, out_err)) return false;
    if (out_key_file_w) *out_key_file_w = path_w;
    return true;
}

bool ew_openai_send_text_request(const EwOpenAiTextRequest& request, EwOpenAiTextResponse* out_response) {
    if (out_response) *out_response = EwOpenAiTextResponse{};

    std::wstring key_path_w;
    std::string err;
    if (!ew_openai_chat_available(&key_path_w, &err)) {
        if (out_response) {
            out_response->error_utf8 = err;
            out_response->key_file_w = key_path_w;
        }
        return false;
    }

    std::string api_key_utf8;
    if (!ew_load_api_key_from_file(key_path_w, &api_key_utf8, &err)) {
        if (out_response) {
            out_response->error_utf8 = err;
            out_response->key_file_w = key_path_w;
        }
        return false;
    }

    const std::string body_utf8 = ew_build_request_body_json(request);
    const std::wstring headers_w =
        std::wstring(L"Content-Type: application/json\r\nAuthorization: Bearer ") +
        ew_utf8_to_wide_local(api_key_utf8) + L"\r\n";

    HINTERNET session = WinHttpOpen(L"GenesisEngine/1.0",
                                    WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                    WINHTTP_NO_PROXY_NAME,
                                    WINHTTP_NO_PROXY_BYPASS,
                                    0);
    if (!session) {
        if (out_response) {
            out_response->error_utf8 = "WinHttpOpen failed";
            out_response->key_file_w = key_path_w;
        }
        return false;
    }
    WinHttpSetTimeouts(session, 10000, 10000, 30000, 30000);

    HINTERNET connect = WinHttpConnect(session, L"api.openai.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!connect) {
        if (out_response) {
            out_response->error_utf8 = "WinHttpConnect failed";
            out_response->key_file_w = key_path_w;
        }
        WinHttpCloseHandle(session);
        return false;
    }

    HINTERNET req = WinHttpOpenRequest(connect,
                                       L"POST",
                                       L"/v1/responses",
                                       nullptr,
                                       WINHTTP_NO_REFERER,
                                       WINHTTP_DEFAULT_ACCEPT_TYPES,
                                       WINHTTP_FLAG_SECURE);
    if (!req) {
        if (out_response) {
            out_response->error_utf8 = "WinHttpOpenRequest failed";
            out_response->key_file_w = key_path_w;
        }
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return false;
    }

    const BOOL sent = WinHttpSendRequest(req,
                                         headers_w.c_str(),
                                         (DWORD)-1L,
                                         (LPVOID)body_utf8.data(),
                                         (DWORD)body_utf8.size(),
                                         (DWORD)body_utf8.size(),
                                         0);
    if (!sent || !WinHttpReceiveResponse(req, nullptr)) {
        if (out_response) {
            out_response->error_utf8 = "OpenAI request failed";
            out_response->key_file_w = key_path_w;
        }
        WinHttpCloseHandle(req);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return false;
    }

    DWORD status_u32 = 0u;
    DWORD status_len = sizeof(status_u32);
    (void)WinHttpQueryHeaders(req,
                              WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                              WINHTTP_HEADER_NAME_BY_INDEX,
                              &status_u32,
                              &status_len,
                              WINHTTP_NO_HEADER_INDEX);

    std::string response_body_utf8;
    const bool read_ok = ew_read_response_body(req, &response_body_utf8);
    WinHttpCloseHandle(req);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);

    if (!read_ok) {
        if (out_response) {
            out_response->http_status_u32 = status_u32;
            out_response->error_utf8 = "OpenAI response read failed";
            out_response->key_file_w = key_path_w;
        }
        return false;
    }

    if (out_response) {
        out_response->http_status_u32 = status_u32;
        out_response->key_file_w = key_path_w;
    }

    std::string output_text_utf8;
    const bool parse_ok = ew_extract_output_text_from_response_json(response_body_utf8, &output_text_utf8);
    if (status_u32 >= 200u && status_u32 < 300u && parse_ok && !output_text_utf8.empty()) {
        if (out_response) {
            std::vector<EwOpenAiSource> sources;
            ew_extract_sources_from_response_json(response_body_utf8, &sources);
            ew_append_sources_to_output_text(&output_text_utf8, sources);
            out_response->ok = true;
            out_response->output_text_utf8 = output_text_utf8;
        }
        return true;
    }

    if (out_response) {
        out_response->error_utf8 = ew_extract_error_message_from_response_json(response_body_utf8);
        if (out_response->error_utf8.empty()) {
            out_response->error_utf8 = parse_ok ? std::string("OpenAI response did not contain output text")
                                                : std::string("OpenAI response could not be parsed");
        }
    }
    return false;
}

} // namespace ewv
