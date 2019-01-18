/* Copyright (c) 2017 - 2019 LiteSpeed Technologies Inc.  See LICENSE. */
#include <assert.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <zlib.h>

#include <openssl/ssl.h>
#ifndef WIN32
#else
#include <stdlib.h>
#include <vc_compat.h>
#endif

#include "lsquic_int_types.h"
#include "lsquic_crypto.h"
#include "lsquic_crt_compress.h"
#include "lsquic_util.h"

#include "lsquic_str.h"

#include "common_cert_set_2.c"
#include "common_cert_set_3.c"

/*
 * common_cert_sub_strings contains ~1500 bytes of common certificate substrings
 * as a dictionary of zlib from the Alexa Top 5000 set.
 */
static const unsigned char common_cert_sub_strings[] = {
    0x04, 0x02, 0x30, 0x00, 0x30, 0x1d, 0x06, 0x03, 0x55, 0x1d, 0x25, 0x04,
    0x16, 0x30, 0x14, 0x06, 0x08, 0x2b, 0x06, 0x01, 0x05, 0x05, 0x07, 0x03,
    0x01, 0x06, 0x08, 0x2b, 0x06, 0x01, 0x05, 0x05, 0x07, 0x03, 0x02, 0x30,
    0x5f, 0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x86, 0xf8, 0x42, 0x04, 0x01,
    0x06, 0x06, 0x0b, 0x60, 0x86, 0x48, 0x01, 0x86, 0xfd, 0x6d, 0x01, 0x07,
    0x17, 0x01, 0x30, 0x33, 0x20, 0x45, 0x78, 0x74, 0x65, 0x6e, 0x64, 0x65,
    0x64, 0x20, 0x56, 0x61, 0x6c, 0x69, 0x64, 0x61, 0x74, 0x69, 0x6f, 0x6e,
    0x20, 0x53, 0x20, 0x4c, 0x69, 0x6d, 0x69, 0x74, 0x65, 0x64, 0x31, 0x34,
    0x20, 0x53, 0x53, 0x4c, 0x20, 0x43, 0x41, 0x30, 0x1e, 0x17, 0x0d, 0x31,
    0x32, 0x20, 0x53, 0x65, 0x63, 0x75, 0x72, 0x65, 0x20, 0x53, 0x65, 0x72,
    0x76, 0x65, 0x72, 0x20, 0x43, 0x41, 0x30, 0x2d, 0x61, 0x69, 0x61, 0x2e,
    0x76, 0x65, 0x72, 0x69, 0x73, 0x69, 0x67, 0x6e, 0x2e, 0x63, 0x6f, 0x6d,
    0x2f, 0x45, 0x2d, 0x63, 0x72, 0x6c, 0x2e, 0x76, 0x65, 0x72, 0x69, 0x73,
    0x69, 0x67, 0x6e, 0x2e, 0x63, 0x6f, 0x6d, 0x2f, 0x45, 0x2e, 0x63, 0x65,
    0x72, 0x30, 0x0d, 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01,
    0x01, 0x05, 0x05, 0x00, 0x03, 0x82, 0x01, 0x01, 0x00, 0x4a, 0x2e, 0x63,
    0x6f, 0x6d, 0x2f, 0x72, 0x65, 0x73, 0x6f, 0x75, 0x72, 0x63, 0x65, 0x73,
    0x2f, 0x63, 0x70, 0x73, 0x20, 0x28, 0x63, 0x29, 0x30, 0x30, 0x09, 0x06,
    0x03, 0x55, 0x1d, 0x13, 0x04, 0x02, 0x30, 0x00, 0x30, 0x1d, 0x30, 0x0d,
    0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x05, 0x05,
    0x00, 0x03, 0x82, 0x01, 0x01, 0x00, 0x7b, 0x30, 0x1d, 0x06, 0x03, 0x55,
    0x1d, 0x0e, 0x30, 0x82, 0x01, 0x22, 0x30, 0x0d, 0x06, 0x09, 0x2a, 0x86,
    0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x01, 0x05, 0x00, 0x03, 0x82, 0x01,
    0x0f, 0x00, 0x30, 0x82, 0x01, 0x0a, 0x02, 0x82, 0x01, 0x01, 0x00, 0xd2,
    0x6f, 0x64, 0x6f, 0x63, 0x61, 0x2e, 0x63, 0x6f, 0x6d, 0x2f, 0x43, 0x2e,
    0x63, 0x72, 0x6c, 0x30, 0x1d, 0x06, 0x03, 0x55, 0x1d, 0x0e, 0x04, 0x16,
    0x04, 0x14, 0xb4, 0x2e, 0x67, 0x6c, 0x6f, 0x62, 0x61, 0x6c, 0x73, 0x69,
    0x67, 0x6e, 0x2e, 0x63, 0x6f, 0x6d, 0x2f, 0x72, 0x30, 0x0b, 0x06, 0x03,
    0x55, 0x1d, 0x0f, 0x04, 0x04, 0x03, 0x02, 0x01, 0x30, 0x0d, 0x06, 0x09,
    0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x05, 0x05, 0x00, 0x30,
    0x81, 0xca, 0x31, 0x0b, 0x30, 0x09, 0x06, 0x03, 0x55, 0x04, 0x06, 0x13,
    0x02, 0x55, 0x53, 0x31, 0x10, 0x30, 0x0e, 0x06, 0x03, 0x55, 0x04, 0x08,
    0x13, 0x07, 0x41, 0x72, 0x69, 0x7a, 0x6f, 0x6e, 0x61, 0x31, 0x13, 0x30,
    0x11, 0x06, 0x03, 0x55, 0x04, 0x07, 0x13, 0x0a, 0x53, 0x63, 0x6f, 0x74,
    0x74, 0x73, 0x64, 0x61, 0x6c, 0x65, 0x31, 0x1a, 0x30, 0x18, 0x06, 0x03,
    0x55, 0x04, 0x0a, 0x13, 0x11, 0x47, 0x6f, 0x44, 0x61, 0x64, 0x64, 0x79,
    0x2e, 0x63, 0x6f, 0x6d, 0x2c, 0x20, 0x49, 0x6e, 0x63, 0x2e, 0x31, 0x33,
    0x30, 0x31, 0x06, 0x03, 0x55, 0x04, 0x0b, 0x13, 0x2a, 0x68, 0x74, 0x74,
    0x70, 0x3a, 0x2f, 0x2f, 0x63, 0x65, 0x72, 0x74, 0x69, 0x66, 0x69, 0x63,
    0x61, 0x74, 0x65, 0x73, 0x2e, 0x67, 0x6f, 0x64, 0x61, 0x64, 0x64, 0x79,
    0x2e, 0x63, 0x6f, 0x6d, 0x2f, 0x72, 0x65, 0x70, 0x6f, 0x73, 0x69, 0x74,
    0x6f, 0x72, 0x79, 0x31, 0x30, 0x30, 0x2e, 0x06, 0x03, 0x55, 0x04, 0x03,
    0x13, 0x27, 0x47, 0x6f, 0x20, 0x44, 0x61, 0x64, 0x64, 0x79, 0x20, 0x53,
    0x65, 0x63, 0x75, 0x72, 0x65, 0x20, 0x43, 0x65, 0x72, 0x74, 0x69, 0x66,
    0x69, 0x63, 0x61, 0x74, 0x69, 0x6f, 0x6e, 0x20, 0x41, 0x75, 0x74, 0x68,
    0x6f, 0x72, 0x69, 0x74, 0x79, 0x31, 0x11, 0x30, 0x0f, 0x06, 0x03, 0x55,
    0x04, 0x05, 0x13, 0x08, 0x30, 0x37, 0x39, 0x36, 0x39, 0x32, 0x38, 0x37,
    0x30, 0x1e, 0x17, 0x0d, 0x31, 0x31, 0x30, 0x0e, 0x06, 0x03, 0x55, 0x1d,
    0x0f, 0x01, 0x01, 0xff, 0x04, 0x04, 0x03, 0x02, 0x05, 0xa0, 0x30, 0x0c,
    0x06, 0x03, 0x55, 0x1d, 0x13, 0x01, 0x01, 0xff, 0x04, 0x02, 0x30, 0x00,
    0x30, 0x1d, 0x30, 0x0f, 0x06, 0x03, 0x55, 0x1d, 0x13, 0x01, 0x01, 0xff,
    0x04, 0x05, 0x30, 0x03, 0x01, 0x01, 0x00, 0x30, 0x1d, 0x06, 0x03, 0x55,
    0x1d, 0x25, 0x04, 0x16, 0x30, 0x14, 0x06, 0x08, 0x2b, 0x06, 0x01, 0x05,
    0x05, 0x07, 0x03, 0x01, 0x06, 0x08, 0x2b, 0x06, 0x01, 0x05, 0x05, 0x07,
    0x03, 0x02, 0x30, 0x0e, 0x06, 0x03, 0x55, 0x1d, 0x0f, 0x01, 0x01, 0xff,
    0x04, 0x04, 0x03, 0x02, 0x05, 0xa0, 0x30, 0x33, 0x06, 0x03, 0x55, 0x1d,
    0x1f, 0x04, 0x2c, 0x30, 0x2a, 0x30, 0x28, 0xa0, 0x26, 0xa0, 0x24, 0x86,
    0x22, 0x68, 0x74, 0x74, 0x70, 0x3a, 0x2f, 0x2f, 0x63, 0x72, 0x6c, 0x2e,
    0x67, 0x6f, 0x64, 0x61, 0x64, 0x64, 0x79, 0x2e, 0x63, 0x6f, 0x6d, 0x2f,
    0x67, 0x64, 0x73, 0x31, 0x2d, 0x32, 0x30, 0x2a, 0x30, 0x28, 0x06, 0x08,
    0x2b, 0x06, 0x01, 0x05, 0x05, 0x07, 0x02, 0x01, 0x16, 0x1c, 0x68, 0x74,
    0x74, 0x70, 0x73, 0x3a, 0x2f, 0x2f, 0x77, 0x77, 0x77, 0x2e, 0x76, 0x65,
    0x72, 0x69, 0x73, 0x69, 0x67, 0x6e, 0x2e, 0x63, 0x6f, 0x6d, 0x2f, 0x63,
    0x70, 0x73, 0x30, 0x34, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x5a, 0x17,
    0x0d, 0x31, 0x33, 0x30, 0x35, 0x30, 0x39, 0x06, 0x08, 0x2b, 0x06, 0x01,
    0x05, 0x05, 0x07, 0x30, 0x02, 0x86, 0x2d, 0x68, 0x74, 0x74, 0x70, 0x3a,
    0x2f, 0x2f, 0x73, 0x30, 0x39, 0x30, 0x37, 0x06, 0x08, 0x2b, 0x06, 0x01,
    0x05, 0x05, 0x07, 0x02, 0x30, 0x44, 0x06, 0x03, 0x55, 0x1d, 0x20, 0x04,
    0x3d, 0x30, 0x3b, 0x30, 0x39, 0x06, 0x0b, 0x60, 0x86, 0x48, 0x01, 0x86,
    0xf8, 0x45, 0x01, 0x07, 0x17, 0x06, 0x31, 0x0b, 0x30, 0x09, 0x06, 0x03,
    0x55, 0x04, 0x06, 0x13, 0x02, 0x47, 0x42, 0x31, 0x1b, 0x53, 0x31, 0x17,
    0x30, 0x15, 0x06, 0x03, 0x55, 0x04, 0x0a, 0x13, 0x0e, 0x56, 0x65, 0x72,
    0x69, 0x53, 0x69, 0x67, 0x6e, 0x2c, 0x20, 0x49, 0x6e, 0x63, 0x2e, 0x31,
    0x1f, 0x30, 0x1d, 0x06, 0x03, 0x55, 0x04, 0x0b, 0x13, 0x16, 0x56, 0x65,
    0x72, 0x69, 0x53, 0x69, 0x67, 0x6e, 0x20, 0x54, 0x72, 0x75, 0x73, 0x74,
    0x20, 0x4e, 0x65, 0x74, 0x77, 0x6f, 0x72, 0x6b, 0x31, 0x3b, 0x30, 0x39,
    0x06, 0x03, 0x55, 0x04, 0x0b, 0x13, 0x32, 0x54, 0x65, 0x72, 0x6d, 0x73,
    0x20, 0x6f, 0x66, 0x20, 0x75, 0x73, 0x65, 0x20, 0x61, 0x74, 0x20, 0x68,
    0x74, 0x74, 0x70, 0x73, 0x3a, 0x2f, 0x2f, 0x77, 0x77, 0x77, 0x2e, 0x76,
    0x65, 0x72, 0x69, 0x73, 0x69, 0x67, 0x6e, 0x2e, 0x63, 0x6f, 0x6d, 0x2f,
    0x72, 0x70, 0x61, 0x20, 0x28, 0x63, 0x29, 0x30, 0x31, 0x10, 0x30, 0x0e,
    0x06, 0x03, 0x55, 0x04, 0x07, 0x13, 0x07, 0x53, 0x31, 0x13, 0x30, 0x11,
    0x06, 0x03, 0x55, 0x04, 0x0b, 0x13, 0x0a, 0x47, 0x31, 0x13, 0x30, 0x11,
    0x06, 0x0b, 0x2b, 0x06, 0x01, 0x04, 0x01, 0x82, 0x37, 0x3c, 0x02, 0x01,
    0x03, 0x13, 0x02, 0x55, 0x31, 0x16, 0x30, 0x14, 0x06, 0x03, 0x55, 0x04,
    0x03, 0x14, 0x31, 0x19, 0x30, 0x17, 0x06, 0x03, 0x55, 0x04, 0x03, 0x13,
    0x31, 0x1d, 0x30, 0x1b, 0x06, 0x03, 0x55, 0x04, 0x0f, 0x13, 0x14, 0x50,
    0x72, 0x69, 0x76, 0x61, 0x74, 0x65, 0x20, 0x4f, 0x72, 0x67, 0x61, 0x6e,
    0x69, 0x7a, 0x61, 0x74, 0x69, 0x6f, 0x6e, 0x31, 0x12, 0x31, 0x21, 0x30,
    0x1f, 0x06, 0x03, 0x55, 0x04, 0x0b, 0x13, 0x18, 0x44, 0x6f, 0x6d, 0x61,
    0x69, 0x6e, 0x20, 0x43, 0x6f, 0x6e, 0x74, 0x72, 0x6f, 0x6c, 0x20, 0x56,
    0x61, 0x6c, 0x69, 0x64, 0x61, 0x74, 0x65, 0x64, 0x31, 0x14, 0x31, 0x31,
    0x30, 0x2f, 0x06, 0x03, 0x55, 0x04, 0x0b, 0x13, 0x28, 0x53, 0x65, 0x65,
    0x20, 0x77, 0x77, 0x77, 0x2e, 0x72, 0x3a, 0x2f, 0x2f, 0x73, 0x65, 0x63,
    0x75, 0x72, 0x65, 0x2e, 0x67, 0x47, 0x6c, 0x6f, 0x62, 0x61, 0x6c, 0x53,
    0x69, 0x67, 0x6e, 0x31, 0x53, 0x65, 0x72, 0x76, 0x65, 0x72, 0x43, 0x41,
    0x2e, 0x63, 0x72, 0x6c, 0x56, 0x65, 0x72, 0x69, 0x53, 0x69, 0x67, 0x6e,
    0x20, 0x43, 0x6c, 0x61, 0x73, 0x73, 0x20, 0x33, 0x20, 0x45, 0x63, 0x72,
    0x6c, 0x2e, 0x67, 0x65, 0x6f, 0x74, 0x72, 0x75, 0x73, 0x74, 0x2e, 0x63,
    0x6f, 0x6d, 0x2f, 0x63, 0x72, 0x6c, 0x73, 0x2f, 0x73, 0x64, 0x31, 0x1a,
    0x30, 0x18, 0x06, 0x03, 0x55, 0x04, 0x0a, 0x68, 0x74, 0x74, 0x70, 0x3a,
    0x2f, 0x2f, 0x45, 0x56, 0x49, 0x6e, 0x74, 0x6c, 0x2d, 0x63, 0x63, 0x72,
    0x74, 0x2e, 0x67, 0x77, 0x77, 0x77, 0x2e, 0x67, 0x69, 0x63, 0x65, 0x72,
    0x74, 0x2e, 0x63, 0x6f, 0x6d, 0x2f, 0x31, 0x6f, 0x63, 0x73, 0x70, 0x2e,
    0x76, 0x65, 0x72, 0x69, 0x73, 0x69, 0x67, 0x6e, 0x2e, 0x63, 0x6f, 0x6d,
    0x30, 0x39, 0x72, 0x61, 0x70, 0x69, 0x64, 0x73, 0x73, 0x6c, 0x2e, 0x63,
    0x6f, 0x73, 0x2e, 0x67, 0x6f, 0x64, 0x61, 0x64, 0x64, 0x79, 0x2e, 0x63,
    0x6f, 0x6d, 0x2f, 0x72, 0x65, 0x70, 0x6f, 0x73, 0x69, 0x74, 0x6f, 0x72,
    0x79, 0x2f, 0x30, 0x81, 0x80, 0x06, 0x08, 0x2b, 0x06, 0x01, 0x05, 0x05,
    0x07, 0x01, 0x01, 0x04, 0x74, 0x30, 0x72, 0x30, 0x24, 0x06, 0x08, 0x2b,
    0x06, 0x01, 0x05, 0x05, 0x07, 0x30, 0x01, 0x86, 0x18, 0x68, 0x74, 0x74,
    0x70, 0x3a, 0x2f, 0x2f, 0x6f, 0x63, 0x73, 0x70, 0x2e, 0x67, 0x6f, 0x64,
    0x61, 0x64, 0x64, 0x79, 0x2e, 0x63, 0x6f, 0x6d, 0x2f, 0x30, 0x4a, 0x06,
    0x08, 0x2b, 0x06, 0x01, 0x05, 0x05, 0x07, 0x30, 0x02, 0x86, 0x3e, 0x68,
    0x74, 0x74, 0x70, 0x3a, 0x2f, 0x2f, 0x63, 0x65, 0x72, 0x74, 0x69, 0x66,
    0x69, 0x63, 0x61, 0x74, 0x65, 0x73, 0x2e, 0x67, 0x6f, 0x64, 0x61, 0x64,
    0x64, 0x79, 0x2e, 0x63, 0x6f, 0x6d, 0x2f, 0x72, 0x65, 0x70, 0x6f, 0x73,
    0x69, 0x74, 0x6f, 0x72, 0x79, 0x2f, 0x67, 0x64, 0x5f, 0x69, 0x6e, 0x74,
    0x65, 0x72, 0x6d, 0x65, 0x64, 0x69, 0x61, 0x74, 0x65, 0x2e, 0x63, 0x72,
    0x74, 0x30, 0x1f, 0x06, 0x03, 0x55, 0x1d, 0x23, 0x04, 0x18, 0x30, 0x16,
    0x80, 0x14, 0xfd, 0xac, 0x61, 0x32, 0x93, 0x6c, 0x45, 0xd6, 0xe2, 0xee,
    0x85, 0x5f, 0x9a, 0xba, 0xe7, 0x76, 0x99, 0x68, 0xcc, 0xe7, 0x30, 0x27,
    0x86, 0x29, 0x68, 0x74, 0x74, 0x70, 0x3a, 0x2f, 0x2f, 0x63, 0x86, 0x30,
    0x68, 0x74, 0x74, 0x70, 0x3a, 0x2f, 0x2f, 0x73,
};

