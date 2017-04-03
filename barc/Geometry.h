//
//  Geometry.hpp
//  barc
//
//  Created by Charley Robinson on 1/27/17.
//

#ifndef Geometry_hpp
#define Geometry_hpp

#include <stdio.h>
#include <map>
#include <string>
#include <vector>

#include "litehtml/include/litehtml.h"

class CssLayoutEngine;

// Some code in this file follows litehtml style to inherit classes and use helpers inside the library
#define object_fit_strings  _t("contain;cover;fill;none;scale-down")

#include "object_fit.h"

namespace Layout {
    const std::string kHorizontalPresentation =
    R"(stream {
    float:left;
    margin-top: 80%;
width: 20%;
height: 20%;
object-fit: cover;
}
stream.focus {
position: absolute;
top: 0;
left: 0;
margin-top: 0px;
height: 80%;
width: 100%;
object-fit: contain;
})";

const std::string kPip =
R"(
stream.focus {
position: absolute;
top: 0;
right: 0;
width: 100%;
height: 100%;
z-index: 100;
object-fit: contain;
}
stream {
position: absolute;
right: 5%;
top: 5%;
width: 15%;
height: 15%;
z-index: 200;
object-fit: cover;
}
)";

const std::string kVerticalPresentation =
R"(stream {
float:left;
left: 0px;
clear: left;
width: 20%;
height: 20%;
padding: 0px;
}
stream.focus {
position: absolute;
padding: 0px;
top: 0;
right: 0;
margin: 0px;
left: 20%;
height: 100%;
width: 80%;
})";

const std::string kCircleTopPresentation =
R"(stream {
float:left;
width: 120px;
height: 120px;
border-radius: 120px;
border: 6px solid #CCCCCC;
object-fit: cover;
padding: 0.5%;
z-index: 200;
}
stream.focus {
position: absolute;
border-radius: 0px none;
border: 0px none;
object-fit: contain;
padding: 0px;
margin: 0px;
bottom: 0;
right: 0;
left: 0%;
height: 100%;
width: 100%;
z-index: 100;
})";

// This CSS only supports up to 3x3 streams.
const std::string kBestfitCss =
R"(stream {
float: left;
object-fit: cover;
}
stream:first-child:nth-last-child(1) {
width: 100%;
height: 100%;
}

stream:first-child:nth-last-child(2),
stream:first-child:nth-last-child(2) ~ stream {
width: 50%;
height: 100%;
}
stream:first-child:nth-last-child(3),
stream:first-child:nth-last-child(3) ~ stream,
stream:first-child:nth-last-child(4),
stream:first-child:nth-last-child(4) ~ stream {
width: 50%;
height: 50%;
}
stream:first-child:nth-last-child(5),
stream:first-child:nth-last-child(5) ~ stream,
stream:first-child:nth-last-child(6),
stream:first-child:nth-last-child(6) ~ stream,
stream:first-child:nth-last-child(7),
stream:first-child:nth-last-child(7) ~ stream,
stream:first-child:nth-last-child(8),
stream:first-child:nth-last-child(8) ~ stream,
stream:first-child:nth-last-child(9),
stream:first-child:nth-last-child(9) ~ stream
{
width: 33.2%;
height: 33.2%;
})";
}  // namespace Layout

enum StreamFit {
    // this is the default value and it increases or decreases the size of the frame to fill
    // the box whilst preserving its aspect-ratio
    kContain = 0,
    // the frames will fill the height and width of its box, once again maintaining its aspect ratio
    // but often cropping the image in the process
    kCover = 1,
    // stretches the frame to fit the content box, regardless of its aspect-ratio
    kFill = 2,
    // frame will ignore the height and width of the parent and retain its original size
    kNone = 3,
    // the frame will compare the difference between none and contain in order to find the smallest concrete object size
    kScaleDown = 4
};

struct ComposerLayoutStreamPosition {
    ComposerLayoutStreamPosition() = default;
    ComposerLayoutStreamPosition(std::string id, int x, int y, int z, int r,
                                 int border_width,
                                 struct litehtml::web_color border_color,
                                 int w, int h,
                                 StreamFit f = StreamFit::kContain)
    : stream_id{id}, x{x}, y{y}, z{z}, radius(r), width{w}, height{h},
  border_width(border_width), border_color(border_color), fit(f) {}
    std::string stream_id;
    int x = 0;
    int y = 0;
    int z = 0;
    int width = 0;
    int height = 0;
    StreamFit fit = kContain;
  int radius = 0;
  int border_width = 0;
  struct litehtml::web_color border_color;

    std::string serialize() {
        std::stringstream serialization;
        serialization <<
        "x: " << x << ", " <<
        "y: " << y << ", " <<
      "z: " << z << ", " <<
      "r: " << radius << ", " <<
      "border: " << border_width << ", " <<
        "width: " << width << ", " <<
        "height: " << height << ", " <<
        "fit: " << fit;
        return serialization.str();
    }
};

class ArchiveStreamInfo {
public:
    ArchiveStreamInfo(std::string stream_id, bool active)
    : ArchiveStreamInfo{stream_id, "", active} {}
    ArchiveStreamInfo(std::string stream_id, std::string layout_class, bool active)
    : stream_id_{stream_id}, layout_class_{layout_class}, active_{active} {}
    ~ArchiveStreamInfo() {}
    std::string stream_id() const { return stream_id_; }
    std::string layout_class() const { return layout_class_; }
    void layout_class(std::string layout_class) { layout_class_ = layout_class; }
    bool active() const { return active_; }

private:
    std::string stream_id_;
    std::string layout_class_;
    bool active_ = false;
};

