//
//  test_manifest_parser.c
//  barc
//
//  Created by Charley Robinson on 4/1/17.
//

extern "C" {
#include "archive_manifest.h"
}

#include "gtest/gtest.h"

TEST(ManifestParser, AllocManifest) {
  struct archive_manifest_s* manifest = NULL;
  archive_manifest_alloc(&manifest);
  EXPECT_TRUE(manifest != NULL);
  archive_manifest_free(manifest);
}

// these values pulled from test_manifest.json
#define TEST_SESSION_ID "SAMPLE_SESSION_ID"
#define TEST_CREATED_AT 1490984250000
#define TEST_ARCHIVE_NAME "SAMPLE_ARCHIVE_NAME"
#define TEST_ARCHIVE_ID "145e08ee-8ba5-4063-8e48-30b4e59318ca"
#define NUM_TEST_FILES 6
#define NUM_TEST_EVENTS 3
#define FILE_NAME_LENGTH 41
#define STREAM_ID_LENGTH 36

static void files_walk(const struct archive_manifest_s* manifest,
                       const struct manifest_file_s* file,
                       void* p)
{
  EXPECT_TRUE(p == manifest);
  EXPECT_TRUE(NULL == file->connection_data);
  EXPECT_TRUE(FILE_NAME_LENGTH == strlen(file->filename));
  EXPECT_TRUE(STREAM_ID_LENGTH == strlen(file->stream_id));
  EXPECT_GT(file->size, 0);
  EXPECT_GT(file->start_time_offset, 0);
  EXPECT_GT(file->stop_time_offset, 0);
  EXPECT_GT(file->stop_time_offset, file->start_time_offset);
  EXPECT_TRUE(NULL != file->video_type);
  EXPECT_FALSE(strlen(file->video_type));
  EXPECT_TRUE(NULL != file->stream_class);
  EXPECT_FALSE(strlen(file->stream_class));
}

static void events_walk(const struct archive_manifest_s* manifest,
                        const struct layout_event_s* event, void* p)
{
  EXPECT_TRUE(p == manifest);
  // TODO: Add test asserts once we're sure this is the API for layout events
}

TEST(ManifestParser, LoadManifest) {
  struct archive_manifest_s* manifest = NULL;
  archive_manifest_alloc(&manifest);
  EXPECT_TRUE(manifest != NULL);
  int ret = archive_manifest_parse(manifest, "/tmp/test_manifest.json");
  EXPECT_TRUE(0 == ret);
  EXPECT_FALSE(strcmp(TEST_SESSION_ID,
                      archive_manifest_get_session_id(manifest)));
  EXPECT_FALSE(strcmp(TEST_ARCHIVE_NAME, archive_manifest_get_name(manifest)));
  EXPECT_FALSE(strcmp(TEST_ARCHIVE_ID, archive_manifest_get_id(manifest)));
  EXPECT_TRUE(archive_manifest_get_created_at(manifest) == TEST_CREATED_AT);
  ret = archive_manifest_files_walk(manifest, files_walk, manifest);
  EXPECT_TRUE(NUM_TEST_FILES == ret);
  ret = archive_manifest_events_walk(manifest, events_walk, manifest);
  EXPECT_TRUE(NUM_TEST_EVENTS == ret);
  archive_manifest_free(manifest);
}

TEST(ManifestParser, LoadManifestInvalidPath) {
  struct archive_manifest_s* manifest = NULL;
  archive_manifest_alloc(&manifest);
  EXPECT_TRUE(manifest != NULL);
  int ret = archive_manifest_parse(manifest, "foo");
  EXPECT_TRUE(ret != 0);
  archive_manifest_free(manifest);
}
