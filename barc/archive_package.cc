//
//  archive_package.c
//  barc
//
//  Created by Charley Robinson on 1/26/17.
//

extern "C" {
#include <unistd.h>
#include <glob.h>
#include <jansson.h>
#include <assert.h>

#include "archive_package.h"
#include "archive_manifest.h"
#include "source_container.h"
#include "webm_source.h"
#include "image_source.h"
#include "barc.h"
}

#include <vector>

#include "Geometry.h"

struct archive_s {
  struct barc_s* barc;
  std::vector<struct source_s*> sources;
  std::vector<const struct layout_event_s*> events;
  const char* source_path;
  double begin_offset;
  double end_offset;
  struct archive_manifest_s* manifest;
};

static int archive_open(struct archive_s* archive);
static double archive_get_finish_clock_time(struct archive_s* archive);
static int setup_streams_for_tick(struct archive_s* archive, double clock_time);
static void process_layout_events(struct archive_s* pthis,
                                  double clock_time);

void archive_alloc(struct archive_s** archive_out) {
  struct archive_s* archive = (struct archive_s*)
  calloc(1, sizeof(struct archive_s));
  barc_alloc(&archive->barc);
  archive_manifest_alloc(&archive->manifest);
  archive->sources = std::vector<struct source_s*>();
  archive->events = std::vector<const struct layout_event_s*>();
  *archive_out = archive;
}

void archive_free(struct archive_s* archive) {
  for (struct source_s* source : archive->sources) {
    source_free(source);
  }
  barc_free(archive->barc);
  archive_manifest_free(archive->manifest);
  free(archive);
}

int archive_load_configuration(struct archive_s* archive,
                               struct archive_config_s* config) {
  struct barc_config_s barc_config;
  barc_config.out_width = config->width;
  barc_config.out_height = config->height;
  barc_config.css_custom = config->css_custom;
  barc_config.css_preset = config->css_preset;
  barc_config.output_path = config->output_path;
  barc_config.video_framerate = 30; // TODO want this in a config file maybe?
  archive->source_path = config->source_path;
  archive->begin_offset = config->begin_offset;
  archive->end_offset = config->end_offset;
  int ret = barc_read_configuration(archive->barc, &barc_config);
  return ret;
}

int archive_main(struct archive_s* archive) {
  int ret = archive_open(archive);
  if (ret) {
    printf("failed to open archive %s", archive->source_path);
    return ret;
  }
  barc_open_outfile(archive->barc);
  if (ret) {
    printf("failed to open archive outfile");
    return ret;
  }

  double end_time = archive_get_finish_clock_time(archive);
  if (archive->end_offset > 0) {
    double duration = archive->end_offset - archive->begin_offset;
    end_time = fmin(end_time, duration);
  }

  double global_clock = 0;

  while (!ret && end_time > global_clock) {
    process_layout_events(archive, global_clock);
    setup_streams_for_tick(archive, global_clock);
    ret = barc_tick(archive->barc);
    global_clock = barc_get_current_clock(archive->barc);
    printf("{\"progress\": {\"complete\": %f, \"total\": %f }}\n",
           global_clock * 1000, end_time * 1000);
  }

  if (ret) {
    printf("problem in barc main loop. closing outfile and aborting. ret=%d",
           ret);
    // don't let this condition stop us from closing the file.
  }
  int fret = barc_close_outfile(archive->barc);
  if (fret) {
    printf("failed to finalize container (ret %d", fret);
  }
  return ret & fret;
}

/* globerr --- print error message for glob() */
int globerr(const char *path, int eerrno)
{
    printf("%s: %s\n", path, strerror(eerrno));
    return 0;	/* let glob() keep going */
}

int ends_with(const char *str, const char *suffix)
{
  if (!str || !suffix)
    return 0;
  size_t lenstr = strlen(str);
  size_t lensuffix = strlen(suffix);
  if (lensuffix >  lenstr)
    return 0;
  return strncmp(str + lenstr - lensuffix, suffix, lensuffix) == 0;
}

