/**
  ******************************************************************************
  * @file    task_manager.c
  * @brief   线程管理模块 — 实现
  *          所有线程通过注册表 g_taskConfigTable 统一创建。
  *          添加新线程: ① TaskId_t 加枚举  ② 表中加一行  ③ 写入口函数
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "task_manager.h"
#include "main.h"
#include <stdio.h>
#include <string.h>

/* ================================================================
 * 外部函数声明 (入口函数分散在不同文件中)
 * ================================================================ */
extern void StartDefaultTask(void const * argument);

/* ================================================================
 * 私有函数声明
 * ================================================================ */
static void StartBlinkerTask (void const * argument);
static void StartProducerTask(void const * argument);
static void StartConsumerTask(void const * argument);
static void StartWorkerTask  (void const * argument);
static void StartMonitorTask (void const * argument);
static void StartPIDTask     (void const * argument);
static void StartWatchdogTask(void const * argument);
static void MX_TIM2_PWM_Init (void);
static void SetMotorPWM      (float duty_percent);

/* ================================================================
 *               线  程  注  册  表  (唯一定义点)
 *   新增线程只需: ① TaskId_t 加枚举值  ② 本表加一行  ③ 写入口函数
 *
 *   │ name            │ entry               │ param │ rtos_prio                │ stack_words            │
 *   │ ─────────────── │ ─────────────────── │ ────  │ ──────────────────────  │ ─────────────────────  │
 * ================================================================ */
const TaskConfig_t g_taskConfigTable[TASK_ID_COUNT] = {
    [TASK_ID_DEFAULT] = { .name = "defaultTask"    , .entry = StartDefaultTask  , .param = NULL  , .rtos_prio = RTOS_PRIO_NORMAL       , .stack_words = 128                  },
    [TASK_ID_BLINKER] = { .name = "blinker"        , .entry = StartBlinkerTask  , .param = NULL  , .rtos_prio = RTOS_PRIO_NORMAL       , .stack_words = BLINKER_STACK_SIZE    },
    [TASK_ID_PRODUCER] = { .name = "producer"       , .entry = StartProducerTask , .param = NULL  , .rtos_prio = RTOS_PRIO_NORMAL       , .stack_words = PRODUCER_STACK_SIZE   },
    [TASK_ID_CONSUMER] = { .name = "consumer"       , .entry = StartConsumerTask , .param = NULL  , .rtos_prio = RTOS_PRIO_ABOVE_NORMAL , .stack_words = CONSUMER_STACK_SIZE   },
    [TASK_ID_WORKER]   = { .name = "worker"         , .entry = StartWorkerTask   , .param = NULL  , .rtos_prio = RTOS_PRIO_NORMAL       , .stack_words = WORKER_STACK_SIZE     },
    [TASK_ID_MONITOR]  = { .name = "monitor"        , .entry = StartMonitorTask  , .param = NULL  , .rtos_prio = RTOS_PRIO_LOW          , .stack_words = MONITOR_STACK_SIZE    },
    [TASK_ID_PID]      = { .name = "pidCtrl"        , .entry = StartPIDTask      , .param = NULL  , .rtos_prio = RTOS_PRIO_NORMAL       , .stack_words = PID_STACK_SIZE        },
    [TASK_ID_WATCHDOG] = { .name = "watchdog"       , .entry = StartWatchdogTask , .param = NULL  , .rtos_prio = RTOS_PRIO_HIGH         , .stack_words = WATCHDOG_STACK_SIZE   },
};

/* 线程句柄数组 — 创建任务时自动填充 */
TaskHandle_t g_taskHandles[TASK_ID_COUNT] = {NULL};

/* 全局资源句柄 */
osMessageQId      g_msgQueueHandle = NULL;
IWDG_HandleTypeDef hiwdg;
TIM_HandleTypeDef  htim2;

