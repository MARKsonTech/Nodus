#pragma once
#include <string>
#include <vector>
#include <memory>
#include <SDL.h>
#include "css_parser.h"

enum class NodeType { Element, Text };

struct VideoSource {
    std::string url;
    int height = -1;
};

struct Node {
    NodeType type;
    std::string tag_name;
    std::string text_data;
    std::string element_id;
    std::string class_name;
    std::string inline_style;
    std::string href;
    std::string media_src;
    std::string poster_src;
    std::vector<VideoSource> media_sources;
    std::string image_src;
    int media_width = -1;
    int media_height = -1;
    int image_width = -1;
    int image_height = -1;
    int intrinsic_width = -1;
    int intrinsic_height = -1;
    bool media_controls = false;
    bool media_autoplay = false;
    bool media_muted = false;
    bool media_loop = false;
    Style style;
    std::vector<std::shared_ptr<Node>> children;
    std::weak_ptr<Node> parent;

    Node(NodeType t) : type(t) {}
    ~Node() {}
};