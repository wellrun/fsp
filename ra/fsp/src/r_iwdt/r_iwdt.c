/***********************************************************************************************************************
 * Copyright [2020] Renesas Electronics Corporation and/or its affiliates.  All Rights Reserved.
 *
 * This software and documentation are supplied by Renesas Electronics America Inc. and may only be used with products
 * of Renesas Electronics Corp. and its affiliates ("Renesas").  No other uses are authorized.  Renesas products are
 * sold pursuant to Renesas terms and conditions of sale.  Purchasers are solely responsible for the selection and use
 * of Renesas products and Renesas assumes no liability.  No license, express or implied, to any intellectual property
 * right is granted by Renesas. This software is protected under all applicable laws, including copyright laws. Renesas
 * reserves the right to change or discontinue this software and/or this documentation. THE SOFTWARE AND DOCUMENTATION
 * IS DELIVERED TO YOU "AS IS," AND RENESAS MAKES NO REPRESENTATIONS OR WARRANTIES, AND TO THE FULLEST EXTENT
 * PERMISSIBLE UNDER APPLICABLE LAW, DISCLAIMS ALL WARRANTIES, WHETHER EXPLICITLY OR IMPLICITLY, INCLUDING WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, AND NONINFRINGEMENT, WITH RESPECT TO THE SOFTWARE OR
 * DOCUMENTATION.  RENESAS SHALL HAVE NO LIABILITY ARISING OUT OF ANY SECURITY VULNERABILITY OR BREACH.  TO THE MAXIMUM
 * EXTENT PERMITTED BY LAW, IN NO EVENT WILL RENESAS BE LIABLE TO YOU IN CONNECTION WITH THE SOFTWARE OR DOCUMENTATION
 * (OR ANY PERSON OR ENTITY CLAIMING RIGHTS DERIVED FROM YOU) FOR ANY LOSS, DAMAGES, OR CLAIMS WHATSOEVER, INCLUDING,
 * WITHOUT LIMITATION, ANY DIRECT, CONSEQUENTIAL, SPECIAL, INDIRECT, PUNITIVE, OR INCIDENTAL DAMAGES; ANY LOST PROFITS,
 * OTHER ECONOMIC DAMAGE, PROPERTY DAMAGE, OR PERSONAL INJURY; AND EVEN IF RENESAS HAS BEEN ADVISED OF THE POSSIBILITY
 * OF SUCH LOSS, DAMAGES, CLAIMS OR COSTS.
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * Includes
 **********************************************************************************************************************/
#include "r_wdt_api.h"
#include "r_iwdt.h"
#include "bsp_api.h"
#include "bsp_cfg.h"

/***********************************************************************************************************************
 * Macro definitions
 **********************************************************************************************************************/
#define IWDT_OPEN                           (0X49574454ULL)
#define IWDT_PRV_OFS0_AUTO_START_MASK       (0x00000002U)
#define IWDT_PRV_OFS0_NMI_REQUEST_MASK      (0x00001000U)
#define IWDT_PRV_OFS0_TIMEOUT_MASK          (0x0000000CU)
#define IWDT_PRV_OFS0_CLOCK_DIVIDER_MASK    (0x000000F0U)
#define IWDT_PRV_OFS0_WINDOW_END_MASK       (0x00000300U)
#define IWDT_PRV_OFS0_WINDOW_START_MASK     (0x00000C00U)
#define IWDT_PRV_OFS0_RESET_CONTROL_MASK    (0x00001000U)
#define IWDT_PRV_OFS0_STOP_CONTROL_MASK     (0x00004000U)
#define IWDT_PRV_IWDTSR_COUNTER_MASK        (0x3FFFU)
#define IWDT_PRV_IWDTSR_FLAGS_MASK          (0x0000C000U)
#define IWDT_PRV_STATUS_START_BIT           (14U) /*Bit 14 and Bit 15*/

/* Refresh register values */
#define IWDT_PRV_REFRESH_STEP_1             (0U)
#define IWDT_PRV_REFRESH_STEP_2             (0xFFU)

/* Clock frequency of IWDT */
#define IWDT_CLOCK_FREQUENCY                15000UL

/* Macros for start mode and NMI support. */
#if ((BSP_CFG_ROM_REG_OFS0 & IWDT_PRV_OFS0_AUTO_START_MASK) == 0)
 #define IWDT_PRV_AUTO_START_MODE           (1)
