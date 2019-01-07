/* Copyright (c) 2017 - 2018 LiteSpeed Technologies Inc.  See LICENSE. */
#include <assert.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/queue.h>

#include "lsquic.h"
#include "lsquic_int_types.h"
#include "lsquic_hq.h"
#include "lsquic_varint.h"
#include "lsquic_hcsi_reader.h"
#include "lsquic_hash.h"
#include "lsquic_conn.h"

struct test
{
    int             lineno;

    unsigned char   input[0x100];
    size_t          input_sz;

    int             retval;
    char            output[0x1000];
};


static const struct test tests[] =
{
    {
        __LINE__,
        {
            0x05,
            0x02,
            (HQET_REQ_STREAM << HQ_PT_SHIFT) | (HQET_PLACEHOLDER << HQ_DT_SHIFT) | 1,
            0x03,
            0x40, 0x33,
            0x77,
        },
        7,
        0,
        "on_priority: request stream #3 depends on placeholder #51; exclusive: 1; weight: 120\n",
    },

    {
        __LINE__,
        {
            0x04,
            0x03,
            0x80, 0x12, 0x34, 0x45,
        },
        6,
        0,
        "on_cancel_push: 1193029\n",
    },

    {
        __LINE__,
        {
            0x02,
            HQFT_MAX_PUSH_ID,
            0x41, 0x23,
        },
        4,
        0,
        "on_max_push_id: 291\n",
    },

    {
        __LINE__,
        {
            0x00,
            HQFT_SETTINGS,
        },
        2,
        0,
        "have SETTINGS frame\n",
    },

    {   /* Frame contents do not match frame length */
        __LINE__,
        {
            0x03,
            HQFT_MAX_PUSH_ID,
            0x41, 0x23,
        },
        4,
        -1,
        "",
    },

    {
        __LINE__,
        {
            13,
            HQFT_SETTINGS,
            0x12, 0x34,
            0xC0, 0x12, 0x23, 0x34, 0x45, 0x56, 0x67, 0x78,
            0x12, 0x35,
            0x00,
        },
        15,
        0,
        "on_setting: 0x1234=0x12233445566778\n"
        "on_setting: 0x1235=0x0\n"
        "have SETTINGS frame\n"
        ,
    },

};


static void
on_priority (void *ctx, const struct hq_priority *priority)
{
    fprintf(ctx, "%s: %s #%"PRIu64" depends on %s #%"PRIu64"; "
        "exclusive: %d; weight: %u\n", __func__,
        lsquic_hqelt2str[priority->hqp_prio_type], priority->hqp_prio_id,
        lsquic_hqelt2str[priority->hqp_dep_type], priority->hqp_dep_id,
        priority->hqp_exclusive, HQP_WEIGHT(priority));
}

static void
on_cancel_push (void *ctx, uint64_t push_id)
{
    fprintf(ctx, "%s: %"PRIu64"\n", __func__, push_id);
}

static void
on_max_push_id (void *ctx, uint64_t push_id)
{
    fprintf(ctx, "%s: %"PRIu64"\n", __func__, push_id);
}

static void
on_settings_frame (void *ctx)
{
    fprintf(ctx, "have SETTINGS frame\n");
}

static void
on_setting (void *ctx, uint16_t setting_id, uint64_t value)
{
    fprintf(ctx, "%s: 0x%"PRIX16"=0x%"PRIX64"\n", __func__, setting_id, value);
}

static void
on_goaway (void *ctx, uint64_t stream_id)
{
    fprintf(ctx, "%s: %"PRIu64"\n", __func__, stream_id);
}

static void
on_unexpected_frame (void *ctx, enum hq_frame_type frame_type)
{
    fprintf(ctx, "%s: %u\n", __func__, frame_type);
}

static const struct hcsi_callbacks callbacks =
{
    .on_priority            = on_priority,
    .on_cancel_push         = on_cancel_push,
    .on_max_push_id         = on_max_push_id,
    .on_settings_frame      = on_settings_frame,
    .on_setting             = on_setting,
    .on_goaway              = on_goaway,
    .on_unexpected_frame    = on_unexpected_frame,
};

static void
run_test (const struct test *test)
{
    struct hcsi_reader reader;
    size_t read_sz, out_sz, toread;
    FILE *out_f;
    char *output;
    const unsigned char *p;
    int s;
    struct lsquic_conn lconn = LSCONN_INITIALIZER_CIDLEN(lconn, 0);

    for (read_sz = 1; read_sz < test->input_sz; ++read_sz)
    {
        out_f = open_memstream(&output, &out_sz);
        lsquic_hcsi_reader_init(&reader, &lconn, &callbacks, out_f);

        p = test->input;
        do
        {
            toread = test->input + test->input_sz - p;
            if (toread > read_sz)
                toread = read_sz;
            s = lsquic_hcsi_reader_feed(&reader, p, toread);
            if (s != 0)
                break;
            p += toread;
        }
        while (p < test->input + test->input_sz);

        assert(s == test->retval);

        fclose(out_f);
        assert(0 == strcmp(test->output, output));
        free(output);
    }
}

int
main (void)
{
    const struct test *test;
    struct test coalesced_test;

    for (test = tests; test < tests + sizeof(tests) / sizeof(tests[0]); ++test)
        run_test(test);

    memset(&coalesced_test, 0, sizeof(coalesced_test));
    for (test = tests; test < tests + sizeof(tests) / sizeof(tests[0]); ++test)
        if (test->retval == 0)
        {
            memcpy(coalesced_test.input + coalesced_test.input_sz,
                    test->input, test->input_sz);
            coalesced_test.input_sz += test->input_sz;
            strcat(coalesced_test.output, test->output);
        }
    run_test(&coalesced_test);

    return 0;
}