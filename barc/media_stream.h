//
//  archive_stream.h
//  barc
//
//  Created by Charley Robinson on 1/26/17.
//

#ifndef archive_stream_h
#define archive_stream_h

#include <stdio.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include "smart_avframe.h"
#include "object_fit.h"

struct media_stream_s;

#pragma mark - media sources

/**
 * Callback-based stream source can be used iff file-based stream source
 * is not being used.
 * @param stream the stream requesting frames
 * @param frame an uncompressed frame
 * @param p user supplied argument pointer
 * @return 0 on success
 */
typedef int (media_stream_get_frame_cb)(struct media_stream_s* stream,
                                        AVFrame* frame, double time_clock,
                                        void* p);
typedef int (media_stream_get_video_frame_cb)
(struct media_stream_s* stream, struct smart_frame_t** frame_out,
 double time_clock, void* p);
void media_stream_set_video_read(struct media_stream_s* stream,
                                 media_stream_get_video_frame_cb* cb, void* p);
void media_stream_set_audio_read(struct media_stream_s* stream,
                                 media_stream_get_frame_cb* cb, void* p);
/**
 * Fetching stream configuration - get attributes about the stream itself.
 * This should probably be cleaned up and decoding moved into the user-source
 */
struct stream_config_s {
  int sample_rate;
  int num_channels;
  AVCodecContext* context;
  int64_t start_offset;
  enum AVSampleFormat sample_format;
  uint64_t channel_layout;
};

typedef int (archive_get_config_cb)
(struct stream_config_s* stream_config, void* p);
void media_stream_set_audio_config_callback
(struct media_stream_s* stream, archive_get_config_cb* cb,
 void* p);
void media_stream_set_video_config_callback
(struct media_stream_s* stream, archive_get_config_cb* cb,
 void* p);

/**
 * File-based stream source may be used to read from a file. We'll do the best
 * we can to let FFmpeg do it's magic and allow any file type, but at the time
 * this was written, there are some assumptions about the input file type.
 */
int archive_stream_open_file(struct media_stream_s** stream_out,
                             const char *filename,
                             int64_t start_offset, int64_t stop_offset,
                             const char* stream_name,
                             const char* stream_class);
#pragma mark - Memory lifecycle

int media_stream_alloc(struct media_stream_s** stream_out);
int media_stream_free(struct media_stream_s* stream);

#pragma mark - Fetching media

void media_stream_set_current_time(struct media_stream_s* stream, double clock);
int archive_stream_get_video_for_time(struct media_stream_s* stream,
                                      struct smart_frame_t** frame,
                                      double clock_time);

/**
 * @return The number of samples read into *samples_out
 */
int archive_stream_get_audio_samples(struct media_stream_s* stream,
                                     int num_samples,
                                     enum AVSampleFormat format,
                                     int sample_rate,
                                     int16_t** samples_out,
                                     int num_channels,
                                     double clock_time);

#pragma mark - video layout management
int archive_stream_get_offset_x(struct media_stream_s* stream);
int archive_stream_get_offset_y(struct media_stream_s* stream);
int archive_stream_get_render_width(struct media_stream_s* stream);
int archive_stream_get_render_height(struct media_stream_s* stream);
int archive_stream_get_z_index(const struct media_stream_s* stream);
enum object_fit archive_stream_get_object_fit(struct media_stream_s* stream);
void archive_stream_set_offset_x(struct media_stream_s* stream,
                                 int x_offset);
void archive_stream_set_offset_y(struct media_stream_s* stream,
                                 int y_offset);
void archive_stream_set_render_width(struct media_stream_s* stream,
                                     int width);
void archive_stream_set_render_height(struct media_stream_s* stream,
                                      int height);
void archive_stream_set_z_index(struct media_stream_s* stream,
                                int z_index);
void archive_stream_set_object_fit(struct media_stream_s* stream,
                                   enum object_fit object_fit);
void media_stream_set_name(struct media_stream_s* stream,
                             const char* sz_name);
void media_stream_set_class(struct media_stream_s* stream,
                              const char* sz_class);
const char* media_stream_get_name(struct media_stream_s* stream);
const char* media_stream_get_class(struct media_stream_s* stream);

#endif /* archive_stream_h */
