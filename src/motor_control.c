#include "motor_control.h"

#include "hal_data.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "r_gpt.h"

#include <string.h>

#ifndef MOTOR_CONTROL_TASK_STACK_WORDS
#define MOTOR_CONTROL_TASK_STACK_WORDS (1024U)
#endif

#ifndef MOTOR_CONTROL_TASK_PRIORITY
#define MOTOR_CONTROL_TASK_PRIORITY (tskIDLE_PRIORITY + 2U)
#endif

#ifndef MOTOR_CONTROL_UPDATE_PERIOD_MS
#define MOTOR_CONTROL_UPDATE_PERIOD_MS (20U)
#endif

#ifndef MOTOR_CMD_HOLD_MS
#define MOTOR_CMD_HOLD_MS (500U)
#endif

#ifndef MOTOR_DEFAULT_SPEED_PERMILLE
#define MOTOR_DEFAULT_SPEED_PERMILLE (600U)
#endif

typedef struct st_motor_pred_rule
{
    int pred_lo;
    int pred_hi;
    motor_action_t action;
    uint16_t speed_permille; /* 0..1000 */
} motor_pred_rule_t;

/*
 * pred -> action mapping (edit here).
 *
 * Example customizations:
 * - Make pred 2..4 all map to TURN_LEFT by changing {2,2,...} to {2,4,...}
 * - Change per-action speed by editing speed_permille.
 */
static const motor_pred_rule_t g_pred_rules[] = {
    /* User mapping (edit as needed):
     * - pred=0 : forward
     * - pred=1 : half rotation
     * - pred=2 : quarter rotation
     *
     * Rotation amount depends on your robot; tune via speed_permille.
     */
    {0, 0, MOTOR_ACTION_FORWARD, MOTOR_DEFAULT_SPEED_PERMILLE},
    {1, 1, MOTOR_ACTION_BACKWARD, MOTOR_DEFAULT_SPEED_PERMILLE},
    {2, 2, MOTOR_ACTION_ROTATE_LEFT, (MOTOR_DEFAULT_SPEED_PERMILLE)},
    {3, 3, MOTOR_ACTION_ROTATE_RIGHT, MOTOR_DEFAULT_SPEED_PERMILLE},
};

static QueueHandle_t s_pred_queue;
static TaskHandle_t s_task;
static uint32_t s_period0 = 0;
static uint32_t s_period1 = 0;

static motor_pred_rule_t const *motor_rule_for_pred(int pred)
{
    for (size_t i = 0; i < (sizeof(g_pred_rules) / sizeof(g_pred_rules[0])); i++)
    {
        if ((pred >= g_pred_rules[i].pred_lo) && (pred <= g_pred_rules[i].pred_hi))
        {
            return &g_pred_rules[i];
        }
    }

    return NULL;
}

static uint32_t duty_counts_from_permille(uint32_t period_counts, uint16_t permille)
{
    if (permille >= 1000U)
    {
        return period_counts;
    }

    return (uint32_t)(((uint64_t)period_counts * (uint64_t)permille) / 1000ULL);
}

static void motor_set_one(timer_ctrl_t *const p_ctrl, uint32_t period_counts, bool forward, uint16_t speed_permille)
{
    uint32_t pwm_counts = duty_counts_from_permille(period_counts, speed_permille);

    if (forward)
    {
        (void)R_GPT_DutyCycleSet(p_ctrl, pwm_counts, GPT_IO_PIN_GTIOCA);
        (void)R_GPT_DutyCycleSet(p_ctrl, 0U, GPT_IO_PIN_GTIOCB);
    }
    else
    {
        (void)R_GPT_DutyCycleSet(p_ctrl, 0U, GPT_IO_PIN_GTIOCA);
        (void)R_GPT_DutyCycleSet(p_ctrl, pwm_counts, GPT_IO_PIN_GTIOCB);
    }
}

static void motor_stop_all(void)
{
    (void)R_GPT_DutyCycleSet(&g_timer0_ctrl, 0U, GPT_IO_PIN_GTIOCA);
    (void)R_GPT_DutyCycleSet(&g_timer0_ctrl, 0U, GPT_IO_PIN_GTIOCB);
    (void)R_GPT_DutyCycleSet(&g_timer1_ctrl, 0U, GPT_IO_PIN_GTIOCA);
    (void)R_GPT_DutyCycleSet(&g_timer1_ctrl, 0U, GPT_IO_PIN_GTIOCB);
}

