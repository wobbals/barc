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

#include "archive_package.h"
#include "file_media_source.h"
#include "barc.h"
}

#include <vector>

#include "Geometry.h"

struct archive_s {
  struct barc_s* barc;
  std::vector<struct file_media_source_s*> sources;
  const char* source_path;
  double begin_offset;
  double end_offset;
};

static int archive_open(struct archive_s* archive);
static double archive_get_finish_clock_time(struct archive_s* archive);
static int setup_streams_for_tick(struct archive_s* archive, double clock_time);

void archive_alloc(struct archive_s** archive_out) {
  struct archive_s* archive = (struct archive_s*)
  calloc(1, sizeof(struct archive_s));
  barc_alloc(&archive->barc);
  *archive_out = archive;
}

void archive_free(struct archive_s* archive) {
  barc_free(archive->barc);
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
    end_time = fmin(end_time, archive->end_offset);
  }

  double global_clock = 0;

  while (!ret && end_time > global_clock) {
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

// we know from documentation these units are in millis
#define MANIFEST_TIME_BASE 1000
static int open_manifest_item(struct file_media_source_s** source_out,
                              json_t* item)
{
    int ret;
    json_t* node = json_object_get(item, "filename");
    if (!json_is_string(node)) {
        printf("unable to parse filename!\n");
        return -1;
    }
    const char* filename_str = json_string_value(node);

    node = json_object_get(item, "startTimeOffset");
    if (!json_is_integer(node)) {
        printf("unable to parse start time!\n");
        return -1;
    }
    double start = (double) json_integer_value(node) / MANIFEST_TIME_BASE;

    node = json_object_get(item, "stopTimeOffset");
    if (!json_is_integer(node)) {
        printf("unable to parse stop time!\n");
        return -1;
    }
    double stop = (double) json_integer_value(node) / MANIFEST_TIME_BASE;

    node = json_object_get(item, "streamId");
    if (!json_is_string(node)) {
        printf("unable to parse streamid!\n");
        return -1;
    }
    const char* stream_id = json_string_value(node);

    const char* stream_class = "";
    node = json_object_get(item, "layoutClass");
    if (json_is_string(node)) {
        stream_class = json_string_value(node);
    }

    const char* video_type = "";
    node = json_object_get(item, "videoType");
    if (json_is_string(node)) {
        video_type = json_string_value(node);
    }
    // hack: automatically set stream class 'focus' if
    // 1) undefined in manifest AND
    // 2) videoType is 'screen' (eg. probably a screenshare)
    // TODO: Consider overriding object-fit properties for these stream types
    if (!strcmp(video_type, "screen") && !strlen(stream_class)) {
        stream_class = "focus";
    }

    ret = file_media_source_open(source_out, filename_str, start, stop,
                                 stream_id, stream_class);
    printf("parsed archive stream %s\n", filename_str);
    return ret;
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
    json_t *manifest;
    json_error_t error;

    manifest = json_load_file(manifest_path, 0, &error);
    if (!manifest) {
        printf("Unable to parse json manifest: line %d: %s\n",
               error.line, error.text);
        return 1;
    }
    json_t* files = json_object_get(manifest, "files");

    if (!json_is_array(files)) {
        printf("No files declared in manifest\n");
        return 1;
    }
    size_t index;
    json_t *value;
    struct file_media_source_s* file_source;

    json_array_foreach(files, index, value) {
        ret = open_manifest_item(&file_source, value);
        if (!ret) {
            archive->sources.push_back(file_source);
        }
    }

    return 0;
}
#pragma mark - Internal utilities
static double archive_get_finish_clock_time(struct archive_s* archive)
{
    double finish_time = 0;
    for (struct file_media_source_s* source : archive->sources)
    {
        if (finish_time < file_stream_get_stop_offset(source)) {
            finish_time = file_stream_get_stop_offset(source);
        }
    }
    return finish_time;
}

static int setup_streams_for_tick(struct archive_s* archive, double clock_time)
{
  // find any streams that should present content on this tick
  for (struct file_media_source_s* stream : archive->sources) {
    struct barc_source_s barc_source;
    barc_source.media_stream = file_media_source_get_stream(stream);
    if (file_stream_is_active_at_time(stream, clock_time)) {
      barc_add_source(archive->barc, &barc_source);
    } else {
      barc_remove_source(archive->barc, &barc_source);
    }
  }
  return 0;
}
