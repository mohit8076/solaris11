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
/**
 * @file
 *
 * @brief This file contains all of the method implementations pertaining
 *        to the framework io request state handler methods.
 */

#include "scic_controller.h"
#include "scif_sas_logger.h"
#include "scif_sas_io_request.h"
#include "scif_sas_remote_device.h"
#include "scif_sas_domain.h"
#include "scif_sas_controller.h"

//******************************************************************************
//* C O N S T R U C T E D   H A N D L E R S
//******************************************************************************

/**
 * @brief This method provides CONSTRUCTED state specific handling for
 *        when the user attempts to start the supplied IO request.
 *
 * @param[in] io_request This parameter specifies the IO request object
 *            to be started.
 *
 * @return This method returns a value indicating if the IO request was
 *         successfully started or not.
 * @retval SCI_SUCCESS This return value indicates successful starting
 *         of the IO request.
 */
PLACEMENT_HINTS((ALWAYS_RESIDENT))
SCI_STATUS scif_sas_io_request_constructed_start_handler(
   SCI_BASE_REQUEST_T * io_request
)
{
   return SCI_SUCCESS;
}

/**
 * @brief This method provides CONSTRUCTED state specific handling for
 *        when the user attempts to abort the supplied IO request.
 *
 * @param[in] io_request This parameter specifies the IO request object
 *            to be aborted.
 *
 * @return This method returns a value indicating if the IO request was
 *         successfully aborted or not.
 * @retval SCI_SUCCESS This return value indicates successful aborting
 *         of the IO request.
 */
PLACEMENT_HINTS((ALWAYS_RESIDENT))
SCI_STATUS scif_sas_io_request_constructed_abort_handler(
   SCI_BASE_REQUEST_T * io_request
)
{
   sci_base_state_machine_change_state(
      &io_request->state_machine, SCI_BASE_REQUEST_STATE_COMPLETED
   );

   return SCI_SUCCESS;
}

//******************************************************************************
//* S T A R T E D   H A N D L E R S
//******************************************************************************

/**
 * @brief This method provides STARTED state specific handling for
 *        when the user attempts to abort the supplied IO request.
 *
 * @param[in] io_request This parameter specifies the IO request object
 *            to be aborted.
 *
 * @return This method returns a value indicating if the aborting the
 *         IO request was successfully started.
 * @retval SCI_SUCCESS This return value indicates that the abort process
 *         began successfully.
 */
PLACEMENT_HINTS((TASK_MANAGEMENT))
SCI_STATUS scif_sas_io_request_started_abort_handler(
   SCI_BASE_REQUEST_T * io_request
)
{
   SCIF_SAS_REQUEST_T * fw_request = (SCIF_SAS_REQUEST_T *) io_request;

   sci_base_state_machine_change_state(
      &io_request->state_machine, SCI_BASE_REQUEST_STATE_ABORTING
   );

   return fw_request->status;
}

/**
 * @brief This method provides STARTED state specific handling for
 *        when the user attempts to complete the supplied IO request.
 *
 * @param[in] io_request This parameter specifies the IO request object
 *            to be completed.
 *
 * @return This method returns a value indicating if the completion of the
 *         IO request was successful.
 * @retval SCI_SUCCESS This return value indicates that the completion process
 *         was successful.
 */
PLACEMENT_HINTS((ALWAYS_RESIDENT))
SCI_STATUS scif_sas_io_request_started_complete_handler(
   SCI_BASE_REQUEST_T * io_request
)
{
   sci_base_state_machine_change_state(
      &io_request->state_machine, SCI_BASE_REQUEST_STATE_COMPLETED
   );

   return SCI_SUCCESS;
}

//******************************************************************************
//* C O M P L E T E D   H A N D L E R S
//******************************************************************************

/**
 * @brief This method provides COMPLETED state specific handling for
 *        when the user attempts to destruct the supplied IO request.
 *
 * @param[in] io_request This parameter specifies the IO request object
 *            to be destructed.
 *
 * @return This method returns a value indicating if the destruct
 *         operation was successful.
 * @retval SCI_SUCCESS This return value indicates that the destruct
 *         was successful.
 */
