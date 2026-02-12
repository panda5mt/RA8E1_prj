#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /* Motor actions derived from HLAC pred.
     *
     * This module is intentionally small:
     * - One FreeRTOS task owns all GPT updates.
     * - Producer (HLAC) only posts pred values (optionally filtered before posting).
     */

    typedef enum e_motor_action
    {
        MOTOR_ACTION_STOP = 0,
        MOTOR_ACTION_FORWARD,
        MOTOR_ACTION_BACKWARD,
        MOTOR_ACTION_TURN_LEFT,
        MOTOR_ACTION_TURN_RIGHT,
        MOTOR_ACTION_ROTATE_LEFT,
        MOTOR_ACTION_ROTATE_RIGHT,
    } motor_action_t;

    /** Start the motor control task.
     *
     * Call this after PWM (GPT0/GPT1) have been opened and started.
     */
    void motor_control_start(void);

    /** Post a new pred label to the motor control task.
     *
     * This is non-blocking. The motor task always uses the latest pred.
     */
    void motor_control_post_pred(int pred);

#ifdef __cplusplus
}
#endif
