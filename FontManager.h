#pragma once
#include <SDL_ttf.h>
#include <unordered_map>
#include <string>
#include <vector>
#include <iostream>

class FontManager {
private:
    std::string font_path;
    std::vector<std::string> fallback_paths;
    std::unordered_map<std::string, TTF_Font*> font_cache;

    static bool is_utf8_lead(unsigned char value) {
        return (value & 0xC0) == 0x80;
    }

    static uint32_t next_codepoint(const std::string& text, size_t& index) {
        if (index >= text.size()) return 0;
        const unsigned char first = static_cast<unsigned char>(text[index]);
        if ((first & 0x80) == 0) {
            ++index;
            return first;
        }
        if ((first & 0xE0) == 0xC0 && index + 1 < text.size()) {
            const uint32_t codepoint = ((first & 0x1F) << 6) | (static_cast<unsigned char>(text[index + 1]) & 0x3F);
            index += 2;
            return codepoint;
        }
        if ((first & 0xF0) == 0xE0 && index + 2 < text.size()) {
            const uint32_t codepoint = ((first & 0x0F) << 12) |
                ((static_cast<unsigned char>(text[index + 1]) & 0x3F) << 6) |
                (static_cast<unsigned char>(text[index + 2]) & 0x3F);
            index += 3;
            return codepoint;
        }
        if ((first & 0xF8) == 0xF0 && index + 3 < text.size()) {
            const uint32_t codepoint = ((first & 0x07) << 18) |
                ((static_cast<unsigned char>(text[index + 1]) & 0x3F) << 12) |
                ((static_cast<unsigned char>(text[index + 2]) & 0x3F) << 6) |
                (static_cast<unsigned char>(text[index + 3]) & 0x3F);
            index += 4;
            return codepoint;
        }
        ++index;
        return 0;
    }

    static bool is_emoji_or_symbol(uint32_t codepoint) {
        return (codepoint >= 0x1F300 && codepoint <= 0x1FAFF) ||
               (codepoint >= 0x2600 && codepoint <= 0x27BF) ||
               (codepoint >= 0xFE00 && codepoint <= 0xFE0F) ||
               (codepoint >= 0x1F1E6 && codepoint <= 0x1F1FF) ||
               (codepoint >= 0x2700 && codepoint <= 0x27B0) ||
               (codepoint == 0x20E3);
    }

    static bool text_requires_emoji_font(const std::string& text) {
        size_t index = 0;
        while (index < text.size()) {
            const uint32_t codepoint = next_codepoint(text, index);
            if (is_emoji_or_symbol(codepoint)) return true;
        }
        return false;
    }

    std::string resolve_font_path_for_text(const std::string& text) const {
        if (text_requires_emoji_font(text)) {
            for (const auto& path : fallback_paths) {
                if (!path.empty()) return path;
            }
        }
        return font_path;
    }

public:
    FontManager(const std::string& path) : font_path(path) {
        fallback_paths.push_back("C:\\Windows\\Fonts\\seguiemj.ttf");
        fallback_paths.push_back("C:\\Windows\\Fonts\\seguiemj.ttf");
        fallback_paths.push_back("C:\\Windows\\Fonts\\seguisym.ttf");
    }

    ~FontManager() {
        for (auto& pair : font_cache) {
            if (pair.second) {
                TTF_CloseFont(pair.second);
            }
        }
        font_cache.clear();
    }

    TTF_Font* get_font(int size, bool bold = false, bool italic = false, const std::string& text = {}) {
        const std::string font_path_for_text = resolve_font_path_for_text(text);
        const std::string cache_key = std::to_string(size) + ":" + std::to_string(bold) + ":" + std::to_string(italic) + ":" + font_path_for_text;
        if (font_cache.find(cache_key) != font_cache.end()) {
            return font_cache[cache_key];
        }

        TTF_Font* font = TTF_OpenFont(font_path_for_text.c_str(), size);
        if (!font) {
            std::cerr << "Failed to load font at size " << size << ": " << TTF_GetError() << "\n";
            return nullptr;
        }

        int style = TTF_STYLE_NORMAL;
        if (bold) style |= TTF_STYLE_BOLD;
        if (italic) style |= TTF_STYLE_ITALIC;
        TTF_SetFontStyle(font, style);

        font_cache[cache_key] = font;
        return font;
    }
};