PLACEMENT_HINTS((ALWAYS_RESIDENT))
SCI_STATUS scif_sas_io_request_completed_destruct_handler(
   SCI_BASE_REQUEST_T * io_request
)
{
   sci_base_state_machine_change_state(
      &io_request->state_machine, SCI_BASE_REQUEST_STATE_FINAL
   );

   sci_base_state_machine_logger_deinitialize(
      &io_request->state_machine_logger,
      &io_request->state_machine
   );

   return SCI_SUCCESS;
}

//******************************************************************************
//* A B O R T I N G   H A N D L E R S
//******************************************************************************

/**
 * @brief This method provides ABORTING state specific handlering for when the
 *        user attempts to abort the supplied IO request.
 *
 * @param[in] io_request This parameter specifies the IO request object
 *            to be completed.
 *
 * @return This method returns a value indicating if the completion
 *         operation was successful.
 * @retval SCI_SUCCESS This return value indicates that the abort operation
 *         was successful.
 */
PLACEMENT_HINTS((TASK_MANAGEMENT))
SCI_STATUS scif_sas_io_request_aborting_abort_handler(
   SCI_BASE_REQUEST_T * io_request
)
{
   SCIF_SAS_IO_REQUEST_T * fw_request = (SCIF_SAS_IO_REQUEST_T *) io_request;

   return scic_controller_terminate_request(
             fw_request->parent.device->domain->controller->core_object,
             fw_request->parent.device->core_object,
             fw_request->parent.core_object
          );
}

/**
 * @brief This method provides ABORTING state specific handling for
 *        when the user attempts to complete the supplied IO request.
 *
 * @param[in] io_request This parameter specifies the IO request object
 *            to be completed.
 *
 * @return This method returns a value indicating if the completion
 *         operation was successful.
 * @retval SCI_SUCCESS This return value indicates that the completion
 *         was successful.
 */
PLACEMENT_HINTS((TASK_MANAGEMENT))
SCI_STATUS scif_sas_io_request_aborting_complete_handler(
   SCI_BASE_REQUEST_T * io_request
)
{
   sci_base_state_machine_change_state(
      &io_request->state_machine, SCI_BASE_REQUEST_STATE_COMPLETED
   );

   return SCI_SUCCESS;
}

//******************************************************************************
//* D E F A U L T   H A N D L E R S
//******************************************************************************

/**
 * @brief This method provides DEFAULT handling for when the user
 *        attempts to start the supplied IO request.
 *
 * @param[in] io_request This parameter specifies the IO request object
 *            to be started.
 *
 * @return This method returns an indication that the start operation is
 *         not allowed.
 * @retval SCI_FAILURE_INVALID_STATE This value is always returned.
 */
PLACEMENT_HINTS((TBD))
SCI_STATUS scif_sas_io_request_default_start_handler(
   SCI_BASE_REQUEST_T * io_request
)
{
   SCIF_LOG_ERROR((
      sci_base_object_get_logger((SCIF_SAS_IO_REQUEST_T *) io_request),
      SCIF_LOG_OBJECT_IO_REQUEST,
      "IoRequest:0x%p State:0x%x invalid state to start\n",
      io_request,
      sci_base_state_machine_get_state(
         &((SCIF_SAS_IO_REQUEST_T *) io_request)->parent.parent.state_machine)
   ));

   return SCI_FAILURE_INVALID_STATE;
}

/**
 * @brief This method provides DEFAULT handling for when the user
 *        attempts to abort the supplied IO request.
 *
 * @param[in] io_request This parameter specifies the IO request object
 *            to be aborted.
 *
 * @return This method returns an indication that the abort operation is
 *         not allowed.
 * @retval SCI_FAILURE_INVALID_STATE This value is always returned.
 */
PLACEMENT_HINTS((TBD))
SCI_STATUS scif_sas_io_request_default_abort_handler(
   SCI_BASE_REQUEST_T * io_request
)
{
   SCIF_LOG_ERROR((
      sci_base_object_get_logger((SCIF_SAS_IO_REQUEST_T *) io_request),
      SCIF_LOG_OBJECT_IO_REQUEST,
      "IoRequest:0x%p State:0x%x invalid state to abort\n",
      io_request,
      sci_base_state_machine_get_state(
         &((SCIF_SAS_IO_REQUEST_T *) io_request)->parent.parent.state_machine)
   ));

   return SCI_FAILURE_INVALID_STATE;
}

