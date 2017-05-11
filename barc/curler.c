//
//  curler.c
//  barc
//
//  Created by Charley Robinson on 4/10/17.
//  Copyright © 2017 TokBox, Inc. All rights reserved.
//

#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include "curler.h"

static size_t write_data(void *ptr, size_t size, size_t nmemb, void *stream)
{
  size_t written = fwrite(ptr, size, nmemb, (FILE *)stream);
  return written;
}

#define OUT_PATH_TEMPLATE "/tmp/barc-XXXXXX"
char* get_http(const char* url)
{
  char* out_path = (char*)calloc(strlen(OUT_PATH_TEMPLATE) + 1, sizeof(char));
  strcpy(out_path, OUT_PATH_TEMPLATE);
  int out_fd = mkstemp(out_path);
  FILE* out_file = fdopen(out_fd, "wb");
  CURL* curl_handle;

  curl_global_init(CURL_GLOBAL_ALL);

  /* init the curl session */
  curl_handle = curl_easy_init();

  /* example.com is redirected, so we tell libcurl to follow redirection */
  curl_easy_setopt(curl_handle, CURLOPT_FOLLOWLOCATION, 1L);

  /* set URL to get here */
  curl_easy_setopt(curl_handle, CURLOPT_URL, url);

  /* Switch on full protocol/debug output while testing */
  curl_easy_setopt(curl_handle, CURLOPT_VERBOSE, 1L);

  /* disable progress meter, set to 0L to enable and disable debug output */
  curl_easy_setopt(curl_handle, CURLOPT_NOPROGRESS, 1L);

  /* send all data to this function  */
  curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, write_data);

  /* write the page body to this file handle */
  curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, out_file);

  /* get it! */
  curl_easy_perform(curl_handle);

  /* close the header file */
  fclose(out_file);

  /* cleanup curl stuff */
  curl_easy_cleanup(curl_handle);

  return out_path;
}