#else

/* Auto start mode */
 #define IWDT_PRV_AUTO_START_MODE           (0)
#endif
#if ((BSP_CFG_ROM_REG_OFS0 & IWDT_PRV_OFS0_NMI_REQUEST_MASK) == 0)
 #define IWDT_PRV_NMI_SUPPORTED             (1)
#else
 #define IWDT_PRV_NMI_SUPPORTED             (0)
#endif

/***********************************************************************************************************************
 * Typedef definitions
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * Private function prototypes
 **********************************************************************************************************************/
static void iwdt_nmi_internal_callback(bsp_grp_irq_t irq);

static uint32_t wdt_clock_divider_get(wdt_clock_division_t division);

/***********************************************************************************************************************
 * Private global variables
 **********************************************************************************************************************/

/* Version data structure used by error logger macro. */
static const fsp_version_t g_iwdt_version =
{
    .api_version_minor  = WDT_API_VERSION_MINOR,
    .api_version_major  = WDT_API_VERSION_MAJOR,
    .code_version_major = IWDT_CODE_VERSION_MAJOR,
    .code_version_minor = IWDT_CODE_VERSION_MINOR
};

static const uint8_t g_ofs0_timeout[] =
{
    0x00U,                             ///< IWDTCR value for WDT_TIMEOUT_128
    0x04U,                             ///< IWDTCR value for WDT_TIMEOUT_512
    0x08U,                             ///< IWDTCR value for WDT_TIMEOUT_1024
    0x0CU,                             ///< WDTCR value for WDT_TIMEOUT_2048
    0xFFU,                             ///< WDTCR value for WDT_TIMEOUT_4096 (not supported by IWDT)
    0xFFU,                             ///< WDTCR value for WDT_TIMEOUT_8192 (not supported by IWDT)
    0xFFU,                             ///< WDTCR value for WDT_TIMEOUT_16384 (not supported by IWDT)
};

/* Convert WDT/IWDT timeout value to an integer */
static const uint32_t g_wdt_timeout[] =
{
    128U,
    512U,
    1024U,
    2048U,
    4096U,
    8192U,
    16384U
};

/* Converts IWDT division enum to log base 2 of the division value, used to shift the PCLKB frequency. */
static const uint8_t g_wdt_division_lookup[] =
{
    0U,                                // log base 2(1)    = 0
    2U,                                // log base 2(4)    = 2
    4U,                                // log base 2(16)   = 4
    5U,                                // log base 2(32)   = 5
    6U,                                // log base 2(64)   = 6
    8U,                                // log base 2(256)  = 8
    9U,                                // log base 2(512)  = 9
    11U,                               // log base 2(2048) = 11
    13U,                               // log base 2(8192) = 13
};

/* Global pointer to control structure for use by the NMI callback.  */
static iwdt_instance_ctrl_t * gp_iwdt_ctrl = NULL;

/* Watchdog implementation of IWDT Driver  */
const wdt_api_t g_wdt_on_iwdt =
{
    .open        = R_IWDT_Open,
    .refresh     = R_IWDT_Refresh,
    .statusGet   = R_IWDT_StatusGet,
    .statusClear = R_IWDT_StatusClear,
    .counterGet  = R_IWDT_CounterGet,
    .timeoutGet  = R_IWDT_TimeoutGet,
    .versionGet  = R_IWDT_VersionGet,
};

/*******************************************************************************************************************//**
 * @addtogroup IWDT
 * @{
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * Functions
 **********************************************************************************************************************/

/*******************************************************************************************************************//**
 * Register the IWDT NMI callback.
 *
 * Example:
 * @snippet r_iwdt_example.c R_IWDT_Open
 *
 * @retval FSP_SUCCESS              IWDT successfully configured.
 * @retval FSP_ERR_ASSERTION        Null Pointer.
 * @retval FSP_ERR_NOT_ENABLED      An attempt to open the IWDT when the OFS0 register is not
 *                                  configured for auto-start mode.
 * @retval FSP_ERR_ALREADY_OPEN     Module is already open.  This module can only be opened once.
 **********************************************************************************************************************/
