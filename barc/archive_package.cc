//
//  archive_package.c
//  barc
//
//  Created by Charley Robinson on 1/26/17.
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
    char auto_layout;
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

    ret = archive_stream_open_file(stream, filename_str, start, stop,
                              stream_id, stream_class);
    printf("parsed archive stream %s\n", filename_str);
    return ret;
}

int archive_open(struct archive_t** archive_out, int width, int height,
                 const char* path,
                 const char* css_preset, const char* css_custom,
                 const char* manifest_supplemental_path)
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

    archive->auto_layout = 0;

    std::string style_sheet;
    if (NULL == css_preset) {
        printf("No stylesheet preset defined. Using auto.");
        archive->auto_layout = 1;
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
        style_sheet = css_custom;
    } else if (!strcmp("auto", css_preset)) {
        archive->auto_layout = 1;
        style_sheet = Layout::kBestfitCss;
    } else {
        printf("unknown css preset defined. Using auto.");
        archive->auto_layout = 1;
        style_sheet = Layout::kBestfitCss;
    }

    archive->layout->setStyleSheet(style_sheet);

    return 0;
}

int archive_free(struct archive_t* archive) {
    // free streams & vector
    // free layout
    // free root struct
    free(archive);
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

bool z_index_sort(const struct archive_stream_t* stream1,
                   const struct archive_stream_t* stream2)
{
    return (archive_stream_get_z_index(stream1) <
            archive_stream_get_z_index(stream2));
}

int archive_get_active_streams_for_time(struct archive_t* archive,
                                        int64_t clock_time,
                                        AVRational time_base,
                                        struct archive_stream_t*** streams_out,
                                        int* num_streams_out)
{
    std::vector<struct archive_stream_t*> result;
    // find any streams that should present content on this tick
    for (struct archive_stream_t* stream : archive->streams) {
        if (archive_stream_is_active_at_time(stream, clock_time, time_base)) {
            result.push_back(stream);
        }
    }
    std::sort(result.begin(), result.end(), z_index_sort);
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
