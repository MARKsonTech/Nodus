#pragma once
#include <SDL.h>
#include <SDL_image.h>
#include <SDL_ttf.h>
#include "LayoutEngine.h"
#include "FontManager.h"
#include "net_fetcher.h"
#include <unordered_map>

class RenderEngine {
public:
    static constexpr size_t max_image_memory = 512ull * 1024ull * 1024ull;
    static constexpr size_t max_text_memory = 256ull * 1024ull * 1024ull;
    static constexpr size_t max_cached_images = 64;
    static constexpr size_t max_cached_text = 512;

    struct TextureEntry {
        SDL_Texture* texture = nullptr;
        size_t bytes = 0;
        Uint32 last_used = 0;
    };

    static void clear_image_cache() {
        for (auto& image : image_cache()) {
            if (image.second.texture) SDL_DestroyTexture(image.second.texture);
        }
        image_cache().clear();
        image_memory() = 0;

        for (auto& text : text_cache()) {
            if (text.second.texture) SDL_DestroyTexture(text.second.texture);
        }
        text_cache().clear();
        text_memory() = 0;
    }

    static void prepare_images(SDL_Renderer* renderer, const std::shared_ptr<Node>& node) {
        if (!node) return;

        if (node->tag_name == "img") {
            SDL_Texture* texture = get_image(renderer, node->image_src);
            if (texture) {
                int intrinsic_width = 0;
                int intrinsic_height = 0;
                SDL_QueryTexture(texture, nullptr, nullptr, &intrinsic_width, &intrinsic_height);
                node->intrinsic_width = intrinsic_width;
                node->intrinsic_height = intrinsic_height;

                if (node->image_width <= 0 && node->image_height <= 0) {
                    node->image_width = intrinsic_width;
                    node->image_height = intrinsic_height;
                } else if (node->image_width <= 0 && node->image_height > 0) {
                    node->image_width = intrinsic_width * node->image_height / intrinsic_height;
                } else if (node->image_height <= 0 && node->image_width > 0) {
                    node->image_height = intrinsic_height * node->image_width / intrinsic_width;
                }
            }
        }

        for (const auto& child : node->children) {
            prepare_images(renderer, child);
        }
    }

    static void render(SDL_Renderer* renderer, const LayoutNode& layout_node, FontManager& font_mgr, int scroll_y = 0) {
        if (!layout_node.node) return;

        if (layout_node.node->style.display == "none") return;

        if (layout_node.node->type == NodeType::Element && layout_node.node->tag_name != "img") {
            SDL_Rect background = layout_node.rect;
            background.y -= scroll_y;
            if (layout_node.node->style.background_set) {
                SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(renderer, layout_node.node->style.r, layout_node.node->style.g,
                                       layout_node.node->style.b, layout_node.node->style.a);
                SDL_RenderFillRect(renderer, &background);
            }
            if (layout_node.node->style.border_width > 0 && layout_node.node->style.border_style != "none") {
                SDL_SetRenderDrawColor(renderer, layout_node.node->style.border_r, layout_node.node->style.border_g,
                                       layout_node.node->style.border_b, layout_node.node->style.border_a);
                for (int border = 0; border < layout_node.node->style.border_width; ++border) {
                    SDL_Rect outline = { background.x + border, background.y + border,
                                         background.w - border * 2, background.h - border * 2 };
                    SDL_RenderDrawRect(renderer, &outline);
                }
            }
        }

        if (layout_node.node->tag_name == "img") {
            SDL_Texture* texture = get_image(renderer, layout_node.node->image_src);
            if (texture) {
                SDL_Rect destination = layout_node.rect;
                destination.y -= scroll_y;
                SDL_RenderCopy(renderer, texture, nullptr, &destination);
            }
        } else if (layout_node.node->type == NodeType::Text) {
            const std::string& text = layout_node.has_text_fragment ? layout_node.text_fragment : layout_node.node->text_data;
            TTF_Font* font = font_mgr.get_font(layout_node.node->style.font_size > 0 ? layout_node.node->style.font_size : 16,
                                               layout_node.node->style.font_weight == "bold" || layout_node.node->style.font_weight == "700",
                                               layout_node.node->style.font_style == "italic",
                                               text);
            if (font && !text.empty()) {
                SDL_Color color = { layout_node.node->style.text_r, layout_node.node->style.text_g,
                                    layout_node.node->style.text_b, 255 };
                SDL_Texture* texture = get_text_texture(renderer, font, text, color);
                if (texture) {
                    SDL_Rect destination = layout_node.rect;
                    destination.y -= scroll_y;
                    SDL_RenderCopy(renderer, texture, nullptr, &destination);
                }
            }
        }

        for (const auto& child : layout_node.children) {
            render(renderer, child, font_mgr, scroll_y);
        }
    }

