#include <stdio.h>
#include <string.h>

#include "board.h"
#include "fsl_ftm_driver.h"
#include "fsl_os_abstraction.h"

int main(void)
{
    ftm_pwm_param_t ftmParam = {
        .mode                   = kFtmEdgeAlignedPWM,
        .edgeMode               = kFtmLowTrue,
        .uFrequencyHZ           = 240000,
        .uDutyCyclePercent      = 0,
        .uFirstEdgeDelayPercent = 0,
    };
    ftm_user_config_t ftmInfo;

    // Configure board specific pin muxing
    hardware_init();

    OSA_Init();
    // Initialize UART terminal
    dbg_uart_init();

    // Print a note to show the start of demo
    printf("\r\nWelcome to FTM PWM demo!\n\n\r");

    configure_ftm_pins(BOARD_FTM_INSTANCE);

    memset(&ftmInfo, 0, sizeof(ftmInfo));

    ftmInfo.syncMethod = kFtmUseSoftwareTrig;
    FTM_DRV_Init(BOARD_FTM_INSTANCE, &ftmInfo);

    while(1)
    {
        FTM_DRV_PwmStart(BOARD_FTM_INSTANCE, &ftmParam, BOARD_FTM_CHANNEL);
        // Issue a software trigger to update registers
        FTM_HAL_SetSoftwareTriggerCmd(g_ftmBaseAddr[BOARD_FTM_INSTANCE], true);

        // delay 50ms
        OSA_TimeDelay(50);

        if (ftmParam.uDutyCyclePercent++ >= 100)
        {
            ftmParam.uDutyCyclePercent = 0;
        }
    }
}