/* ================================================================
 * TaskManager_Init — 遍历注册表创建所有线程 + 初始化硬件资源
 * ================================================================ */
void TaskManager_Init(void)
{
    /* ---- 启动横幅 ---- */
    PrintBootBanner();

    /* ---- 初始化 IWDG ---- */
    hiwdg.Instance       = IWDG;
    hiwdg.Init.Prescaler = IWDG_PRESCALER;
    hiwdg.Init.Reload    = IWDG_RELOAD;
    if (HAL_IWDG_Init(&hiwdg) != HAL_OK) {
        printf("[ERROR] IWDG 初始化失败!\r\n");
        Error_Handler();
    }
    printf("[INIT] IWDG 看门狗启动 (超时: %lu ms, watchdog线程喂狗)\r\n",
           (uint32_t)IWDG_TIMEOUT_MS);

    /* ---- 初始化 TIM2 PWM (PA0, 20kHz) ---- */
    MX_TIM2_PWM_Init();
    printf("[INIT] TIM2 PWM 启动 (PA0, %lu Hz)\r\n", (uint32_t)MOTOR_PWM_FREQ_HZ);

    /* ---- 创建消息队列 ---- */
    osMessageQDef(msgQueue, MSG_QUEUE_SIZE, uint32_t);
    g_msgQueueHandle = osMessageCreate(osMessageQ(msgQueue), NULL);
    if (g_msgQueueHandle == NULL) {
        printf("[ERROR] 消息队列创建失败!\r\n");
        Error_Handler();
    }
    printf("[INIT] 消息队列创建成功 (容量: %lu)\r\n", (uint32_t)MSG_QUEUE_SIZE);

    /* ---- 遍历注册表创建所有线程 ---- */
    printf("[INIT] 开始创建 %d 个线程...\r\n", TASK_ID_COUNT);
    for (int i = 0; i < TASK_ID_COUNT; i++) {
        const TaskConfig_t *cfg = &g_taskConfigTable[i];
        BaseType_t ret = xTaskCreate(
            (TaskFunction_t)cfg->entry,   /* 入口函数        */
            cfg->name,                    /* 线程名称        */
            (uint16_t)cfg->stack_words,   /* 栈深度 (字)     */
            cfg->param,                   /* 线程参数        */
            cfg->rtos_prio,               /* FreeRTOS 优先级 */
            &g_taskHandles[i]             /* 返回句柄        */
        );
        if (ret == pdPASS) {
            printf("  [OK] %-16s prio=%lu  stack=%lu words\r\n",
                   cfg->name, cfg->rtos_prio, cfg->stack_words);
        } else {
            printf("  [FAIL] %s 创建失败! (堆不足?)\r\n", cfg->name);
            Error_Handler();
        }
    }
    printf("[INIT] 全部线程创建完成\r\n\r\n");
}

/* ================================================================
 * PrintBootBanner — 上电启动横幅
 * ================================================================ */
void PrintBootBanner(void)
{
    printf("\r\n\r\n");
    printf("╔══════════════════════════════════════════════╗\r\n");
    printf("║  %-44s ║\r\n", PROJECT_NAME);
    printf("╠══════════════════════════════════════════════╣\r\n");
    printf("║  固件版本 : %-32s ║\r\n", PROJECT_VERSION_STR);
    printf("║  作者     : %-32s ║\r\n", PROJECT_AUTHOR);
    printf("║  描述     : %-32s ║\r\n", PROJECT_DESCRIPTION);
    printf("╠══════════════════════════════════════════════╣\r\n");
    printf("║  FreeRTOS : %s%-34s ║\r\n",
           tskKERNEL_VERSION_NUMBER, "");
    printf("║  CMSIS-RTOS: V1.02%-25s ║\r\n", "");
    printf("║  HAL 库   : STM32Cube FW_F1 V1.8.7%-11s ║\r\n", "");
    printf("║  编译时间 : %s %s%-*s ║\r\n",
           __DATE__, __TIME__,
           (int)(26 - strlen(__DATE__) - strlen(__TIME__)), "");
    printf("╠══════════════════════════════════════════════╣\r\n");
    printf("║  系统时钟 : %lu Hz%-28s ║\r\n", SystemCoreClock, "");
    printf("║  RTOS 滴答: %lu Hz%-28s ║\r\n",
           (uint32_t)configTICK_RATE_HZ, "");
    printf("║  芯片型号 : STM32F103C8T6 (Cortex-M3)%-9s ║\r\n", "");
    printf("║  线程总数 : %d%-37s ║\r\n", TASK_ID_COUNT, "");
    printf("╚══════════════════════════════════════════════╝\r\n\r\n");
}

