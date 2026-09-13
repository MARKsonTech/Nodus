#pragma once
#include <string>
#include <vector>
#include <memory>
#include <SDL.h>
#include "css_parser.h"

enum class NodeType { Element, Text };

struct Node {
    NodeType type;
    std::string tag_name;
    std::string text_data;
    std::string element_id;
    std::string class_name;
    std::string inline_style;
    std::string image_src;
    int image_width = -1;
    int image_height = -1;
    Style style;
    std::vector<std::shared_ptr<Node>> children;
    std::weak_ptr<Node> parent;

    Node(NodeType t) : type(t) {}
    ~Node() {}
};