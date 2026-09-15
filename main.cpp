#include <SDL.h>
#include <SDL_ttf.h>
#include <SDL_image.h>
#include <iostream>
#include <string>

#include "net_fetcher.h"
#include "html_parser.h"
#include "FontManager.h"
#include "LayoutEngine.h"
#include "RenderEngine.h"

int main(int argc, char* argv[]) {
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        std::cerr << "SDL initialization failed: " << SDL_GetError() << "\n";
        return -1;
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

    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);

    // Target URL
    std::string target_url = "https://nodus-engine.netlify.app/simple";

    FontManager font_mgr("C:\\Windows\\Fonts\\arial.ttf");
    int scroll_y = 0;
    int viewport_width = 800;
    int viewport_height = 600;
    std::shared_ptr<Node> dom_root;
    LayoutNode layout_tree;

    auto load_page = [&]() {
        std::cout << "Fetching: " << target_url << "...\n";
        const std::string page = NetFetcher::fetch(target_url);
        std::cout << "Fetched " << page.length() << " bytes.\n";
        RenderEngine::clear_image_cache();
        dom_root = HTMLParser::parse(page, target_url);
        RenderEngine::prepare_images(renderer, dom_root);
        layout_tree = LayoutEngine::layout(dom_root, font_mgr, viewport_width - 40);
        scroll_y = 0;
    };

    load_page();

    bool running = true;
    SDL_Event event;
    SDL_Cursor* arrow_cursor = SDL_GetDefaultCursor();
    SDL_Cursor* hand_cursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_HAND);
    SDL_SetCursor(arrow_cursor);

    while (running) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
            } else if (event.type == SDL_MOUSEWHEEL) {
                scroll_y -= event.wheel.y * 40;
            } else if (event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_LEFT) {
                const std::string href = RenderEngine::link_at(layout_tree, event.button.x, event.button.y, scroll_y);
                if (!href.empty() && href[0] != '#') {
                    target_url = href;
                    load_page();
                }
            } else if (event.type == SDL_MOUSEMOTION) {
                const std::string href = RenderEngine::link_at(layout_tree, event.motion.x, event.motion.y, scroll_y);
                SDL_SetCursor(href.empty() ? arrow_cursor : hand_cursor);
            } else if (event.type == SDL_KEYDOWN) {
                if (event.key.keysym.sym == SDLK_DOWN) {
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
                    scroll_y = layout_tree.rect.y + layout_tree.rect.h - viewport_height;
                }

                const int max_scroll = layout_tree.rect.y + layout_tree.rect.h - viewport_height;
                if (scroll_y < 0) scroll_y = 0;
                if (scroll_y > max_scroll && max_scroll > 0) scroll_y = max_scroll;
            }
        }

        SDL_GetWindowSize(window, &viewport_width, &viewport_height);
        static int laid_out_width = viewport_width;
        if (viewport_width != laid_out_width) {
            layout_tree = LayoutEngine::layout(dom_root, font_mgr, viewport_width - 40);
            laid_out_width = viewport_width;
        }
        const int max_scroll = layout_tree.rect.y + layout_tree.rect.h - viewport_height;
        if (scroll_y < 0) scroll_y = 0;
        if (max_scroll <= 0) scroll_y = 0;
        else if (scroll_y > max_scroll) scroll_y = max_scroll;

        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        SDL_RenderClear(renderer);

        SDL_Rect viewport = { 0, 0, viewport_width, viewport_height };
        SDL_RenderSetClipRect(renderer, &viewport);
        RenderEngine::render(renderer, layout_tree, font_mgr, scroll_y);
        SDL_RenderSetClipRect(renderer, nullptr);

        SDL_RenderPresent(renderer);
    }

    RenderEngine::clear_image_cache();
    if (hand_cursor) SDL_FreeCursor(hand_cursor);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    IMG_Quit();
    TTF_Quit();
    SDL_Quit();

    return 0;
}