#pragma once
#include "dom.h"
#include "FontManager.h"
#include <vector>

struct LayoutNode {
    SDL_Rect rect{0, 0, 0, 0};
    std::shared_ptr<Node> node;
    std::vector<LayoutNode> children;
};

class LayoutEngine {
public:
    static LayoutNode layout(const std::shared_ptr<Node>& node, FontManager& font_mgr, int current_y = 10) {
        LayoutNode layout_node;
        layout_node.node = node;
        layout_node.rect.x = 20;
        layout_node.rect.y = current_y;

        if (node->style.display == "none") return layout_node;

        if (node->tag_name == "img") {
            layout_node.rect.w = node->image_width > 0 ? node->image_width : 0;
            layout_node.rect.h = node->image_height > 0 ? node->image_height : 0;
            const auto parent = node->parent.lock();
            if (parent && parent->style.text_align == "center") layout_node.rect.x = 20 + (760 - layout_node.rect.w) / 2;
            else if (parent && (parent->style.text_align == "right" || parent->style.text_align == "end")) layout_node.rect.x = 780 - layout_node.rect.w;
            return layout_node;
        }

        int y_cursor = current_y + node->style.padding_top;

        for (const auto& child : node->children) {
            if (child->type == NodeType::Text) {
                TTF_Font* font = font_mgr.get_font(node->style.font_size > 0 ? node->style.font_size : 16,
                                                   node->style.font_weight == "bold" || node->style.font_weight == "700",
                                                   node->style.font_style == "italic");
                if (font) {
                    int w = 0, h = 0;
                    TTF_SizeUTF8(font, child->text_data.c_str(), &w, &h);

                    LayoutNode text_layout;
                    text_layout.node = child;
                    int text_x = 20;
                    if (node->style.text_align == "center") text_x = 20 + (760 - w) / 2;
                    else if (node->style.text_align == "right" || node->style.text_align == "end") text_x = 780 - w;
                    text_layout.rect = { text_x, y_cursor, w, h };
                    layout_node.children.push_back(text_layout);

                    y_cursor += h + 5;
                }
            } else {
                LayoutNode child_layout = layout(child, font_mgr, y_cursor);
                y_cursor = child_layout.rect.y + child_layout.rect.h + 10;
                layout_node.children.push_back(child_layout);
            }
        }

        layout_node.rect.w = 760;
        layout_node.rect.h = y_cursor - current_y + node->style.padding_bottom;
        return layout_node;
    }
};