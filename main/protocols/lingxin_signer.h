#ifndef LINGXIN_SIGNER_H
#define LINGXIN_SIGNER_H

#include <string>

class LingxinSigner {
public:
    static std::string UrlEncode(const std::string& value);
    static std::string GenerateSignature(const std::string& sn,
                                         const std::string& app_key,
                                         const std::string& app_id,
                                         const std::string& timestamp_ms);
};

#endif // LINGXIN_SIGNER_H
