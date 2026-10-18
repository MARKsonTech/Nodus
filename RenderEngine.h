#pragma once
#include <SDL.h>
#include <SDL_image.h>
#include <SDL_ttf.h>
#include <cstdio>
#include <cmath>
#include "LayoutEngine.h"
#include "FontManager.h"
#include "net_fetcher.h"
#include "VideoPlayer.h"
#include <unordered_map>

class RenderEngine {
public:
    static constexpr size_t max_image_memory = 512ull * 1024ull * 1024ull;
    static constexpr size_t max_text_memory = 256ull * 1024ull * 1024ull;
    static constexpr size_t max_cached_images = 64;
    static constexpr size_t max_cached_text = 512;
    static constexpr int controls_height = 42;

    struct TextureEntry {
        SDL_Texture* texture = nullptr;
        size_t bytes = 0;
        Uint32 last_used = 0;
    };

    struct VideoEntry {
        std::unique_ptr<VideoPlayer> player;
        SDL_Texture* texture = nullptr;
        std::shared_ptr<const VideoPlayer::Frame> frame;
        int texture_width = 0;
        int texture_height = 0;
        uint64_t last_frame_serial = 0;
        uint64_t uploaded_frame_serial = 0;
        int64_t last_position_ms = 0;
        bool settings_open = false;
        bool seeking = false;
        int selected_quality = 1080;
        std::string active_source;
        bool last_ready = false;
        bool last_playing = false;
        std::string last_error;
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

