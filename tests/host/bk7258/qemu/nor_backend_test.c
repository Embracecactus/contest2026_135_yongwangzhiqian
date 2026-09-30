/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Downstream AI-assisted experiment; not for upstream QEMU submission.
 *
 * Compile the production NOR helper implementation selected by NOR_SOURCE
 * with the matching QEMU source/build headers. BlockBackend I/O is mocked
 * here to inject deterministic write/flush failures and readonly media.
 * This does not exercise QOM realization, real image formats, controller
 * timing, MMIO, migration or XIP. Those require separate integration tests.
 * Protection expectations use the independent size tables from GigaDevice
 * DS-00476-GD25WQ64E-Rev1.2, tables 4 and 5; no vendor code is copied.
 */
#include "qemu/osdep.h"
#include "qemu/module.h"

#ifndef NOR_SOURCE
#error "Define NOR_SOURCE as the production hw/block/bk7258_nor.c path"
#endif

/* Stack-local helper fixtures do not need QOM type registration. */
#undef type_init
#define type_init(function)
#include NOR_SOURCE

struct BlockBackend {
    BK7258NORState *owner;
    uint8_t *bytes;
    size_t length;
    bool writable;
    int write_error;
    int flush_error;
    unsigned writes;
    unsigned flushes;
};

static unsigned io_error_reports;

bool blk_supports_write_perm(BlockBackend *blk)
{
    return blk->writable;
}

int blk_pwrite(BlockBackend *blk, int64_t offset, int64_t length,
               const void *data, BdrvRequestFlags flags)
{
    g_assert_true(blk->owner->busy);
    g_assert_false(blk->owner->wel);
    g_assert_true(blk->writable);
    g_assert_cmpint(offset, >=, 0);
    g_assert_cmpint(length, >=, 0);
    g_assert_cmpuint(offset, <=, blk->length);
    g_assert_cmpuint(length, <=, blk->length - offset);
    if (blk->length == BK7258_NOR_STATUS_SIZE) {
        g_assert_cmpint(offset, ==, 0);
        g_assert_cmpint(length, ==, BK7258_NOR_STATUS_SIZE);
    }
    blk->writes++;
    if (blk->write_error) {
        return blk->write_error;
    }
    memcpy(blk->bytes + offset, data, length);
    return 0;
}

int blk_flush(BlockBackend *blk)
{
    g_assert_true(blk->owner->busy);
    g_assert_false(blk->owner->wel);
    blk->flushes++;
    return blk->flush_error;
}

void error_report(const char *fmt, ...)
{
    /* Expected failures are counted without emitting host error messages. */
    io_error_reports++;
}

static void fixture_init(BK7258NORState *s)
{
    memset(s, 0, sizeof(*s));
    s->data = g_malloc(BK7258_NOR_SIZE);
    memset(s->data, 0xff, BK7258_NOR_SIZE);
    s->status[2] = 0x20;
    s->wp_level = true;
    io_error_reports = 0;
}

static void status_write(BK7258NORState *s, unsigned index, uint8_t value)
{
    g_assert_cmpint(bk7258_nor_write_enable(s), ==, 0);
    g_assert_cmpint(bk7258_nor_write_status(s, index, value), ==, 0);
    g_assert_false(s->wel);
}

static bool protection_expected(unsigned bp, bool cmp, uint32_t addr)
{
    static const uint32_t block_sizes[8] = {
        0, 0x20000, 0x40000, 0x80000,
        0x100000, 0x200000, 0x400000, 0x800000
    };
    static const uint32_t sector_sizes[8] = {
        0, 0x1000, 0x2000, 0x4000,
        0x8000, 0x8000, 0x8000, 0x800000
    };
    uint32_t size = (bp & 16 ? sector_sizes : block_sizes)[bp & 7];
    bool protected = bp & 8 ? addr < size : addr >= 0x800000 - size;

    return protected ^ cmp;
}