/* ================================================================
 * MX_TIM2_PWM_Init
 * ================================================================ */
static void MX_TIM2_PWM_Init(void)
{
    TIM_OC_InitTypeDef sConfigOC = {0};
    htim2.Instance               = TIM2;
    htim2.Init.Prescaler         = 0;
    htim2.Init.CounterMode       = TIM_COUNTERMODE_UP;
    htim2.Init.Period            = MOTOR_TIM_ARR;
    htim2.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    if (HAL_TIM_PWM_Init(&htim2) != HAL_OK) Error_Handler();

    sConfigOC.OCMode     = TIM_OCMODE_PWM1;
    sConfigOC.Pulse      = 0;
    sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
    sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
    if (HAL_TIM_PWM_ConfigChannel(&htim2, &sConfigOC, MOTOR_TIM_CHANNEL) != HAL_OK)
        Error_Handler();

    HAL_TIM_PWM_Start(&htim2, MOTOR_TIM_CHANNEL);
}

/* ================================================================
 * HAL_TIM_MspInit
 * ================================================================ */
void HAL_TIM_MspInit(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM2) {
        __HAL_RCC_TIM2_CLK_ENABLE();
        __HAL_RCC_GPIOA_CLK_ENABLE();
        GPIO_InitTypeDef s = {0};
        s.Pin   = GPIO_PIN_0;
        s.Mode  = GPIO_MODE_AF_PP;
        s.Speed = GPIO_SPEED_FREQ_HIGH;
        HAL_GPIO_Init(GPIOA, &s);
    }
}

/* ================================================================
 * SetMotorPWM
 * ================================================================ */
static void SetMotorPWM(float duty_percent)
{
    if (duty_percent < 0.0f)  duty_percent = 0.0f;
    if (duty_percent > 100.0f) duty_percent = 100.0f;
    uint32_t pulse = (uint32_t)(duty_percent * (MOTOR_TIM_ARR + 1) / 100.0f);
    __HAL_TIM_SET_COMPARE(&htim2, MOTOR_TIM_CHANNEL, pulse);
}

/* ================================================================
 * PID 辅助函数
 * ================================================================ */
void PID_Init(PID_Controller *pid, float Kp, float Ki, float Kd, float setpoint)
{
    pid->Kp         = Kp;
    pid->Ki         = Ki;
    pid->Kd         = Kd;
    pid->setpoint   = setpoint;
    pid->integral   = 0.0f;
    pid->prev_error = 0.0f;
    pid->output_min = 0.0f;
    pid->output_max = 100.0f;
}

float PID_Compute(PID_Controller *pid, float measured_value, float dt)
{
    float error   = pid->setpoint - measured_value;
    float P_term  = pid->Kp * error;
    pid->integral += error * dt;
    if (pid->integral > pid->output_max) pid->integral = pid->output_max;
    if (pid->integral < pid->output_min) pid->integral = pid->output_min;
    float I_term    = pid->Ki * pid->integral;
    float derivative = (error - pid->prev_error) / dt;
    pid->prev_error  = error;
    float D_term    = pid->Kd * derivative;
    float output    = P_term + I_term + D_term;
    if (output > pid->output_max) output = pid->output_max;
    if (output < pid->output_min) output = pid->output_min;
    return output;
}

