/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __MOCK_MEDIA_RECORDER_H
#define __MOCK_MEDIA_RECORDER_H

#include <stddef.h>
#include <sys/types.h>

#define MEDIA_SOURCE_MIC "mic"
#define MEDIA_EVENT_STARTED 2
typedef void (*media_event_callback)(void *, int, int, const char *);
int media_recorder_set_event_callback(void *handle, void *cookie,
                                      media_event_callback callback);

void *media_recorder_open(const char *params);
int media_recorder_prepare(void *handle, const char *url,
                           const char *options);
int media_recorder_start(void *handle);
ssize_t media_recorder_read_data(void *handle, void *data, size_t len);
int media_recorder_stop(void *handle);
int media_recorder_close(void *handle);

#endif /* __MOCK_MEDIA_RECORDER_H */
