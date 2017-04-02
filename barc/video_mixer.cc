//
//  video_mixer.cc
//  barc
//
//  Created by Charley Robinson on 3/14/17.
//

extern "C" {
#include "video_mixer.h"
#include "file_writer.h"
#include "frame_builder.h"
}

#include <vector>
#include <algorithm>
#include "Geometry.h"

struct video_mixer_s {
  std::vector<struct media_stream_s*>streams;
  struct frame_builder_t* frame_builder;
  ArchiveLayout* layout;
  char auto_layout;
  size_t out_width;
  size_t out_height;
};

static bool z_index_sort(const struct media_stream_s* stream1,
                         const struct media_stream_s* stream2)
{
  return (archive_stream_get_z_index(stream1) <
          archive_stream_get_z_index(stream2));
}

static void do_auto_layout(struct video_mixer_s* pthis,
                           std::vector<ArchiveStreamInfo>& stream_info)
{
  char has_focus = 0;
  int non_focus_count = 0;
  for (auto info : stream_info) {
    if (0 == strcmp(info.layout_class().c_str(), "focus")) {
      has_focus = 1;
    } else {
      non_focus_count++;
    }
  }

  if (has_focus && non_focus_count < 2) {
    pthis->layout->setStyleSheet(Layout::kPip);
  } else if (has_focus && non_focus_count > 1) {
    pthis->layout->setStyleSheet(Layout::kHorizontalPresentation);
  } else {
    pthis->layout->setStyleSheet(Layout::kBestfitCss);
  }
}

void video_mixer_alloc(struct video_mixer_s** mixer_out) {
  struct video_mixer_s* mixer =
  (struct video_mixer_s*)calloc(1, sizeof(video_mixer_s));
  mixer->streams = std::vector<struct media_stream_s*>();
  frame_builder_alloc(&mixer->frame_builder);
  *mixer_out = mixer;
}

static int populate_stream_coords(struct video_mixer_s* pthis)
{
  std::sort(pthis->streams.begin(), pthis->streams.end(), z_index_sort);

  // Regenerate stream list every tick to allow on-the-fly layout changes
  std::vector<ArchiveStreamInfo> stream_info;
  for (struct media_stream_s* stream : pthis->streams) {
    stream_info.push_back(ArchiveStreamInfo(media_stream_get_name(stream),
                                            media_stream_get_class(stream),
                                            true));
  }
  
  // switch between bestfit and horizontal presentation if in auto layout mode
  if (pthis->auto_layout) {
    do_auto_layout(pthis, stream_info);
  }

  StreamPositions positions = pthis->layout->layout(stream_info);
  for (ComposerLayoutStreamPosition position : positions) {
    for (struct media_stream_s* stream : pthis->streams) {
      if (!strcmp(position.stream_id.c_str(),
                  media_stream_get_name(stream))) {
        archive_stream_set_offset_x(stream, position.x);
        archive_stream_set_offset_y(stream, position.y);
        archive_stream_set_radius(stream, position.radius);
        archive_stream_set_render_width(stream, position.width);
        archive_stream_set_render_height(stream, position.height);
        archive_stream_set_object_fit(stream,
                                      (enum object_fit)position.fit);
        archive_stream_set_z_index(stream, position.z);
      }
    }
  }
  return 0;
}

void video_mixer_free(struct video_mixer_s* mixer) {
  // wait for all frames to write out before closing down.
  frame_builder_wait(mixer->frame_builder, 0);
  frame_builder_free(mixer->frame_builder);
  // instance does not hold ownership of it's streams, so we can just drop refs
  // TODO refcount media_stream_s
  mixer->streams.clear();
  delete mixer->layout;
  free(mixer);
}

int video_mixer_flush(struct video_mixer_s* mixer) {
  return frame_builder_wait(mixer->frame_builder, 0);
}

struct frame_builder_callback_data_t {
  int64_t pts;
  struct file_writer_t* file_writer;
};

static void frame_builder_cb(AVFrame* frame, void *p) {
  struct frame_builder_callback_data_t* data =
  ((struct frame_builder_callback_data_t*)p);
  frame->pts = data->pts;
  int ret = file_writer_push_video_frame(data->file_writer, frame);
  if (ret) {
    printf("Unable to push video frame %lld\n", frame->pts);
  }
  free(p);
}

