//
//  test_file_writer.c
//  barc
//
//  Created by Charley Robinson on 3/16/17.
//

extern "C" {
#include <unistd.h>
#include "file_writer.h"
}

#include "gtest/gtest.h"

TEST(FileWriter, AllocFileWriter) {
  struct file_writer_t* file_writer = NULL;
  file_writer_alloc(&file_writer);
  EXPECT_TRUE(file_writer != NULL);
  file_writer_free(file_writer);
}

AVFrame* empty_audio_frame() {
  AVFrame* frame = av_frame_alloc();
  // TODO: Dig the format out of the file writer context for better flexibility
  frame->pts = 0;
  frame->format = AV_SAMPLE_FMT_FLTP;
  frame->nb_samples = 1024;
  frame->channel_layout = AV_CH_LAYOUT_MONO;
  frame->sample_rate = 48000;
  frame->channels = 1;
  frame->data[0] = NULL;
  EXPECT_TRUE(NULL == frame->data[0]);
  int ret = av_frame_get_buffer(frame, 0);
  EXPECT_TRUE(NULL != frame->data[0]);
  return frame;
}

/* This isn't working. I think it might be waiting for a video frame.
 * TODO: add a video frame.
TEST(FileWriter, WriteFrame) {
  av_register_all();
  avfilter_register_all();
  int ret;
  const char* outfile = "/tmp/output.mp4";
  struct file_writer_t* file_writer = NULL;
  file_writer_alloc(&file_writer);
  EXPECT_TRUE(NULL != file_writer);
  ret = file_writer_open(file_writer, outfile, 320, 240);
  EXPECT_TRUE(0 == ret);
  for (int i = 0; i < 1000; i++) {
    AVFrame* frame = empty_audio_frame();
    frame->pts = (i * 48000) / 20;
    // probably the filtergraph resetting the frame. is this safe?
    ret = file_writer_push_audio_frame(file_writer, frame);
    // write frames until the filter graph is happy
    if (0 == ret) {
      break;
    }
  }
  if (ret) printf("%s\n", av_err2str(ret));
  EXPECT_TRUE(0 == ret);
  ret = file_writer_close(file_writer);
  EXPECT_TRUE(0 == ret);
  EXPECT_TRUE(file_writer != NULL);
  file_writer_free(file_writer);

  unlink(outfile);
}
*/