fsp_err_t R_IWDT_Open (wdt_ctrl_t * const p_api_ctrl, wdt_cfg_t const * const p_cfg)
{
    iwdt_instance_ctrl_t * p_ctrl = (iwdt_instance_ctrl_t *) p_api_ctrl;
    FSP_PARAMETER_NOT_USED(p_cfg);
    FSP_PARAMETER_NOT_USED(iwdt_nmi_internal_callback);

#if IWDT_CFG_PARAM_CHECKING_ENABLE
    FSP_ASSERT(NULL != p_cfg);
    FSP_ASSERT(NULL != p_ctrl);
 #if IWDT_PRV_NMI_SUPPORTED
    FSP_ASSERT(NULL != p_cfg->p_callback);
 #endif

    /* Enum checking is done here because some enums in wdt_timeout_t are not supported by the IWDT peripheral (they are
     * included for other implementations of the watchdog interface). */
    FSP_ASSERT((p_cfg->timeout == WDT_TIMEOUT_128) || (p_cfg->timeout == WDT_TIMEOUT_512) || \
               (p_cfg->timeout == WDT_TIMEOUT_1024) || (p_cfg->timeout == WDT_TIMEOUT_2048));

    /* Check the IWDT is enabled. */
    FSP_ERROR_RETURN((0U != (uint32_t) IWDT_PRV_AUTO_START_MODE), FSP_ERR_NOT_ENABLED);

    /* Check to see IWDT is already configured */
    FSP_ERROR_RETURN(IWDT_OPEN != p_ctrl->wdt_open, FSP_ERR_ALREADY_OPEN);
#endif

#if IWDT_PRV_NMI_SUPPORTED

    /* Initialize global pointer to IWDT for NMI callback use. */
    gp_iwdt_ctrl = p_ctrl;
    R_BSP_GroupIrqWrite(BSP_GRP_IRQ_IWDT_ERROR, iwdt_nmi_internal_callback);

    p_ctrl->p_callback = p_cfg->p_callback;
    p_ctrl->p_context  = p_cfg->p_context;

    /* Enable the IWDT underflow/refresh error interrupt to generate an NMI. NMIER bits cannot be cleared after reset,
     * so no need to read-modify-write. */
    R_ICU->NMIER = R_ICU_NMIER_IWDTEN_Msk;
#endif

    p_ctrl->wdt_open = IWDT_OPEN;

    return FSP_SUCCESS;
}

/*******************************************************************************************************************//**
 * Refresh the Independent Watchdog Timer.
 * If the refresh fails due to being performed outside of the
 * permitted refresh period the device will either reset or trigger an NMI ISR to run.
 *
 * Example:
 * @snippet r_iwdt_example.c R_IWDT_Refresh
 *
 * @retval FSP_SUCCESS              IWDT successfully refreshed.
 * @retval FSP_ERR_ASSERTION        One or more parameters are NULL pointers.
 * @retval FSP_ERR_NOT_OPEN         The driver has not been opened. Perform R_IWDT_Open() first.
 **********************************************************************************************************************/
fsp_err_t R_IWDT_Refresh (wdt_ctrl_t * const p_api_ctrl)
{
    FSP_PARAMETER_NOT_USED(p_api_ctrl);

#if (1 == IWDT_CFG_PARAM_CHECKING_ENABLE)
    iwdt_instance_ctrl_t * p_ctrl = (iwdt_instance_ctrl_t *) p_api_ctrl;
    FSP_ASSERT(p_ctrl != NULL);
    FSP_ERROR_RETURN((IWDT_OPEN == p_ctrl->wdt_open), FSP_ERR_NOT_OPEN);
#endif

    /* Described in hardware manual (see Section 26.3.2
     * 'Refresh Operation' of the RA6M3 manual R01UH0886EJ0100). */
    R_IWDT->IWDTRR = IWDT_PRV_REFRESH_STEP_1;
    R_IWDT->IWDTRR = IWDT_PRV_REFRESH_STEP_2;

    return FSP_SUCCESS;
}

/*******************************************************************************************************************//**
 * Read the IWDT status flags.
 * When the IWDT is configured to output a reset on underflow or refresh error reading the status and error flags
 * can be read after reset to establish if the IWDT caused the reset.
 * Reading the status and error flags in NMI output mode indicates whether the IWDT generated the NMI interrupt.
 *
 * Indicates both status and error conditions.
 *
 * Example:
 * @snippet r_iwdt_example.c R_IWDT_StatusGet
 *
 * @retval FSP_SUCCESS              IWDT status successfully read.
 * @retval FSP_ERR_ASSERTION        Null pointer as a parameter.
 * @retval FSP_ERR_NOT_OPEN         The driver has not been opened. Perform R_IWDT_Open() first.
 **********************************************************************************************************************/