static void unrealized_helpers(void)
{
    BK7258NORState s = { 0 };
    uint8_t value = 0xa5;

    g_assert_cmpint(bk7258_nor_read(&s, 0, &value, 1), ==, -ENODEV);
    g_assert_cmpint(bk7258_nor_read_status(&s, 0, &value), ==, -ENODEV);
    g_assert_cmpint(bk7258_nor_write_enable(&s), ==, -ENODEV);
    g_assert_cmpint(bk7258_nor_write_disable(&s), ==, -ENODEV);
    g_assert_cmpint(bk7258_nor_write_status(&s, 0, 0), ==, -ENODEV);
    g_assert_cmpint(bk7258_nor_program(&s, 0, &value, 1), ==, -ENODEV);
    g_assert_cmpint(bk7258_nor_erase(&s, 0, 4096), ==, -ENODEV);
    g_assert_cmphex(value, ==, 0xa5);
    g_assert_null(bk7258_nor_storage(&s));
}

static void program_and_page_wrap(void)
{
    BK7258NORState s;
    uint8_t data[BK7258_NOR_PAGE_SIZE];
    uint8_t actual[4];

    fixture_init(&s);
    memset(data, 0x55, sizeof(data));
    g_assert_cmpint(bk7258_nor_program(&s, 0, data, 4), ==, -EACCES);
    g_assert_cmpint(bk7258_nor_erase(&s, 0, 4096), ==, -EACCES);
    g_assert_cmpint(bk7258_nor_write_enable(&s), ==, 0);
    g_assert_cmpint(bk7258_nor_program(&s, 0, data, 4), ==, 0);
    g_assert_false(s.wel);
    memset(data, 0xff, sizeof(data));
    g_assert_cmpint(bk7258_nor_write_enable(&s), ==, 0);
    g_assert_cmpint(bk7258_nor_program(&s, 0, data, 4), ==, 0);
    g_assert_cmpint(bk7258_nor_read(&s, 0, actual, sizeof(actual)), ==, 0);
    for (unsigned i = 0; i < sizeof(actual); i++) {
        g_assert_cmphex(actual[i], ==, 0x55);
    }
    memset(data, 0x0f, sizeof(data));
    g_assert_cmpint(bk7258_nor_write_enable(&s), ==, 0);
    g_assert_cmpint(bk7258_nor_program(&s, 0, data, 4), ==, 0);
    g_assert_cmphex(s.data[0], ==, 0x05);

    /* A full page starting near the final byte wraps within the last page. */
    for (unsigned i = 0; i < sizeof(data); i++) {
        data[i] = i;
    }
    g_assert_cmpint(bk7258_nor_write_enable(&s), ==, 0);
    g_assert_cmpint(bk7258_nor_program(&s, BK7258_NOR_SIZE - 2,
                                      data, sizeof(data)), ==, 0);
    for (unsigned i = 0; i < sizeof(data); i++) {
        uint32_t addr = BK7258_NOR_SIZE - BK7258_NOR_PAGE_SIZE +
                        ((BK7258_NOR_PAGE_SIZE - 2 + i) & 255);

        g_assert_cmphex(s.data[addr], ==, data[i]);
    }
    g_assert_cmphex(s.data[BK7258_NOR_SIZE - 257], ==, 0xff);
    g_free(s.data);
}