/**
 * @brief This method provides DEFAULT handling for when the user
 *        attempts to complete the supplied IO request.
 *
 * @param[in] io_request This parameter specifies the IO request object
 *            to be completed.
 *
 * @return This method returns an indication that complete operation is
 *         not allowed.
 * @retval SCI_FAILURE_INVALID_STATE This value is always returned.
 */
PLACEMENT_HINTS((TBD))
SCI_STATUS scif_sas_io_request_default_complete_handler(
   SCI_BASE_REQUEST_T * io_request
)
{
   SCIF_LOG_ERROR((
      sci_base_object_get_logger((SCIF_SAS_IO_REQUEST_T *) io_request),
      SCIF_LOG_OBJECT_IO_REQUEST,
      "IoRequest:0x%p State:0x%x invalid state to complete\n",
      io_request,
      sci_base_state_machine_get_state(
         &((SCIF_SAS_IO_REQUEST_T *) io_request)->parent.parent.state_machine)
   ));

   return SCI_FAILURE_INVALID_STATE;
}

/**
 * @brief This method provides DEFAULT handling for when the user
 *        attempts to destruct the supplied IO request.
 *
 * @param[in] io_request This parameter specifies the IO request object
 *            to be destructed.
 *
 * @return This method returns an indication that destruct operation is
 *         not allowed.
 * @retval SCI_FAILURE_INVALID_STATE This value is always returned.
 */
PLACEMENT_HINTS((TBD))
SCI_STATUS scif_sas_io_request_default_destruct_handler(
   SCI_BASE_REQUEST_T * io_request
)
{
   SCIF_LOG_ERROR((
      sci_base_object_get_logger((SCIF_SAS_IO_REQUEST_T *) io_request),
      SCIF_LOG_OBJECT_IO_REQUEST,
      "IoRequest:0x%p State:0x%x invalid state to destruct.\n",
      io_request,
      sci_base_state_machine_get_state(
         &((SCIF_SAS_IO_REQUEST_T *) io_request)->parent.parent.state_machine)
   ));

   return SCI_FAILURE_INVALID_STATE;
}


SCI_BASE_REQUEST_STATE_HANDLER_T scif_sas_io_request_state_handler_table[] =
{
   // SCI_BASE_REQUEST_STATE_INITIAL
   {
      scif_sas_io_request_default_start_handler,
      scif_sas_io_request_default_abort_handler,
      scif_sas_io_request_default_complete_handler,
      scif_sas_io_request_default_destruct_handler
   },
   // SCI_BASE_REQUEST_STATE_CONSTRUCTED
   {
      scif_sas_io_request_constructed_start_handler,
      scif_sas_io_request_constructed_abort_handler,
      scif_sas_io_request_default_complete_handler,
      scif_sas_io_request_default_destruct_handler
   },
   // SCI_BASE_REQUEST_STATE_STARTED
   {
      scif_sas_io_request_default_start_handler,
      scif_sas_io_request_started_abort_handler,
      scif_sas_io_request_started_complete_handler,
      scif_sas_io_request_default_destruct_handler
   },
   // SCI_BASE_REQUEST_STATE_COMPLETED
   {
      scif_sas_io_request_default_start_handler,
      scif_sas_io_request_default_abort_handler,
      scif_sas_io_request_default_complete_handler,
      scif_sas_io_request_completed_destruct_handler
   },
   // SCI_BASE_REQUEST_STATE_ABORTING
   {
      scif_sas_io_request_default_start_handler,
      scif_sas_io_request_aborting_abort_handler,
      scif_sas_io_request_aborting_complete_handler,
      scif_sas_io_request_default_destruct_handler
   },
   // SCI_BASE_REQUEST_STATE_FINAL
   {
      scif_sas_io_request_default_start_handler,
      scif_sas_io_request_default_abort_handler,
      scif_sas_io_request_default_complete_handler,
      scif_sas_io_request_default_destruct_handler
   },
};
