//
//  file_audio_source.h
//  barc
//
//  Created by Charley Robinson on 3/23/17.
//

#ifndef file_audio_source_h
#define file_audio_source_h

struct file_audio_source_s;

struct file_audio_config_s {
  const char* file_path;
};

void file_audio_source_alloc(struct file_audio_source_s** source_out);
void file_audio_source_free(struct file_audio_source_s* source);
int file_audio_source_load_config(struct file_audio_source_s* source,
                                  struct file_audio_config_s* config);
int file_audio_source_seek(struct file_audio_source_s* source, double to_time);
double file_audio_source_get_pos(struct file_audio_source_s* source);
int file_audio_source_get_next(struct file_audio_source_s* source,
                               int num_samples,
                               int16_t* samples);

#endif /* file_audio_source_h */