        if (node->tag_name == "video" && !node->poster_src.empty()) {
            get_image(renderer, node->poster_src);
        } else if (node->tag_name == "img") {
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

    static void clear_video_players() {
        for (auto& video : video_players()) {
            if (video.second.texture) SDL_DestroyTexture(video.second.texture);
        }
        video_players().clear();
        fullscreen_node() = nullptr;
    }

    static bool is_video_fullscreen() {
        return fullscreen_node() != nullptr;
    }

    static void exit_video_fullscreen(SDL_Window* window) {
        if (fullscreen_node()) set_video_fullscreen(nullptr, window);
    }

    static void prepare_videos(const std::shared_ptr<Node>& node) {
        if (!node) return;
        if (node->tag_name == "video" && !node->media_src.empty() &&
            video_players().find(node.get()) == video_players().end()) {
            const VideoSource* default_source = nullptr;
            for (const auto& source : node->media_sources) {
                if (source.height <= 0) continue;
                if (!default_source ||
                    (source.height <= 1080 && source.height > default_source->height) ||
                    (default_source->height > 1080 && source.height < default_source->height)) {
                    default_source = &source;
                }
            }
            const std::string initial_source = default_source ? default_source->url : node->media_src;
            VideoEntry video;
            video.active_source = initial_source;
            video.player = std::make_unique<VideoPlayer>(initial_source, node->media_autoplay,
                                                         node->media_muted, node->media_loop);
            video.player->set_quality_height(1080);
            video_players().emplace(node.get(), std::move(video));
        }
        for (const auto& child : node->children) prepare_videos(child);
    }

    static bool any_video_playing() {
        for (const auto& video : video_players()) {
            if (video.second.player->is_playing()) return true;
        }
        return false;
    }

    static bool video_state_changed() {
        bool changed = false;
        for (auto& video : video_players()) {
            const bool ready = video.second.player->is_ready();
            const bool playing = video.second.player->is_playing();
            const uint64_t frame_serial = video.second.player->frame_serial();
            const int64_t position_ms = video.second.player->position_ms();
            const std::string error = video.second.player->error();
            if (ready != video.second.last_ready || playing != video.second.last_playing ||
                frame_serial != video.second.last_frame_serial || position_ms != video.second.last_position_ms ||
                error != video.second.last_error) {
                video.second.last_ready = ready;
                video.second.last_playing = playing;
                video.second.last_frame_serial = frame_serial;
                video.second.last_position_ms = position_ms;
                video.second.last_error = error;
                changed = true;
            }
        }
        return changed;
    }

    static bool handle_video_click(const LayoutNode& layout_node, int x, int y, int scroll_y,
                                   SDL_Window* window) {
        if (!layout_node.node) return false;
        if (layout_node.node->tag_name == "video") {
            SDL_Rect bounds = layout_node.rect;
            const bool fullscreen = fullscreen_node() == layout_node.node.get();
            if (fullscreen) {
                SDL_GetWindowSize(window, &bounds.w, &bounds.h);
                bounds.x = 0;
                bounds.y = 0;
            } else {
                bounds.y -= scroll_y;
            }
            const auto found = video_players().find(layout_node.node.get());
            if (found == video_players().end()) {
                return x >= bounds.x && x < bounds.x + bounds.w &&
                       y >= bounds.y && y < bounds.y + bounds.h;
            }
            VideoEntry& video = found->second;
            VideoPlayer& player = *video.player;
            const SDL_Rect menu = settings_rect(bounds);
            if (video.settings_open && x >= menu.x && x < menu.x + menu.w &&
                y >= menu.y && y < menu.y + menu.h) {
                const int local_x = x - menu.x;
                const int local_y = y - menu.y;
                if (local_x < 132) {
                    const int row = (local_y - 30) / 22;
                    if (local_y >= 30 && row >= 0 && row < 10) {
                        static const int quality_heights[] = { 144, 240, 360, 480, 720, 1080, 1440, 2160, 4320, 8640 };
                        select_quality(*layout_node.node, player, quality_heights[row]);
                        video.selected_quality = quality_heights[row];
                        video.settings_open = false;
                    }
                } else {
                    const int row = (local_y - 30) / 22;
                    if (local_y >= 30 && row >= 0 && row <= 11) {
                        player.set_playback_rate((row + 1) * 0.25);
                        video.settings_open = false;
                    }
                }
                return true;
            }

            if (x < bounds.x || x >= bounds.x + bounds.w || y < bounds.y || y >= bounds.y + bounds.h) return false;
            const int controls_top = bounds.y + bounds.h - controls_height;
            if (y < controls_top) {
                if (video.settings_open) video.settings_open = false;
                else player.toggle_playback();
            } else if (x < bounds.x + 42) {
                player.play();
            } else if (x < bounds.x + bounds.w - 224) {
                seek_to_x(player, bounds, x);
                video.seeking = true;
            } else if (x < bounds.x + bounds.w - 184) {
                player.toggle_muted();
            } else if (x < bounds.x + bounds.w - 108) {
                player.set_volume((x - (bounds.x + bounds.w - 178)) * 100 / 68);
            } else if (x < bounds.x + bounds.w - 62) {
                video.settings_open = !video.settings_open;
            } else {
                set_video_fullscreen(fullscreen ? nullptr : layout_node.node.get(), window);
            }
            return true;
        }

        for (const auto& child : layout_node.children) {
            if (handle_video_click(child, x, y, scroll_y, window)) return true;
        }
        return false;
    }

    static bool drag_video_seek(const LayoutNode& node, int x, int y, int scroll_y) {
        if (!node.node) return false;
        if (node.node->tag_name == "video") {
            auto found = video_players().find(node.node.get());
            if (found == video_players().end() || !found->second.seeking) return false;
            SDL_Rect bounds = node.rect;
            if (fullscreen_node() == node.node.get()) {
                bounds = fullscreen_rect();
            } else {
                bounds.y -= scroll_y;
            }
            seek_to_x(*found->second.player, bounds, x);
            return true;
        }
        for (const auto& child : node.children) {
            if (drag_video_seek(child, x, y, scroll_y)) return true;
        }
        return false;
    }

    static bool finish_video_seek(const LayoutNode& node) {
        if (!node.node) return false;
        bool finished = false;
        if (node.node->tag_name == "video") {
            auto found = video_players().find(node.node.get());
            if (found != video_players().end() && found->second.seeking) {
                found->second.seeking = false;
                finished = true;
            }
        }
        for (const auto& child : node.children) finished = finish_video_seek(child) || finished;
        return finished;
    }

    static void render(SDL_Renderer* renderer, const LayoutNode& layout_node, FontManager& font_mgr,
                       SDL_Window* window, int scroll_y = 0) {
        if (fullscreen_node()) {
            if (!layout_node.node || layout_node.node->tag_name != "body") return;
            SDL_Rect screen{ 0, 0, 0, 0 };
            SDL_GetRendererOutputSize(renderer, &screen.w, &screen.h);
            fullscreen_rect() = screen;
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
            SDL_RenderFillRect(renderer, &screen);
            const LayoutNode* target = find_video_layout(layout_node, fullscreen_node());
            if (target) render_video(renderer, *target, 0, font_mgr, window, &screen);
            return;
        }
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
        } else if (layout_node.node->tag_name == "video") {
            render_video(renderer, layout_node, scroll_y, font_mgr, window);
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

        if (layout_node.node->tag_name != "video") {
            for (const auto& child : layout_node.children) {
                render(renderer, child, font_mgr, window, scroll_y);
            }
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
    static const LayoutNode* find_video_layout(const LayoutNode& node, const Node* wanted) {
        if (node.node.get() == wanted) return &node;
        for (const auto& child : node.children) {
            const LayoutNode* found = find_video_layout(child, wanted);
            if (found) return found;
        }
        return nullptr;
    }

    static void set_video_fullscreen(const Node* node, SDL_Window* window) {
        if (fullscreen_node() == node) return;
        fullscreen_node() = node;
        if (SDL_SetWindowFullscreen(window, node ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0) < 0) {
            fullscreen_node() = nullptr;
            std::cerr << "Could not change video fullscreen mode: " << SDL_GetError() << "\n";
        }
    }

    static const Node*& fullscreen_node() {
        static const Node* active = nullptr;
        return active;
    }

    static SDL_Rect& fullscreen_rect() {
        static SDL_Rect bounds{ 0, 0, 0, 0 };
        return bounds;
    }

    static std::string resource_path(const char* relative_path) {
        char* base_path = SDL_GetBasePath();
        if (!base_path) return relative_path;
        std::string path = base_path;
        path += relative_path;
        SDL_free(base_path);
        return path;
    }

    static std::unordered_map<const Node*, VideoEntry>& video_players() {
        static std::unordered_map<const Node*, VideoEntry> players;
        return players;
    }

    static void render_video(SDL_Renderer* renderer, const LayoutNode& layout_node, int scroll_y,
                             FontManager& font_mgr, SDL_Window* window,
                             const SDL_Rect* fullscreen_bounds = nullptr) {
        SDL_Rect destination = fullscreen_bounds ? *fullscreen_bounds : layout_node.rect;
        if (!fullscreen_bounds) destination.y -= scroll_y;
        SDL_SetRenderDrawColor(renderer, 12, 15, 20, 255);
        SDL_RenderFillRect(renderer, &destination);

        const auto found = video_players().find(layout_node.node.get());
        bool has_frame = false;
        if (found != video_players().end()) {
            VideoEntry& video = found->second;
            video.player->set_output_size(destination.w, destination.h);
            const auto current_frame = video.player->latest_frame();
            if (current_frame && current_frame->serial != video.uploaded_frame_serial) {
                video.frame = current_frame;
                video.uploaded_frame_serial = current_frame->serial;
                if (video.texture_width != current_frame->width ||
                    video.texture_height != current_frame->height) {
                    if (video.texture) SDL_DestroyTexture(video.texture);
                    video.texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32,
                                                      SDL_TEXTUREACCESS_STREAMING,
                                                      current_frame->width, current_frame->height);
                    video.texture_width = video.texture ? current_frame->width : 0;
                    video.texture_height = video.texture ? current_frame->height : 0;
                }
                if (video.texture &&
                    SDL_UpdateTexture(video.texture, nullptr, current_frame->rgba.data(),
                                      current_frame->width * 4) < 0) {
                    std::cerr << "Could not update video frame texture: " << SDL_GetError() << "\n";
                }
            }
            has_frame = video.texture != nullptr;
            if (has_frame) {
                SDL_Rect video_destination = destination;
                if (fullscreen_bounds && video.frame && video.frame->width > 0 && video.frame->height > 0) {
                    const double scale = std::min(
                        static_cast<double>(destination.w) / video.frame->width,
                        static_cast<double>(destination.h) / video.frame->height);
                    video_destination.w = static_cast<int>(video.frame->width * scale);
                    video_destination.h = static_cast<int>(video.frame->height * scale);
                    video_destination.x += (destination.w - video_destination.w) / 2;
                    video_destination.y += (destination.h - video_destination.h) / 2;
                }
                SDL_RenderCopy(renderer, video.texture, nullptr, &video_destination);
            }
        }

        if (!has_frame && !layout_node.node->poster_src.empty()) {
            SDL_Texture* poster = get_image(renderer, layout_node.node->poster_src);
            if (poster) SDL_RenderCopy(renderer, poster, nullptr, &destination);
        }

        std::string status_message;
        if (layout_node.node->media_src.empty()) {
            status_message = "No video source found";
        } else if (found != video_players().end() && !found->second.player->error().empty() && !has_frame) {
            status_message = "Unable to play this video";
        } else if (found != video_players().end() && !found->second.player->is_ready() && !has_frame &&
                   layout_node.node->poster_src.empty()) {
            status_message = "Loading video...";
        }
        if (!status_message.empty()) {
            SDL_Color error_color = { 255, 255, 255, 255 };
            TTF_Font* font = font_mgr.get_font(14);
            if (font) {
                SDL_Surface* surface = TTF_RenderUTF8_Blended(font, status_message.c_str(), error_color);
                if (surface) {
                    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
                    if (texture) {
                        SDL_Rect text_rect = { destination.x + 12, destination.y + destination.h / 2 - surface->h / 2,
                                               surface->w, surface->h };
                        SDL_RenderCopy(renderer, texture, nullptr, &text_rect);
                        SDL_DestroyTexture(texture);
                    }
                    SDL_FreeSurface(surface);
                }
            }
        }

        VideoEntry* video = found == video_players().end() ? nullptr : &found->second;
        VideoPlayer* player = video ? video->player.get() : nullptr;
        if (layout_node.node->media_controls) {
            SDL_Rect controls = { destination.x, destination.y + std::max(0, destination.h - controls_height),
                                  destination.w, std::min(controls_height, destination.h) };
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(renderer, 8, 10, 14, 235);
            SDL_RenderFillRect(renderer, &controls);
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);

            const bool is_playing = player && player->is_playing();
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
            if (player && player->is_finished()) {
                const int center_x = destination.x + 21;
                const int center_y = controls.y + controls.h / 2;
                SDL_RenderDrawLine(renderer, center_x + 8, center_y - 9, center_x - 7, center_y - 9);
                SDL_RenderDrawLine(renderer, center_x - 7, center_y - 9, center_x - 7, center_y + 7);
                SDL_RenderDrawLine(renderer, center_x - 7, center_y + 7, center_x + 6, center_y + 7);
                SDL_RenderDrawLine(renderer, center_x + 6, center_y + 7, center_x + 6, center_y - 3);
                SDL_RenderDrawLine(renderer, center_x + 6, center_y - 3, center_x + 1, center_y + 2);
                SDL_RenderDrawLine(renderer, center_x + 6, center_y - 3, center_x + 11, center_y + 2);
            } else if (is_playing) {
                SDL_Rect pause_left = { destination.x + 13, controls.y + 13, 5, 16 };
                SDL_Rect pause_right = { destination.x + 23, controls.y + 13, 5, 16 };
                SDL_RenderFillRect(renderer, &pause_left);
                SDL_RenderFillRect(renderer, &pause_right);
            } else {
                const int center_x = destination.x + 21;
                const int center_y = controls.y + controls.h / 2;
                SDL_RenderDrawLine(renderer, center_x - 5, center_y - 9, center_x - 5, center_y + 9);
                SDL_RenderDrawLine(renderer, center_x - 5, center_y - 9, center_x + 9, center_y);
                SDL_RenderDrawLine(renderer, center_x + 9, center_y, center_x - 5, center_y + 9);
            }

            const int track_start = destination.x + 138;
            const int track_end = destination.x + destination.w - 232;
            const int track_width = std::max(1, track_end - track_start);
            const int track_y = controls.y + controls.h / 2;
            TTF_Font* controls_font = font_mgr.get_font(11);
            const int64_t duration = player ? player->duration_ms() : 0;
            const int64_t position = player ? player->position_ms() : 0;
            char elapsed_text[16]{};
            char duration_text[16]{};
            std::snprintf(elapsed_text, sizeof(elapsed_text), "%lld:%02lld",
                          static_cast<long long>(position / 60000),
                          static_cast<long long>((position / 1000) % 60));
            std::snprintf(duration_text, sizeof(duration_text), "%lld:%02lld",
                          static_cast<long long>(duration / 60000),
                          static_cast<long long>((duration / 1000) % 60));
            SDL_Color time_color = { 238, 240, 245, 255 };
            if (controls_font) {
                draw_label(renderer, controls_font, elapsed_text, time_color, destination.x + 50, controls.y + 14);
                draw_label(renderer, controls_font, "/", { 130, 135, 144, 255 }, destination.x + 83, controls.y + 14);
                draw_label(renderer, controls_font, duration_text, { 170, 175, 184, 255 }, destination.x + 91, controls.y + 14);
            }

            SDL_SetRenderDrawColor(renderer, 83, 88, 96, 255);
            SDL_RenderDrawLine(renderer, track_start, track_y, track_end, track_y);
            int progress_x = track_start;
            if (player && duration > 0) {
                const double fraction = std::max(0.0, std::min(1.0,
                    static_cast<double>(position) / duration));
                progress_x = track_start + static_cast<int>(track_width * fraction);
                SDL_SetRenderDrawColor(renderer, 219, 222, 228, 255);
                SDL_RenderDrawLine(renderer, track_start, track_y, progress_x, track_y);
                SDL_Rect thumb = { progress_x - 6, track_y - 6, 12, 12 };
                SDL_RenderFillRect(renderer, &thumb);
            }

            const int volume_x = destination.x + destination.w - 216;
            const bool volume_muted = !player || player->is_muted() || player->volume() == 0;
            SDL_Texture* volume_icon = get_image(renderer, resource_path(
                volume_muted ? "resources/video/mute.png" : "resources/video/volume.png"));
            if (volume_icon) {
                SDL_Rect icon = { volume_x, track_y - 11, 22, 22 };
                SDL_RenderCopy(renderer, volume_icon, nullptr, &icon);
            }
            const int volume_track_start = destination.x + destination.w - 184;
            const int volume_track_end = destination.x + destination.w - 118;
            SDL_SetRenderDrawColor(renderer, 83, 88, 96, 255);
            SDL_RenderDrawLine(renderer, volume_track_start, track_y, volume_track_end, track_y);
            if (player) {
                const int volume_thumb_x = volume_track_start +
                    (volume_track_end - volume_track_start) * player->volume() / 100;
                SDL_SetRenderDrawColor(renderer, 232, 235, 241, 255);
                SDL_RenderDrawLine(renderer, volume_track_start, track_y, volume_thumb_x, track_y);
                SDL_Rect thumb = { volume_thumb_x - 5, track_y - 5, 10, 10 };
                SDL_RenderFillRect(renderer, &thumb);
            }

            SDL_Texture* settings_icon = get_image(renderer, resource_path("resources/video/settings.png"));
            if (settings_icon) {
                SDL_Rect icon = { destination.x + destination.w - 96, track_y - 11, 22, 22 };
                SDL_RenderCopy(renderer, settings_icon, nullptr, &icon);
            }

            const char* fullscreen_icon_path = fullscreen_node() == layout_node.node.get()
                ? "resources/video/exitfullscreen.png"
                : "resources/video/fullscreen.png";
            SDL_Texture* fullscreen_icon = get_image(renderer, resource_path(fullscreen_icon_path));
            if (fullscreen_icon) {
                SDL_Rect icon = { destination.x + destination.w - 42, track_y - 11, 22, 22 };
                SDL_RenderCopy(renderer, fullscreen_icon, nullptr, &icon);
            }
        }

        if (video && video->settings_open && layout_node.node->media_controls) {
            const SDL_Rect menu = settings_rect(destination);
            SDL_SetRenderDrawColor(renderer, 24, 27, 33, 250);
            SDL_RenderFillRect(renderer, &menu);
            SDL_SetRenderDrawColor(renderer, 79, 84, 94, 255);
            SDL_RenderDrawRect(renderer, &menu);
            TTF_Font* menu_font = font_mgr.get_font(12);
            if (menu_font) {
                const SDL_Color heading = { 182, 189, 201, 255 };
                const SDL_Color normal = { 238, 240, 244, 255 };
                draw_label(renderer, menu_font, "QUALITY", heading, menu.x + 10, menu.y + 8);
                draw_label(renderer, menu_font, "SPEED", heading, menu.x + 145, menu.y + 8);
                static const int quality_heights[] = { 144, 240, 360, 480, 720, 1080, 1440, 2160, 4320, 8640 };
                static const char* quality_labels[] = { "144p", "240p", "360p", "480p", "720p",
                                                        "1080p", "1440p", "4K", "8K", "16K" };
                for (int index = 0; index < 10; ++index) {
                    const int row_y = menu.y + 30 + index * 22;
                    if (video->selected_quality == quality_heights[index]) {
                        SDL_SetRenderDrawColor(renderer, 52, 103, 185, 255);
                        SDL_Rect highlight = { menu.x + 5, row_y - 2, 124, 20 };
                        SDL_RenderFillRect(renderer, &highlight);
                    }
                    draw_label(renderer, menu_font, quality_labels[index], normal, menu.x + 10, row_y);
                }
                for (int index = 0; index < 12; ++index) {
                    const double rate = (index + 1) * 0.25;
                    const int row_y = menu.y + 30 + index * 22;
                    if (std::abs(player->playback_rate() - rate) < 0.01) {
                        SDL_SetRenderDrawColor(renderer, 52, 103, 185, 255);
                        SDL_Rect highlight = { menu.x + 139, row_y - 2, 124, 20 };
                        SDL_RenderFillRect(renderer, &highlight);
                    }
                    char rate_label[12]{};
                    std::snprintf(rate_label, sizeof(rate_label), "%.2gx", rate);
                    draw_label(renderer, menu_font, rate_label, normal, menu.x + 145, row_y);
                }
                const bool has_alternate_source = layout_node.node->media_sources.size() > 1 ||
                    (!layout_node.node->media_sources.empty() &&
                     layout_node.node->media_src != layout_node.node->media_sources.front().url);
                const char* source_note = has_alternate_source
                    ? "Available sources change bandwidth"
                    : "Resolution cap only; source has no variants";
                draw_label(renderer, menu_font, source_note, heading, menu.x + 10, menu.y + menu.h - 19);
            }
        }

        SDL_SetRenderDrawColor(renderer, 52, 57, 66, 255);
        SDL_RenderDrawRect(renderer, &destination);
    }

    static SDL_Rect settings_rect(const SDL_Rect& bounds) {
        constexpr int menu_width = 270;
        constexpr int menu_height = 320;
        return { std::max(bounds.x + bounds.w - menu_width, bounds.x),
                 std::max(0, bounds.y + bounds.h - controls_height - menu_height),
                 menu_width, menu_height };
    }

    static void seek_to_x(VideoPlayer& player, const SDL_Rect& bounds, int x) {
        const int track_start = bounds.x + 44;
        const int track_end = bounds.x + bounds.w - 230;
        const int track_width = std::max(1, track_end - track_start);
        const double fraction = std::max(0.0, std::min(1.0,
            static_cast<double>(x - track_start) / track_width));
        const int64_t duration = player.duration_ms();
        if (duration > 0) player.seek(static_cast<int64_t>(duration * fraction));
    }

    static void select_quality(const Node& node, VideoPlayer& player, int requested_height) {
        player.set_quality_height(requested_height);
        const VideoSource* selected = nullptr;
        for (const auto& source : node.media_sources) {
            if (source.height <= 0) continue;
            if (!selected ||
                (source.height <= requested_height && source.height > selected->height) ||
                (selected->height > requested_height && source.height < selected->height)) {
                selected = &source;
            }
        }
        auto found = video_players().find(&node);
        if (selected && found != video_players().end() && selected->url != found->second.active_source) {
            player.change_source(selected->url);
            found->second.active_source = selected->url;
        }
    }

    static void draw_label(SDL_Renderer* renderer, TTF_Font* font, const char* text,
                           SDL_Color color, int x, int y) {
        SDL_Texture* texture = get_text_texture(renderer, font, text, color);
        if (!texture) return;
        int width = 0;
        int height = 0;
        if (SDL_QueryTexture(texture, nullptr, nullptr, &width, &height) == 0) {
            SDL_Rect destination = { x, y, width, height };
            SDL_RenderCopy(renderer, texture, nullptr, &destination);
        }
    }

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