static void malformed_bounds(void)
{
    BK7258NORState s;
    uint8_t byte = 0xa5;
    static const uint32_t bad_erase_sizes[] = { 0, 1, 4095, 8192, UINT32_MAX };

    fixture_init(&s);
    g_assert_cmpint(bk7258_nor_write_enable(&s), ==, 0);
    g_assert_cmpint(bk7258_nor_read(&s, UINT32_MAX, &byte, 1), ==, -EINVAL);
    g_assert_cmpint(bk7258_nor_read(&s, 0, &byte, SIZE_MAX), ==, -EINVAL);
    g_assert_cmpint(bk7258_nor_read(&s, 0, &byte, 0), ==, -EINVAL);
    g_assert_cmpint(bk7258_nor_read(&s, 0, NULL, 1), ==, -EINVAL);
    g_assert_cmpint(bk7258_nor_read(&s, BK7258_NOR_SIZE - 1,
                                   &byte, 2), ==, -EINVAL);
    g_assert_cmpint(bk7258_nor_read_status(&s, 3, &byte), ==, -EINVAL);
    g_assert_cmpint(bk7258_nor_read_status(&s, UINT_MAX, &byte), ==, -EINVAL);
    g_assert_cmpint(bk7258_nor_read_status(&s, 0, NULL), ==, -EINVAL);
    g_assert_cmpint(bk7258_nor_program(&s, UINT32_MAX, &byte, 1), ==, -EINVAL);
    g_assert_cmpint(bk7258_nor_program(&s, BK7258_NOR_SIZE,
                                      &byte, 1), ==, -EINVAL);
    g_assert_cmpint(bk7258_nor_program(&s, 0, &byte, 0), ==, -EINVAL);
    g_assert_cmpint(bk7258_nor_program(&s, 0, &byte, 257), ==, -EINVAL);
    g_assert_cmpint(bk7258_nor_program(&s, 0, &byte, SIZE_MAX), ==, -EINVAL);
    g_assert_cmpint(bk7258_nor_program(&s, 0, NULL, 1), ==, -EINVAL);
    g_assert_cmpint(bk7258_nor_erase(&s, UINT32_MAX, 4096), ==, -EINVAL);
    g_assert_cmpint(bk7258_nor_erase(&s, BK7258_NOR_SIZE, 4096), ==, -EINVAL);
    g_assert_cmpint(bk7258_nor_erase(&s, 1, BK7258_NOR_SIZE), ==, -EINVAL);
    for (unsigned i = 0; i < ARRAY_SIZE(bad_erase_sizes); i++) {
        g_assert_cmpint(bk7258_nor_erase(&s, 0, bad_erase_sizes[i]),
                        ==, -EINVAL);
    }
    g_assert_cmpint(bk7258_nor_write_status(&s, 3, 0), ==, -EINVAL);
    g_assert_cmpint(bk7258_nor_write_status(&s, UINT_MAX, 0), ==, -EINVAL);
    g_assert_true(s.wel);
    g_assert_cmphex(byte, ==, 0xa5);
    g_assert_cmphex(s.data[0], ==, 0xff);
    g_assert_cmphex(s.status[0], ==, 0);
    g_free(s.data);
}

static void busy_and_reset(void)
{
    BK7258NORState s;
    uint8_t byte = 0x12;
    uint8_t status;

    fixture_init(&s);
    status_write(&s, 0, 0x0e << 2);
    status_write(&s, 1, 2);
    g_assert_cmpint(bk7258_nor_write_enable(&s), ==, 0);
    g_assert_cmpint(bk7258_nor_program(&s, 0x400000, &byte, 1), ==, 0);
    g_assert_cmpint(bk7258_nor_write_enable(&s), ==, 0);
    bk7258_nor_set_busy(&s, true);
    g_assert_true(bk7258_nor_is_busy(&s));
    g_assert_cmpint(bk7258_nor_read_status(&s, 0, &status), ==, 0);
    g_assert_cmphex(status, ==, (0x0e << 2) | 3);
    g_assert_cmpint(bk7258_nor_write_enable(&s), ==, -EBUSY);
    g_assert_cmpint(bk7258_nor_write_disable(&s), ==, -EBUSY);
    g_assert_cmpint(bk7258_nor_write_status(&s, 0, 0), ==, -EBUSY);
    g_assert_cmpint(bk7258_nor_program(&s, 0, &byte, 1), ==, -EBUSY);
    g_assert_cmpint(bk7258_nor_erase(&s, 0, 4096), ==, -EBUSY);
    g_assert_cmpint(bk7258_nor_read(&s, 0, &byte, 1), ==, -EBUSY);
    g_assert_true(s.wel);
    bk7258_nor_set_wp(&s, false);
    bk7258_nor_reset(&s);
    g_assert_false(s.busy);
    g_assert_false(s.wel);
    g_assert_false(s.wp_level);
    g_assert_cmphex(s.status[0], ==, 0x0e << 2);
    g_assert_cmphex(s.status[1], ==, 2);
    g_assert_cmphex(s.status[2], ==, 0x20);
    g_assert_cmphex(s.data[0x400000], ==, 0x12);
    g_assert_cmpint(bk7258_nor_write_enable(&s), ==, 0);
    g_assert_cmpint(bk7258_nor_write_disable(&s), ==, 0);
    g_assert_false(s.wel);
    g_free(s.data);
}

