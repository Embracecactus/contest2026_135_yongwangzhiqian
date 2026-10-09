/****************************************************************************
 * tests/host/bk7258/mocks/kvdb.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef TEST_BK7258_MOCK_KVDB_H
#define TEST_BK7258_MOCK_KVDB_H

#define PROP_VALUE_MAX 92

int property_get_with_err(const char *key, char *value);
int property_set(const char *key, const char *value);
int property_delete(const char *key);
int property_commit(void);

#endif /* TEST_BK7258_MOCK_KVDB_H */