/* ================================================================
 * Task 1: LED 闪烁 (osPriorityNormal)
 * ================================================================ */
static void StartBlinkerTask(void const * argument)
{
    (void)argument;
    GPIO_InitTypeDef s = {0};
    __HAL_RCC_GPIOC_CLK_ENABLE();
    s.Pin   = GPIO_PIN_13;
    s.Mode  = GPIO_MODE_OUTPUT_PP;
    s.Pull  = GPIO_NOPULL;
    s.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &s);
    printf("[BLINKER] LED 闪烁任务启动 (周期: %lu ms)\r\n", (uint32_t)BLINKER_PERIOD_MS);
    for (;;) {
        HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
        osDelay(BLINKER_PERIOD_MS);
    }
}

/* ================================================================
 * Task 2: 生产者 (osPriorityNormal)
 * ================================================================ */
static void StartProducerTask(void const * argument)
{
    (void)argument;
    uint32_t counter = 0;
    printf("[PRODUCER] 生产者启动 (周期: %lu ms)\r\n", (uint32_t)PRODUCER_PERIOD_MS);
    for (;;) {
        counter++;
        if (osMessagePut(g_msgQueueHandle, counter, 0) != osOK)
            printf("[PRODUCER] 队列满! 丢弃: %lu\r\n", counter);
        osDelay(PRODUCER_PERIOD_MS);
    }
}

/* ================================================================
 * Task 3: 消费者 (osPriorityAboveNormal — 抢占演示)
 * ================================================================ */
static void StartConsumerTask(void const * argument)
{
    (void)argument;
    printf("[CONSUMER] 消费者启动 (优先级: AboveNormal, 超时: %lu ms)\r\n",
           (uint32_t)CONSUMER_TIMEOUT_MS);
    for (;;) {
        osEvent event = osMessageGet(g_msgQueueHandle, CONSUMER_TIMEOUT_MS);
        if (event.status == osEventMessage) {
            printf("[CONSUMER] 收到: %lu | 当前任务ID: 0x%08X\r\n",
                   (uint32_t)event.value.v, (unsigned int)osThreadGetId());
        } else if (event.status == osEventTimeout) {
            printf("[CONSUMER] 超时 — Producer 可能被阻塞!\r\n");
        }
    }
}

/* ================================================================
 * Task 4: 工作线程 (osPriorityNormal — 时间片轮转)
 * ================================================================ */
static void StartWorkerTask(void const * argument)
{
    (void)argument;
    uint32_t iteration = 0;
    volatile uint32_t __attribute__((unused)) dummy;
    printf("[WORKER] 工作线程启动 (周期: %lu ms)\r\n", (uint32_t)WORKER_PERIOD_MS);
    for (;;) {
        iteration++;
        for (volatile uint32_t i = 0; i < 50000; i++) dummy = i * 3 + 1;
        if (iteration % 50 == 0)
            printf("[WORKER] 迭代 %lu 完成 (同优先级轮转)\r\n", iteration);
        osDelay(WORKER_PERIOD_MS);
    }
}

/* ================================================================
 * Task 5: 系统监视器 (osPriorityLow — 最低)
 * ================================================================ */
