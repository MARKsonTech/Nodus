#include <SDL.h>
#include <SDL_syswm.h>
#include <SDL_ttf.h>
#include <SDL_image.h>
#include <windows.h>
#include <algorithm>
#include <iostream>
#include <string>

#include "net_fetcher.h"
#include "url_utils.h"
#include "html_parser.h"
#include "FontManager.h"
#include "LayoutEngine.h"
#include "RenderEngine.h"

namespace {
size_t previous_utf8_index(const std::string& text, size_t index) {
    if (index == 0) return 0;
    --index;
    while (index > 0 && (static_cast<unsigned char>(text[index]) & 0xC0) == 0x80) --index;
    return index;
}

size_t next_utf8_index(const std::string& text, size_t index) {
    if (index >= text.size()) return text.size();
    ++index;
    while (index < text.size() && (static_cast<unsigned char>(text[index]) & 0xC0) == 0x80) ++index;
    return index;
}

int text_width(TTF_Font* font, const std::string& text, size_t end) {
    if (end == 0) return 0;
    int width = 0;
    int height = 0;
    TTF_SizeUTF8(font, text.substr(0, end).c_str(), &width, &height);
    return width;
}

size_t address_index_at_x(TTF_Font* font, const std::string& text, int x) {
    if (x <= 0) return 0;
    size_t index = 0;
    int previous_width = 0;
    while (index < text.size()) {
        const size_t next_index = next_utf8_index(text, index);
        const int next_width = text_width(font, text, next_index);
        if (x < (previous_width + next_width) / 2) return index;
        index = next_index;
        previous_width = next_width;
    }
    return text.size();
}

bool is_url_separator(unsigned char character) {
    return std::isspace(character) || character == '/' || character == '.' ||
           character == '-' || character == '_' || character == '?' ||
           character == '&' || character == '=' || character == '#';
}
}