#define common_certs_num 2
const common_cert_t common_cert_set[common_certs_num] = {
    {common_certs2_num, common_certs2, common_certs2_lens, common_certs2_hash},
    {common_certs3_num, common_certs3, common_certs3_lens, common_certs3_hash},
};


static lsquic_str_t *s_ccsbuf;

lsquic_str_t * get_common_certs_hash()
{
    int i;
    if (s_ccsbuf == NULL)
    {
        s_ccsbuf = lsquic_str_new(NULL, 0);
        for (i=0 ;i<common_certs_num; ++i)
        {
            lsquic_str_append(s_ccsbuf, (const char *)&common_cert_set[i].hash, 8);
        }
    }
    return s_ccsbuf;
}


/* return 0 found, -1 not found */
int get_common_cert(uint64_t hash, uint32_t index, lsquic_str_t *buf)
{
    int i;
    for (i = 0; i < common_certs_num; i++)
    {
        if (common_cert_set[i].hash == hash)
        {
            if (index < common_cert_set[i].num_certs)
            {
                lsquic_str_setto(buf, (const char *) common_cert_set[i].certs[index],
                         common_cert_set[i].lens[index]);
                return 0;
            }
            break;
        }
    }
    return -1;
}


/* result is written to dict */
static void
make_zlib_dict_for_entries(cert_entry_t *entries,
                                lsquic_str_t **certs, size_t certs_count,
                                lsquic_str_t *dict)
{
    int i;
    size_t zlib_dict_size = 0;
    for (i = certs_count - 1; i >= 0; --i)
    {
        if (entries[i].type != ENTRY_COMPRESSED)
        {
            zlib_dict_size += lsquic_str_len(certs[i]);
        }
    }

    // At the end of the dictionary is a block of common certificate substrings.
    zlib_dict_size += sizeof(common_cert_sub_strings);
    
    for (i = certs_count - 1; i >= 0; --i)
    {
        if (entries[i].type != ENTRY_COMPRESSED)
        {
            lsquic_str_append(dict, lsquic_str_buf(certs[i]), lsquic_str_len(certs[i]));
        }
    }

    lsquic_str_append(dict, (const char *)common_cert_sub_strings, sizeof(common_cert_sub_strings));
    assert((size_t)lsquic_str_len(dict) == zlib_dict_size);
}


