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
}

struct archive_t {
    FILE* source_file;
    std::vector<struct archive_stream_t*> streams;
    ArchiveLayout* layout;
    int width;
    int height;
};

int archive_open(struct archive_t** archive_out, int width, int height)
{
    int ret;
    const char* first = "/Users/charley/src/barc/sample/388da791-581a-4719-a964-49a23b877e97.webm";
    const char* second = "/Users/charley/src/barc/sample/def26e29-eb3f-4472-b89a-feb27342acb7.webm";
    const char* third = "/Users/charley/src/barc/sample/dda29d9c-8b03-45cb-b9f5-375c8331532f.webm";
    struct archive_stream_t* archive_stream;
    struct archive_t* archive = (struct archive_t*) calloc(1, sizeof(struct archive_t));
    *archive_out = archive;

    archive->streams = std::vector<struct archive_stream_t*>();
    archive->width = width;
    archive->height = height;

    ret = archive_stream_open(&archive_stream, first, 9681, 29384, "388da791-581a-4719-a964-49a23b877e97", "");
    if (ret < 0)
    {
        printf("Error: failed to open %s\n", first);
        return(ret);
    }
    archive->streams.push_back(archive_stream);

    ret = archive_stream_open(&archive_stream, second, 220, 12756, "def26e29-eb3f-4472-b89a-feb27342acb7", "");
    if (ret < 0)
    {
        printf("Error: failed to open %s\n", second);
        return(ret);
    }
    archive->streams.push_back(archive_stream);

    ret = archive_stream_open(&archive_stream, third, 17580, 24538, "dda29d9c-8b03-45cb-b9f5-375c8331532f", "focus");
    if (ret < 0)
    {
        printf("Error: failed to open %s\n", third);
        return(ret);
    }
    archive->streams.push_back(archive_stream);

    archive->layout = new ArchiveLayout(width, height);
    archive->layout->setStyleSheet(Layout::kHorizontalPresentation);

    return 0;
}

int archive_free(struct archive_t* archive) {
    // free streams & vector
    // free layout
    // free root struct
    free(archive);
    return 0;
}

int archive_populate_stream_coords(struct archive_t* archive,
                                   int64_t global_clock)
{
    // Regenerate stream list every tick to allow on-the-fly layout changes
    std::vector<ArchiveStreamInfo> stream_info;
    for (struct archive_stream_t* stream : archive->streams) {
        if (archive_stream_is_active_at_time(stream, global_clock)) {
            stream_info.push_back(ArchiveStreamInfo(stream->sz_name, stream->sz_class, true));
        }
    }
    
    StreamPositions positions = archive->layout->layout(stream_info);
    for (ComposerLayoutStreamPosition position : positions) {
        for (struct archive_stream_t* stream : archive->streams) {
            if (!strcmp(position.stream_id.c_str(), stream->sz_name)) {
                stream->x_offset = position.x;
                stream->y_offset = position.y;
                stream->render_width = position.width;
                stream->render_height = position.height;
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
        if (finish_time < stream->stop_offset) {
            finish_time = stream->stop_offset;
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