static void protection_matrix(void)
{
    BK7258NORState s;
    unsigned cases = 0;

    fixture_init(&s);
    for (unsigned bp = 0; bp < 32; bp++) {
        for (unsigned cmp = 0; cmp < 2; cmp++) {
            status_write(&s, 0, bp << 2);
            status_write(&s, 1, cmp << 6);
            g_assert_cmpint(bk7258_nor_write_enable(&s), ==, 0);
            for (uint32_t addr = 0; addr < BK7258_NOR_SIZE; addr += 4096) {
                int expected = protection_expected(bp, cmp, addr) ?
                               -EACCES : 0;

                g_assert_cmpint(bk7258_nor_check_program(&s, addr, 1),
                                ==, expected);
                g_assert_cmpint(bk7258_nor_check_program(&s, addr + 4095, 1),
                                ==, expected);
                g_assert_cmpint(bk7258_nor_check_erase(&s, addr, 4096),
                                ==, expected);
                cases++;
            }
            for (uint32_t size = 32768; size <= 65536; size *= 2) {
                for (uint32_t addr = 0; addr < BK7258_NOR_SIZE; addr += size) {
                    bool protected = false;

                    for (uint32_t pos = addr; pos < addr + size; pos += 4096) {
                        protected |= protection_expected(bp, cmp, pos);
                    }
                    g_assert_cmpint(bk7258_nor_check_erase(&s, addr, size),
                                    ==, protected ? -EACCES : 0);
                }
            }
            bool protected = protection_expected(bp, cmp, 0) ||
                             protection_expected(bp, cmp,
                                                 BK7258_NOR_SIZE - 1);

            g_assert_cmpint(bk7258_nor_check_erase(&s, 0, BK7258_NOR_SIZE),
                            ==, protected ? -EACCES : 0);
            g_assert_true(s.wel);
        }
    }
    g_assert_cmpuint(cases, ==, 131072);
    g_test_message("131072 protection-sector cases plus 32/64-KiB/chip checks");
    g_free(s.data);
}

static void erase_and_sdk_protection(void)
{
    BK7258NORState s;
    uint8_t byte = 0;
    static const uint32_t sizes[] = { 4096, 32768, 65536, BK7258_NOR_SIZE };

    fixture_init(&s);
    status_write(&s, 0, 0x0e << 2);
    g_assert_cmpint(bk7258_nor_write_enable(&s), ==, 0);
    g_assert_cmpint(bk7258_nor_program(&s, 0x3fffff, &byte, 1), ==, -EACCES);
    g_assert_cmpint(bk7258_nor_erase(&s, 0, 4096), ==, -EACCES);
    g_assert_cmpint(bk7258_nor_erase(&s, 0, BK7258_NOR_SIZE), ==, -EACCES);
    g_assert_true(s.wel);
    g_assert_cmphex(s.data[0x3fffff], ==, 0xff);
    g_assert_cmpint(bk7258_nor_program(&s, 0x400000, &byte, 1), ==, 0);
    status_write(&s, 0, 0);
    for (unsigned i = 0; i < ARRAY_SIZE(sizes); i++) {
        uint32_t size = sizes[i];
        uint32_t addr = size == BK7258_NOR_SIZE ? 0 : size - 1;

        memset(s.data, 0, BK7258_NOR_SIZE);
        g_assert_cmpint(bk7258_nor_write_enable(&s), ==, 0);
        g_assert_cmpint(bk7258_nor_erase(&s, addr, size), ==, 0);
        g_assert_false(s.wel);
        for (uint32_t pos = 0; pos < size; pos++) {
            g_assert_cmphex(s.data[pos], ==, 0xff);
        }
        if (size < BK7258_NOR_SIZE) {
            g_assert_cmphex(s.data[size], ==, 0);
        }
    }
    g_free(s.data);
}

