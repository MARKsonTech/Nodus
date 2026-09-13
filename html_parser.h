#pragma once
#include "dom.h"
#include "net_fetcher.h"
#include <algorithm>
#include <cctype>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

class HTMLParser {
public:
    static std::shared_ptr<Node> parse(const std::string& html, const std::string& base_url = {}) {
        auto root = std::make_shared<Node>(NodeType::Element);
        root->tag_name = "body";
        std::vector<std::shared_ptr<Node>> stack{ root };
        std::vector<CSSRule> rules;
        size_t position = 0;

        while (position < html.length()) {
            if (html[position] != '<') {
                const size_t next_tag = html.find('<', position);
                std::string text = trim(html.substr(position, next_tag == std::string::npos ? std::string::npos : next_tag - position));
                position = next_tag == std::string::npos ? html.length() : next_tag;
                if (!text.empty()) {
                    auto text_node = std::make_shared<Node>(NodeType::Text);
                    text_node->text_data = text;
                    text_node->parent = stack.back();
                    stack.back()->children.push_back(text_node);
                }
                continue;
            }

            const size_t close_pos = html.find('>', position);
            if (close_pos == std::string::npos) break;
            const std::string tag_content = html.substr(position + 1, close_pos - position - 1);
            position = close_pos + 1;
            if (tag_content.empty() || tag_content[0] == '!') continue;

            if (tag_content[0] == '/') {
                const std::string closing_name = lower(trim(tag_content.substr(1)));
                while (stack.size() > 1) {
                    const std::string current_name = stack.back()->tag_name;
                    stack.pop_back();
                    if (current_name == closing_name) break;
                }
                continue;
            }

            const size_t name_end = tag_content.find_first_of(" \t\r\n/");
            const std::string tag_name = lower(tag_content.substr(0, name_end));

            if (tag_name == "style") {
                const size_t closing = find_closing_tag(html, position, tag_name);
                const size_t content_end = closing == std::string::npos ? html.length() : closing;
                const std::vector<CSSRule> style_rules = CSSParser::parse_stylesheet(html.substr(position, content_end - position));
                rules.insert(rules.end(), style_rules.begin(), style_rules.end());
                position = skip_closing_tag(html, closing);
                continue;
            }
            if (tag_name == "script" || tag_name == "title") {
                position = skip_closing_tag(html, find_closing_tag(html, position, tag_name));
                continue;
            }
            if (tag_name == "link") {
                const std::string rel = lower(get_attribute(tag_content, "rel"));
                if (rel.find("stylesheet") != std::string::npos) {
                    const std::string href = resolve_url(base_url, get_attribute(tag_content, "href"));
                    const std::vector<CSSRule> external_rules = CSSParser::parse_stylesheet(NetFetcher::fetch(href));
                    rules.insert(rules.end(), external_rules.begin(), external_rules.end());
                }
                continue;
            }

            auto element = std::make_shared<Node>(NodeType::Element);
            element->tag_name = tag_name;
            element->element_id = get_attribute(tag_content, "id");
            element->class_name = get_attribute(tag_content, "class");
            element->inline_style = get_attribute(tag_content, "style");
            if (tag_name == "img") {
                element->image_src = resolve_url(base_url, get_attribute(tag_content, "src"));
                element->image_width = parse_attribute_length(get_attribute(tag_content, "width"));
                element->image_height = parse_attribute_length(get_attribute(tag_content, "height"));
            }

            element->parent = stack.back();
            stack.back()->children.push_back(element);
            if (!is_void_element(tag_name)) stack.push_back(element);
        }

        apply_styles(root, rules);
        return root;
    }

private:
    static std::string trim(const std::string& value) {
        const size_t first = value.find_first_not_of(" \t\n\r\f");
        if (first == std::string::npos) return {};
        const size_t last = value.find_last_not_of(" \t\n\r\f");
        return value.substr(first, last - first + 1);
    }

    static std::string lower(std::string value) {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
        return value;
    }

    static std::string get_attribute(const std::string& tag_content, const std::string& name) {
        const std::string normalized = lower(tag_content);
        size_t position = normalized.find(lower(name) + "=");
        if (position == std::string::npos) return {};
        position += name.length() + 1;
        while (position < tag_content.length() && std::isspace(static_cast<unsigned char>(tag_content[position]))) ++position;
        if (position >= tag_content.length()) return {};

        const char quote = tag_content[position];
        if (quote == '\'' || quote == '"') {
            const size_t end = tag_content.find(quote, position + 1);
            return end == std::string::npos ? std::string{} : tag_content.substr(position + 1, end - position - 1);
        }
        const size_t end = tag_content.find_first_of(" \t\r\n", position);
        return tag_content.substr(position, end == std::string::npos ? std::string::npos : end - position);
    }

    static int parse_attribute_length(const std::string& value) {
        if (value.empty()) return -1;
        try {
            size_t consumed = 0;
            const int result = std::stoi(value, &consumed);
            return consumed == 0 ? -1 : result;
        } catch (...) {
            return -1;
        }
    }