static void motor_apply_action(motor_action_t action, uint16_t speed_permille)
{
    switch (action)
    {
    default:
    case MOTOR_ACTION_STOP:
        motor_stop_all();
        break;

    case MOTOR_ACTION_FORWARD:
        motor_set_one(&g_timer0_ctrl, s_period0, false, speed_permille);
        motor_set_one(&g_timer1_ctrl, s_period1, true, speed_permille);
        break;

    case MOTOR_ACTION_BACKWARD:
        motor_set_one(&g_timer0_ctrl, s_period0, true, speed_permille);
        motor_set_one(&g_timer1_ctrl, s_period1, false, speed_permille);
        break;

    case MOTOR_ACTION_TURN_LEFT:
        /* Simple pivot: left stop, right forward */
        motor_set_one(&g_timer0_ctrl, s_period0, true, 0U);
        motor_set_one(&g_timer1_ctrl, s_period1, true, speed_permille);
        break;

    case MOTOR_ACTION_TURN_RIGHT:
        motor_set_one(&g_timer0_ctrl, s_period0, true, speed_permille);
        motor_set_one(&g_timer1_ctrl, s_period1, true, 0U);
        break;

    case MOTOR_ACTION_ROTATE_LEFT:
        motor_set_one(&g_timer0_ctrl, s_period0, true, speed_permille);
        motor_set_one(&g_timer1_ctrl, s_period1, true, speed_permille);
        break;

    case MOTOR_ACTION_ROTATE_RIGHT:
        motor_set_one(&g_timer0_ctrl, s_period0, false, speed_permille);
        motor_set_one(&g_timer1_ctrl, s_period1, false, speed_permille);
        break;
    }
}

static void motor_control_task(void *pvParameters)
{
    FSP_PARAMETER_NOT_USED(pvParameters);

    int pred = -1;
    bool active = false;
    bool pred_locked = false;
    TickType_t expire_tick = 0;

    for (;;)
    {
        /* Defer pred updates while an action is active.
         * Keep the latest pred in the 1-deep queue by peeking (do not consume).
         * We still block briefly to avoid busy looping.
         */
        if (pred_locked)
        {
            int peek_pred;
            (void)xQueuePeek(s_pred_queue, &peek_pred, pdMS_TO_TICKS(MOTOR_CONTROL_UPDATE_PERIOD_MS));
        }
        else
        {
            int rx_pred;
            if (xQueueReceive(s_pred_queue, &rx_pred, pdMS_TO_TICKS(MOTOR_CONTROL_UPDATE_PERIOD_MS)) == pdTRUE)
            {
                pred = rx_pred;
                const motor_pred_rule_t *rule = motor_rule_for_pred(pred);
                if (!rule)
                {
                    motor_stop_all();
                    active = false;
                    pred_locked = false;
                }
                else
                {
                    motor_apply_action(rule->action, rule->speed_permille);
                    active = (rule->action != MOTOR_ACTION_STOP);
                    pred_locked = active;
                    expire_tick = xTaskGetTickCount() + pdMS_TO_TICKS(MOTOR_CMD_HOLD_MS);
                }
            }
        }

        if (active)
        {
            TickType_t now = xTaskGetTickCount();
            if ((int32_t)(now - expire_tick) >= 0)
            {
                motor_stop_all();
                active = false;
                pred_locked = false;
            }
        }
    }
}

void motor_control_start(void)
{
    if (s_task)
    {
        return;
    }

    /* Determine period counts (prefer driver info). */
    timer_info_t info;
    s_period0 = g_timer0_cfg.period_counts;
    s_period1 = g_timer1_cfg.period_counts;

    if (R_GPT_InfoGet(&g_timer0_ctrl, &info) == FSP_SUCCESS)
    {
        s_period0 = info.period_counts;
    }
    if (R_GPT_InfoGet(&g_timer1_ctrl, &info) == FSP_SUCCESS)
    {
        s_period1 = info.period_counts;
    }

    s_pred_queue = xQueueCreate(1, sizeof(int));
    if (!s_pred_queue)
    {
        return;
    }

    /* Safe default at boot. */
    motor_stop_all();

    (void)xTaskCreate(motor_control_task,
                      "motor",
                      MOTOR_CONTROL_TASK_STACK_WORDS,
                      NULL,
                      MOTOR_CONTROL_TASK_PRIORITY,
                      &s_task);
}

void motor_control_post_pred(int pred)
{
    if (!s_pred_queue)
    {
        return;
    }

    (void)xQueueOverwrite(s_pred_queue, &pred);
}
