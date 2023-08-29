/*
    ChibiOS - Copyright (C) 2006..2016 Giovanni Di Sirio

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/

/**
 * @file    hal_serial_usb.c
 * @brief   Serial over USB Driver code.
 *
 * @addtogroup SERIAL_USB
 * @{
 */

#include "hal.h"

#if (HAL_USE_SERIAL_USB == TRUE) || defined(__DOXYGEN__)

#ifndef USB_WRITE_TIMEOUT
#define USB_WRITE_TIMEOUT TIME_INFINITE
#endif

#ifndef USB_READ_TIMEOUT
#define USB_READ_TIMEOUT TIME_INFINITE
#endif

/*===========================================================================*/
/* Driver local definitions.                                                 */
/*===========================================================================*/

/*===========================================================================*/
/* Driver exported variables.                                                */
/*===========================================================================*/

/*===========================================================================*/
/* Driver local variables and types.                                         */
/*===========================================================================*/

/*
 * Current Line Coding.
 */

cdc_linecoding_t linecoding = {
  {0x00, 0x40, 0x38, 0x00},             /* 0x00384000 = 3686400              */
  LC_STOP_1, LC_PARITY_NONE, 8
};

#define DISABLE_TIME_FUNCTIONS
/*===========================================================================*/
/* Driver local functions.                                                   */
/*===========================================================================*/

/*
 * Interface implementation.
 */

static size_t write(void *ip, const uint8_t *bp, size_t n) {
  if (usbGetDriverStateI(((SerialUSBDriver *)ip)->config->usbp) != USB_ACTIVE) {
    return 0;
  }
  return obqWriteTimeout(&((SerialUSBDriver *)ip)->obqueue, bp, n, USB_WRITE_TIMEOUT);
}

static size_t read(void *ip, uint8_t *bp, size_t n) {
//  if (usbGetDriverStateI(((SerialUSBDriver *)ip)->config->usbp) != USB_ACTIVE) {
//    return 0;
//  }
  return ibqReadTimeout(&((SerialUSBDriver *)ip)->ibqueue, bp, n, USB_READ_TIMEOUT);
}

static msg_t put(void *ip, uint8_t b) {
  if (usbGetDriverStateI(((SerialUSBDriver *)ip)->config->usbp) != USB_ACTIVE) {
    return MSG_RESET;
  }
  return obqPutTimeout(&((SerialUSBDriver *)ip)->obqueue, b, USB_WRITE_TIMEOUT);
}

static msg_t get(void *ip) {
//  if (usbGetDriverStateI(((SerialUSBDriver *)ip)->config->usbp) != USB_ACTIVE) {
//    return MSG_RESET;
//  }
  return ibqGetTimeout(&((SerialUSBDriver *)ip)->ibqueue, USB_READ_TIMEOUT);
}

#ifndef DISABLE_TIME_FUNCTIONS
static msg_t putt(void *ip, uint8_t b, systime_t timeout) {
  return obqPutTimeout(&((SerialUSBDriver *)ip)->obqueue, b, timeout);
}

static msg_t gett(void *ip, systime_t timeout) {
  return ibqGetTimeout(&((SerialUSBDriver *)ip)->ibqueue, timeout);
}

static size_t writet(void *ip, const uint8_t *bp, size_t n, systime_t timeout) {
  return obqWriteTimeout(&((SerialUSBDriver *)ip)->obqueue, bp, n, timeout);
}

static size_t readt(void *ip, uint8_t *bp, size_t n, systime_t timeout) {
  return ibqReadTimeout(&((SerialUSBDriver *)ip)->ibqueue, bp, n, timeout);
}
#endif

static const struct SerialUSBDriverVMT vmt = {
  write, read, put, get,
#ifndef DISABLE_TIME_FUNCTIONS
  putt, gett, writet, readt
#else
  NULL, NULL, NULL, NULL
#endif
};

