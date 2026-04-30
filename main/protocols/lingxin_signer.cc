#include "lingxin_signer.h"

#include <mbedtls/base64.h>
#include <mbedtls/md.h>

#include <cctype>
#include <algorithm>
#include <array>

std::string LingxinSigner::UrlEncode(const std::string& value) {
    static const char hex[] = "0123456789ABCDEF";
    std::string encoded;
    encoded.reserve(value.size() * 3);

    for (unsigned char c : value) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            encoded.push_back(static_cast<char>(c));
        } else {
            encoded.push_back('%');
            encoded.push_back(hex[(c >> 4) & 0x0f]);
            encoded.push_back(hex[c & 0x0f]);
        }
    }

    return encoded;
}

std::string LingxinSigner::GenerateSignature(const std::string& sn,
                                             const std::string& app_key,
                                             const std::string& app_id,
                                             const std::string& timestamp_ms) {
    std::array<std::pair<std::string, std::string>, 3> params = {{
        {"app_id", app_id},
        {"sn", sn},
        {"timestamp", timestamp_ms},
    }};
    std::sort(params.begin(), params.end(),
        [](const auto& lhs, const auto& rhs) {
            return lhs.first < rhs.first;
        });

    std::string signing_text;
    for (size_t i = 0; i < params.size(); ++i) {
        if (i > 0) {
            signing_text.push_back('&');
        }
        signing_text += UrlEncode(params[i].first);
        signing_text.push_back('=');
        signing_text += UrlEncode(params[i].second);
    }

    unsigned char hmac[20] = {0};
    const mbedtls_md_info_t* md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA1);
    if (md_info == nullptr) {
        return "";
    }

    if (mbedtls_md_hmac(md_info,
                        reinterpret_cast<const unsigned char*>(app_key.data()),
                        app_key.size(),
                        reinterpret_cast<const unsigned char*>(signing_text.data()),
                        signing_text.size(),
                        hmac) != 0) {
        return "";
    }

    unsigned char base64[64] = {0};
    size_t base64_len = 0;
    if (mbedtls_base64_encode(base64, sizeof(base64), &base64_len, hmac, sizeof(hmac)) != 0) {
        return "";
    }

    return std::string(reinterpret_cast<char*>(base64), base64_len);
}
