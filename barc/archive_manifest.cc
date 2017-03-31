//
//  archive_manifest.c
//  barc
//
//  Created by Charley Robinson on 3/31/17.
//

extern "C" {
#include <stdlib.h>
#include <jansson.h>
#include "archive_manifest.h"
}

#include <vector>

struct archive_manifest_s {
  const char* archive_id;
  const char* name;
  const char* session_id;
  double created_at;
  std::vector<struct manifest_file_s*> files;
};

void archive_manifest_alloc(struct archive_manifest_s** manifest_out) {
  struct archive_manifest_s* pthis = (struct archive_manifest_s*)
  calloc(1, sizeof(struct archive_manifest_s));
  pthis->files = std::vector<struct manifest_file_s*>();
  *manifest_out = pthis;
}
void archive_manifest_free(struct archive_manifest_s* pthis) {
  for(struct manifest_file_s* file : pthis->files) {
    free(file);
  }
  pthis->files.clear();
  free(pthis);
}

// we know from documentation time units are in millis
#define MANIFEST_TIME_BASE 1000
int parse_file(struct manifest_file_s* file, json_t* json) {
  json_t* node = json_object_get(json, "filename");
  if (!json_is_string(node)) {
    printf("unable to parse filename!\n");
    return -1;
  }
  file->filename = json_string_value(node);

  node = json_object_get(json, "startTimeOffset");
  if (!json_is_integer(node)) {
    printf("unable to parse start time!\n");
    return -1;
  }
  file->start_time_offset =
  (double) json_integer_value(node) / MANIFEST_TIME_BASE;

  node = json_object_get(json, "stopTimeOffset");
  if (!json_is_integer(node)) {
    printf("unable to parse stop time!\n");
    return -1;
  }
  file->stop_time_offset =
  (double) json_integer_value(node) / MANIFEST_TIME_BASE;

  node = json_object_get(json, "streamId");
  if (!json_is_string(node)) {
    printf("unable to parse streamid!\n");
    return -1;
  }
  file->stream_id = json_string_value(node);

  const char* stream_class = "";
  node = json_object_get(json, "layoutClass");
  if (json_is_string(node)) {
    stream_class = json_string_value(node);
  }

  const char* video_type = "";
  node = json_object_get(json, "videoType");
  if (json_is_string(node)) {
    video_type = json_string_value(node);
  }
  file->video_type = video_type;
  // hack: automatically set stream class 'focus' if
  // 1) undefined in manifest AND
  // 2) videoType is 'screen' (eg. probably a screenshare)
  // TODO: Consider overriding object-fit properties for these stream types
  if (!strcmp(video_type, "screen") && !strlen(stream_class)) {
    stream_class = "focus";
  }
  file->stream_class = stream_class;
  return 0;
}

int archive_manifest_parse(struct archive_manifest_s* pthis,
                           const char* path)
{
  json_t* manifest;
  json_error_t error;

  manifest = json_load_file(path, 0, &error);
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
  json_t* item;

  json_array_foreach(files, index, item) {
    struct manifest_file_s* file = (struct manifest_file_s*)
    calloc(1, sizeof(manifest_file_s));
    int ret = parse_file(file, item);
    if (ret) {
      printf("warning! unable to parse file in arhive manifest\n");
      free(file);
    } else {
      pthis->files.push_back(file);
    }
  }

  return 0;
}

int archive_manifest_files_walk(struct archive_manifest_s* pthis,
                                manifest_files_walk_f* walk_f, void* parg)
{
  for (struct manifest_file_s* file : pthis->files) {
    walk_f(pthis, file, parg);
  }
  return (int) pthis->files.size();
}

int archive_manifest_events_walk(struct archive_manifest_s* manifest,
                                 manifest_layout_events_walk_f* walk_f,
                                 void* parg)
{
  return 0;
}

double archive_manifest_get_created_at(struct archive_manifest_s* manifest) {
  return manifest->created_at;
}

const char* archive_manifest_get_id(struct archive_manifest_s* manifest) {
  return manifest->archive_id;
}

const char* archive_manifest_get_name(struct archive_manifest_s* manifest) {
  return manifest->name;
}

const char* archive_manifest_get_session_id
(struct archive_manifest_s* manifest)
{
  return manifest->session_id;
}