    static bool is_void_element(const std::string& tag_name) {
        return tag_name == "area" || tag_name == "base" || tag_name == "br" || tag_name == "col" ||
               tag_name == "embed" || tag_name == "hr" || tag_name == "img" || tag_name == "input" ||
               tag_name == "link" || tag_name == "meta" || tag_name == "param" || tag_name == "source" ||
               tag_name == "track" || tag_name == "wbr";
    }

    static size_t find_closing_tag(const std::string& html, size_t start, const std::string& tag_name) {
        return lower(html).find("</" + tag_name, start);
    }

    static size_t skip_closing_tag(const std::string& html, size_t closing_position) {
        if (closing_position == std::string::npos) return html.length();
        const size_t end = html.find('>', closing_position);
        return end == std::string::npos ? html.length() : end + 1;
    }

    static std::string resolve_url(const std::string& base_url, const std::string& value) {
        if (value.empty() || value.rfind("data:", 0) == 0) return value;
        if (value.rfind("http://", 0) == 0 || value.rfind("https://", 0) == 0) return value;
        if (base_url.empty()) return value;
        const size_t scheme_end = base_url.find("://");
        const size_t host_end = base_url.find('/', scheme_end == std::string::npos ? 0 : scheme_end + 3);
        const std::string origin = base_url.substr(0, host_end == std::string::npos ? base_url.length() : host_end);
        if (value[0] == '/') return origin + value;
        const size_t directory_end = base_url.rfind('/');
        return (directory_end == std::string::npos ? base_url : base_url.substr(0, directory_end + 1)) + value;
    }

    static bool has_class(const std::string& classes, const std::string& wanted) {
        std::stringstream stream(classes);
        std::string class_name;
        while (stream >> class_name) if (class_name == wanted) return true;
        return false;
    }

    static bool matches_simple_selector(const std::shared_ptr<Node>& node, std::string selector) {
        selector = trim(selector);
        const size_t pseudo = selector.find(':');
        if (pseudo != std::string::npos) selector.erase(pseudo);
        const size_t attribute = selector.find('[');
        if (attribute != std::string::npos) selector.erase(attribute);
        if (selector.empty()) return false;

        const size_t tag_end = selector.find_first_of(".#");
        const std::string tag = tag_end == std::string::npos ? selector : selector.substr(0, tag_end);
        if (!tag.empty() && tag != "*" && lower(tag) != node->tag_name) return false;

        size_t position = tag_end == std::string::npos ? selector.length() : tag_end;
        while (position < selector.length()) {
            const char marker = selector[position++];
            const size_t end = selector.find_first_of(".#", position);
            const std::string value = selector.substr(position, end == std::string::npos ? std::string::npos : end - position);
            if (marker == '#' && value != node->element_id) return false;
            if (marker == '.' && !has_class(node->class_name, value)) return false;
            position = end == std::string::npos ? selector.length() : end;
        }
        return true;
    }

    static bool matches_selector(const std::shared_ptr<Node>& node, const std::string& selector) {
        std::string normalized = selector;
        std::replace(normalized.begin(), normalized.end(), '>', ' ');
        std::stringstream stream(normalized);
        std::vector<std::string> parts;
        std::string part;
        while (stream >> part) parts.push_back(part);
        if (parts.empty() || !matches_simple_selector(node, parts.back())) return false;

        std::shared_ptr<Node> ancestor = node->parent.lock();
        for (size_t index = parts.size() - 1; index > 0; --index) {
            while (ancestor && !matches_simple_selector(ancestor, parts[index - 1])) ancestor = ancestor->parent.lock();
            if (!ancestor) return false;
            ancestor = ancestor->parent.lock();
        }
        return true;
    }

    static void apply_styles(const std::shared_ptr<Node>& node, const std::vector<CSSRule>& rules) {
        if (!node) return;
        Style computed;
        for (const auto& rule : rules) {
            if (matches_selector(node, rule.selector)) CSSParser::merge(computed, rule.style);
        }
        if (!node->inline_style.empty()) CSSParser::merge(computed, CSSParser::parse_style_attribute(node->inline_style));
        node->style = computed;
        if (node->tag_name == "img") {
            if (node->style.properties.count("width")) node->image_width = node->style.width;
            if (node->style.properties.count("height")) node->image_height = node->style.height;
        }
        for (const auto& child : node->children) {
            apply_styles(child, rules);
            if (child->type == NodeType::Text) {
                child->style.text_r = node->style.text_r;
                child->style.text_g = node->style.text_g;
                child->style.text_b = node->style.text_b;
                child->style.font_size = node->style.font_size;
                child->style.font_family = node->style.font_family;
                child->style.font_weight = node->style.font_weight;
                child->style.font_style = node->style.font_style;
                child->style.text_align = node->style.text_align;
            }
        }
    }
};