void get_certs_hash(lsquic_str_t *certs, size_t certs_count, uint64_t *hashs)
{
    size_t i;
    for(i = 0; i < certs_count; ++i)
    {
        hashs[i] = fnv1a_64((const uint8_t *)lsquic_str_buf(&certs[i]), lsquic_str_len(&certs[i]));
    }
}


size_t get_entries_size(cert_entry_t *entries, size_t entries_count)
{
    size_t i;
    size_t entries_size = 0;
    for(i=0; i<entries_count; ++i)
    {
        entries_size++;
        switch (entries[i].type)
        {
        case ENTRY_COMPRESSED:
            break;
        case ENTRY_CACHED:
            entries_size += sizeof(uint64_t);
            break;
        case ENTRY_COMMON:
            entries_size += sizeof(uint64_t) + sizeof(uint32_t);
            break;
        default:
            break;
        }
    }
    entries_size++;  /* for end marker */
    return entries_size;
}


void serialize_cert_entries(uint8_t* out, int *out_len, cert_entry_t *entries,
                            size_t entries_count)
{
    size_t i;
    uint8_t *start = out;
    for(i=0; i<entries_count; ++i)
    {
        *out++ = (uint8_t)(entries[i].type);
        switch (entries[i].type)
        {
        case ENTRY_COMPRESSED:
            break;
        case ENTRY_CACHED:
            memcpy(out, &entries[i].hash, sizeof(uint64_t));
            out += sizeof(uint64_t);
            break;
        case ENTRY_COMMON:
            memcpy(out, &entries[i].set_hash, sizeof(uint64_t));
            out += sizeof(uint64_t);
            memcpy(out, &entries[i].index, sizeof(uint32_t));
            out += sizeof(uint32_t);
            break;
        default:
            break;
        }
    }

    *out++ = 0;  // end marker
    *out_len = out - start;
}


