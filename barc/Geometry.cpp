//
//  Geometry.cpp
//  barc
//
//  Created by Charley Robinson on 1/27/17.
//

#include <iostream>
#include <string>
#include <regex>
#include <utility>
#include <map>
#include <vector>
#include "assert.h"

#include "Geometry.h"

static const std::string HTML_HEADER = "<html><head><style>";
static const std::string BODY_HEADER = "</style></head>";
static const std::string HTML_FOOTER = "</html>";


ArchiveLayout::ArchiveLayout(int width, int height) : engine_{width, height}
{ }

void ArchiveLayout::setStyleSheet(std::string style_sheet) {
    style_sheet_ = style_sheet;
    notifyChanged();
}

void ArchiveLayout::addListener(ComposerLayoutListener *listener) {
    auto iter = std::find(listeners_.begin(), listeners_.end(), listener);
    if (iter == listeners_.end()) {
        listeners_.push_back(listener);
    } else {
        assert(false && "Adding the same listener twice");
    }
}

void ArchiveLayout::removeListener(ComposerLayoutListener *listener) {
    listeners_.erase(std::remove(listeners_.begin(), listeners_.end(), listener),
                     listeners_.end());
}

StreamPositions ArchiveLayout::layout(const std::vector<ArchiveStreamInfo> &streams_info) {
    streams_.clear();
    computeStreamPositions(streams_info);
    sortStreamsByZIndex();
    return streams_;
}

void ArchiveLayout::sortStreamsByZIndex() {
    std::sort(streams_.begin(), streams_.end(),
              [](ComposerLayoutStreamPosition s1, ComposerLayoutStreamPosition s2) {
                  return s1.z < s2.z;
              });
}

void ArchiveLayout::computeStreamPositions(const std::vector<ArchiveStreamInfo> &streams) {
    StreamPositionMap stream_map = engine_.render(streams, style_sheet_);
    streams_.reserve(stream_map.size());
    for (auto stream_pair : stream_map) {
        ComposerLayoutStreamPosition position = stream_pair.second;
        if (position.width > 0 && position.height > 0) {
            streams_.push_back(position);
        }
    }
}

void ArchiveLayout::notifyChanged() {
    for (auto listener : listeners_) {
        listener->onLayoutChanged();
    }
}

StreamElement::StreamElement(const std::shared_ptr<litehtml::document>& doc) : litehtml::html_tag(doc) {
    m_obj_position.width.predef(object_fit_contain);
    m_obj_position.height.predef(object_fit_contain);
}

void StreamElement::parse_styles(bool is_reparse) {
    const litehtml::tchar_t *str = litehtml::html_tag::get_style_property(_t("object-fit"), false, _t("contain"));
    if (str) {
        m_obj_position.width.fromString(str, object_fit_strings);
        m_obj_position.height.fromString(str, object_fit_strings);
    }
    litehtml::html_tag::parse_styles(is_reparse);
}

void StreamElement::draw(litehtml::uint_ptr hdc, int x, int y, const litehtml::position* clip) {
    auto engine = static_cast<CssLayoutEngine*>(hdc);
    litehtml::html_tag::draw(hdc, x, y, clip);

    // we now calculate the object position
    litehtml::position pos = m_pos;
    pos.x += x;
    pos.y += y;

    litehtml::position border_box = pos;
    border_box += m_padding;
    border_box += m_borders;

    litehtml::borders bdr = m_css_borders;

    bdr.radius = m_css_borders.radius.calc_percents(border_box.width, border_box.height);

    engine->draw_object(hdc, m_pos, bdr, border_box, have_parent() ? false : true,
                        m_obj_position.width.predef(), get_attr("id"));
}

CssLayoutEngine::CssLayoutEngine(int width, int height) :
screen_width_(width), screen_height_(height) {
    container_ = "archive";
    std::string master_css = container_ + "{width: " + std::to_string(width) +
    "px;height: " + std::to_string(height) + "px;display: block;})";
    m_context_.load_master_stylesheet(master_css.c_str());
}

StreamPositionMap CssLayoutEngine::render(const std::vector<ArchiveStreamInfo> &streams, std::string css) {
    std::string html = HTML_HEADER + css + BODY_HEADER + "<" + container_ + ">";
    for (auto &stream_info : streams) {
        if (!stream_info.active()) {
            continue;
        }
        html += "<stream id=\"" + stream_info.stream_id() + "\" " +
        "class=\"" + stream_info.layout_class() + "\"></stream>";
    }

    html += + "</" + container_ + ">" +HTML_FOOTER;
    stream_positions_.clear();
    litehtml::document::ptr doc = litehtml::document::createFromUTF8(html.c_str(), this, &m_context_);
    int best_width = doc->render(screen_width_, screen_height_);
    int doc_width = doc->width();
    if (best_width != screen_width_ || doc_width != screen_width_) {
        printf("Warning: Rendering width mismatched. Something is wrong.\n");
    }
    int doc_height = doc->height();
    if (doc_height != screen_height_) {
        printf("Warning: Rendered height does not match container height. "
               "Check for missing content\n");
    }
    litehtml::position pos{0, 0, screen_width_, screen_height_};
    doc->draw(this, 0, 0, &pos);
    return stream_positions_;
}

void CssLayoutEngine::draw_object(litehtml::uint_ptr hdc, const litehtml::position& pos,
                                  const litehtml::borders& borders, const litehtml::position& draw_pos,
                                  bool root, int object_fit, std::string element_id) {
//    std::cout << __FUNCTION__
//                  << " " << pos.x << " " << pos.y << " "
//                  << pos.width << " " << pos.height
//                  << " " << root << " " << element_id;
    int height = pos.height;
    int width = pos.width;
  
    stream_positions_[element_id] = {element_id,
        pos.x, pos.y, static_cast<int>(stream_positions_.size()),
// This could be improved by checking all of the radii values.
      borders.radius.top_left_x,
      width, height, (StreamFit) object_fit};
}

std::shared_ptr<litehtml::element> CssLayoutEngine::create_element(const litehtml::tchar_t *tag_name,
                                                                   const litehtml::string_map &attributes,
                                                                   const std::shared_ptr<litehtml::document> &doc) {
    //std::cout << __FUNCTION__ <<  " " << tag_name;
    if (strcmp("stream", tag_name) == 0) {
        return std::make_shared<StreamElement>(doc);
    } else if (strcmp("broadcast", tag_name) == 0) {
        return std::make_shared<litehtml::html_tag>(doc);
    } else if (strcmp("archive", tag_name) == 0) {
        return std::make_shared<litehtml::html_tag>(doc);
    }
    return {};
}

void CssLayoutEngine::get_client_rect(litehtml::position& client) const {
    client.x = 0;
    client.y = 0;
    client.width = screen_width_;
    client.height = screen_height_;
//    std::cout << __FUNCTION__ << " " << client.x << " " << client.y << " "
//                  << client.width << " " << client.height;
}

void CssLayoutEngine::get_media_features(litehtml::media_features& media) const {
    media.width = screen_width_;
    media.height = screen_height_;
    media.device_width = screen_width_;
    media.device_height = screen_height_;
}