fsp_err_t R_IWDT_StatusGet (wdt_ctrl_t * const p_api_ctrl, wdt_status_t * const p_status)
{
#if (1 == IWDT_CFG_PARAM_CHECKING_ENABLE)
    iwdt_instance_ctrl_t * p_ctrl = (iwdt_instance_ctrl_t *) p_api_ctrl;
    FSP_ASSERT(p_ctrl != NULL);
    FSP_ASSERT(p_status != NULL);
    FSP_ERROR_RETURN((IWDT_OPEN == p_ctrl->wdt_open), FSP_ERR_NOT_OPEN);
#endif
    FSP_PARAMETER_NOT_USED(p_api_ctrl);

    *p_status = (wdt_status_t) (R_IWDT->IWDTSR >> (uint32_t) IWDT_PRV_STATUS_START_BIT);

    return FSP_SUCCESS;
}

/*******************************************************************************************************************//**
 * Clear the IWDT status and error flags. Implements @ref wdt_api_t::statusClear.
 *
 * Example:
 * @snippet r_iwdt_example.c R_IWDT_StatusClear
 *
 * @retval FSP_SUCCESS              IWDT flag(s) successfully cleared.
 * @retval FSP_ERR_ASSERTION        Null pointer as a parameter.
 * @retval FSP_ERR_NOT_OPEN         The driver has not been opened. Perform R_IWDT_Open() first.
 **********************************************************************************************************************/
fsp_err_t R_IWDT_StatusClear (wdt_ctrl_t * const p_api_ctrl, const wdt_status_t status)
{
#if (1 == IWDT_CFG_PARAM_CHECKING_ENABLE)
    iwdt_instance_ctrl_t * p_ctrl = (iwdt_instance_ctrl_t *) p_api_ctrl;
    FSP_ASSERT(p_ctrl != NULL);
    FSP_ERROR_RETURN((IWDT_OPEN == p_ctrl->wdt_open), FSP_ERR_NOT_OPEN);
#endif
    FSP_PARAMETER_NOT_USED(p_api_ctrl);
    uint16_t value;
    uint16_t read_value;

    value = (uint16_t) status;

    /* Write zero to clear flags */
    value = (uint16_t) ((uint16_t) (~value) << (uint32_t) IWDT_PRV_STATUS_START_BIT);

    /* Read back status flags until required flag(s) cleared.
     * Flags cannot be cleared until after the clock cycle after they are set.
     * Described in hardware manual (see Section 26.2.2
     * 'IWDT Status Register (IWDTSR)' of the RA6M3 manual R01UH0886EJ0100).
     */
    do
    {
        R_IWDT->IWDTSR = value;
        read_value     = R_IWDT->IWDTSR;
        read_value    &= (uint16_t) ((uint16_t) status << (uint32_t) IWDT_PRV_STATUS_START_BIT);
    } while (read_value);

    return FSP_SUCCESS;
}

/*******************************************************************************************************************//**
 * Read the current count value of the IWDT. Implements @ref wdt_api_t::counterGet.
 *
 * Example:
 * @snippet r_iwdt_example.c R_IWDT_CounterGet
 *
 * @retval FSP_SUCCESS          IWDT current count successfully read.
 * @retval FSP_ERR_ASSERTION    Null pointer passed as a parameter.
 * @retval FSP_ERR_NOT_OPEN     The driver has not been opened. Perform R_IWDT_Open() first.
 **********************************************************************************************************************/
fsp_err_t R_IWDT_CounterGet (wdt_ctrl_t * const p_api_ctrl, uint32_t * const p_count)
{
#if (1 == IWDT_CFG_PARAM_CHECKING_ENABLE)
    iwdt_instance_ctrl_t * p_ctrl = (iwdt_instance_ctrl_t *) p_api_ctrl;
    FSP_ASSERT(p_ctrl != NULL);
    FSP_ASSERT(p_count != NULL);
    FSP_ERROR_RETURN((IWDT_OPEN == p_ctrl->wdt_open), FSP_ERR_NOT_OPEN);
#endif
    FSP_PARAMETER_NOT_USED(p_api_ctrl);

    (*p_count) = (uint32_t) R_IWDT->IWDTSR & (uint32_t) IWDT_PRV_IWDTSR_COUNTER_MASK;

    return FSP_SUCCESS;
}

