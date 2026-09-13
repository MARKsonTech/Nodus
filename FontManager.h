#pragma once
#include <SDL_ttf.h>
#include <unordered_map>
#include <string>
#include <iostream>

class FontManager {
private:
    std::string font_path;
    std::unordered_map<int, TTF_Font*> font_cache;

public:
    FontManager(const std::string& path) : font_path(path) {}

    ~FontManager() {
        for (auto& pair : font_cache) {
            if (pair.second) {
                TTF_CloseFont(pair.second);
            }
        }
        font_cache.clear();
    }

    TTF_Font* get_font(int size, bool bold = false, bool italic = false) {
        const int cache_key = size * 4 + (bold ? 2 : 0) + (italic ? 1 : 0);
        if (font_cache.find(cache_key) != font_cache.end()) {
            return font_cache[cache_key];
        }

        TTF_Font* font = TTF_OpenFont(font_path.c_str(), size);
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