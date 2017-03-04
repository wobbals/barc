//
//  zipper.c
//  barc
//
//  Created by Charley Robinson on 2/10/17.
//

#define _XOPEN_SOURCE 500

#include "zipper.h"
#include <stdlib.h>
#include <unistd.h>
#include <zip.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/errno.h>
#include <ftw.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>

int unlink_cb(const char *fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf)
{
    int rv = remove(fpath);

    if (rv)
        perror(fpath);

    return rv;
}

int rmrf(const char *path)
{
    return nftw(path, unlink_cb, 64, FTW_DEPTH | FTW_PHYS);
}

static void safe_create_dir(const char *dir)
{
    if (mkdir(dir, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) < 0) {
        if (errno != EEXIST) {
            perror(dir);
            exit(1);
        }
    }
}

char* unzip_archive(const char* path) {
    int err, i, fd;
    int64_t sum, len;
    struct zip_file *zf;
    struct zip_stat sb;
    char buf[100];
    struct zip* za = zip_open(path, ZIP_CHECKCONS | ZIP_RDONLY, &err);
    if (NULL == za) {
        zip_error_to_str(buf, sizeof(buf), err, errno);
        printf("can't open zip archive `%s': %s/n",
               path, buf);
        return NULL;
    }

    const char* working_directory = "out";
    rmrf(working_directory);
    safe_create_dir(working_directory);
    err = chdir(working_directory);

    for (i = 0; i < zip_get_num_entries(za, 0); i++) {
        if (zip_stat_index(za, i, 0, &sb) == 0) {
            printf("==================\n");
            len = strlen(sb.name);
            printf("Name: [%s], ", sb.name);
            printf("Size: [%llu], ", sb.size);
            printf("mtime: [%u]\n", (unsigned int)sb.mtime);
            if (sb.name[len - 1] == '/') {
                safe_create_dir(sb.name);
            } else {
                zf = zip_fopen_index(za, i, 0);
                if (!zf) {
                    fprintf(stderr, "boese, boese\n");
                    exit(100);
                }

                fd = open(sb.name, O_RDWR | O_TRUNC | O_CREAT, 0644);
                if (fd < 0) {
                    fprintf(stderr, "boese, boese\n");
                    exit(101);
                }

                sum = 0;
                while (sum != sb.size) {
                    len = zip_fread(zf, buf, 100);
                    if (len < 0) {
                        fprintf(stderr, "boese, boese\n");
                        exit(102);
                    }
                    write(fd, buf, len);
                    sum += len;
                }
                close(fd);
                zip_fclose(zf);
            }
        } else {
            printf("File[%s] Line[%d]/n", __FILE__, __LINE__);
        }
    }
    char* pwd = getcwd(NULL, 0);
    return pwd;
}

