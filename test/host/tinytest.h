/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdio.h>
#include <string.h>

static int tt_failed;
static int tt_checks;

#define CHECK(cond)                                                          \
    do {                                                                     \
        tt_checks++;                                                         \
        if (!(cond)) {                                                       \
            tt_failed++;                                                     \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                    \
    } while (0)

#define CHECK_STR(a, b)                                                      \
    do {                                                                     \
        tt_checks++;                                                         \
        if (strcmp((a), (b)) != 0) {                                         \
            tt_failed++;                                                     \
            fprintf(stderr, "%s:%d: CHECK_STR failed: \"%s\" != \"%s\"\n",  \
                    __FILE__, __LINE__, (a), (b));                           \
        }                                                                    \
    } while (0)

#define TT_REPORT(name)                                                      \
    do {                                                                     \
        printf("%s: %d checks, %d failed\n", name, tt_checks, tt_failed);   \
        return tt_failed != 0;                                               \
    } while (0)