static void open_manifest_item(const struct archive_manifest_s* manifest,
                               const struct manifest_file_s* file, void* p)
{
  struct archive_s* pthis = (struct archive_s*)p;
  struct source_s* source = NULL;
  int ret = 0;
  if (ends_with(file->filename, ".webm") ||
      ends_with(file->filename, ".mkv")) {
    struct webm_source_s* file_source;
    ret = webm_source_open(&file_source, file->filename,
                                     file->start_time_offset,
                                     file->stop_time_offset,
                                     file->stream_id,
                                     file->stream_class);
    source = webm_source_get_container(file_source);
    if (pthis->begin_offset > 0) {
      webm_source_seek(file_source, pthis->begin_offset);
    }
  } else {
    printf("%s does not look like a webm. attempting to open as an image\n",
           file->filename);
    struct image_source_s* image_source;
    ret = image_source_create(&image_source, file->filename,
                              file->start_time_offset,
                              file->stop_time_offset,
                              file->stream_id,
                              file->stream_class);
    source = image_source_get_container(image_source);
  }
  if (ret) {
    printf("failed to open archive stream source %s\n", file->filename);
    return;
  }

  if (source) {
    pthis->sources.push_back(source);
  }
  printf("opened archive stream source %s\n", file->filename);
}

static void register_layout_event(const struct archive_manifest_s* manifest,
                                  const struct layout_event_s* event,
                                  void* p)
{
  struct archive_s* pthis = (struct archive_s*)p;
  double event_offset =
  (event->created_at - archive_manifest_get_created_at(manifest)) / 1000;
  if (event_offset > pthis->begin_offset) {
    pthis->events.push_back(event);
  }
}

static int archive_open(struct archive_s* archive)
{
  int ret;
  glob_t globbuf;
  ret = chdir(archive->source_path);
  if (ret) {
    printf("unknown path %s\n", archive->source_path);
    return -1;
  }
  glob("*.json", 0, globerr, &globbuf);

  if (!globbuf.gl_pathc) {
    printf("no json manifest found at %s\n", archive->source_path);
  }
  // use the first json file we find inside the archive zip (hopefully only)
  const char* manifest_path = globbuf.gl_pathv[0];

  ret = archive_manifest_parse(archive->manifest, manifest_path);
  if (ret) {
    printf("CRITICAL: failed to parse archive manifest.");
    return ret;
  }
  archive_manifest_files_walk(archive->manifest, open_manifest_item, archive);
  archive_manifest_events_walk(archive->manifest, register_layout_event,
                               archive);
  return 0;
}

#pragma mark - Internal utilities
static double archive_get_finish_clock_time(struct archive_s* archive)
{
    double finish_time = 0;
    for (struct source_s* source : archive->sources)
    {
        if (finish_time < source_get_stop_offset(source)) {
            finish_time = source_get_stop_offset(source);
        }
    }
    return finish_time;
}

static int setup_streams_for_tick(struct archive_s* archive, double clock_time)
{
  // find any streams that should present content on this tick
  for (struct source_s* stream : archive->sources) {
    struct barc_source_s barc_source;
    barc_source.media_stream = source_get_media_stream(stream);
    if (source_is_active_at_time(stream, clock_time + archive->begin_offset))
    {
      barc_add_source(archive->barc, &barc_source);
    } else {
      barc_remove_source(archive->barc, &barc_source);
    }
  }
  return 0;
}

static void handle_layout_event(struct archive_s* pthis,
                                const struct layout_event_s* event)
{
  if (layout_changed_event == event->action) {
    barc_set_css_preset(pthis->barc, event->layout_changed.type);
    barc_set_custom_css(pthis->barc, event->layout_changed.stylesheet);
  } else if (stream_changed_event == event->action) {
    for (struct source_s* source : pthis->sources) {
      struct media_stream_s* stream = source_get_media_stream(source);
      const char* stream_id = media_stream_get_name(stream);
      if (!strcmp(stream_id, event->stream_changed.stream_id)) {
        media_stream_set_class(stream, event->stream_changed.layout_class);
      }
    }
  }
}

static void process_layout_events(struct archive_s* pthis,
                                  double clock_time)
{
  double event_offset;
  for (const struct layout_event_s* event : pthis->events) {
    event_offset = (event->created_at -
    archive_manifest_get_created_at(pthis->manifest)) / 1000;
    // compensate for begin offset
    event_offset -= pthis->begin_offset;
    if (event_offset < clock_time) {
      handle_layout_event(pthis, event);
      auto index = std::find(pthis->events.begin(), pthis->events.end(), event);
      pthis->events.erase(index);
    }
  }
}
