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

struct archive_stream_t;

#pragma mark - User sourced media
/**
 * Callback-based stream source can be used iff file-based stream source
 * is not being used.
 * @param stream the stream requesting frames
 * @param packet a read packet
 * @param p user supplied argument pointer
 * @return 0 on success
 */
typedef int (archive_read_packet_cb)(struct archive_stream_t* stream,
                                     AVPacket* packet, void* p);
void archive_stream_set_video_read(struct archive_stream_t* stream,
                                   archive_read_packet_cb* cb, void* p);
void archive_stream_set_audio_read(struct archive_stream_t* stream,
                                   archive_read_packet_cb* cb, void* p);
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
void archive_stream_set_audio_config_callback
(struct archive_stream_t* stream, archive_get_config_cb* cb,
 void* p);
void archive_stream_set_video_config_callback
(struct archive_stream_t* stream, archive_get_config_cb* cb,
 void* p);

/**
 * File-based stream source may be used to read from a file. We'll do the best
 * we can to let FFmpeg do it's magic and allow any file type, but at the time
 * this was written, there are some assumptions about the input file type.
 */
int archive_stream_open_file(struct archive_stream_t** stream_out,
                             const char *filename,
                             int64_t start_offset, int64_t stop_offset,
                             const char* stream_name,
                             const char* stream_class);
#pragma mark - Memory lifecycle

int archive_stream_alloc(struct archive_stream_t** stream_out,
                         const char* stream_name,
                         const char* stream_class);
int archive_stream_free(struct archive_stream_t* stream);

int archive_stream_get_video_for_time(struct archive_stream_t* stream,
                                      struct smart_frame_t** frame,
                                      int64_t clock_time,
                                      AVRational clock_time_base);

int archive_stream_has_video_for_time(struct archive_stream_t* stream,
                                      int64_t clock_time,
                                      AVRational clock_time_base);

/**
 * @return The number of samples read into *samples_out
 */
int archive_stream_pop_audio_samples(struct archive_stream_t* stream,
                                     int num_samples,
                                     enum AVSampleFormat format,
                                     int sample_rate,
                                     int16_t** samples_out,
                                     int64_t clock_time,
                                     AVRational time_base);

#pragma mark - video layout management
int archive_stream_get_offset_x(struct archive_stream_t* stream);
int archive_stream_get_offset_y(struct archive_stream_t* stream);
int archive_stream_get_render_width(struct archive_stream_t* stream);
int archive_stream_get_render_height(struct archive_stream_t* stream);
int archive_stream_get_z_index(const struct archive_stream_t* stream);
enum object_fit archive_stream_get_object_fit(struct archive_stream_t* stream);
void archive_stream_set_offset_x(struct archive_stream_t* stream,
                                 int x_offset);
void archive_stream_set_offset_y(struct archive_stream_t* stream,
                                 int y_offset);
void archive_stream_set_render_width(struct archive_stream_t* stream,
                                     int width);
void archive_stream_set_render_height(struct archive_stream_t* stream,
                                      int height);
void archive_stream_set_z_index(struct archive_stream_t* stream,
                                int z_index);
void archive_stream_set_object_fit(struct archive_stream_t* stream,
                                   enum object_fit object_fit);
const char* archive_stream_get_name(struct archive_stream_t* stream);
const char* archive_stream_get_class(struct archive_stream_t* stream);

#endif /* archive_stream_h */