static bool sdu_start_receive(SerialUSBDriver *sdup) {
  uint8_t *buf;

  /* If the USB driver is not in the appropriate state then transactions must not be started.*/
  if ((usbGetDriverStateI(sdup->config->usbp) != USB_ACTIVE) || (sdup->state != SDU_READY)) {
    return true;
  }

  /* Checking if there is already a transaction ongoing on the endpoint.*/
  if (usbGetReceiveStatusI(sdup->config->usbp, sdup->config->bulk_out)) {
    return true;
  }

  /* Checking if there is a buffer ready for incoming data.*/
  buf = ibqGetEmptyBufferI(&sdup->ibqueue);
  if (buf == NULL) {
    return true;
  }

  /* Buffer found, starting a new transaction.*/
  usbStartReceiveI(sdup->config->usbp, sdup->config->bulk_out, buf, SERIAL_USB_RX_BUFFERS_SIZE);

  return false;
}


/**
 * @brief   Notification of empty buffer released into the input buffers queue.
 *
 * @param[in] bqp       the buffers queue pointer.
 */
static void ibnotify(io_buffers_queue_t *bqp) {
  SerialUSBDriver *sdup = bqGetLinkX(bqp);
  (void) sdu_start_receive(sdup);
}

/**
 * @brief   Notification of filled buffer inserted into the output buffers queue.
 *
 * @param[in] bqp       the buffers queue pointer.
 */
static void obnotify(io_buffers_queue_t *bqp) {
  size_t n;
  SerialUSBDriver *sdup = bqGetLinkX(bqp);

  /* If the USB driver is not in the appropriate state then transactions must not be started.*/
  if ((usbGetDriverStateI(sdup->config->usbp) != USB_ACTIVE) ||
      (sdup->state != SDU_READY) ||
      usbGetTransmitStatusI(sdup->config->usbp, sdup->config->bulk_in)) {
    return;
  }
  /* Trying to get a full buffer.*/
  uint8_t *buf = obqGetFullBufferI(&sdup->obqueue, &n);
  if (buf == NULL)  return;

  /* Buffer found, starting a new transaction.*/
  usbStartTransmitI(sdup->config->usbp, sdup->config->bulk_in, buf, n);
}

/*===========================================================================*/
/* Driver exported functions.                                                */
/*===========================================================================*/

/**
 * @brief   Serial Driver initialization.
 * @note    This function is implicitly invoked by @p halInit(), there is
 *          no need to explicitly initialize the driver.
 *
 * @init
 */
void sduInit(void) {
}

/**
 * @brief   Initializes a generic full duplex driver object.
 * @details The HW dependent part of the initialization has to be performed
 *          outside, usually in the hardware initialization code.
 *
 * @param[out] sdup     pointer to a @p SerialUSBDriver structure
 *
 * @init
 */
void sduObjectInit(SerialUSBDriver *sdup) {

  sdup->vmt = &vmt;
  //osalEventObjectInit(&sdup->event);
  sdup->state = SDU_STOP;
  bqObjectInit(&sdup->ibqueue, sdup->ib,
               SERIAL_USB_RX_BUFFERS_SIZE, SERIAL_USB_RX_BUFFERS_NUMBER,
               ibnotify, sdup);
  bqObjectInit(&sdup->obqueue, sdup->ob,
               SERIAL_USB_TX_BUFFERS_SIZE, SERIAL_USB_TX_BUFFERS_NUMBER,
               obnotify, sdup);
}

/**
 * @brief   Configures and starts the driver.
 *
 * @param[in] sdup      pointer to a @p SerialUSBDriver object
 * @param[in] config    the serial over USB driver configuration
 *
 * @api
 */
void sduStart(SerialUSBDriver *sdup, const SerialUSBConfig *config) {
  USBDriver *usbp = config->usbp;

  osalDbgCheck(sdup != NULL);

  osalSysLock();
  osalDbgAssert((sdup->state == SDU_STOP) || (sdup->state == SDU_READY),
                "invalid state");
  usbp->in_params[config->bulk_in - 1U]   = sdup;
  usbp->out_params[config->bulk_out - 1U] = sdup;
  if (config->int_in > 0U) {
    usbp->in_params[config->int_in - 1U]  = sdup;
  }
  sdup->config = config;
  sdup->state = SDU_READY;
  osalSysUnlock();
}

