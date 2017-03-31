//
//  archive_manifest.h
//  barc
//
//  Created by Charley Robinson on 3/31/17.
//

#ifndef archive_manifest_h
#define archive_manifest_h

struct archive_manifest_s;

struct manifest_file_s {
  const char* connection_data;
  const char* filename;
  size_t size;
  double start_time_offset;
  double stop_time_offset;
  const char* stream_id;
  const char* video_type;
  // initial value only. may be changed with layout events.
  const char* stream_class;
};

enum layout_event_action {
  layout_changed_event,
  stream_changed_event
};

struct layout_changed_s {
  const char* type;
  const char* stylesheet;
};

struct stream_changed_s {
  const char* stream_id;
  const char* video_type;
  const char** layout_class_list;
  size_t num_layout_classes;
};

struct layout_event_s {
  const double created_at;
  const enum layout_event_action action;
  union {
    const struct layout_changed_s layout_changed;
    const struct stream_changed_s stream_changed;
  };
};

typedef void (manifest_files_walk_f)
(struct archive_manifest_s* manifest, struct manifest_file_s* file, void* p);
typedef void (manifest_layout_events_walk_f)
(struct archive_manifest_s* manifest, struct layout_event_s* event, void* p);

/**
 * Parser for individual stream archive manifest json
 */
void archive_manifest_alloc(struct archive_manifest_s** manifest_out);
void archive_manifest_free(struct archive_manifest_s* manifest);
int archive_manifest_parse(struct archive_manifest_s* manifest,
                           const char* path);
/**
 * @return the number of files walked.
 */
int archive_manifest_files_walk(struct archive_manifest_s* manifest,
                                manifest_files_walk_f* walk_f, void* parg);

int archive_manifest_events_walk(struct archive_manifest_s* manifest,
                                 manifest_layout_events_walk_f* walk_f,
                                 void* parg);
double archive_manifest_get_created_at(struct archive_manifest_s* manifest);
const char* archive_manifest_get_id(struct archive_manifest_s* manifest);
const char* archive_manifest_get_name(struct archive_manifest_s* manifest);
const char* archive_manifest_get_session_id
(struct archive_manifest_s* manifest);

#endif /* archive_manifest_h */
