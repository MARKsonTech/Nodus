#pragma once
#include <string>

class URLUtils {
public:
    static bool is_valid_url(const std::string& url) {
        return (url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0);
    }
};