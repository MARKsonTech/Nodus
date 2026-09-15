#pragma once
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

struct Style {
    unsigned char r = 255, g = 255, b = 255, a = 255;
    unsigned char text_r = 0, text_g = 0, text_b = 0;
    int font_size = 16;

    unsigned char border_r = 0, border_g = 0, border_b = 0, border_a = 255;
    int width = -1, height = -1;
    int max_width = -1;
    float width_percent = -1.0f, height_percent = -1.0f;
    float max_width_percent = -1.0f;
    int margin_top = 0, margin_right = 0, margin_bottom = 0, margin_left = 0;
    int padding_top = 0, padding_right = 0, padding_bottom = 0, padding_left = 0;
    int border_width = 0;
    float opacity = 1.0f;
    std::string display = "block";
    std::string position = "static";
    std::string font_family = "Arial";
    std::string font_weight = "normal";
    std::string font_style = "normal";
    std::string text_align = "left";
    std::string text_decoration = "none";
    std::string overflow = "visible";
    std::string box_sizing = "content-box";
    std::string border_style = "none";
    std::string border_radius;
    std::string background_image;
    std::string background_repeat = "repeat";
    std::string background_position = "0% 0%";
    std::string background_size = "auto";

    std::unordered_map<std::string, std::string> properties;
};

struct CSSRule {
    std::string selector;
    Style style;
};

class CSSParser {
public:
    static Style parse_style_attribute(const std::string& style_str) {
        Style style;
        for (const auto& declaration : split_declarations(style_str)) {
            const auto colon_pos = declaration.find(':');
            if (colon_pos == std::string::npos) continue;

            const std::string key = lower(trim(declaration.substr(0, colon_pos)));
            std::string value = trim(declaration.substr(colon_pos + 1));
            const std::string important = lower(value);
            const size_t important_pos = important.rfind("!important");
            if (important_pos != std::string::npos && trim(important.substr(important_pos + 10)).empty()) {
                value = trim(value.substr(0, important_pos));
            }
            if (key.empty() || value.empty()) continue;

            style.properties[key] = value;
            apply(style, key, value);
        }
        return style;
    }

    static std::vector<CSSRule> parse_stylesheet(const std::string& stylesheet) {
        std::vector<CSSRule> rules;
        std::string source = stylesheet;
        size_t comment_start = source.find("/*");
        while (comment_start != std::string::npos) {
            const size_t comment_end = source.find("*/", comment_start + 2);
            if (comment_end == std::string::npos) {
                source.erase(comment_start);
                break;
            }
            source.erase(comment_start, comment_end - comment_start + 2);
            comment_start = source.find("/*");
        }

        size_t position = 0;
        while (position < source.length()) {
            const size_t open = source.find('{', position);
            if (open == std::string::npos) break;
            const size_t close = source.find('}', open + 1);
            if (close == std::string::npos) break;

            const std::string selector_text = trim(source.substr(position, open - position));
            const Style declarations = parse_style_attribute(source.substr(open + 1, close - open - 1));
            size_t selector_start = 0;
            while (selector_start < selector_text.length()) {
                const size_t comma = selector_text.find(',', selector_start);
                const std::string selector = trim(selector_text.substr(
                    selector_start, comma == std::string::npos ? std::string::npos : comma - selector_start));
                if (!selector.empty() && selector[0] != '@') rules.push_back({ selector, declarations });
                if (comma == std::string::npos) break;
                selector_start = comma + 1;
            }
            position = close + 1;
        }
        return rules;
    }

    static void merge(Style& target, const Style& source) {
        for (const auto& property : source.properties) {
            apply(target, property.first, property.second);
        }
    }

private:
    static std::string trim(const std::string& value) {
        const auto first = value.find_first_not_of(" \t\n\r\f");
        if (first == std::string::npos) return {};
        const auto last = value.find_last_not_of(" \t\n\r\f");
        return value.substr(first, last - first + 1);
    }

    static std::string lower(std::string value) {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
        return value;
    }

    static std::vector<std::string> split_declarations(const std::string& input) {
        std::vector<std::string> declarations;
        std::string current;
        int parentheses = 0;
        char quote = '\0';

        for (const char character : input) {
            if (quote != '\0') {
                current += character;
                if (character == quote) quote = '\0';
            } else if (character == '\'' || character == '"') {
                quote = character;
                current += character;
            } else if (character == '(') {
                ++parentheses;
                current += character;
            } else if (character == ')' && parentheses > 0) {
                --parentheses;
                current += character;
            } else if (character == ';' && parentheses == 0) {
                declarations.push_back(current);
                current.clear();
            } else {
                current += character;
            }
        }
        if (!current.empty()) declarations.push_back(current);
        return declarations;
    }

