/*
 * BSD LICENSE
 *
 * Copyright(c) 2008 - 2011 Intel Corporation. All rights reserved.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   * Neither the name of Intel Corporation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef _SCIF_SAS_STP_IO_REQUEST_H_
#define _SCIF_SAS_STP_IO_REQUEST_H_

/**
 * @file
 *
 * @brief This file contains the protected interface structures, constants,
 *        and methods for the SCIF_SAS_STP_IO_REQUEST object.  
 */

#include "sati_translator_sequence.h"
#include "sci_types.h"
#include "sci_status.h"
#include "sci_base_request.h"

extern SCI_BASE_REQUEST_STATE_HANDLER_T stp_io_request_constructed_handlers;

struct SCIF_SAS_IO_REQUEST;
SCI_STATUS scif_sas_stp_io_request_construct(
   struct SCIF_SAS_IO_REQUEST * fw_io
);

#if !defined(DISABLE_ATAPI)
SCI_STATUS scif_sas_stp_packet_io_request_construct(
   struct SCIF_SAS_IO_REQUEST * fw_io
);
#else // !defined(DISABLE_ATAPI)
#define scif_sas_stp_packet_io_request_construct(fw_io) SCI_FAILURE
#endif // !defined(DISABLE_ATAPI)

#if !defined(DISABLE_ATAPI)
U32 scif_sas_stp_packet_io_request_get_number_of_bytes_transferred(
   struct SCIF_SAS_IO_REQUEST * fw_io
);
#else // !defined(DISABLE_ATAPI)
#define scif_sas_stp_packet_io_request_get_number_of_bytes_transferred(fw_io) 0
#endif // !defined(DISABLE_ATAPI)

#endif // _SCIF_SAS_STP_IO_REQUEST_H_