int video_mixer_add_stream(struct video_mixer_s* mixer,
                           struct media_stream_s* stream)
{
  auto index = std::find(mixer->streams.begin(), mixer->streams.end(), stream);
  if (index == mixer->streams.end()) {
    mixer->streams.push_back(stream);
    return 0;
  } else {
    return 1;
  }
}

void video_mixer_clear_streams(struct video_mixer_s* mixer) {
  mixer->streams.clear();
}

int video_mixer_async_push_frame(struct video_mixer_s* mixer,
                                 struct file_writer_t* file_writer,
                                 double time_clock, int64_t pts)
{
  int ret = -1;

  populate_stream_coords(mixer);

  struct frame_builder_callback_data_t* callback_data =
  (struct frame_builder_callback_data_t*)
  malloc(sizeof(struct frame_builder_callback_data_t));
  callback_data->pts = pts;
  callback_data->file_writer = file_writer;
  frame_builder_begin_frame(mixer->frame_builder,
                            file_writer->out_width,
                            file_writer->out_height,
                            (enum AVPixelFormat)AV_PIX_FMT_YUV420P,
                            callback_data);


  // append source frames to magic frame
  for (struct media_stream_s* stream : mixer->streams) {
    struct smart_frame_t* smart_frame;
    ret = archive_stream_get_video_for_time(stream, &smart_frame, time_clock);
    if (NULL == smart_frame || ret) {
      continue;
    }

    struct frame_builder_subframe_t subframe;
    subframe.smart_frame = smart_frame;
    subframe.x_offset = archive_stream_get_offset_x(stream);
    subframe.y_offset = archive_stream_get_offset_y(stream);
    subframe.radius = archive_stream_get_radius(stream);
    subframe.render_width = archive_stream_get_render_width(stream);
    subframe.render_height = archive_stream_get_render_height(stream);
    // TODO: configurable from the manifest
    subframe.object_fit = archive_stream_get_object_fit(stream);

    frame_builder_add_subframe(mixer->frame_builder, &subframe);
  }
  frame_builder_finish_frame(mixer->frame_builder, frame_builder_cb);
  return ret;
}

// ArchiveLayout is fixed to a single size, so instead of fixing it, just
// realloc whenever the dimesnsions change
static void video_mixer_resize(struct video_mixer_s* mixer) {
  if (mixer->layout) {
    delete mixer->layout;
  }
  mixer->layout = new ArchiveLayout((int) mixer->out_width,
                                    (int) mixer->out_height);
}

void video_mixer_set_width(struct video_mixer_s* mixer, size_t width) {
  mixer->out_width = width;
  video_mixer_resize(mixer);
}

void video_mixer_set_height(struct video_mixer_s* mixer, size_t height) {
  mixer->out_height = height;
  video_mixer_resize(mixer);
}

void video_mixer_set_css_preset(struct video_mixer_s* mixer,
                                const char* css_preset)
{
  mixer->auto_layout = 0;

  std::string style_sheet;
  if (NULL == css_preset) {
    printf("Video Mixer: no stylesheet preset defined. using auto.\n");
    mixer->auto_layout = 1;
    style_sheet = Layout::kBestfitCss;
  } else if (!strcmp("bestFit", css_preset)) {
    style_sheet = Layout::kBestfitCss;
  } else if (!strcmp("verticalPresentation", css_preset)) {
    style_sheet = Layout::kVerticalPresentation;
  } else if (!strcmp("horizontalPresentation", css_preset)) {
    style_sheet = Layout::kHorizontalPresentation;
  } else if (!strcmp("pip", css_preset)) {
    style_sheet = Layout::kPip;
  } else if (!strcmp("custom", css_preset)) {
    style_sheet = "SET_ME";
  } else if (!strcmp("auto", css_preset)) {
    mixer->auto_layout = 1;
    style_sheet = Layout::kBestfitCss;
  } else {
    printf("unknown css preset defined. Using auto.");
    mixer->auto_layout = 1;
    style_sheet = Layout::kBestfitCss;
  }
  mixer->layout->setStyleSheet(style_sheet);
}

void video_mixer_set_css_custom(struct video_mixer_s* mixer,
                                const char* css)
{
  if (css && strlen(css) > 0) {
    mixer->layout->setStyleSheet(css);
  }
}


