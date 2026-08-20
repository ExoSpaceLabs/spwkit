// SPDX-License-Identifier: Apache-2.0
#ifndef SPWKIT_VSPW_CUSE_RECORD_H
#define SPWKIT_VSPW_CUSE_RECORD_H

/*
 * libfuse3 requires a 64-bit off_t ABI, including on 32-bit Linux. The
 * production presenter defines FUSE_USE_VERSION before including this private
 * header, so establish the feature-test macro before this header includes any
 * libc headers. Codec-only users do not define FUSE_USE_VERSION and are left
 * untouched.
 */
#if defined(FUSE_USE_VERSION) && !defined(_FILE_OFFSET_BITS)
#define _FILE_OFFSET_BITS 64
#endif

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Packet-record ABI used by the Linux /dev/vspwX CUSE presentation.
 *
 * This header remains private: the fixed-width format is documented for raw
 * device consumers, while libfuse and native CUSE types stay outside the
 * portable public SpWKit headers and ABI.
 */
#define VSPW_CUSE_RECORD_MAGIC UINT32_C(0x53505752) /* "SPWR" */
#define VSPW_CUSE_RECORD_VERSION 1u
#define VSPW_CUSE_RECORD_HEADER_SIZE 16u
#define VSPW_CUSE_RECORD_MAX_PAYLOAD (1024u * 1024u)

#define VSPW_CUSE_RECORD_DATA      1u
#define VSPW_CUSE_RECORD_TIME_CODE 2u

#define VSPW_CUSE_RECORD_FLAG_EEP 0x01u
#define VSPW_CUSE_RECORD_KNOWN_FLAGS VSPW_CUSE_RECORD_FLAG_EEP

typedef struct vspw_cuse_record_header {
    uint8_t type;
    uint8_t flags;
    uint32_t payload_size;
} vspw_cuse_record_header_t;

typedef enum vspw_cuse_record_result {
    VSPW_CUSE_RECORD_OK = 0,
    VSPW_CUSE_RECORD_INVALID_ARGUMENT = -1,
    VSPW_CUSE_RECORD_INVALID_MAGIC = -2,
    VSPW_CUSE_RECORD_INVALID_VERSION = -3,
    VSPW_CUSE_RECORD_INVALID_TYPE = -4,
    VSPW_CUSE_RECORD_INVALID_FLAGS = -5,
    VSPW_CUSE_RECORD_INVALID_SIZE = -6,
    VSPW_CUSE_RECORD_INVALID_PAYLOAD = -7
} vspw_cuse_record_result_t;

vspw_cuse_record_result_t vspw_cuse_record_encode_header(
    const vspw_cuse_record_header_t* header,
    uint8_t out[VSPW_CUSE_RECORD_HEADER_SIZE]);

vspw_cuse_record_result_t vspw_cuse_record_decode_header(
    const uint8_t in[VSPW_CUSE_RECORD_HEADER_SIZE],
    vspw_cuse_record_header_t* out_header);

vspw_cuse_record_result_t vspw_cuse_record_validate_payload(
    const vspw_cuse_record_header_t* header,
    const uint8_t* payload,
    size_t payload_size);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SPWKIT_VSPW_CUSE_RECORD_H */
