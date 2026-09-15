#pragma once
#include "dom.h"
#include "FontManager.h"
#include <algorithm>
#include <vector>

struct LayoutNode {
    SDL_Rect rect{0, 0, 0, 0};
    std::shared_ptr<Node> node;
    std::vector<LayoutNode> children;
};

class LayoutEngine {
public:
    static LayoutNode layout(const std::shared_ptr<Node>& node, FontManager& font_mgr, int available_width = 800, int current_y = 10, int origin_x = 20) {
        LayoutNode layout_node;
        layout_node.node = node;
        layout_node.rect.x = origin_x + node->style.margin_left;
        layout_node.rect.y = current_y;

        if (node->style.display == "none") return layout_node;

        if (node->tag_name == "img") {
            const int content_width = available_width - node->style.margin_left - node->style.margin_right;
            const int intrinsic_width = node->intrinsic_width > 0 ? node->intrinsic_width : node->image_width;
            const int intrinsic_height = node->intrinsic_height > 0 ? node->intrinsic_height : node->image_height;
            const bool css_width = node->style.width_percent >= 0 || node->style.width >= 0;
            const bool css_height = node->style.height_percent >= 0 || node->style.height >= 0;

            layout_node.rect.w = node->image_width > 0 ? node->image_width : intrinsic_width;
            layout_node.rect.h = node->image_height > 0 ? node->image_height : intrinsic_height;
            if (node->style.width_percent >= 0) layout_node.rect.w = static_cast<int>(content_width * node->style.width_percent);
            else if (node->style.width >= 0) layout_node.rect.w = node->style.width;
            if (node->style.max_width_percent >= 0) layout_node.rect.w = minimum(layout_node.rect.w, static_cast<int>(content_width * node->style.max_width_percent));
            else if (node->style.max_width >= 0) layout_node.rect.w = minimum(layout_node.rect.w, node->style.max_width);
            if (node->style.height_percent >= 0) {
                layout_node.rect.h = static_cast<int>(content_width * node->style.height_percent);
            } else if (node->style.height >= 0) {
                layout_node.rect.h = node->style.height;
            } else if ((css_width || node->style.max_width >= 0 || node->style.max_width_percent >= 0) &&
                       intrinsic_width > 0 && intrinsic_height > 0) {
                layout_node.rect.h = intrinsic_height * layout_node.rect.w / intrinsic_width;
            }
            if (layout_node.rect.w < 0) layout_node.rect.w = 0;
            if (layout_node.rect.h < 0) layout_node.rect.h = 0;
            const auto parent = node->parent.lock();
            if (parent && parent->style.text_align == "center") layout_node.rect.x = origin_x + (content_width - layout_node.rect.w) / 2;
            else if (parent && (parent->style.text_align == "right" || parent->style.text_align == "end")) layout_node.rect.x = origin_x + content_width - layout_node.rect.w;
            return layout_node;
        }

        int box_width = available_width - node->style.margin_left - node->style.margin_right;
        if (node->style.width_percent >= 0) box_width = static_cast<int>(available_width * node->style.width_percent);
        else if (node->style.width >= 0) box_width = node->style.width;
        if (node->style.max_width_percent >= 0) box_width = minimum(box_width, static_cast<int>(available_width * node->style.max_width_percent));
        else if (node->style.max_width >= 0) box_width = minimum(box_width, node->style.max_width);
        if (box_width < 0) box_width = 0;
        const int content_x = layout_node.rect.x + node->style.padding_left;
        const int content_width = maximum(0, box_width - node->style.padding_left - node->style.padding_right);
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
                    int text_x = content_x;
                    if (node->style.text_align == "center") text_x = content_x + (content_width - w) / 2;
                    else if (node->style.text_align == "right" || node->style.text_align == "end") text_x = content_x + content_width - w;
                    text_layout.rect = { text_x, y_cursor, w, h };
                    layout_node.children.push_back(text_layout);

                    y_cursor += h + 5;
                }
            } else {
                LayoutNode child_layout = layout(child, font_mgr, content_width, y_cursor, content_x);
                y_cursor = child_layout.rect.y + child_layout.rect.h + 10;
                layout_node.children.push_back(child_layout);
            }
        }

        layout_node.rect.w = box_width;
        layout_node.rect.h = y_cursor - current_y + node->style.padding_bottom;
        return layout_node;
    }

private:
    static int minimum(int left, int right) {
        return left < right ? left : right;
    }

    static int maximum(int left, int right) {
        return left > right ? left : right;
    }
};