    static int clamp(int value) {
        if (value < 0) return 0;
        if (value > 255) return 255;
        return value;
    }

    static int rounded(double value) {
        return static_cast<int>(value < 0 ? value - 0.5 : value + 0.5);
    }

    static unsigned char channel(double value, bool percentage) {
        if (percentage) value = value * 2.55;
        return static_cast<unsigned char>(clamp(rounded(value)));
    }

    static bool parse_number(const std::string& value, double& number, bool& percentage) {
        char* end = nullptr;
        number = std::strtod(value.c_str(), &end);
        if (end == value.c_str()) return false;
        std::string suffix = lower(trim(end));
        percentage = suffix == "%";
        return percentage || suffix.empty();
    }

    static bool parse_color(const std::string& input, unsigned char& red, unsigned char& green,
                            unsigned char& blue, unsigned char& alpha) {
        const std::string value = lower(trim(input));
        static const std::unordered_map<std::string, std::string> named = {
            {"black", "000000"}, {"silver", "c0c0c0"}, {"gray", "808080"},
            {"grey", "808080"}, {"white", "ffffff"}, {"maroon", "800000"},
            {"red", "ff0000"}, {"purple", "800080"}, {"fuchsia", "ff00ff"},
            {"green", "008000"}, {"lime", "00ff00"}, {"olive", "808000"},
            {"yellow", "ffff00"}, {"navy", "000080"}, {"blue", "0000ff"},
            {"teal", "008080"}, {"aqua", "00ffff"}, {"orange", "ffa500"},
            {"transparent", "00000000"}, {"lightgray", "d3d3d3"}
        };

        std::string hex = value;
        if (named.count(hex)) hex = named.at(hex);
        if (hex.size() > 0 && hex[0] == '#') hex.erase(0, 1);
        if ((hex.size() == 3 || hex.size() == 4) &&
            hex.find_first_not_of("0123456789abcdef") == std::string::npos) {
            std::string expanded;
            for (const char digit : hex) expanded += std::string(2, digit);
            hex = expanded;
        }
        if ((hex.size() == 6 || hex.size() == 8) &&
            hex.find_first_not_of("0123456789abcdef") == std::string::npos) {
            red = static_cast<unsigned char>(std::stoi(hex.substr(0, 2), nullptr, 16));
            green = static_cast<unsigned char>(std::stoi(hex.substr(2, 2), nullptr, 16));
            blue = static_cast<unsigned char>(std::stoi(hex.substr(4, 2), nullptr, 16));
            alpha = hex.size() == 8 ? static_cast<unsigned char>(std::stoi(hex.substr(6, 2), nullptr, 16)) : 255;
            return true;
        }

        const auto open = value.find('(');
        const auto close = value.rfind(')');
        if (open == std::string::npos || close <= open) return false;
        const std::string function = value.substr(0, open);
        std::string arguments = value.substr(open + 1, close - open - 1);
        std::replace(arguments.begin(), arguments.end(), ',', ' ');
        std::stringstream stream(arguments);
        std::string red_value, green_value, blue_value, alpha_value;
        if (!(stream >> red_value >> green_value >> blue_value)) return false;
        bool red_percent = false, green_percent = false, blue_percent = false;
        double red_number = 0, green_number = 0, blue_number = 0;
        if (!parse_number(red_value, red_number, red_percent) || !parse_number(green_value, green_number, green_percent) ||
            !parse_number(blue_value, blue_number, blue_percent)) return false;
        red = channel(red_number, red_percent);
        green = channel(green_number, green_percent);
        blue = channel(blue_number, blue_percent);
        alpha = 255;
        if (stream >> alpha_value && function == "rgba") {
            double alpha_number = 0;
            bool alpha_percent = false;
            if (parse_number(alpha_value, alpha_number, alpha_percent)) {
                alpha = static_cast<unsigned char>(clamp(rounded(alpha_percent ? alpha_number * 2.55 : alpha_number * 255)));
            }
        }
        return function == "rgb" || function == "rgba";
    }

    static int length(const std::string& value, int fallback = 0) {
        const std::string normalized = lower(trim(value));
        if (normalized == "0") return 0;
        try {
            size_t consumed = 0;
            const double number = std::stod(normalized, &consumed);
            const std::string unit = normalized.substr(consumed);
            if (unit.empty() || unit == "px" || unit == "pt") return rounded(number);
            if (unit == "em" || unit == "rem") return rounded(number * 16);
            if (unit == "%") return rounded(number);
        } catch (...) {}
        return fallback;
    }

    static float percentage(const std::string& value) {
        const std::string normalized = lower(trim(value));
        if (normalized.empty() || normalized.back() != '%') return -1.0f;
        try {
            return std::stof(normalized.substr(0, normalized.length() - 1)) / 100.0f;
        } catch (...) {
            return -1.0f;
        }
    }

