//
//  archive_package.c
//  barc
//
//  Created by Charley Robinson on 1/26/17.
//  Copyright © 2017 TokBox, Inc. All rights reserved.
//

#include <vector>

#include "Geometry.h"

extern "C" {
#include "archive_package.h"
#include "archive_stream.h"
#include <jansson.h>
#include <glob.h>
#include <unistd.h>
}

struct archive_t {
    FILE* source_file;
    std::vector<struct archive_stream_t*> streams;
    ArchiveLayout* layout;
    int width;
    int height;
};

/* globerr --- print error message for glob() */

int globerr(const char *path, int eerrno)
{
    printf("%s: %s\n", path, strerror(eerrno));
    return 0;	/* let glob() keep going */
}

int open_manifest_item(struct archive_stream_t** stream, json_t* item) {
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
    json_int_t start = json_integer_value(node);

    node = json_object_get(item, "stopTimeOffset");
    if (!json_is_integer(node)) {
        printf("unable to parse stop time!\n");
        return -1;
    }
    json_int_t stop = json_integer_value(node);

    node = json_object_get(item, "streamId");
    if (!json_is_string(node)) {
        printf("unable to parse streamid!\n");
        return -1;
    }
    const char* stream_id = json_string_value(node);
    ret = archive_stream_open(stream, filename_str, start, stop, stream_id, "");
    printf("parsed archive stream %s\n", filename_str);
    return ret;
}

int archive_open(struct archive_t** archive_out, int width, int height,
                 const char* path)
{
    int ret;
    glob_t globbuf;
    ret = chdir(path);
    if (ret) {
        printf("unknown path %s\n", path);
        return -1;
    }
    glob("*.json", 0, globerr, &globbuf);

    if (!globbuf.gl_pathc) {
        printf("no json manifest found at %s\n", path);
    }
    // use the first one we find
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
    struct archive_stream_t* archive_stream;
    struct archive_t* archive =
    (struct archive_t*) calloc(1, sizeof(struct archive_t));
    *archive_out = archive;

    archive->streams = std::vector<struct archive_stream_t*>();
    archive->width = width;
    archive->height = height;

    json_array_foreach(files, index, value) {
        ret = open_manifest_item(&archive_stream, value);
        if (!ret) {
            archive->streams.push_back(archive_stream);
        }
    }

    archive->layout = new ArchiveLayout(width, height);
    archive->layout->setStyleSheet(Layout::kBestfitCss);

    return 0;
}

int archive_free(struct archive_t* archive) {
    // free streams & vector
    // free layout
    // free root struct
    free(archive);
    return 0;
}

void archive_set_output_video_fps(struct archive_t* archive, int fps) {
    for (struct archive_stream_t* stream : archive->streams) {
        archive_stream_set_output_video_fps(stream, fps);
    }
}

int archive_populate_stream_coords(struct archive_t* archive,
                                   int64_t clock_time,
                                   AVRational clock_time_base)
{
    // Regenerate stream list every tick to allow on-the-fly layout changes
    std::vector<ArchiveStreamInfo> stream_info;
    for (struct archive_stream_t* stream : archive->streams) {
        if (archive_stream_is_active_at_time(stream, clock_time) &&
            archive_stream_has_video_for_time(stream, clock_time,
                                              clock_time_base))
        {
            stream_info.push_back
            (ArchiveStreamInfo(archive_stream_get_name(stream),
                               archive_stream_get_class(stream),
                               true));
        }
    }
    
    StreamPositions positions = archive->layout->layout(stream_info);
    for (ComposerLayoutStreamPosition position : positions) {
        for (struct archive_stream_t* stream : archive->streams) {
            if (!strcmp(position.stream_id.c_str(),
                        archive_stream_get_name(stream))) {
                archive_stream_set_offset_x(stream, position.x);
                archive_stream_set_offset_y(stream, position.y);
                archive_stream_set_render_width(stream, position.width);
                archive_stream_set_render_height(stream, position.height);
            }
        }
    }
    return 0;
}

int64_t archive_get_finish_clock_time(struct archive_t* archive)
{
    int64_t finish_time = 0;
    for (struct archive_stream_t* stream : archive->streams)
    {
        if (finish_time < archive_stream_get_stop_offset(stream)) {
            finish_time = archive_stream_get_stop_offset(stream);
        }
    }
    return finish_time;
}

int archive_get_active_streams_for_time(struct archive_t* archive,
                                        int64_t clock_time,
                                        struct archive_stream_t*** streams_out,
                                        int* num_streams_out)
{
    std::vector<struct archive_stream_t*> result;
    // find any streams that should present content on this tick
    for (struct archive_stream_t* stream : archive->streams) {
        if (archive_stream_is_active_at_time(stream, clock_time)) {
            result.push_back(stream);
        }
    }
    *num_streams_out = (int) result.size();
    // convert to c-style array (isn't there a vector function for this??)
    struct archive_stream_t** streams_arr =
    (struct archive_stream_t**)calloc(*num_streams_out,
                                      sizeof(struct archive_stream_t*));
    for (int i = 0; i < *num_streams_out; i++) {
        streams_arr[i] = result[i];
    }
    *streams_out = streams_arr;
    return 0;
}

int archive_free_active_streams(struct archive_stream_t* streams)
{
    free(streams);
    return 0;
}
