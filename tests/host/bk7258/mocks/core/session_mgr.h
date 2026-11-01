/* SPDX-License-Identifier: Apache-2.0 */

#ifndef __TESTS_HOST_BK7258_MOCKS_CORE_SESSION_MGR_H
#define __TESTS_HOST_BK7258_MOCKS_CORE_SESSION_MGR_H

#include <stddef.h>

int session_append(const char *chat_id, const char *role, const char *content);
int session_get_history_json(const char *chat_id, char *buffer, size_t size,
                             int maximum);
int session_clear(const char *chat_id);

#endif