static void StartMonitorTask(void const * argument)
{
    (void)argument;
    printf("[MONITOR] 系统监视器启动 (周期: %lu ms)\r\n", (uint32_t)MONITOR_PERIOD_MS);
    for (;;) {
        printf("\r\n========== 系统监视器 (tick=%lu) ==========\r\n",
               (uint32_t)xTaskGetTickCount());

        printf("  [HEAP] 剩余: %lu / %lu B\r\n",
               (uint32_t)xPortGetFreeHeapSize(), (uint32_t)configTOTAL_HEAP_SIZE);

        /* 遍历注册表打印各线程栈高水位 */
        for (int i = 0; i < TASK_ID_COUNT; i++) {
            if (g_taskHandles[i] != NULL) {
                UBaseType_t hwm = uxTaskGetStackHighWaterMark(g_taskHandles[i]);
                printf("  [STACK] %-12s: %lu words 剩余 (共 %lu)\r\n",
                       g_taskConfigTable[i].name,
                       (uint32_t)hwm,
                       g_taskConfigTable[i].stack_words);
            }
        }

        char buf[512];
        vTaskList(buf);
        printf("  [TASKS]\r\n%s\r\n", buf);
        printf("=============================================\r\n\r\n");
        osDelay(MONITOR_PERIOD_MS);
    }
}

/* ================================================================
 * Task 6: PID 直流电机调速 (osPriorityNormal)
 * ================================================================ */
static void StartPIDTask(void const * argument)
{
    (void)argument;
    PID_Controller pid;
    float dt       = (float)PID_PERIOD_MS / 1000.0f;
    float measured = 0.0f;
    float pwm_duty = 0.0f;
    uint32_t step  = 0;

    PID_Init(&pid, PID_KP_DEFAULT, PID_KI_DEFAULT, PID_KD_DEFAULT, PID_TARGET_RPM);
    pid.output_min = 0.0f;
    pid.output_max = PID_PWM_MAX;

    printf("[PID] 直流电机 PID 调速启动 (目标: %.0f RPM | Kp=%.2f Ki=%.2f Kd=%.2f | τ=%.2fs)\r\n",
           PID_TARGET_RPM, pid.Kp, pid.Ki, pid.Kd, MOTOR_TAU);

    for (;;) {
        step++;
        pwm_duty = PID_Compute(&pid, measured, dt);   /* Step 1: PID 计算   */
        SetMotorPWM(pwm_duty);                         /* Step 2: PWM → PA0  */
        measured += dt * (MOTOR_GAIN * pwm_duty - measured) / MOTOR_TAU; /* Step 3: 模型 */
        printf("[PID] t=%.2fs | 设定=%.0f | 转速=%.1f RPM | PWM=%.1f%% | 误差=%.1f\r\n",
               step * dt, pid.setpoint, measured, pwm_duty, pid.setpoint - measured);

        float error = pid.setpoint - measured;
        if ((error < 0.0f ? -error : error) < (PID_TARGET_RPM * 0.01f)) {
            printf("[PID] >>> 已达稳态! 误差=%.1f RPM <<<\r\n", error);
            static float next_target = 500.0f;
            pid.setpoint = next_target;
            printf("[PID] >>> 切换目标 → %.0f RPM <<<\r\n\r\n", next_target);
            next_target = (next_target == 500.0f) ? 1000.0f : 500.0f;
            pid.integral   = 0.0f;
            pid.prev_error = 0.0f;
        }
        osDelay(PID_PERIOD_MS);
    }
}

/* ================================================================
 * Task 7: 看门狗喂狗线程 (osPriorityHigh — 最高线程优先级)
 *   独立高优先级线程定期喂狗。
 *   若任何线程死锁导致调度器无法切换到本线程 → 看门狗超时复位。
 *   喂养周期 = 200ms, IWDG 超时 = 1000ms, 容许丢失 4 次喂狗。
 * ================================================================ */
static void StartWatchdogTask(void const * argument)
{
    (void)argument;
    printf("[WATCHDOG] 看门狗喂狗线程启动 (优先级: High/%u | 周期: %lu ms | IWDG超时: %lu ms)\r\n",
           (unsigned int)RTOS_PRIO_HIGH, (uint32_t)WATCHDOG_PERIOD_MS, (uint32_t)IWDG_TIMEOUT_MS);
    for (;;) {
        HAL_IWDG_Refresh(&hiwdg);
        osDelay(WATCHDOG_PERIOD_MS);
    }
}
