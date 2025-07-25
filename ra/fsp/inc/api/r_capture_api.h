/*
* Copyright (c) 2020 - 2025 Renesas Electronics Corporation and/or its affiliates
*
* SPDX-License-Identifier: BSD-3-Clause
*/

/*******************************************************************************************************************//**
 * @ingroup RENESAS_GRAPHICS_INTERFACES
 * @defgroup CAPTURE_API CAPTURE Interface
 * @brief Interface for CAPTURE functions.
 *
 * @section CAPTURE_API_SUMMARY Summary
 * The CAPTURE interface provides the functionality for capturing an image from an image sensor/camera.
 * When a capture is complete a capture complete interrupt is triggered.
 *
 *
 * @{
 **********************************************************************************************************************/

#ifndef R_CAPTURE_API_H
#define R_CAPTURE_API_H

/***********************************************************************************************************************
 * Includes
 **********************************************************************************************************************/

/* Register definitions, common services and error codes. */
#include "bsp_api.h"

/* Common macro for FSP header files. There is also a corresponding FSP_FOOTER macro at the end of this file. */
FSP_HEADER

/**********************************************************************************************************************
 * Macro definitions
 **********************************************************************************************************************/

/**********************************************************************************************************************
 * Typedef definitions
 **********************************************************************************************************************/

/** CAPTURE states. */
typedef enum e_capture_state
{
    CAPTURE_STATE_IDLE        = 0,     ///< CAPTURE is idle
    CAPTURE_STATE_IN_PROGRESS = 1,     ///< CAPTURE capture in progress
    CAPTURE_STATE_BUSY        = 2,     ///< CAPTURE reset in progress
} capture_state_t;

/** CAPTURE status */
typedef struct e_capture_status
{
    capture_state_t state;             ///< Current capture state
    uint32_t      * p_buffer;          ///< Pointer to active buffer
    uint32_t        data_size;         ///< Size of data written to provided buffer
} capture_status_t;

/** CAPTURE callback event ID - see implementation for details */
typedef uint32_t capture_event_t;

/** CAPTURE callback function parameter data */
typedef struct st_capture_callback_args
{
    capture_event_t event;             ///< Event causing the callback - See instance header for details.
    uint32_t        event_status;      ///< Capture status data corresponding to given event type - See instance header for details.
    uint32_t        interrupt_status;  ///< Module interrupt status data corresponding to given event type - See instance header for details.
    uint8_t       * p_buffer;          ///< Pointer to buffer that contains the most recently captured data
    void          * p_context;         ///< Placeholder for user data.  Set in @ref capture_api_t::open function in @ref capture_cfg_t.
} capture_callback_args_t;

/** CAPTURE configuration parameters. */
typedef struct st_capture_cfg
{
    uint16_t x_capture_start_pixel;                        ///< Horizontal position to start capture
    uint16_t x_capture_pixels;                             ///< Number of horizontal pixels to capture
    uint16_t y_capture_start_pixel;                        ///< Vertical position to start capture
    uint16_t y_capture_pixels;                             ///< Number of vertical lines/pixels to capture
    uint8_t  bytes_per_pixel;                              ///< Number of bytes per pixel
    void (* p_callback)(capture_callback_args_t * p_args); ///< Callback provided when a CAPTURE transfer ISR occurs
    void       * p_context;                                ///< User defined context passed to callback function
    void const * p_extend;                                 ///< Extension parameter for hardware specific settings
} capture_cfg_t;

/** CAPTURE control block.  Allocate an instance specific control block to pass into the CAPTURE API calls.
 */
typedef void capture_ctrl_t;

/** CAPTURE functions implemented at the HAL layer will follow this API. */
typedef struct st_capture_api
{
    /** Initial configuration.
     *
     * @note To reconfigure after calling this function, call @ref capture_api_t::close first.
     * @param[in]  p_ctrl       Pointer to control structure.
     * @param[in]  p_cfg        Pointer to pin configuration structure.
     */
    fsp_err_t (* open)(capture_ctrl_t * const p_ctrl, capture_cfg_t const * const p_cfg);

    /** Closes the driver and allows reconfiguration. May reduce power consumption.
     *
     * @param[in]  p_ctrl       Pointer to control structure.
     */
    fsp_err_t (* close)(capture_ctrl_t * const p_ctrl);

    /** Start a capture.
     *
     * @param[in]  p_ctrl       Pointer to control structure.
     * @param[in]  p_buffer     New pointer to store captured image data.
     */
    fsp_err_t (* captureStart)(capture_ctrl_t * const p_ctrl, uint8_t * const p_buffer);

    /**
     * Specify callback function and optional context pointer and working memory pointer.
     *
     * @param[in]   p_ctrl                   Pointer to the CAPTURE control block.
     * @param[in]   p_callback               Callback function
     * @param[in]   p_context                Pointer to send to callback function
     * @param[in]   p_working_memory         Pointer to volatile memory where callback structure can be allocated.
     *                                       Callback arguments allocated here are only valid during the callback.
     */
    fsp_err_t (* callbackSet)(capture_ctrl_t * const p_ctrl, void (* p_callback)(capture_callback_args_t *),
                              void * const p_context, capture_callback_args_t * const p_callback_memory);

    /** Check scan status.
     *
     * @param[in]  p_ctrl   Pointer to control handle structure
     * @param[out] p_status Pointer to store current status in
     */
    fsp_err_t (* statusGet)(capture_ctrl_t * const p_ctrl, capture_status_t * p_status);
} capture_api_t;

/** This structure encompasses everything that is needed to use an instance of this interface. */
typedef struct st_capture_instance
{
    capture_ctrl_t      * p_ctrl;      ///< Pointer to the control structure for this instance
    capture_cfg_t const * p_cfg;       ///< Pointer to the configuration structure for this instance
    capture_api_t const * p_api;       ///< Pointer to the API structure for this instance
} capture_instance_t;

/* Common macro for FSP header files. There is also a corresponding FSP_HEADER macro at the top of this file. */
FSP_FOOTER

#endif                                 // R_CAPTURE_H

/*******************************************************************************************************************//**
 * @} (end addtogroup CAPTURE_API)
 **********************************************************************************************************************/
