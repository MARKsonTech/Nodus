#pragma once
#include <cctype>
#include <string>

class URLUtils {
public:
    static std::string normalize_url(const std::string& input) {
        size_t first = 0;
        while (first < input.size() && std::isspace(static_cast<unsigned char>(input[first]))) ++first;
        size_t last = input.size();
        while (last > first && std::isspace(static_cast<unsigned char>(input[last - 1]))) --last;

        std::string url = input.substr(first, last - first);
        if (url.empty()) return url;
        std::string prefix = url.substr(0, 8);
        for (char& character : prefix) {
            character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
        }
        if (prefix == "https://" || prefix.substr(0, 7) == "http://") return url;
        return "https://" + url;
    }

    static bool is_valid_url(const std::string& url) {
        return (url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0);
    }
};