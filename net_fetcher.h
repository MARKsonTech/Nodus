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

    static std::unordered_map<std::string, std::string>& effective_urls() {
        static std::unordered_map<std::string, std::string> urls;
        return urls;
    }

    static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
        size_t total_size = size * nmemb;
        std::string* str = static_cast<std::string*>(userp);
        str->append(static_cast<char*>(contents), total_size);
        return total_size;
    }

public:
    static std::string fetch(const std::string& url, bool report_errors = true,
                             std::string* effective_url = nullptr) {
        auto& page_cache = cache();
        const auto cached = page_cache.find(url);
        if (cached != page_cache.end()) {
            if (effective_url) {
                const auto redirected = effective_urls().find(url);
                *effective_url = redirected == effective_urls().end() ? url : redirected->second;
            }
            return cached->second;
        }

        CURL* curl = curl_easy_init();
        std::string read_buffer;
        std::string fetched_url = url;

        if (curl) {
            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
            curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
            curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 15000L);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 60000L);
            curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &read_buffer);
            curl_easy_setopt(curl, CURLOPT_USERAGENT, "NodusEngine/1.0");

            CURLcode res = curl_easy_perform(curl);
            if (res != CURLE_OK && report_errors) {
                std::cerr << "cURL error: " << curl_easy_strerror(res) << "\n";
            }
            char* effective = nullptr;
            if (curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &effective) == CURLE_OK && effective) {
                fetched_url = effective;
            }
            curl_easy_cleanup(curl);
        }

        if (effective_url) *effective_url = fetched_url;
        if (!read_buffer.empty()) {
            page_cache[url] = read_buffer;
            effective_urls()[url] = fetched_url;
        }
        return read_buffer;
    }
};