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
};

int open_archive(struct archive_t** archive_out)
{
    int ret;
    const char* first = "/Users/charley/src/barc/sample/388da791-581a-4719-a964-49a23b877e97.webm";
    const char* second = "/Users/charley/src/barc/sample/def26e29-eb3f-4472-b89a-feb27342acb7.webm";
    const char* third = "/Users/charley/src/barc/sample/dda29d9c-8b03-45cb-b9f5-375c8331532f.webm";
    struct archive_stream_t* archive_stream;
    struct archive_t* archive = (struct archive_t*) calloc(1, sizeof(struct archive_t));
    *archive_out = archive;

    archive->streams = std::vector<struct archive_stream_t*>();

    ret = archive_stream_open(&archive_stream, first, 9681, 29384, "388da791-581a-4719-a964-49a23b877e97");
    if (ret < 0)
    {
        printf("Error: failed to open %s\n", first);
        return(ret);
    }
    archive->streams.push_back(archive_stream);

    ret = archive_stream_open(&archive_stream, second, 220, 12756, "def26e29-eb3f-4472-b89a-feb27342acb7");
    if (ret < 0)
    {
        printf("Error: failed to open %s\n", second);
        return(ret);
    }
    archive->streams.push_back(archive_stream);

    ret = archive_stream_open(&archive_stream, third, 220, 12756, "dda29d9c-8b03-45cb-b9f5-375c8331532f");
    if (ret < 0)
    {
        printf("Error: failed to open %s\n", third);
        return(ret);
    }
    archive->streams.push_back(archive_stream);

    archive->layout = new ArchiveLayout(1280, 720);
    archive->layout->setStyleSheet(Layout::kBestfitCss);

    return 0;
}

int free_archive(struct archive_t* archive) {
    // free streams & vector
    // free layout
    // free root struct
    free(archive);
    return 0;
}

int populate_stream_coords(struct archive_t* archive, int64_t global_clock)
{
    std::vector<ArchiveStreamInfo> stream_info;
    for (struct archive_stream_t* stream : archive->streams) {
        stream_info.push_back(ArchiveStreamInfo(stream->sz_name, true));
    }
    StreamPositions positions = archive->layout->layout(stream_info);
    for (ComposerLayoutStreamPosition position : positions) {
        // push positions back to stream
    }
    return 0;
}
