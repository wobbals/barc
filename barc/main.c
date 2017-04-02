//
//  main.c
//  barc
//
//  Created by Charley Robinson on 1/26/17.
//

#include <unistd.h>
#include <ctype.h>
#include <getopt.h>

#include <libavformat/avformat.h>
#include <libavfilter/avfilter.h>
#include <assert.h>
#include <MagickWand/MagickWand.h>
#include <uv.h>

#include "barc.h"
#include "archive_package.h"
#include "zipper.h"

int main(int argc, char **argv)
{
    char* input_path = NULL;
    char* output_path = NULL;
    char* css_preset = NULL;
    char* css_custom = NULL;
    char* manifest_supplemental = NULL;
    int out_width = 0;
    int out_height = 0;
    int64_t begin_offset = -1;
    int64_t end_offset = -1;
    int c;

    static struct option long_options[] =
    {
        /* These options set a flag. */
        //{"repairmode", no_argument, &repairmode_flag, 0},
        /* These options don’t set a flag.
         We distinguish them by their indices. */
        {"input", required_argument,        0, 'i'},
        {"output", optional_argument,       0, 'o'},
        {"width", optional_argument,        0, 'w'},
        {"height", optional_argument,       0, 'h'},
        {"css_preset", optional_argument,   0, 'p'},
        {"custom_css", optional_argument,   0, 'c'},
        {"css_preset", optional_argument,   0, 'p'},
        {"begin_offset", optional_argument, 0, 'b'},
        {"end_offset", optional_argument,   0, 'e'},
        {0, 0, 0, 0}
    };
    /* getopt_long stores the option index here. */
    int option_index = 0;

    while ((c = getopt_long(argc, argv, "i:o:w:h:p:c:b:e:",
                            long_options, &option_index)) != -1)
    {
        switch (c)
        {
            case 'i':
                input_path = optarg;
                break;
            case 'o':
                output_path = optarg;
                break;
            case 'w':
                out_width = atoi(optarg);
                break;
            case 'h':
                out_height = atoi(optarg);
                break;
            case 'b':
                begin_offset = atoi(optarg);
                break;
            case 'e':
                end_offset = atoi(optarg);
                break;
            case 'p':
                css_preset = optarg;
                break;
            case 'c':
                css_custom = optarg;
                break;
            case '?':
                if (isprint (optopt))
                    fprintf (stderr, "Unknown option `-%c'.\n", optopt);
                else
                    fprintf (stderr,
                             "Unknown option character `\\x%x'.\n",
                             optopt);
                return 1;
            default:
                abort ();
        }
    }

    if (!output_path) {
        output_path = "output.mp4";
    }
    if (!out_width) {
        out_width = 640;
    }
    if (!out_height) {
        out_height = 480;
    }
  
  barc_bootstrap();
    //av_log_set_level(AV_LOG_VERBOSE);
    //av_log_set_callback(my_log_callback);

    int ret;
    time_t start_time = time(NULL);

    struct stat file_stat;
    stat(input_path, &file_stat);

    if (S_ISDIR(file_stat.st_mode)) {
        printf("using directory %s\n", input_path);
    } else if (S_ISREG(file_stat.st_mode)) {
        printf("attempt to unzip regular file %s\n", input_path);
        input_path = unzip_archive(input_path);
    } else {
        printf("Unknown file type %s\n", input_path);
        exit(-1);
    }

    if (!input_path) {
        printf("No working path. Exit.\n");
        exit(-1);
    }

  struct archive_s* archive;
  archive_alloc(&archive);
  struct archive_config_s archive_config = { 0 };
  archive_config.begin_offset = begin_offset;
  archive_config.end_offset = end_offset;
  archive_config.css_custom = css_custom;
  archive_config.css_preset = css_preset;
  archive_config.height = out_height;
  archive_config.width = out_width;
  archive_config.source_path = input_path;
  archive_config.output_path = output_path;
  archive_load_configuration(archive, &archive_config);
  ret = archive_main(archive);
  if (ret) {
    printf("archive main returned %d", ret);
  }
  archive_free(archive);

  time_t finish_time = time(NULL);
  printf("Composition took %ld seconds\n", finish_time - start_time);

  char cwd[1024];
  printf("%s\n", getcwd(cwd, sizeof(cwd)));

  MagickWandTerminus();

  return(0);

}