int main(int argc, char* argv[]) {
    constexpr int address_bar_height = 64;

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        std::cerr << "SDL initialization failed: " << SDL_GetError() << "\n";
        return -1;
    }
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) {
        std::cerr << "Audio initialization failed; videos will play without sound: " << SDL_GetError() << "\n";
    }

    if (TTF_Init() < 0) {
        std::cerr << "TTF initialization failed: " << TTF_GetError() << "\n";
        SDL_Quit();
        return -1;
    }

    if ((IMG_Init(IMG_INIT_PNG | IMG_INIT_JPG | IMG_INIT_WEBP) & (IMG_INIT_PNG | IMG_INIT_JPG | IMG_INIT_WEBP)) == 0) {
        std::cerr << "SDL_image initialization failed: " << IMG_GetError() << "\n";
        TTF_Quit();
        SDL_Quit();
        return -1;
    }

    SDL_Window* window = SDL_CreateWindow(
        "Nodus Engine - Browser View",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        800, 600,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
    );

    if (!window) {
        TTF_Quit();
        SDL_Quit();
        return -1;
    }

    const HICON app_icon = static_cast<HICON>(LoadImageA(
        GetModuleHandleA(nullptr), MAKEINTRESOURCEA(101), IMAGE_ICON,
        0, 0, LR_DEFAULTSIZE | LR_SHARED
    ));
    SDL_SysWMinfo window_info;
    SDL_VERSION(&window_info.version);
    if (app_icon && SDL_GetWindowWMInfo(window, &window_info)) {
        SendMessageA(window_info.info.win.window, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(app_icon));
        SendMessageA(window_info.info.win.window, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(app_icon));
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

    // Target URL
    std::string target_url = "https://nodus-engine.netlify.app/demo";

    FontManager font_mgr("C:\\Windows\\Fonts\\arial.ttf");
    int scroll_y = 0;
    int viewport_width = 800;
    int viewport_height = 600;
    std::shared_ptr<Node> dom_root;
    LayoutNode layout_tree;

    auto load_page = [&]() {
        std::cout << "Fetching: " << target_url << "...\n";
        std::string effective_url;
        const std::string page = NetFetcher::fetch(target_url, true, &effective_url);
        if (!effective_url.empty()) target_url = effective_url;
        std::cout << "Fetched " << page.length() << " bytes.\n";
        RenderEngine::clear_video_players();
        RenderEngine::clear_image_cache();
        dom_root = HTMLParser::parse(page, target_url);
        RenderEngine::prepare_images(renderer, dom_root);
        RenderEngine::prepare_videos(dom_root);
        layout_tree = LayoutEngine::layout(dom_root, font_mgr, viewport_width - 40, address_bar_height + 10);
        scroll_y = 0;
    };

    load_page();

    bool running = true;
    bool needs_redraw = true;
    bool address_bar_focused = false;
    bool dragging_address_selection = false;
    std::string address_text = target_url;
    size_t address_cursor = address_text.size();
    size_t address_anchor = address_cursor;
    int address_scroll_x = 0;
    SDL_Event event;
    SDL_Cursor* arrow_cursor = SDL_GetDefaultCursor();
    SDL_Cursor* hand_cursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_HAND);
    SDL_Cursor* ibeam_cursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_IBEAM);
    SDL_SetCursor(arrow_cursor);
    const Uint32 frame_delay = 1000u / 60u;
    Uint32 last_redraw = 0;
    Uint32 last_caret_phase = SDL_GetTicks() / 500;
    bool video_click_consumed = false;

    auto erase_address_selection = [&]() {
        const size_t selection_start = std::min(address_cursor, address_anchor);
        const size_t selection_end = std::max(address_cursor, address_anchor);
        if (selection_start != selection_end) {
            address_text.erase(selection_start, selection_end - selection_start);
            address_cursor = selection_start;
            address_anchor = selection_start;
        }
    };

    auto insert_address_text = [&](const std::string& text) {
        erase_address_selection();
        address_text.insert(address_cursor, text);
        address_cursor += text.size();
        address_anchor = address_cursor;
    };

    auto focus_address_bar = [&](int mouse_x) {
        const bool was_focused = address_bar_focused;
        address_bar_focused = true;
        if (!was_focused) {
            address_text = target_url;
            address_cursor = address_text.size();
            address_anchor = address_cursor;
            address_scroll_x = 0;
            SDL_StartTextInput();
        }
        TTF_Font* font = font_mgr.get_font(16);
        if (font) address_cursor = address_index_at_x(font, address_text, mouse_x - 58 + address_scroll_x);
        address_anchor = address_cursor;
    };

    auto finish_address_edit = [&]() {
        address_bar_focused = false;
        dragging_address_selection = false;
        SDL_StopTextInput();
    };

    auto navigate_to_address = [&]() {
        const std::string normalized_url = URLUtils::normalize_url(address_text);
        if (normalized_url.empty()) return;
        target_url = normalized_url;
        address_text = target_url;
        address_cursor = address_text.size();
        address_anchor = address_cursor;
        finish_address_edit();
        load_page();
    };

    while (running) {
        const Uint32 frame_start = SDL_GetTicks();
        bool processed_event = false;
        while (SDL_PollEvent(&event)) {
            processed_event = true;
            if (event.type == SDL_QUIT) {
                running = false;
            } else if (event.type == SDL_MOUSEWHEEL) {
                scroll_y -= event.wheel.y * 40;
                needs_redraw = true;
            } else if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
                video_click_consumed = false;
                if (event.button.y < address_bar_height && !RenderEngine::is_video_fullscreen()) {
                    const int go_button_x = viewport_width - 106;
                    if (event.button.x >= go_button_x) {
                        navigate_to_address();
                    } else {
                        focus_address_bar(event.button.x);
                        dragging_address_selection = true;
                    }
                    needs_redraw = true;
                } else {
                    if (address_bar_focused) finish_address_edit();
                    video_click_consumed = RenderEngine::handle_video_click(
                        layout_tree, event.button.x, event.button.y, scroll_y, window);
                    if (video_click_consumed) needs_redraw = true;
                }
            } else if (event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_LEFT) {
                if (RenderEngine::finish_video_seek(layout_tree)) needs_redraw = true;
                dragging_address_selection = false;
                if (event.button.y >= address_bar_height) {
                    if (!video_click_consumed) {
                        const std::string href = RenderEngine::link_at(layout_tree, event.button.x, event.button.y, scroll_y);
                        if (!href.empty() && href[0] != '#') {
                            finish_address_edit();
                            target_url = href;
                            address_text = target_url;
                            address_cursor = address_text.size();
                            address_anchor = address_cursor;
                            load_page();
                        }
                    }
                    video_click_consumed = false;
                    needs_redraw = true;
                }
            } else if (event.type == SDL_MOUSEMOTION) {
                if (RenderEngine::drag_video_seek(layout_tree, event.motion.x, event.motion.y, scroll_y)) {
                    needs_redraw = true;
                }
                if (dragging_address_selection && address_bar_focused) {
                    TTF_Font* font = font_mgr.get_font(16);
                    if (font) address_cursor = address_index_at_x(font, address_text, event.motion.x - 58 + address_scroll_x);
                    needs_redraw = true;
                }
                if (event.motion.y < address_bar_height) {
                    SDL_SetCursor(address_bar_focused ? ibeam_cursor : arrow_cursor);
                } else {
                    const std::string href = RenderEngine::link_at(layout_tree, event.motion.x, event.motion.y, scroll_y);
                    SDL_SetCursor(href.empty() ? arrow_cursor : hand_cursor);
                }
                needs_redraw = true;
            } else if (event.type == SDL_TEXTINPUT && address_bar_focused) {
                insert_address_text(event.text.text);
                needs_redraw = true;
            } else if (event.type == SDL_KEYDOWN) {
                if (RenderEngine::is_video_fullscreen() &&
                    (event.key.keysym.sym == SDLK_ESCAPE ||
                     ((event.key.keysym.mod & KMOD_CTRL) && event.key.keysym.sym == SDLK_l))) {
                    RenderEngine::exit_video_fullscreen(window);
                    needs_redraw = true;
                } else if (address_bar_focused) {
                    const SDL_Keycode key = event.key.keysym.sym;
                    const SDL_Keymod modifiers = static_cast<SDL_Keymod>(event.key.keysym.mod);
                    const bool control = (modifiers & KMOD_CTRL) != 0;
                    const bool shift = (modifiers & KMOD_SHIFT) != 0;
                    if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
                        navigate_to_address();
                    } else if (event.key.keysym.sym == SDLK_ESCAPE) {
                        address_text = target_url;
                        address_cursor = address_text.size();
                        address_anchor = address_cursor;
                        finish_address_edit();
                    } else if (event.key.keysym.sym == SDLK_BACKSPACE) {
                        if (address_cursor != address_anchor) {
                            erase_address_selection();
                        } else if (address_cursor > 0) {
                            size_t erase_from = previous_utf8_index(address_text, address_cursor);
                            if (control) {
                                while (erase_from > 0 && is_url_separator(static_cast<unsigned char>(address_text[erase_from]))) {
                                    erase_from = previous_utf8_index(address_text, erase_from);
                                }
                                while (erase_from > 0 && !is_url_separator(static_cast<unsigned char>(address_text[erase_from]))) {
                                    erase_from = previous_utf8_index(address_text, erase_from);
                                }
                            }
                            address_text.erase(erase_from, address_cursor - erase_from);
                            address_cursor = erase_from;
                            address_anchor = address_cursor;
                        }
                    } else if (key == SDLK_DELETE) {
                        if (address_cursor != address_anchor) {
                            erase_address_selection();
                        } else if (address_cursor < address_text.size()) {
                            size_t erase_to = next_utf8_index(address_text, address_cursor);
                            if (control) {
                                while (erase_to < address_text.size() && is_url_separator(static_cast<unsigned char>(address_text[erase_to]))) {
                                    erase_to = next_utf8_index(address_text, erase_to);
                                }
                                while (erase_to < address_text.size() && !is_url_separator(static_cast<unsigned char>(address_text[erase_to]))) {
                                    erase_to = next_utf8_index(address_text, erase_to);
                                }
                            }
                            address_text.erase(address_cursor, erase_to - address_cursor);
                            address_anchor = address_cursor;
                        }
                    } else if (control && key == SDLK_a) {
                        address_anchor = 0;
                        address_cursor = address_text.size();
                    } else if (control && (key == SDLK_c || key == SDLK_x)) {
                        const size_t selection_start = std::min(address_cursor, address_anchor);
                        const size_t selection_end = std::max(address_cursor, address_anchor);
                        if (selection_start != selection_end) {
                            SDL_SetClipboardText(address_text.substr(selection_start, selection_end - selection_start).c_str());
                            if (key == SDLK_x) erase_address_selection();
                        }
                    } else if (control && key == SDLK_v) {
                        char* clipboard_text = SDL_GetClipboardText();
                        if (clipboard_text) {
                            insert_address_text(clipboard_text);
                            SDL_free(clipboard_text);
                        }
                    } else if (key == SDLK_LEFT || key == SDLK_RIGHT || key == SDLK_HOME || key == SDLK_END) {
                        size_t new_cursor = address_cursor;
                        if (key == SDLK_HOME) {
                            new_cursor = 0;
                        } else if (key == SDLK_END) {
                            new_cursor = address_text.size();
                        } else if (!shift && address_cursor != address_anchor) {
                            new_cursor = key == SDLK_LEFT
                                ? std::min(address_cursor, address_anchor)
                                : std::max(address_cursor, address_anchor);
                        } else if (key == SDLK_LEFT && new_cursor > 0) {
                            new_cursor = previous_utf8_index(address_text, new_cursor);
                            if (control) {
                                while (new_cursor > 0 && is_url_separator(static_cast<unsigned char>(address_text[new_cursor]))) {
                                    new_cursor = previous_utf8_index(address_text, new_cursor);
                                }
                                while (new_cursor > 0 && !is_url_separator(static_cast<unsigned char>(address_text[new_cursor]))) {
                                    new_cursor = previous_utf8_index(address_text, new_cursor);
                                }
                            }
                        } else if (key == SDLK_RIGHT && new_cursor < address_text.size()) {
                            new_cursor = next_utf8_index(address_text, new_cursor);
                            if (control) {
                                while (new_cursor < address_text.size() && is_url_separator(static_cast<unsigned char>(address_text[new_cursor]))) {
                                    new_cursor = next_utf8_index(address_text, new_cursor);
                                }
                                while (new_cursor < address_text.size() && !is_url_separator(static_cast<unsigned char>(address_text[new_cursor]))) {
                                    new_cursor = next_utf8_index(address_text, new_cursor);
                                }
                            }
                        }
                        address_cursor = new_cursor;
                        if (!shift) address_anchor = address_cursor;
                    }
                    needs_redraw = true;
                } else if ((event.key.keysym.mod & KMOD_CTRL) && event.key.keysym.sym == SDLK_l) {
                    address_bar_focused = true;
                    address_text = target_url;
                    address_anchor = 0;
                    address_cursor = address_text.size();
                    address_scroll_x = 0;
                    SDL_StartTextInput();
                    needs_redraw = true;
                } else if (event.key.keysym.sym == SDLK_DOWN) {
                    scroll_y += 40;
                } else if (event.key.keysym.sym == SDLK_UP) {
                    scroll_y -= 40;
                } else if (event.key.keysym.sym == SDLK_PAGEDOWN) {
                    scroll_y += viewport_height - 80;
                } else if (event.key.keysym.sym == SDLK_PAGEUP) {
                    scroll_y -= viewport_height - 80;
                } else if (event.key.keysym.sym == SDLK_HOME) {
                    scroll_y = 0;
                } else if (event.key.keysym.sym == SDLK_END) {
                    scroll_y = layout_tree.rect.y + layout_tree.rect.h - (viewport_height - address_bar_height);
                }

                const int max_scroll = layout_tree.rect.y + layout_tree.rect.h - (viewport_height - address_bar_height);
                if (scroll_y < 0) scroll_y = 0;
                if (scroll_y > max_scroll && max_scroll > 0) scroll_y = max_scroll;
                if (!address_bar_focused) needs_redraw = true;
            }
        }

        SDL_GetWindowSize(window, &viewport_width, &viewport_height);
        static int laid_out_width = viewport_width;
        if (viewport_width != laid_out_width) {
            layout_tree = LayoutEngine::layout(dom_root, font_mgr, viewport_width - 40, address_bar_height + 10);
            laid_out_width = viewport_width;
            needs_redraw = true;
        }
        const int max_scroll = layout_tree.rect.y + layout_tree.rect.h - (viewport_height - address_bar_height);
        if (scroll_y < 0) scroll_y = 0;
        if (max_scroll <= 0) scroll_y = 0;
        else if (scroll_y > max_scroll) scroll_y = max_scroll;

        const Uint32 now = SDL_GetTicks();
        const Uint32 caret_phase = now / 500;
        if (address_bar_focused && caret_phase != last_caret_phase) needs_redraw = true;
        last_caret_phase = caret_phase;
        if (RenderEngine::video_state_changed()) needs_redraw = true;
        const bool idle = !needs_redraw && !processed_event;
        if (idle && !RenderEngine::any_video_playing()) {
            SDL_Delay(8);
            continue;
        }
        if (RenderEngine::any_video_playing()) needs_redraw = true;

        if (needs_redraw && now - last_redraw >= frame_delay) {
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
            SDL_RenderClear(renderer);

            SDL_Rect address_background = { 0, 0, viewport_width, address_bar_height };
            SDL_SetRenderDrawColor(renderer, 244, 246, 250, 255);
            SDL_RenderFillRect(renderer, &address_background);
            SDL_Rect address_input = { 16, 11, viewport_width - 32, 42 };
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
            SDL_RenderFillRect(renderer, &address_input);
            SDL_SetRenderDrawColor(renderer, address_bar_focused ? 49 : 218,
                                   address_bar_focused ? 104 : 224,
                                   address_bar_focused ? 210 : 232, 255);
            SDL_RenderDrawRect(renderer, &address_input);

            TTF_Font* address_font = font_mgr.get_font(16);
            if (address_font) {
                const std::string& shown_address = address_bar_focused ? address_text : target_url;
                const int text_left = address_input.x + 42;
                const int text_top = address_input.y + 12;
                const int go_button_width = 62;
                const int text_right = address_input.x + address_input.w - go_button_width - 24;
                const int text_view_width = std::max(1, text_right - text_left);
                const size_t shown_cursor = address_bar_focused ? address_cursor : shown_address.size();
                address_scroll_x = std::max(0, text_width(address_font, shown_address, shown_cursor) - text_view_width + 8);
                const int cursor_width = text_width(address_font, shown_address, shown_cursor);
                if (cursor_width - address_scroll_x < 0) address_scroll_x = cursor_width;

                SDL_Rect lock_area = { address_input.x + 14, address_input.y + 14, 18, 16 };
                SDL_SetRenderDrawColor(renderer, 48, 126, 95, 255);
                SDL_Rect lock_body = { lock_area.x + 2, lock_area.y + 6, 14, 10 };
                SDL_RenderFillRect(renderer, &lock_body);
                SDL_Rect lock_top = { lock_area.x + 5, lock_area.y + 1, 8, 8 };
                SDL_RenderDrawRect(renderer, &lock_top);

                SDL_Rect go_button = { address_input.x + address_input.w - go_button_width - 8,
                                       address_input.y + 6, go_button_width, address_input.h - 12 };
                SDL_SetRenderDrawColor(renderer, 45, 104, 210, 255);
                SDL_RenderFillRect(renderer, &go_button);
                SDL_Color button_text_color = { 255, 255, 255, 255 };
                SDL_Surface* button_surface = TTF_RenderUTF8_Blended(address_font, "Go", button_text_color);
                if (button_surface) {
                    SDL_Texture* button_texture = SDL_CreateTextureFromSurface(renderer, button_surface);
                    if (button_texture) {
                        SDL_Rect button_text_rect = {
                            go_button.x + (go_button.w - button_surface->w) / 2,
                            go_button.y + (go_button.h - button_surface->h) / 2,
                            button_surface->w, button_surface->h
                        };
                        SDL_RenderCopy(renderer, button_texture, nullptr, &button_text_rect);
                        SDL_DestroyTexture(button_texture);
                    }
                    SDL_FreeSurface(button_surface);
                }

                SDL_Rect text_clip = { text_left, address_input.y + 5, text_view_width, address_input.h - 10 };
                SDL_RenderSetClipRect(renderer, &text_clip);
                const size_t selection_start = std::min(address_cursor, address_anchor);
                const size_t selection_end = std::max(address_cursor, address_anchor);
                const int selection_x = text_left + text_width(address_font, shown_address, selection_start) - address_scroll_x;
                const int selection_end_x = text_left + text_width(address_font, shown_address, selection_end) - address_scroll_x;
                if (address_bar_focused && selection_start != selection_end) {
                    SDL_Rect selection_rect = { selection_x, text_top, selection_end_x - selection_x, 20 };
                    SDL_SetRenderDrawColor(renderer, 55, 125, 232, 255);
                    SDL_RenderFillRect(renderer, &selection_rect);
                }

                SDL_Color text_color = { 38, 45, 58, 255 };
                SDL_Surface* address_surface = TTF_RenderUTF8_Blended(address_font, shown_address.c_str(), text_color);
                if (address_surface) {
                    SDL_Texture* address_texture = SDL_CreateTextureFromSurface(renderer, address_surface);
                    if (address_texture) {
                        SDL_Rect address_text_rect = { text_left - address_scroll_x, text_top,
                                                       address_surface->w, address_surface->h };
                        SDL_RenderCopy(renderer, address_texture, nullptr, &address_text_rect);
                        SDL_DestroyTexture(address_texture);
                    }
                    SDL_FreeSurface(address_surface);
                }

                if (address_bar_focused && selection_start != selection_end) {
                    const std::string selected_text = shown_address.substr(selection_start, selection_end - selection_start);
                    SDL_Color selected_color = { 255, 255, 255, 255 };
                    SDL_Surface* selected_surface = TTF_RenderUTF8_Blended(address_font, selected_text.c_str(), selected_color);
                    if (selected_surface) {
                        SDL_Texture* selected_texture = SDL_CreateTextureFromSurface(renderer, selected_surface);
                        if (selected_texture) {
                            SDL_Rect selected_rect = { selection_x, text_top, selected_surface->w, selected_surface->h };
                            SDL_RenderCopy(renderer, selected_texture, nullptr, &selected_rect);
                            SDL_DestroyTexture(selected_texture);
                        }
                        SDL_FreeSurface(selected_surface);
                    }
                }

                if (address_bar_focused && selection_start == selection_end && caret_phase % 2 == 0) {
                    const int caret_x = text_left + cursor_width - address_scroll_x;
                    SDL_SetRenderDrawColor(renderer, 40, 92, 190, 255);
                    SDL_RenderDrawLine(renderer, caret_x, text_top - 2, caret_x, text_top + 18);
                }
                SDL_RenderSetClipRect(renderer, nullptr);
            }

            if (RenderEngine::is_video_fullscreen()) {
                SDL_RenderSetClipRect(renderer, nullptr);
                RenderEngine::render(renderer, layout_tree, font_mgr, window, scroll_y);
            } else {
                SDL_Rect viewport = { 0, address_bar_height, viewport_width, viewport_height - address_bar_height };
                SDL_RenderSetClipRect(renderer, &viewport);
                RenderEngine::render(renderer, layout_tree, font_mgr, window, scroll_y);
            }
            SDL_RenderSetClipRect(renderer, nullptr);

            SDL_RenderPresent(renderer);
            needs_redraw = false;
            last_redraw = now;
        }

        const Uint32 elapsed = SDL_GetTicks() - frame_start;
        if (elapsed < frame_delay) {
            SDL_Delay(frame_delay - elapsed);
        }
    }

    RenderEngine::clear_video_players();
    RenderEngine::clear_image_cache();
    if (hand_cursor) SDL_FreeCursor(hand_cursor);
    if (ibeam_cursor) SDL_FreeCursor(ibeam_cursor);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    IMG_Quit();
    TTF_Quit();
    SDL_Quit();

    return 0;
}