#pragma once
#include <string>
#include <unordered_map>
#include <curl/curl.h>
#include <iostream>

class NetFetcher {
private:
    static std::unordered_map<std::string, std::string>& cache() {
        static std::unordered_map<std::string, std::string> page_cache;
        return page_cache;
    }

    static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
        size_t total_size = size * nmemb;
        std::string* str = static_cast<std::string*>(userp);
        str->append(static_cast<char*>(contents), total_size);
        return total_size;
    }

public:
    static std::string fetch(const std::string& url, bool report_errors = true) {
        auto& page_cache = cache();
        const auto cached = page_cache.find(url);
        if (cached != page_cache.end()) {
            return cached->second;
        }

        CURL* curl = curl_easy_init();
        std::string read_buffer;

        if (curl) {
            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
            curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
            curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 400L);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 500L);
            curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &read_buffer);
            curl_easy_setopt(curl, CURLOPT_USERAGENT, "NodusEngine/1.0");

            CURLcode res = curl_easy_perform(curl);
            if (res != CURLE_OK && report_errors) {
                std::cerr << "cURL error: " << curl_easy_strerror(res) << "\n";
            }
            curl_easy_cleanup(curl);
        }

        if (!read_buffer.empty()) {
            page_cache[url] = read_buffer;
        }
        return read_buffer;
    }
};