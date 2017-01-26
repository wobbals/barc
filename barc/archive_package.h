//
//  archive_package.h
//  barc
//
//  Created by Charley Robinson on 1/26/17.
//  Copyright © 2017 TokBox, Inc. All rights reserved.
//

#ifndef archive_package_h
#define archive_package_h

#include <stdio.h>

struct archive_t {
    FILE* src;
};

void open_archive(FILE* input);

#endif /* archive_package_h */
