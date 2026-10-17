#pragma once
#include "dom.h"
#include "FontManager.h"
#include <algorithm>
#include <cctype>
#include <map>
#include <string>
#include <vector>

struct LayoutNode {
    SDL_Rect rect{0, 0, 0, 0};
    std::shared_ptr<Node> node;
    std::string text_fragment;
    bool has_text_fragment = false;
    std::vector<LayoutNode> children;
};

class LayoutEngine {
public:
    static LayoutNode layout(const std::shared_ptr<Node>& node, FontManager& font_mgr, int available_width = 800, int current_y = 10, int origin_x = 20) {
        LayoutNode layout_node;
        layout_node.node = node;
        layout_node.rect.x = origin_x + node->style.margin_left;
        layout_node.rect.y = current_y + node->style.margin_top;

        if (node->style.display == "none") return layout_node;

        if (node->tag_name == "img") {
            const int content_width = maximum(0, available_width - node->style.margin_left - node->style.margin_right);
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

        int box_width = maximum(0, available_width - node->style.margin_left - node->style.margin_right);
        if (node->style.width_percent >= 0) box_width = static_cast<int>(available_width * node->style.width_percent);
        else if (node->style.width >= 0) box_width = node->style.width;
        if (node->style.max_width_percent >= 0) box_width = minimum(box_width, static_cast<int>(available_width * node->style.max_width_percent));
        else if (node->style.max_width >= 0) box_width = minimum(box_width, node->style.max_width);
        const bool has_explicit_width = node->style.width >= 0 || node->style.width_percent >= 0;
        if ((node->style.display == "inline-block" || node->style.display == "inline-flex") && !has_explicit_width) {
            box_width = minimum(box_width, intrinsic_width(node, font_mgr) + node->style.padding_left + node->style.padding_right);
        }
        if (box_width < 0) box_width = 0;
        const int content_x = layout_node.rect.x + node->style.padding_left;
        const int content_width = maximum(0, box_width - node->style.padding_left - node->style.padding_right);
        InlineCursor cursor{ content_x, layout_node.rect.y + node->style.padding_top, content_x, content_x + content_width };
        size_t inline_start = 0;

        for (const auto& child : node->children) {
            if (is_inline(child)) {
                append_inline(child, font_mgr, cursor, layout_node.children);
            } else {
                align_inline_range(layout_node.children, inline_start, content_x, content_width,
                                   node->style.text_align);
                if (cursor.line_height > 0) cursor.y += cursor.line_height + 5;
                cursor.x = cursor.start_x;
                cursor.line_height = 0;
                cursor.pending_space = false;
                LayoutNode child_layout = layout(child, font_mgr, content_width, cursor.y, content_x);
                cursor.y = child_layout.rect.y + child_layout.rect.h + 10;
                layout_node.children.push_back(child_layout);
                inline_start = layout_node.children.size();
            }
        }

        align_inline_range(layout_node.children, inline_start, content_x, content_width, node->style.text_align);
        layout_node.rect.w = box_width;
        layout_node.rect.h = cursor.y + cursor.line_height - layout_node.rect.y + node->style.padding_bottom + node->style.margin_bottom;
        return layout_node;
    }

private:
    struct InlineCursor {
        int x;
        int y;
        int start_x;
        int right;
        int line_height = 0;
        bool pending_space = false;
    };

    static bool is_inline(const std::shared_ptr<Node>& node) {
        if (node->type == NodeType::Text) return true;
        if (node->tag_name == "img" || node->tag_name == "br") return true;
        const auto display = node->style.properties.find("display");
        if (display != node->style.properties.end()) {
            return node->style.display == "inline" || node->style.display == "inline-block" ||
                   node->style.display == "inline-flex";
        }
        return node->tag_name == "a" || node->tag_name == "abbr" || node->tag_name == "b" ||
               node->tag_name == "cite" || node->tag_name == "code" || node->tag_name == "em" ||
               node->tag_name == "i" || node->tag_name == "label" || node->tag_name == "mark" ||
               node->tag_name == "q" || node->tag_name == "s" || node->tag_name == "small" ||
               node->tag_name == "span" || node->tag_name == "strong" || node->tag_name == "sub" ||
               node->tag_name == "sup" || node->tag_name == "u";
    }

    static TTF_Font* font_for(const std::shared_ptr<Node>& node, FontManager& font_mgr) {
        return font_mgr.get_font(node->style.font_size > 0 ? node->style.font_size : 16,
                                 node->style.font_weight == "bold" || node->style.font_weight == "700",
                                 node->style.font_style == "italic",
                                 node->type == NodeType::Text ? node->text_data : std::string{});
    }

    static void next_line(InlineCursor& cursor, int fallback_height) {
        cursor.y += cursor.line_height > 0 ? cursor.line_height : fallback_height;
        cursor.x = cursor.start_x;
        cursor.line_height = 0;
        cursor.pending_space = false;
    }

    static void append_text(const std::shared_ptr<Node>& node, FontManager& font_mgr,
                            InlineCursor& cursor, std::vector<LayoutNode>& output) {
        TTF_Font* font = font_for(node, font_mgr);
        if (!font) return;

        auto append_word = [&](const std::string& word) {
            if (word.empty()) return;
            std::string fragment = cursor.pending_space && cursor.x > cursor.start_x ? " " + word : word;
            int width = 0, height = 0;
            TTF_SizeUTF8(font, fragment.c_str(), &width, &height);
            if (cursor.x > cursor.start_x && cursor.x + width > cursor.right) {
                next_line(cursor, height);
                fragment = word;
                TTF_SizeUTF8(font, word.c_str(), &width, &height);
            }
            LayoutNode text_layout;
            text_layout.node = node;
            text_layout.text_fragment = fragment;
            text_layout.has_text_fragment = true;
            text_layout.rect = { cursor.x, cursor.y, width, height };
            output.push_back(text_layout);
            cursor.x += width;
            cursor.line_height = maximum(cursor.line_height, height);
            cursor.pending_space = false;
        };

        std::string word;
        for (const unsigned char character : node->text_data) {
            if (std::isspace(character)) {
                append_word(word);
                word.clear();
                cursor.pending_space = true;
            } else {
                word += static_cast<char>(character);
            }
        }
        append_word(word);
    }

    static int intrinsic_width(const std::shared_ptr<Node>& node, FontManager& font_mgr) {
        if (node->type == NodeType::Text) {
            TTF_Font* font = font_for(node, font_mgr);
            int width = 0, height = 0;
            if (font) TTF_SizeUTF8(font, node->text_data.c_str(), &width, &height);
            return width;
        }
        if (node->tag_name == "img") return node->image_width > 0 ? node->image_width : node->intrinsic_width;
        int width = 0;
        for (const auto& child : node->children) width += intrinsic_width(child, font_mgr);
        return width;
    }

    static void append_inline(const std::shared_ptr<Node>& node, FontManager& font_mgr,
                              InlineCursor& cursor, std::vector<LayoutNode>& output) {
        if (node->type == NodeType::Text) {
            append_text(node, font_mgr, cursor, output);
            return;
        }
        if (node->tag_name == "br") {
            TTF_Font* font = font_for(node, font_mgr);
            next_line(cursor, font ? TTF_FontLineSkip(font) : 16);
            return;
        }
        if (node->tag_name == "img" || node->style.display == "inline-block" || node->style.display == "inline-flex") {
            LayoutNode item = layout(node, font_mgr, cursor.right - cursor.start_x, cursor.y, cursor.x);
            if (cursor.x > cursor.start_x && cursor.x + item.rect.w > cursor.right) next_line(cursor, item.rect.h);
            item.rect.x = cursor.x;
            item.rect.y = cursor.y;
            cursor.x += item.rect.w;
            cursor.line_height = maximum(cursor.line_height, item.rect.h);
            cursor.pending_space = false;
            output.push_back(item);
            return;
        }

        LayoutNode inline_layout;
        inline_layout.node = node;
        const int start_x = cursor.x;
        const int start_y = cursor.y;
        for (const auto& child : node->children) {
            if (child->style.display == "none") continue;
            if (is_inline(child)) {
                append_inline(child, font_mgr, cursor, inline_layout.children);
            } else {
                if (cursor.line_height > 0) next_line(cursor, cursor.line_height);
                LayoutNode nested = layout(child, font_mgr, cursor.right - cursor.start_x,
                                           cursor.y, cursor.start_x);
                inline_layout.children.push_back(nested);
                cursor.y = nested.rect.y + nested.rect.h + 5;
                cursor.x = cursor.start_x;
                cursor.line_height = 0;
                cursor.pending_space = false;
            }
        }
        inline_layout.rect = { start_x, start_y, cursor.x - start_x,
                               cursor.y + cursor.line_height - start_y };
        output.push_back(inline_layout);
    }

    static void collect_line_bounds(const LayoutNode& node, std::map<int, std::pair<int, int>>& bounds) {
        if (node.node && (node.node->type == NodeType::Text || node.node->tag_name == "img")) {
            auto found = bounds.find(node.rect.y);
            if (found == bounds.end()) bounds[node.rect.y] = { node.rect.x, node.rect.x + node.rect.w };
            else {
                found->second.first = minimum(found->second.first, node.rect.x);
                found->second.second = maximum(found->second.second, node.rect.x + node.rect.w);
            }
        }
        for (const auto& child : node.children) collect_line_bounds(child, bounds);
    }

    static void shift_inline_line(LayoutNode& node, int y, int offset) {
        if (node.node && (node.node->type == NodeType::Text || node.node->tag_name == "img") && node.rect.y == y) {
            node.rect.x += offset;
        } else if (node.node && node.rect.y == y) {
            node.rect.x += offset;
        }
        for (auto& child : node.children) shift_inline_line(child, y, offset);
    }

    static void align_inline_range(std::vector<LayoutNode>& children, size_t start,
                                   int content_x, int content_width, const std::string& alignment) {
        if (alignment != "center" && alignment != "right" && alignment != "end") return;
        std::map<int, std::pair<int, int>> bounds;
        for (size_t index = start; index < children.size(); ++index) collect_line_bounds(children[index], bounds);
        for (const auto& line : bounds) {
            const int line_width = line.second.second - line.second.first;
            const int offset = alignment == "center" ? (content_width - line_width) / 2 : content_width - line_width;
            const int shift = content_x + offset - line.second.first;
            for (size_t index = start; index < children.size(); ++index) {
                shift_inline_line(children[index], line.first, shift);
            }
        }
    }

    static int minimum(int left, int right) {
        return left < right ? left : right;
    }

    static int maximum(int left, int right) {
        return left > right ? left : right;
    }
};