    static void four_sides(const std::string& value, int& top, int& right, int& bottom, int& left) {
        std::stringstream stream(value);
        std::vector<std::string> values;
        std::string part;
        while (stream >> part) values.push_back(part);
        if (values.empty() || values.size() > 4) return;
        if (values.size() == 1) top = right = bottom = left = length(values[0]);
        if (values.size() == 2) { top = bottom = length(values[0]); right = left = length(values[1]); }
        if (values.size() == 3) { top = length(values[0]); right = left = length(values[1]); bottom = length(values[2]); }
        if (values.size() == 4) { top = length(values[0]); right = length(values[1]); bottom = length(values[2]); left = length(values[3]); }
    }

    static void border_shorthand(Style& style, const std::string& value) {
        std::stringstream stream(value);
        std::string part;
        while (stream >> part) {
            const std::string normalized = lower(part);
            const int parsed_width = length(normalized, -1);
            if (parsed_width >= 0) {
                style.border_width = parsed_width;
                continue;
            }
            if (normalized == "none" || normalized == "hidden" || normalized == "dotted" ||
                normalized == "dashed" || normalized == "solid" || normalized == "double" ||
                normalized == "groove" || normalized == "ridge" || normalized == "inset" || normalized == "outset") {
                style.border_style = normalized;
                continue;
            }
            unsigned char red = 0, green = 0, blue = 0, alpha = 255;
            if (parse_color(normalized, red, green, blue, alpha)) {
                style.border_r = red;
                style.border_g = green;
                style.border_b = blue;
                style.border_a = alpha;
            }
        }
    }

    static void apply(Style& style, const std::string& key, const std::string& value) {
        unsigned char red = 0, green = 0, blue = 0, alpha = 255;
        if (key == "color" && parse_color(value, red, green, blue, alpha)) { style.text_r = red; style.text_g = green; style.text_b = blue; }
        else if ((key == "background-color" || key == "background") && parse_color(value, red, green, blue, alpha)) { style.r = red; style.g = green; style.b = blue; style.a = alpha; }
        else if (key == "border-color" && parse_color(value, red, green, blue, alpha)) { style.border_r = red; style.border_g = green; style.border_b = blue; style.border_a = alpha; }
        else if (key == "font-size") { style.font_size = length(value, style.font_size); if (style.font_size < 1) style.font_size = 1; }
        else if (key == "width") { style.width = length(value, style.width); style.width_percent = percentage(value); }
        else if (key == "height") { style.height = length(value, style.height); style.height_percent = percentage(value); }
        else if (key == "max-width") { style.max_width = length(value, style.max_width); style.max_width_percent = percentage(value); }
        else if (key == "margin") four_sides(value, style.margin_top, style.margin_right, style.margin_bottom, style.margin_left);
        else if (key == "padding") four_sides(value, style.padding_top, style.padding_right, style.padding_bottom, style.padding_left);
        else if (key == "margin-top") style.margin_top = length(value, style.margin_top);
        else if (key == "margin-right") style.margin_right = length(value, style.margin_right);
        else if (key == "margin-bottom") style.margin_bottom = length(value, style.margin_bottom);
        else if (key == "margin-left") style.margin_left = length(value, style.margin_left);
        else if (key == "padding-top") style.padding_top = length(value, style.padding_top);
        else if (key == "padding-right") style.padding_right = length(value, style.padding_right);
        else if (key == "padding-bottom") style.padding_bottom = length(value, style.padding_bottom);
        else if (key == "padding-left") style.padding_left = length(value, style.padding_left);
        else if (key == "border") border_shorthand(style, value);
        else if (key == "border-width") style.border_width = length(value, style.border_width);
        else if (key == "opacity") {
            try {
                style.opacity = std::stof(value);
                if (style.opacity < 0.0f) style.opacity = 0.0f;
                if (style.opacity > 1.0f) style.opacity = 1.0f;
            } catch (...) {}
        }
        else if (key == "display") style.display = lower(value);
        else if (key == "position") style.position = lower(value);
        else if (key == "font-family") style.font_family = value;
        else if (key == "font-weight") style.font_weight = lower(value);
        else if (key == "font-style") style.font_style = lower(value);
        else if (key == "text-align") style.text_align = lower(value);
        else if (key == "text-decoration") style.text_decoration = lower(value);
        else if (key == "overflow") style.overflow = lower(value);
        else if (key == "box-sizing") style.box_sizing = lower(value);
        else if (key == "border-style") style.border_style = lower(value);
        else if (key == "border-radius") style.border_radius = value;
        else if (key == "background-image") style.background_image = value;
        else if (key == "background-repeat") style.background_repeat = lower(value);
        else if (key == "background-position") style.background_position = value;
        else if (key == "background-size") style.background_size = value;
    }
};