static void status_masks_and_wp(void)
{
    BK7258NORState s;
    uint8_t status;

    fixture_init(&s);
    g_assert_cmpint(bk7258_nor_read_status(&s, 0, &status), ==, 0);
    g_assert_cmphex(status, ==, 0);
    g_assert_cmpint(bk7258_nor_read_status(&s, 1, &status), ==, 0);
    g_assert_cmphex(status, ==, 0);
    g_assert_cmpint(bk7258_nor_read_status(&s, 2, &status), ==, 0);
    g_assert_cmphex(status, ==, 0x20);
    status_write(&s, 0, 0x80);
    bk7258_nor_set_wp(&s, false);
    g_assert_cmpint(bk7258_nor_write_enable(&s), ==, 0);
    g_assert_cmpint(bk7258_nor_write_status(&s, 1, 2), ==, -EACCES);
    g_assert_true(s.wel);
    g_assert_cmphex(s.status[1], ==, 0);
    bk7258_nor_set_wp(&s, true);
    g_assert_cmpint(bk7258_nor_write_status(&s, 1, 2), ==, 0);
    bk7258_nor_set_wp(&s, false);
    status_write(&s, 0, 0); /* QE repurposes WP# as IO2. */
    g_assert_cmpint(bk7258_nor_write_enable(&s), ==, 0);
    g_assert_cmpint(bk7258_nor_write_status(&s, 1, 1), ==, -ENOTSUP);
    for (uint8_t lock = 8; lock <= 0x20; lock <<= 1) {
        g_assert_cmpint(bk7258_nor_write_status(&s, 1, lock), ==, -ENOTSUP);
    }
    g_assert_cmpint(bk7258_nor_write_status(&s, 2, 0x80), ==, -EINVAL);
    g_assert_true(s.wel);
    g_assert_cmphex(s.status[1], ==, 2);
    status_write(&s, 0, 3); /* WIP and WEL input bits are read-only. */
    status_write(&s, 1, 0x84); /* SUS1 and SUS2 inputs are read-only. */
    status_write(&s, 2, 0x61);
    g_assert_cmphex(s.status[0], ==, 0);
    g_assert_cmphex(s.status[1], ==, 0);
    g_assert_cmphex(s.status[2], ==, 0x61);
    bk7258_nor_reset(&s);
    g_assert_cmphex(s.status[2], ==, 0x61);
    g_free(s.data);
}

static void persistence_and_readonly(void)
{
    BK7258NORState s;
    uint8_t status[BK7258_NOR_STATUS_SIZE] = { 0, 0, 0x20 };
    uint8_t byte = 0x12;
    BlockBackend array = {
        .owner = &s,
        .bytes = g_malloc(BK7258_NOR_SIZE),
        .length = BK7258_NOR_SIZE,
        .writable = true,
    };
    BlockBackend nv = {
        .owner = &s,
        .bytes = status,
        .length = sizeof(status),
        .writable = true,
    };

    fixture_init(&s);
    memset(array.bytes, 0xff, array.length);
    s.blk = &array;
    s.status_blk = &nv;
    status_write(&s, 1, 2);
    g_assert_cmphex(status[1], ==, 2);
    g_assert_cmpuint(nv.writes, ==, 1);
    g_assert_cmpuint(nv.flushes, ==, 1);
    for (unsigned i = 3; i < sizeof(status); i++) {
        g_assert_cmphex(status[i], ==, 0);
    }
    g_assert_cmpint(bk7258_nor_write_enable(&s), ==, 0);
    g_assert_cmpint(bk7258_nor_program(&s, 0x200, &byte, 1), ==, 0);
    g_assert_cmphex(array.bytes[0x200], ==, 0x12);
    g_assert_cmphex(s.data[0x200], ==, 0x12);
    g_assert_cmpuint(array.writes, ==, 1);
    g_assert_cmpuint(array.flushes, ==, 1);

    array.writable = false;
    nv.writable = false;
    g_assert_cmpint(bk7258_nor_write_enable(&s), ==, 0);
    g_assert_cmpint(bk7258_nor_erase(&s, 0, 4096), ==, -EROFS);
    g_assert_cmpint(bk7258_nor_program(&s, 0x200, &byte, 1), ==, -EROFS);
    g_assert_cmpint(bk7258_nor_write_status(&s, 1, 0), ==, -EROFS);
    g_assert_true(s.wel);
    g_assert_false(s.io_failed);
    g_assert_cmphex(s.status[1], ==, 2);
    g_assert_cmphex(status[1], ==, 2);
    g_assert_cmphex(array.bytes[0x200], ==, 0x12);
    g_assert_cmpuint(nv.writes, ==, 1);
    g_assert_cmpuint(array.writes, ==, 1);
    g_assert_cmpuint(io_error_reports, ==, 0);
    nv.writable = true;
    g_assert_cmpint(bk7258_nor_write_status(&s, 1, 0), ==, 0);
    g_assert_false(s.wel);
    g_assert_cmphex(status[1], ==, 0);
    g_free(array.bytes);
    g_free(s.data);
}