using StreamPositions = std::vector<ComposerLayoutStreamPosition>;
using StreamPositionMap = std::map<std::string, ComposerLayoutStreamPosition>;

class StreamElement : public litehtml::html_tag {
public:
    explicit StreamElement(const std::shared_ptr<litehtml::document>& doc);
    ~StreamElement() {}
    void setComposer(CssLayoutEngine* layout) {
        layout_ = layout;
    }
    void parse_styles(bool is_reparse = false) override;
    void draw(litehtml::uint_ptr hdc, int x, int y, const litehtml::position* clip) override;

private:
    CssLayoutEngine* layout_ = nullptr;
    litehtml::css_position m_obj_position;
};

class CssLayoutEngine : public litehtml::document_container {
public:
    CssLayoutEngine() = delete;
    CssLayoutEngine(int width, int height);
    virtual ~CssLayoutEngine() {}

    StreamPositionMap render(const std::vector<ArchiveStreamInfo> &streams, std::string css);

    litehtml::uint_ptr create_font(const litehtml::tchar_t* faceName, int size, int weight,
                                   litehtml::font_style italic, unsigned int decoration,
                                   litehtml::font_metrics* fm) override { return nullptr; }
    void delete_font(litehtml::uint_ptr hFont) override {}
    int text_width(const litehtml::tchar_t* text, litehtml::uint_ptr hFont) override { return 1; }
    void draw_text(litehtml::uint_ptr hdc, const litehtml::tchar_t* text, litehtml::uint_ptr hFont,
                   litehtml::web_color color, const litehtml::position& pos) override {}
    int pt_to_px(int pt) override { return 1; }
    int get_default_font_size() const override { return 1; }
    const litehtml::tchar_t* get_default_font_name() const override { return ""; }
    void draw_list_marker(litehtml::uint_ptr hdc, const litehtml::list_marker& marker) override {}
    void load_image(const litehtml::tchar_t* src, const litehtml::tchar_t* baseurl,
                    bool redraw_on_ready) override {}
    void get_image_size(const litehtml::tchar_t* src, const litehtml::tchar_t* baseurl,
                        litehtml::size& sz) override {}
    void draw_background(litehtml::uint_ptr hdc, const litehtml::background_paint& bg) override {}
    void draw_borders(litehtml::uint_ptr hdc, const litehtml::borders& borders,
                      const litehtml::position& draw_pos, bool root) override {}

    void set_caption(const litehtml::tchar_t* caption) override {}
    void set_base_url(const litehtml::tchar_t* base_url) override {}
    void link(const std::shared_ptr<litehtml::document>& doc, const litehtml::element::ptr& el) override {}
    void on_anchor_click(const litehtml::tchar_t* url, const litehtml::element::ptr& el) override {}
    void set_cursor(const litehtml::tchar_t* cursor) override {}
    void transform_text(litehtml::tstring& text, litehtml::text_transform tt) override {}
    void import_css(litehtml::tstring& text, const litehtml::tstring& url, litehtml::tstring& baseurl) override {}
    void set_clip(const litehtml::position& pos, const litehtml::border_radiuses& bdr_radius,
                  bool valid_x, bool valid_y) override {}
    void del_clip() override {}
    void get_language(litehtml::tstring& language, litehtml::tstring & culture) const override {}
    void get_client_rect(litehtml::position& client) const override;
    std::shared_ptr<litehtml::element> create_element(const litehtml::tchar_t *tag_name,
                                                      const litehtml::string_map &attributes,
                                                      const std::shared_ptr<litehtml::document> &doc) override;

    void get_media_features(litehtml::media_features& media) const override;

    void draw_object(litehtml::uint_ptr hdc, const litehtml::position& pos,
                     const litehtml::borders& borders, const litehtml::position& draw_pos,
                     bool root, int object_fit, std::string element_id);

private:
    std::string container_;
    int screen_width_ = 0;
    int screen_height_ = 0;
    litehtml::context m_context_;
    std::map<std::string, ComposerLayoutStreamPosition> stream_positions_;
};


class ComposerLayoutListener {
public:
    virtual ~ComposerLayoutListener() {}
    virtual void onLayoutChanged() = 0;
};

class ArchiveLayout {
public:
    ArchiveLayout() = delete;
    ArchiveLayout(const ArchiveLayout&) = delete;
    ArchiveLayout(int width, int height);
    virtual ~ArchiveLayout() {}

    void setStyleSheet(std::string style_sheet);
    virtual StreamPositions layout(const std::vector<ArchiveStreamInfo> &streams);

    void addListener(ComposerLayoutListener* listener);
    void removeListener(ComposerLayoutListener* listener);

private:
    std::vector<std::string> getStreamsInRole(std::string key) const;

    bool isInRole(std::string stream_id, std::string role) const;
    void sortStreamsByZIndex();
    void applyLayoutClasses(std::vector<ArchiveStreamInfo> &streams);  // NOLINT
    void computeStreamPositions(const std::vector<ArchiveStreamInfo> &streams);
    void notifyChanged();

private:
    std::string style_sheet_;
    StreamPositions streams_;
    CssLayoutEngine engine_;
    std::vector<ComposerLayoutListener*> listeners_;
};

#endif /* Geometry_hpp */
