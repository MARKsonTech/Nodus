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
    static void clear_image_cache() {
        for (auto& image : image_cache()) {
            SDL_DestroyTexture(image.second);
        }
        image_cache().clear();
    }

    static void prepare_images(SDL_Renderer* renderer, const std::shared_ptr<Node>& node) {
        if (!node) return;

        if (node->tag_name == "img") {
            SDL_Texture* texture = get_image(renderer, node->image_src);
            if (texture) {
                int intrinsic_width = 0;
                int intrinsic_height = 0;
                SDL_QueryTexture(texture, nullptr, nullptr, &intrinsic_width, &intrinsic_height);

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
            SDL_SetRenderDrawColor(renderer, layout_node.node->style.r, layout_node.node->style.g,
                                   layout_node.node->style.b, layout_node.node->style.a);
            SDL_RenderFillRect(renderer, &background);
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
            TTF_Font* font = font_mgr.get_font(layout_node.node->style.font_size > 0 ? layout_node.node->style.font_size : 16,
                                               layout_node.node->style.font_weight == "bold" || layout_node.node->style.font_weight == "700",
                                               layout_node.node->style.font_style == "italic");
            if (font && !layout_node.node->text_data.empty()) {
                SDL_Color color = { layout_node.node->style.text_r, layout_node.node->style.text_g,
                                    layout_node.node->style.text_b, layout_node.node->style.a };
                SDL_Surface* surface = TTF_RenderUTF8_Blended(font, layout_node.node->text_data.c_str(), color);
                if (surface) {
                    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
                    if (texture) {
                        SDL_Rect destination = layout_node.rect;
                        destination.y -= scroll_y;
                        SDL_RenderCopy(renderer, texture, nullptr, &destination);
                        SDL_DestroyTexture(texture);
                    }
                    SDL_FreeSurface(surface);
                }
            }
        }

        for (const auto& child : layout_node.children) {
            render(renderer, child, font_mgr, scroll_y);
        }
    }

private:
    static std::unordered_map<std::string, SDL_Texture*>& image_cache() {
        static std::unordered_map<std::string, SDL_Texture*> cache;
        return cache;
    }

    static SDL_Texture* get_image(SDL_Renderer* renderer, const std::string& source) {
        if (source.empty()) return nullptr;
        auto& cache = image_cache();
        const auto existing = cache.find(source);
        if (existing != cache.end()) return existing->second;

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
        SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
        SDL_FreeSurface(surface);
        if (!texture) return nullptr;

        SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
        cache[source] = texture;
        return texture;
    }
};