int get_certs_count(lsquic_str_t *compressed_crt_buf)
{
    char *in = lsquic_str_buf(compressed_crt_buf);
    char *in_end = in + lsquic_str_len(compressed_crt_buf);
    size_t idx = 0;
    uint8_t type_byte;
    
    for (;;)
    {
        if (in >= in_end)
            return -1;

        type_byte = in[0];
        ++in;
        if (type_byte == 0)
            break;

        ++idx;
        switch(type_byte)
        {
        case ENTRY_COMPRESSED:
            break;
        case ENTRY_CACHED:
        {
            if (in_end - in < (int)sizeof(uint64_t))
                return -1;
            in += sizeof(uint64_t);
            break;
        }
        case ENTRY_COMMON:
        {
            if (in_end - in < (int)(sizeof(uint64_t) + sizeof(uint32_t)))
                return -1;
            in += sizeof(uint64_t) + sizeof(uint32_t);
            break;
        }
        default:
            return -1;
        }
    }
    return idx;
}


/* return 0: OK, -1, error */
static int parse_entries(const unsigned char **in_out, const unsigned char *const in_end,
                         lsquic_str_t *cached_certs, size_t cached_certs_count,
                         cert_entry_t *out_entries,
                         lsquic_str_t **out_certs, size_t *out_certs_count)
{
    const unsigned char *in = *in_out;
    size_t idx = 0;
    uint64_t* cached_hashes;
    cert_entry_t *entry;
    lsquic_str_t *cert;
    uint8_t type_byte;
    int rv;
    size_t i;

    cached_hashes = NULL;

    for (;;)
    {
        /* XXX potential invalid read */
        type_byte = in[0];
        ++in;

        if (type_byte == 0)
            break;
        
        entry = &out_entries[idx];
        cert = out_certs[idx];
        /* XXX This seems dangerous -- there is no guard that `idx' does not
         * exceed `out_certs_count'.
         */
        lsquic_str_d(cert);
        
        ++idx;
        entry->type = type_byte;
        switch (entry->type)
        {
        case ENTRY_COMPRESSED:
            break;
        case ENTRY_CACHED:
        {
            memcpy(&entry->hash, in, sizeof(uint64_t));
            in += sizeof(uint64_t);
            
            if (!cached_hashes)
            {
                cached_hashes = malloc(cached_certs_count * sizeof(uint64_t));;
                if (!cached_hashes)
                    goto err;
                get_certs_hash(cached_certs, cached_certs_count, cached_hashes);
            }

            for (i=0; i<cached_certs_count; ++i)
            {
                if (cached_hashes[i] == entry->hash)
                {
                    lsquic_str_append(cert, lsquic_str_buf(&cached_certs[i]),
                                  lsquic_str_len(&cached_certs[i]));
                    break;
                }
            }
            /* XXX: return -1 if not found?  Logic removed in
                                4fd7e76bc031ac637e76c7f0930aff53f5b71705 */
            break;
        }
        case ENTRY_COMMON:
        {
            memcpy(&entry->set_hash, in, sizeof(uint64_t));
            in += sizeof(uint64_t);
            memcpy(&entry->index, in, sizeof(uint32_t));
            in += sizeof(uint32_t);

            if (0 == get_common_cert(entry->set_hash, entry->index, cert))
                break;
            else
                goto err;
        }
        default:
            goto err;
        }
    }

    rv = 0;
    *in_out = in;
    *out_certs_count = idx;

  cleanup:
    free(cached_hashes);
    return rv;

  err:
    rv = -1;
    goto cleanup;
}


