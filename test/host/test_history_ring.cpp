/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "history_ring.h"

#include <string.h>

#include "tinytest.h"

static hist_rec_t rec(uint32_t t, uint16_t s, uint16_t d)
{
    hist_rec_t r;
    r.t = t;
    r.static_x10 = s;
    r.dynamic_x10 = d;
    return r;
}

static void test_init_and_check(void)
{
    hist_store_t s;
    hist_store_init(&s);
    CHECK(s.magic == HIST_MAGIC);
    CHECK(s.version == HIST_VERSION);
    CHECK(s.count == 0 && s.next == 0);
    CHECK(hist_store_check(&s));

    s.magic = 0;
    CHECK(!hist_store_check(&s));

    hist_store_init(&s);
    s.count = HIST_RING_CAP + 5;
    s.next = HIST_RING_CAP + 1;
    CHECK(hist_store_check(&s));
    CHECK(s.count == HIST_RING_CAP);
    CHECK(s.next == 0);
}

static void test_append_and_sort(void)
{
    hist_store_t s;
    hist_store_init(&s);
    hist_rec_t out[HIST_RING_CAP];

    CHECK(hist_store_sorted(&s, out) == 0);

    hist_store_append(&s, rec(600, 150, 120));
    hist_store_append(&s, rec(1200, 151, 121));
    CHECK(s.count == 2);
    CHECK(hist_store_sorted(&s, out) == 2);
    CHECK(out[0].t == 600 && out[1].t == 1200);

    /* Same timestamp replaces the newest record. */
    hist_store_append(&s, rec(1200, 999, HIST_NONE));
    CHECK(s.count == 2);
    hist_store_sorted(&s, out);
    CHECK(out[1].static_x10 == 999 && out[1].dynamic_x10 == HIST_NONE);
}

static void test_wraparound(void)
{
    hist_store_t s;
    hist_store_init(&s);
    hist_rec_t out[HIST_RING_CAP];

    for (uint32_t i = 1; i <= HIST_RING_CAP + 10; i++) {
        hist_store_append(&s, rec(i * 600, (uint16_t)i, (uint16_t)i));
    }
    CHECK(s.count == HIST_RING_CAP);
    uint16_t n = hist_store_sorted(&s, out);
    CHECK(n == HIST_RING_CAP);
    CHECK(out[0].t == 11 * 600);
    CHECK(out[n - 1].t == (HIST_RING_CAP + 10) * 600);
    for (uint16_t i = 1; i < n; i++) {
        CHECK(out[i].t > out[i - 1].t);
    }
}

static void test_newest(void)
{
    hist_store_t s;
    hist_store_init(&s);
    CHECK(hist_store_newest_t(&s) == 0);
    hist_store_append(&s, rec(600, 1, 1));
    hist_store_append(&s, rec(1200, 2, 2));
    CHECK(hist_store_newest_t(&s) == 1200);
    for (uint32_t i = 3; i <= HIST_RING_CAP + 2; i++) {
        hist_store_append(&s, rec(i * 600, 1, 1));
    }
    CHECK(hist_store_newest_t(&s) == (HIST_RING_CAP + 2) * 600);
}

static void test_sorted_skips_empty(void)
{
    hist_store_t s;
    hist_store_init(&s);
    hist_rec_t out[HIST_RING_CAP];
    hist_store_append(&s, rec(0, 1, 1)); /* t == 0 is "no record" */
    hist_store_append(&s, rec(600, 2, 2));
    CHECK(hist_store_sorted(&s, out) == 1);
    CHECK(out[0].t == 600);
}

int main(void)
{
    test_init_and_check();
    test_append_and_sort();
    test_wraparound();
    test_newest();
    test_sorted_skips_empty();
    TT_REPORT("history_ring");
}