/*******************************************************************************************************************//**
 * Read timeout information for the watchdog timer. Implements @ref wdt_api_t::timeoutGet.
 *
 * @retval FSP_SUCCESS              IWDT timeout information retrieved successfully.
 * @retval FSP_ERR_ASSERTION        One or more parameters are NULL pointers.
 * @retval FSP_ERR_NOT_OPEN         The driver has not been opened. Perform R_IWDT_Open() first.
 **********************************************************************************************************************/
fsp_err_t R_IWDT_TimeoutGet (wdt_ctrl_t * const p_api_ctrl, wdt_timeout_values_t * const p_timeout)
{
#if (1 == IWDT_CFG_PARAM_CHECKING_ENABLE)
    iwdt_instance_ctrl_t * p_ctrl = (iwdt_instance_ctrl_t *) p_api_ctrl;
    FSP_ASSERT(p_ctrl != NULL);
    FSP_ASSERT(p_timeout != NULL);
    FSP_ERROR_RETURN((IWDT_OPEN == p_ctrl->wdt_open), FSP_ERR_NOT_OPEN);
#endif
    FSP_PARAMETER_NOT_USED(p_api_ctrl);
    uint32_t frequency;
    uint32_t shift;
    uint8_t  index;

    for (index = 0U; index < (uint8_t) (sizeof(g_ofs0_timeout)); index++)
    {
        /* Get timeout value from OFS0 register. */
        if (g_ofs0_timeout[index] == (uint8_t) ((uint32_t) BSP_CFG_ROM_REG_OFS0 & IWDT_PRV_OFS0_TIMEOUT_MASK))
        {
            p_timeout->timeout_clocks = g_wdt_timeout[index];
        }
    }

    /* Get the frequency of the clock supplying the watchdog */
    frequency = (uint32_t) IWDT_CLOCK_FREQUENCY;

    shift =
        wdt_clock_divider_get((wdt_clock_division_t) (((uint32_t) BSP_CFG_ROM_REG_OFS0 &
                                                       IWDT_PRV_OFS0_CLOCK_DIVIDER_MASK) >> 4));

    p_timeout->clock_frequency_hz = frequency >> shift;

    return FSP_SUCCESS;
}

/*******************************************************************************************************************//**
 * Return IWDT HAL driver version. Implements @ref wdt_api_t::versionGet.
 *
 * @retval          FSP_SUCCESS         Call successful.
 * @retval      FSP_ERR_ASSERTION       Null pointer passed as a parameter.
 **********************************************************************************************************************/
fsp_err_t R_IWDT_VersionGet (fsp_version_t * const p_data)
{
#if (1 == IWDT_CFG_PARAM_CHECKING_ENABLE)
    FSP_ASSERT(p_data != NULL);
#endif

    p_data->version_id = g_iwdt_version.version_id;

    return FSP_SUCCESS;
}

/*******************************************************************************************************************//**
 * @} (end addtogroup IWDT)
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * Private Functions
 **********************************************************************************************************************/

/*******************************************************************************************************************//**
 * Internal NMI ISR callback which calls the user provided callback passing the context provided by the user.
 *
 * @param[in]   irq     IRQ which has triggered the NMI interrupt.
 *
 **********************************************************************************************************************/
static void iwdt_nmi_internal_callback (bsp_grp_irq_t irq)
{
    FSP_PARAMETER_NOT_USED(irq);

    /* Call user registered callback */
    wdt_callback_args_t p_args;
    p_args.p_context = gp_iwdt_ctrl->p_context;
    gp_iwdt_ctrl->p_callback(&p_args);
}

/*******************************************************************************************************************//**
 * Internal function to return timeout in terms of watchdog clocks from provided timeout setting.
 *
 * @param[in]   division    Watchdog division setting.
 *
 **********************************************************************************************************************/
static uint32_t wdt_clock_divider_get (wdt_clock_division_t division)
{
    uint32_t shift;

    if (WDT_CLOCK_DIVISION_128 == division)
    {
        shift = 7U;                    /* log base 2(128) = 7 */
    }
    else
    {
        shift = g_wdt_division_lookup[division];
    }

    return shift;
}

/*******************************************************************************************************************//**
 * @} (end defgroup IWDT)
 **********************************************************************************************************************/
