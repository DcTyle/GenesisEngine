#pragma once

#include <cstdint>
#include <string>

namespace ewv {

struct EwOpenAiTextRequest {
    std::string instructions_utf8;
    std::string input_utf8;
    std::string model_utf8 = "gpt-5-mini";
    bool enable_web_search = true;
    uint32_t max_output_tokens_u32 = 900u;
};

struct EwOpenAiTextResponse {
    bool ok = false;
    uint32_t http_status_u32 = 0u;
    std::string output_text_utf8;
    std::string error_utf8;
    std::wstring key_file_w;
};

const char* ew_openai_default_model_utf8();
bool ew_openai_chat_available(std::wstring* out_key_file_w, std::string* out_err);
bool ew_openai_send_text_request(const EwOpenAiTextRequest& request, EwOpenAiTextResponse* out_response);

} // namespace ewv