/**
 * @brief   Stops the driver.
 * @details Any thread waiting on the driver's queues will be awakened with
 *          the message @p MSG_RESET.
 *
 * @param[in] sdup      pointer to a @p SerialUSBDriver object
 *
 * @api
 */
void sduStop(SerialUSBDriver *sdup) {
  USBDriver *usbp = sdup->config->usbp;

  osalDbgCheck(sdup != NULL);

  osalSysLock();
  osalDbgAssert((sdup->state == SDU_STOP) || (sdup->state == SDU_READY),
                "invalid state");

  /* Driver in stopped state.*/
  usbp->in_params[sdup->config->bulk_in - 1U]   = NULL;
  usbp->out_params[sdup->config->bulk_out - 1U] = NULL;
  if (sdup->config->int_in > 0U) {
    usbp->in_params[sdup->config->int_in - 1U]  = NULL;
  }
  sdup->state = SDU_STOP;

  /* Enforces a disconnection.*/
  sduDisconnectI(sdup);
  osalOsRescheduleS();
  osalSysUnlock();
}

/**
 * @brief   USB device disconnection handler.
 * @note    If this function is not called from an ISR then an explicit call
 *          to @p osalOsRescheduleS() in necessary afterward.
 *
 * @param[in] sdup      pointer to a @p SerialUSBDriver object
 *
 * @iclass
 */
void sduDisconnectI(SerialUSBDriver *sdup) {

  /* Queues reset in order to signal the driver stop to the application.*/
  chnAddFlagsI(sdup, CHN_DISCONNECTED);
  bqResetI(&sdup->ibqueue);
  bqResetI(&sdup->obqueue);
}

/**
 * @brief   USB device configured handler.
 *
 * @param[in] sdup      pointer to a @p SerialUSBDriver object
 *
 * @iclass
 */
void sduConfigureHookI(SerialUSBDriver *sdup) {
  bqResetI(&sdup->ibqueue);
  bqResetI(&sdup->obqueue);
  chnAddFlagsI(sdup, CHN_CONNECTED);
  (void) sdu_start_receive(sdup);
}

/**
 * @brief   Default requests hook.
 * @details Applications wanting to use the Serial over USB driver can use
 *          this function as requests hook in the USB configuration.
 *          The following requests are emulated:
 *          - CDC_GET_LINE_CODING.
 *          - CDC_SET_LINE_CODING.
 *          - CDC_SET_CONTROL_LINE_STATE.
 *          .
 *
 * @param[in] usbp      pointer to the @p USBDriver object
 * @return              The hook status.
 * @retval true         Message handled internally.
 * @retval false        Message not handled.
 */
bool sduRequestsHook(USBDriver *usbp) {

  if ((usbp->setup.bmRequestType & USB_RTYPE_TYPE_MASK) == USB_RTYPE_TYPE_CLASS) {
    switch (usbp->setup.bRequest) {
    case CDC_GET_LINE_CODING:
      usbSetupTransfer(usbp, (uint8_t *)&linecoding, sizeof(linecoding), NULL);
      return true;
    case CDC_SET_LINE_CODING:
      usbSetupTransfer(usbp, (uint8_t *)&linecoding, sizeof(linecoding), NULL);
      return true;
    case CDC_SET_CONTROL_LINE_STATE:
      /* Nothing to do, there are no control lines.*/
      usbSetupTransfer(usbp, NULL, 0, NULL);
      return true;
    default:
      return false;
    }
  }
  return false;
}

/**
 * @brief   SOF handler.
 * @details The SOF interrupt is used for automatic flushing of incomplete
 *          buffers pending in the output queue.
 *
 * @param[in] sdup      pointer to a @p SerialUSBDriver object
 *
 * @iclass
 */