static void commit_failure(bool status_failure, bool flush_failure)
{
    BK7258NORState s;
    uint32_t size = status_failure ? BK7258_NOR_STATUS_SIZE : BK7258_NOR_SIZE;
    uint8_t byte = 0x12;
    uint8_t status;
    BlockBackend backing = {
        .owner = &s,
        .bytes = g_malloc(size),
        .length = size,
        .writable = true,
        .write_error = flush_failure ? 0 : -ENOSPC,
        .flush_error = flush_failure ? -EIO : 0,
    };
    int expected = flush_failure ? -EIO : -ENOSPC;

    fixture_init(&s);
    memset(backing.bytes, status_failure ? 0 : 0xff, size);
    if (status_failure) {
        backing.bytes[2] = 0x20;
        s.status_blk = &backing;
    } else {
        s.blk = &backing;
    }
    g_assert_cmpint(bk7258_nor_write_enable(&s), ==, 0);
    if (status_failure) {
        g_assert_cmpint(bk7258_nor_write_status(&s, 1, 2), ==, expected);
        g_assert_cmphex(s.status[1], ==, 0);
        g_assert_cmphex(backing.bytes[1], ==, flush_failure ? 2 : 0);
    } else {
        g_assert_cmpint(bk7258_nor_program(&s, 0, &byte, 1), ==, expected);
        g_assert_cmphex(s.data[0], ==, 0xff);
        g_assert_cmphex(backing.bytes[0], ==, flush_failure ? 0x12 : 0xff);
    }
    g_assert_cmpuint(backing.writes, ==, 1);
    g_assert_cmpuint(backing.flushes, ==, flush_failure ? 1 : 0);
    g_assert_cmpuint(io_error_reports, ==, 1);
    g_assert_false(s.wel);
    g_assert_false(s.busy);
    g_assert_true(bk7258_nor_has_io_error(&s));
    g_assert_null(bk7258_nor_storage(&s));
    g_assert_cmpint(bk7258_nor_read_status(&s, 0, &status), ==, 0);
    g_assert_cmphex(status, ==, 0);
    g_assert_cmpint(bk7258_nor_read(&s, 0, &byte, 1), ==, -EIO);
    g_assert_cmpint(bk7258_nor_write_status(&s, 0, 0), ==, -EIO);
    g_assert_cmpint(bk7258_nor_program(&s, 0, &byte, 1), ==, -EIO);
    g_assert_cmpint(bk7258_nor_erase(&s, 0, 4096), ==, -EIO);
    bk7258_nor_reset(&s);
    g_assert_true(bk7258_nor_has_io_error(&s));
    g_assert_cmpint(bk7258_nor_write_enable(&s), ==, -EIO);
    g_assert_cmpint(bk7258_nor_write_disable(&s), ==, -EIO);
    g_assert_cmpuint(io_error_reports, ==, 1);
    g_free(backing.bytes);
    g_free(s.data);
}

static void array_write_failure(void)
{
    commit_failure(false, false);
}

static void array_flush_failure(void)
{
    commit_failure(false, true);
}

static void status_write_failure(void)
{
    commit_failure(true, false);
}

static void status_flush_failure(void)
{
    commit_failure(true, true);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/nor/unrealized", unrealized_helpers);
    g_test_add_func("/nor/program-and-page-wrap", program_and_page_wrap);
    g_test_add_func("/nor/malformed-bounds", malformed_bounds);
    g_test_add_func("/nor/busy-and-reset", busy_and_reset);
    g_test_add_func("/nor/protection-matrix", protection_matrix);
    g_test_add_func("/nor/erase-and-sdk-protection", erase_and_sdk_protection);
    g_test_add_func("/nor/status-masks-and-wp", status_masks_and_wp);
    g_test_add_func("/nor/persistence-and-readonly", persistence_and_readonly);
    g_test_add_func("/nor/array-write-failure", array_write_failure);
    g_test_add_func("/nor/array-flush-failure", array_flush_failure);
    g_test_add_func("/nor/status-write-failure", status_write_failure);
    g_test_add_func("/nor/status-flush-failure", status_flush_failure);
    return g_test_run();
}
