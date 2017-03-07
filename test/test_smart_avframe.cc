//
//  test_smart_avframe.c
//  barc
//
//  Created by Charley Robinson on 3/4/17.
//

extern "C" {
#include "smart_avframe.h"
}

#include "gtest/gtest.h"

// A simple test to get things started
TEST(SmartFrame, AllocSmartFrame) {
    AVFrame* frame = av_frame_alloc();
    EXPECT_TRUE(frame != NULL);
    struct smart_frame_t* smart_frame;
    smart_frame_create(&smart_frame, frame);
    EXPECT_TRUE(NULL != smart_frame);
    AVFrame* another_frame = smart_frame_get(smart_frame);
    EXPECT_TRUE(another_frame == frame);
    smart_frame_release(smart_frame);
}