    static std::string link_at(const LayoutNode& layout_node, int x, int y, int scroll_y) {
        if (!layout_node.node) return {};
        SDL_Rect bounds = layout_node.rect;
        bounds.y -= scroll_y;
        if (!layout_node.node->href.empty() && x >= bounds.x && x < bounds.x + bounds.w &&
            y >= bounds.y && y < bounds.y + bounds.h) {
            return layout_node.node->href;
        }
        for (const auto& child : layout_node.children) {
            const std::string href = link_at(child, x, y, scroll_y);
            if (!href.empty()) return href;
        }
        return {};
    }

private:
    static std::unordered_map<std::string, TextureEntry>& image_cache() {
        static std::unordered_map<std::string, TextureEntry> cache;
        return cache;
    }

    static std::unordered_map<std::string, TextureEntry>& text_cache() {
        static std::unordered_map<std::string, TextureEntry> cache;
        return cache;
    }

    static size_t& text_memory() {
        static size_t bytes = 0;
        return bytes;
    }

    static void prune_text_cache() {
        auto& cache = text_cache();
        while (text_memory() > max_text_memory || cache.size() > max_cached_text) {
            auto oldest = cache.begin();
            for (auto it = cache.begin(); it != cache.end(); ++it) {
                if (it->second.texture && (oldest == cache.end() || it->second.last_used < oldest->second.last_used)) {
                    oldest = it;
                }
            }
            if (oldest == cache.end()) break;
            if (oldest->second.texture) SDL_DestroyTexture(oldest->second.texture);
            text_memory() -= oldest->second.bytes;
            cache.erase(oldest);
        }
    }

    static SDL_Texture* get_text_texture(SDL_Renderer* renderer, TTF_Font* font, const std::string& text, const SDL_Color& color) {
        if (!font || text.empty()) return nullptr;
        const std::string font_key = std::to_string(TTF_FontHeight(font)) + ":" + std::to_string(TTF_GetFontStyle(font)) + ":" + std::to_string(color.r) + ":" + std::to_string(color.g) + ":" + std::to_string(color.b) + ":" + text;
        auto& cache = text_cache();
        auto existing = cache.find(font_key);
        if (existing != cache.end()) {
            existing->second.last_used = SDL_GetTicks();
            return existing->second.texture;
        }

        SDL_Surface* surface = TTF_RenderUTF8_Blended(font, text.c_str(), color);
        if (!surface) return nullptr;
        SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
        const size_t texture_bytes = static_cast<size_t>(surface->w) * static_cast<size_t>(surface->h) * 4;
        SDL_FreeSurface(surface);
        if (!texture) return nullptr;

        prune_text_cache();
        if (text_memory() + texture_bytes > max_text_memory) {
            SDL_DestroyTexture(texture);
            return nullptr;
        }
        if (cache.size() >= max_cached_text) {
            prune_text_cache();
        }

        cache[font_key] = { texture, texture_bytes, SDL_GetTicks() };
        text_memory() += texture_bytes;
        return texture;
    }

    static void prune_cache() {
        auto& cache = image_cache();
        while (image_memory() > max_image_memory || cache.size() > max_cached_images) {
            auto oldest = cache.begin();
            for (auto it = cache.begin(); it != cache.end(); ++it) {
                if (it->second.texture && (oldest == cache.end() || it->second.last_used < oldest->second.last_used)) {
                    oldest = it;
                }
            }
            if (oldest == cache.end()) break;
            if (oldest->second.texture) SDL_DestroyTexture(oldest->second.texture);
            image_memory() -= oldest->second.bytes;
            cache.erase(oldest);
        }
    }

    static SDL_Texture* get_image(SDL_Renderer* renderer, const std::string& source) {
        if (source.empty()) return nullptr;
        auto& cache = image_cache();
        auto existing = cache.find(source);
        if (existing != cache.end()) {
            existing->second.last_used = SDL_GetTicks();
            return existing->second.texture;
        }

        SDL_Surface* surface = nullptr;
        if (source.rfind("http://", 0) == 0 || source.rfind("https://", 0) == 0) {
            const std::string data = NetFetcher::fetch(source);
            if (!data.empty()) {
                SDL_RWops* rw = SDL_RWFromConstMem(data.data(), static_cast<int>(data.size()));
                if (rw) surface = IMG_Load_RW(rw, 1);
            }
        } else {
            surface = IMG_Load(source.c_str());
        }

        if (!surface) return nullptr;
        const size_t texture_bytes = static_cast<size_t>(surface->w) * static_cast<size_t>(surface->h) * 4;
        SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
        SDL_FreeSurface(surface);
        if (!texture) return nullptr;

        SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
        prune_cache();
        if (image_memory() + texture_bytes > max_image_memory) {
            if (!cache.empty()) prune_cache();
            if (image_memory() + texture_bytes > max_image_memory) {
                SDL_DestroyTexture(texture);
                return nullptr;
            }
        }
        if (cache.size() >= max_cached_images) {
            prune_cache();
        }

        cache[source] = { texture, texture_bytes, SDL_GetTicks() };
        image_memory() += texture_bytes;
        return texture;
    }

    static size_t& image_memory() {
        static size_t bytes = 0;
        return bytes;
    }
};