/* 0: ok */
int decompress_certs(const unsigned char *in, const unsigned char *in_end,
                     lsquic_str_t *cached_certs, size_t cached_certs_count,
                     lsquic_str_t **out_certs, size_t *out_certs_count)
{
    int ret;
    size_t i;
    uint8_t* uncompressed_data, *uncompressed_data_buf;
    lsquic_str_t *dict;
    uint32_t uncompressed_size;
    size_t count = *out_certs_count;
    cert_entry_t *entries;
    z_stream z;

    assert(*out_certs_count > 0 && *out_certs_count < 10000
            && "Call get_certs_count() to get right certificates count first and make enough room for out_certs_count");
    
    if (count == 0 || count > 10000)
        return -1;

    dict = lsquic_str_new(NULL, 0);
    if (!dict)
        return -1;

    uncompressed_data_buf = NULL;
#ifdef WIN32
    uncompressed_data = NULL;
#endif
    entries = malloc(count * sizeof(cert_entry_t));
    if (!entries)
        goto err;

    ret = parse_entries(&in, in_end, cached_certs, cached_certs_count,
                  entries, out_certs, out_certs_count);
    if (ret)
        goto err;

    /* re-assign count with real valus */
    count = *out_certs_count;

    if (in < in_end)
    {
        if (in_end - in < (int)sizeof(uint32_t))
            goto err;

        memcpy(&uncompressed_size, in, sizeof(uncompressed_size));
        in += sizeof(uint32_t);
        /* XXX Is 128 KB an arbitrary limit or is there a reason behind it? */
        if (uncompressed_size > 128 * 1024)
            goto err;
        
        uncompressed_data_buf = uncompressed_data = malloc(uncompressed_size);
        if (!uncompressed_data)
            goto err;

        memset(&z, 0, sizeof(z));
        z.next_out  = uncompressed_data;
        z.avail_out = uncompressed_size;
        z.next_in   = (unsigned char *) in;
        z.avail_in  = in_end - in;

        if (Z_OK != inflateInit(&z))
            goto err;
        
        ret = inflate(&z, Z_FINISH);
        if (ret == Z_NEED_DICT)
        {
            lsquic_str_d(dict);
            make_zlib_dict_for_entries(entries, out_certs, count, dict);
            if (Z_OK != inflateSetDictionary(&z, (const unsigned char *)lsquic_str_buf(dict), lsquic_str_len(dict)))
                goto err;
            ret = inflate(&z, Z_FINISH);
        }

        if (Z_STREAM_END != ret || z.avail_out > 0 || z.avail_in > 0)
            goto err;
    }
    else
        uncompressed_size = 0;

    for (i = 0; i < count; i++)
    {
        switch (entries[i].type)
        {
          case ENTRY_COMPRESSED:
              if (uncompressed_size < sizeof(uint32_t))
                  goto err;
              lsquic_str_d(out_certs[i]);
              uint32_t cert_len;
              memcpy(&cert_len, uncompressed_data, sizeof(cert_len));
              uncompressed_data += sizeof(uint32_t);
              uncompressed_size -= sizeof(uint32_t);
              if (uncompressed_size < cert_len)
                  goto err;
              lsquic_str_append(out_certs[i], (const char *)uncompressed_data, cert_len);
              uncompressed_data += cert_len;
              uncompressed_size -= cert_len;
              break;
          case ENTRY_CACHED:
          case ENTRY_COMMON:
          default:
            break;
        }
    }

  cleanup:
    lsquic_str_delete(dict);
    free(entries);
    if (uncompressed_data_buf)
        inflateEnd(&z);
    free(uncompressed_data_buf);
    if (0 == uncompressed_size)
        return 0;
    else
        return -1;

  err:
    uncompressed_size = 1;  /* This triggers return -1 above */
    goto cleanup;
}


void
lsquic_crt_cleanup (void)
{
    if (s_ccsbuf)
    {
        lsquic_str_delete(s_ccsbuf);
        s_ccsbuf = NULL;
    }
}