void sduSOFHookI(SerialUSBDriver *sdup) {

  /* If the USB driver is not in the appropriate state then transactions
     must not be started.*/
  if ((usbGetDriverStateI(sdup->config->usbp) != USB_ACTIVE) ||
      (sdup->state != SDU_READY)) {
    return;
  }

  /* If there is already a transaction ongoing then another one cannot be
     started.*/
  if (usbGetTransmitStatusI(sdup->config->usbp, sdup->config->bulk_in)) {
    return;
  }

  /* Checking if there only a buffer partially filled, if so then it is
     enforced in the queue and transmitted.*/
  if (obqTryFlushI(&sdup->obqueue)) {
    size_t n;
    uint8_t *buf = obqGetFullBufferI(&sdup->obqueue, &n);

    osalDbgAssert(buf != NULL, "queue is empty");

    usbStartTransmitI(sdup->config->usbp, sdup->config->bulk_in, buf, n);
  }
}

/**
 * @brief   Default data transmitted callback.
 * @details The application must use this function as callback for the IN
 *          data endpoint.
 *
 * @param[in] usbp      pointer to the @p USBDriver object
 * @param[in] ep        IN endpoint number
 */
void sduDataTransmitted(USBDriver *usbp, usbep_t ep) {
  uint8_t *buf;
  size_t n;
  SerialUSBDriver *sdup = usbp->in_params[ep - 1U];

  if (sdup == NULL) {
    return;
  }

  osalSysLockFromISR();

  /* Signaling that space is available in the output queue.*/
  chnAddFlagsI(sdup, CHN_OUTPUT_EMPTY);

  /* Freeing the buffer just transmitted, if it was not a zero size packet.*/
  if (usbp->epc[ep]->in_state->txsize > 0U) {
    obqReleaseEmptyBufferI(&sdup->obqueue);
  }

  /* Checking if there is a buffer ready for transmission.*/
  buf = obqGetFullBufferI(&sdup->obqueue, &n);

  if (buf != NULL) {
    /* The endpoint cannot be busy, we are in the context of the callback,
       so it is safe to transmit without a check.*/
    usbStartTransmitI(usbp, ep, buf, n);
  }
  else if (usbp->epc[ep]->in_state->txsize == usbp->epc[ep]->in_maxsize) {
    /* Transmit zero sized packet in case the last one has maximum allowed
       size. Otherwise the recipient may expect more data coming soon and
       not return buffered data to app. See section 5.8.3 Bulk Transfer
       Packet Size Constraints of the USB Specification document.*/
    usbStartTransmitI(usbp, ep, NULL, 0);
  }
  else {
    /* Nothing to transmit.*/
  }

  osalSysUnlockFromISR();
}

/**
 * @brief   Default data received callback.
 * @details The application must use this function as callback for the OUT
 *          data endpoint.
 *
 * @param[in] usbp      pointer to the @p USBDriver object
 * @param[in] ep        OUT endpoint number
 */
void sduDataReceived(USBDriver *usbp, usbep_t ep) {
  size_t size;
  SerialUSBDriver *sdup = usbp->out_params[ep - 1U];

  if (sdup == NULL) {
    return;
  }

  osalSysLockFromISR();
  /* Checking for zero-size transactions.*/
  size = usbGetReceiveTransactionSizeX(sdup->config->usbp,
                                       sdup->config->bulk_out);
  if (size > (size_t)0) {
    /* Signaling that data is available in the input queue.*/
    chnAddFlagsI(sdup, CHN_INPUT_AVAILABLE);

    /* Posting the filled buffer in the queue.*/
    ibqPostFullBufferI(&sdup->ibqueue, size);
  }

  /* The endpoint cannot be busy, we are in the context of the callback,
     so a packet is in the buffer for sure. Trying to get a free buffer
     for the next transaction.*/
  (void) sdu_start_receive(sdup);

  osalSysUnlockFromISR();
}

/**
 * @brief   Default data received callback.
 * @details The application must use this function as callback for the IN
 *          interrupt endpoint.
 *
 * @param[in] usbp      pointer to the @p USBDriver object
 * @param[in] ep        endpoint number
 */
void sduInterruptTransmitted(USBDriver *usbp, usbep_t ep) {

  (void)usbp;
  (void)ep;
}

#endif /* HAL_USE_SERIAL_USB == TRUE */

/** @} */
