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

struct archive_t;

int open_archive(struct archive_t** archive_out);
int free_archive(struct archive_t* archive);


#endif /* archive_package_h */
