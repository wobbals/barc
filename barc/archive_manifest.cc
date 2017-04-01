//
//  archive_manifest.c
//  barc
//
//  Created by Charley Robinson on 3/31/17.
//

extern "C" {
#include <stdlib.h>
#include <string.h>
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
  std::vector<struct layout_event_s*> layout_events;
};

void archive_manifest_alloc(struct archive_manifest_s** manifest_out) {
  struct archive_manifest_s* pthis = (struct archive_manifest_s*)
  calloc(1, sizeof(struct archive_manifest_s));
  pthis->files = std::vector<struct manifest_file_s*>();
  pthis->layout_events = std::vector<struct layout_event_s*>();
  *manifest_out = pthis;
}
void archive_manifest_free(struct archive_manifest_s* pthis) {
  for (struct manifest_file_s* file : pthis->files) {
    free(file);
  }
  for (struct layout_event_s* event : pthis->layout_events) {
    if (stream_changed_event == event->action) {
      free((void*)event->stream_changed.layout_class);
    }
    free(event);
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
  if (!json_is_number(node)) {
    printf("unable to parse start time!\n");
    return -1;
  }
  file->start_time_offset =
  (double) json_number_value(node) / MANIFEST_TIME_BASE;

  node = json_object_get(json, "stopTimeOffset");
  if (!json_is_number(node)) {
    printf("unable to parse stop time!\n");
    return -1;
  }
  file->stop_time_offset =
  (double) json_number_value(node) / MANIFEST_TIME_BASE;

  node = json_object_get(json, "size");
  if (!json_is_number(node)) {
    printf("unable to parse size!\n");
    return -1;
  }
  file->size = (double) json_number_value(node);

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

int parse_layout_changed_event(struct layout_event_s* event, json_t* json) {
  json_t* layout_json = json_object_get(json, "layout");
  if (!json_is_object(layout_json)) {
    printf("missing layout changed event data\n");
    return -1;
  }
  json_t* node = json_object_get(layout_json, "type");
  if (!json_is_string(node)) {
    printf("unable to parse layout changed event type\n");
    return -1;
  }
  event->layout_changed.type = json_string_value(node);

  node = json_object_get(layout_json, "stylesheet");
  if (!json_is_string(node)) {
    //optional field. do not fail if missing.
    printf("missing layout changed event stylesheet\n");
  } else {
    event->layout_changed.stylesheet = json_string_value(node);
  }

  return 0;
}

int parse_stream_changed_event(struct layout_event_s* event, json_t* json) {
  json_t* stream_json = json_object_get(json, "stream");
  if (!json_is_object(stream_json)) {
    printf("missing stream changed event data\n");
    return -1;
  }
  json_t* node = json_object_get(stream_json, "id");
  if (!json_is_string(node)) {
    printf("unable to parse stream changed event id\n");
    return -1;
  }
  event->stream_changed.stream_id = json_string_value(node);
  node = json_object_get(stream_json, "videoType");
  if (!json_is_string(node)) {
    // optional field
    printf("unable to parse stream changed event videoType\n");
  } else {
    event->stream_changed.video_type = json_string_value(node);
  }
  node = json_object_get(stream_json, "layoutClassList");
  if (!json_is_array(node)) {
    // optional field
    printf("missing stream event changed layoutClassList\n");
  } else {
    size_t num_classes = json_array_size(node);
    json_t* value;
    int index;
    size_t concatenated_strlen = 0;
    std::vector<const char*> classes;
    // two-pass: calculate total string length
    json_array_foreach(node, index, value) {
      if (!json_is_string(value)) {
        printf("non-string stream changed event layout class\n");
        continue;
      }
      const char* sz_class = json_string_value(value);
      concatenated_strlen += strlen(sz_class) + 1;
      classes.push_back(sz_class);
    }
    // then join the array of strings with a space
    index = 0;
    char* result = (char*) calloc(concatenated_strlen, sizeof(char));
    for (const char* sz_class : classes) {
      strcpy(&(result[index]), sz_class);
      index += strlen(sz_class);
      result[index++] = ' ';
    }
    // null terminate the string
    result[index - 1] = '\0';
    event->stream_changed.layout_class = (const char*)result;
  }
  return 0;
}

int parse_event(struct layout_event_s* event, json_t* json) {
  json_t* node = json_object_get(json, "createdAt");
  if (!json_is_number(node)) {
    printf("unable to parse layout event createdAt\n");
    return -1;
  }
  event->created_at = json_number_value(node);

  node = json_object_get(json, "action");
  if (!json_is_string(node)) {
    printf("unable to parse layout event action\n");
    return -1;
  }
  const char* action = json_string_value(node);
  if (!strcmp("layoutChanged", action)) {
    event->action = layout_changed_event;
  } else if (!strcmp("streamChanged", action)) {
    event->action = stream_changed_event;
  } else {
    event->action = unknown_event;
  }

  int ret;
  switch(event->action) {
    case layout_changed_event:
      ret = parse_layout_changed_event(event, json);
      break;
    case stream_changed_event:
      ret = parse_stream_changed_event(event, json);
      break;
    default:
      printf("unable to parse unknown layout event\n");
      ret = -1;
      break;
  }
  
  return ret;
}

int archive_manifest_parse(struct archive_manifest_s* pthis,
                           const char* path)
{
  json_t* json;
  json_error_t error;

  json = json_load_file(path, 0, &error);
  if (!json) {
    printf("Unable to parse json manifest: line %d: %s\n",
           error.line, error.text);
    return 1;
  }
  json_t* files = json_object_get(json, "files");

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
      printf("warning: unable to parse file in archive manifest\n");
      free(file);
    } else {
      pthis->files.push_back(file);
    }
  }

  json_t* layout_events_json = json_object_get(json, "layoutEvents");

  if (!json_is_array(layout_events_json)) {
    // field is optional: not present in production currently
    printf("No layout events declared in manifest\n");
  } else {
    json_array_foreach(layout_events_json, index, item) {
      struct layout_event_s* event = (struct layout_event_s*)
      calloc(1, sizeof(layout_event_s));
      int ret = parse_event(event, item);
      if (ret) {
        printf("warning: unable to parse layout event in archive manifest\n");
        free(event);
      } else {
        pthis->layout_events.push_back(event);
      }
    }
  }

  json_t* node = json_object_get(json, "id");
  if (!json_is_string(node)) {
    printf("warning: unable to parse archive id!\n");
  } else {
    pthis->archive_id = json_string_value(node);
  }

  node = json_object_get(json, "name");
  if (!json_is_string(node)) {
    printf("warning: unable to parse archive id!\n");
    return -1;
  }
  pthis->name = json_string_value(node);

  node = json_object_get(json, "sessionId");
  if (!json_is_string(node)) {
    printf("warning: unable to parse archive id!\n");
    return -1;
  }

  pthis->session_id = json_string_value(node);

  node = json_object_get(json, "createdAt");
  if (!json_is_number(node)) {
    printf("warning: unable to createdAt!\n");
    return -1;
  }
  pthis->created_at = json_number_value(node);

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

int archive_manifest_events_walk(struct archive_manifest_s* pthis,
                                 manifest_layout_events_walk_f* walk_f,
                                 void* parg)
{
  for (struct layout_event_s* event : pthis->layout_events) {
    walk_f(pthis, (const struct layout_event_s*) event, parg);
  }
  return (int) pthis->layout_events.size();
}

double archive_manifest_get_created_at
(const struct archive_manifest_s* manifest)
{
  return manifest->created_at;
}

const char* archive_manifest_get_id
(const struct archive_manifest_s* manifest)
{
  return manifest->archive_id;
}

const char* archive_manifest_get_name
(const struct archive_manifest_s* manifest)
{
  return manifest->name;
}

const char* archive_manifest_get_session_id
(const struct archive_manifest_s* manifest)
{
  return manifest->session_id;
}
