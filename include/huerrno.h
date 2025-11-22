/*
 * Copyright (c) 2025 HU Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>

#ifndef HU_ERROR_NO
#define HU_ERROR_NO

#define NOERROR         (0)
#define ENMCRC16        (__ELASTERROR + 0)  /* Not mismatch crc16 */
#define ECODEA85        (__ELASTERROR + 1)  /* ascii85 error code*/
#define EDESZA85        (__ELASTERROR + 2)  /* Decode size error for ascii85 */
#define EASCII85        (__ELASTERROR + 3)  /* for ascii85 */
// ascii85_err_out_buf_too_small            (__ELASTERROR + 3)
// ascii85_err_in_buf_too_large             (__ELASTERROR + 4)
// ascii85_err_bad_decode_char              (__ELASTERROR + 5)
// ascii85_err_decode_overflow              (__ELASTERROR + 6)
// reserved                                 (__ELASTERROR + 7)
// reserved                                 (__ELASTERROR + 8)
// reserved                                 (__ELASTERROR + 9)

#endif // HU_ERROR_NO
