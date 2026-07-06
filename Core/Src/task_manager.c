/**
  ******************************************************************************
  * @file    task_manager.c
  * @brief   演示任务管理模块 — 实现
  *          包含6个 FreeRTOS 演示任务:
  *           1. Blinker  — LED 心跳闪烁 (验证调度器存活)
  *           2. Producer — 生产者, 通过消息队列发送数据
  *           3. Consumer — 消费者, 高优先级抢占演示
  *           4. Worker   — 同优先级时间片轮转演示
  *           5. Monitor  — 系统监视器, 输出任务栈/堆状态
  *           6. PID      — 直流电机 PID 调速控制
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "task_manager.h"
#include "main.h"

/* FreeRTOS 底层 API (用于 Monitor 调试) */
#include "FreeRTOS.h"
#include "task.h"

#include <stdio.h>
#include <string.h>

/* ================================================================
 * 全局句柄定义
 * ================================================================ */
osThreadId  blinkerTaskHandle  = NULL;
osThreadId  producerTaskHandle = NULL;
osThreadId  consumerTaskHandle = NULL;
osThreadId  workerTaskHandle   = NULL;
osThreadId  monitorTaskHandle  = NULL;
osThreadId  pidTaskHandle      = NULL;
osMessageQId msgQueueHandle    = NULL;
IWDG_HandleTypeDef hiwdg;  /* 独立看门狗句柄 */
TIM_HandleTypeDef  htim2;  /* TIM2 句柄 — PWM 输出 */

/* ================================================================
 * 任务函数前置声明
 * ================================================================ */
static void StartBlinkerTask (void const * argument);
static void StartProducerTask(void const * argument);
static void StartConsumerTask(void const * argument);
static void StartWorkerTask  (void const * argument);
static void StartMonitorTask (void const * argument);
static void StartPIDTask     (void const * argument);

/* --- 硬件辅助 --- */
static void MX_TIM2_PWM_Init(void);
static void SetMotorPWM(float duty_percent);

/* ================================================================
 * PrintBootBanner — 上电启动横幅
 * 打印: 项目名称、固件版本、FreeRTOS 版本、编译时间、系统时钟
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
    printf("╚══════════════════════════════════════════════╝\r\n\r\n");
}

/* ================================================================
 * TaskManager_Init — 创建所有演示任务和消息队列
 * 由 main() 在硬件初始化完成后调用
 * ================================================================ */
void TaskManager_Init(void)
{
    /* 打印启动横幅 */
    PrintBootBanner();

    /* ---- 初始化独立看门狗 IWDG (超时 %lu ms) ---- */
    hiwdg.Instance       = IWDG;
    hiwdg.Init.Prescaler = IWDG_PRESCALER;
    hiwdg.Init.Reload    = IWDG_RELOAD;
    if (HAL_IWDG_Init(&hiwdg) != HAL_OK)
    {
        printf("[ERROR] IWDG 初始化失败!\r\n");
        Error_Handler();
    }
    printf("[INIT] 独立看门狗启动 (超时: %lu ms, 空闲钩子自动喂狗)\r\n",
           (uint32_t)IWDG_TIMEOUT_MS);

    /* ---- 初始化 TIM2 PWM 输出 (PA0, 20kHz) ---- */
    MX_TIM2_PWM_Init();
    printf("[INIT] TIM2 PWM 启动 (PA0, %lu Hz)\r\n", (uint32_t)MOTOR_PWM_FREQ_HZ);

    /* ---- 创建消息队列 (Producer → Consumer) ---- */
    osMessageQDef(msgQueue, MSG_QUEUE_SIZE, uint32_t);
    msgQueueHandle = osMessageCreate(osMessageQ(msgQueue), NULL);
    if (msgQueueHandle == NULL)
    {
        printf("[ERROR] 消息队列创建失败!\r\n");
        Error_Handler();
    }
    printf("[INIT] 消息队列创建成功 (容量: %lu)\r\n", (uint32_t)MSG_QUEUE_SIZE);

    /* ---- Task 1: LED 闪烁 (Normal 优先级) ---- */
    osThreadDef(blinker, StartBlinkerTask, osPriorityNormal, 0, BLINKER_STACK_SIZE);
    blinkerTaskHandle = osThreadCreate(osThread(blinker), NULL);

    /* ---- Task 2: 生产者 (Normal 优先级) ---- */
    osThreadDef(producer, StartProducerTask, osPriorityNormal, 0, PRODUCER_STACK_SIZE);
    producerTaskHandle = osThreadCreate(osThread(producer), NULL);

    /* ---- Task 3: 消费者 (AboveNormal 优先级 — 抢占演示) ---- */
    osThreadDef(consumer, StartConsumerTask, osPriorityAboveNormal, 0, CONSUMER_STACK_SIZE);
    consumerTaskHandle = osThreadCreate(osThread(consumer), NULL);

    /* ---- Task 4: 工作线程 (Normal 优先级 — 时间片轮转演示) ---- */
    osThreadDef(worker, StartWorkerTask, osPriorityNormal, 0, WORKER_STACK_SIZE);
    workerTaskHandle = osThreadCreate(osThread(worker), NULL);

    /* ---- Task 5: 系统监视器 (Low 优先级 — 打印任务状态) ---- */
    osThreadDef(monitor, StartMonitorTask, osPriorityLow, 0, MONITOR_STACK_SIZE);
    monitorTaskHandle = osThreadCreate(osThread(monitor), NULL);

    /* ---- Task 6: PID 控制器 (Normal 优先级) ---- */
    osThreadDef(pidCtrl, StartPIDTask, osPriorityNormal, 0, PID_STACK_SIZE);
    pidTaskHandle = osThreadCreate(osThread(pidCtrl), NULL);

    printf("[INIT] 6个演示任务全部创建完成\r\n\r\n");
}

/* ================================================================
 * MX_TIM2_PWM_Init — TIM2_CH1 → PA0, 20kHz PWM 输出
 * APB1 时钟 = 8MHz, ARR = 399 → 20kHz
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
    if (HAL_TIM_PWM_Init(&htim2) != HAL_OK)
    {
        Error_Handler();
    }

    sConfigOC.OCMode     = TIM_OCMODE_PWM1;
    sConfigOC.Pulse      = 0;  /* 初始占空比 0% */
    sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
    sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
    if (HAL_TIM_PWM_ConfigChannel(&htim2, &sConfigOC, MOTOR_TIM_CHANNEL) != HAL_OK)
    {
        Error_Handler();
    }

    /* 启动 PWM 输出 */
    HAL_TIM_PWM_Start(&htim2, MOTOR_TIM_CHANNEL);
}

/* ================================================================
 * HAL_TIM_MspInit — TIM2 引脚和时钟初始化 (HAL 回调)
 * ================================================================ */
void HAL_TIM_MspInit(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM2)
    {
        __HAL_RCC_TIM2_CLK_ENABLE();
        __HAL_RCC_GPIOA_CLK_ENABLE();

        /* PA0: TIM2_CH1 (复用推挽输出) */
        GPIO_InitTypeDef GPIO_InitStruct = {0};
        GPIO_InitStruct.Pin       = GPIO_PIN_0;
        GPIO_InitStruct.Mode      = GPIO_MODE_AF_PP;
        GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_HIGH;
        HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
    }
}

/* ================================================================
 * SetMotorPWM — 设置电机 PWM 占空比
 * @param duty_percent 0.0 ~ 100.0 (%)
 * ================================================================ */
static void SetMotorPWM(float duty_percent)
{
    uint32_t pulse;

    /* 限幅 */
    if (duty_percent < 0.0f)  duty_percent = 0.0f;
    if (duty_percent > 100.0f) duty_percent = 100.0f;

    /* duty% → CCR: 0%→0, 100%→ARR+1 */
    pulse = (uint32_t)(duty_percent * (MOTOR_TIM_ARR + 1) / 100.0f);

    __HAL_TIM_SET_COMPARE(&htim2, MOTOR_TIM_CHANNEL, pulse);
}

/* ================================================================
 * PID 控制器辅助函数
 * ================================================================ */

/** 初始化 PID 控制器参数 */
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

/** 计算 PID 控制输出
 * @param measured_value 当前测量值
 * @param dt            采样周期 (秒)
 * @return              控制输出 (已限幅)
 */
float PID_Compute(PID_Controller *pid, float measured_value, float dt)
{
    float error = pid->setpoint - measured_value;

    /* P: 比例项 */
    float P_term = pid->Kp * error;

    /* I: 积分项 (带输出限幅防止积分饱和) */
    pid->integral += error * dt;
    if (pid->integral > pid->output_max) pid->integral = pid->output_max;
    if (pid->integral < pid->output_min) pid->integral = pid->output_min;
    float I_term = pid->Ki * pid->integral;

    /* D: 微分项 (对误差微分) */
    float derivative = (error - pid->prev_error) / dt;
    pid->prev_error = error;
    float D_term = pid->Kd * derivative;

    /* 合成 + 限幅 */
    float output = P_term + I_term + D_term;
    if (output > pid->output_max) output = pid->output_max;
    if (output < pid->output_min) output = pid->output_min;

    return output;
}

/* ================================================================
 * Task 1: LED 闪烁任务
 * 优先级: osPriorityNormal
 * 用途:   用硬件 LED 直观验证调度器是否正常运转。
 *         LED 常灭/乱闪 → 栈溢出、死锁等调度故障。
 * ================================================================ */
static void StartBlinkerTask(void const * argument)
{
    (void)argument;

    /* 初始化 PC13 (Blue Pill 板载 LED, 低电平点亮) */
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    __HAL_RCC_GPIOC_CLK_ENABLE();
    GPIO_InitStruct.Pin   = GPIO_PIN_13;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

    printf("[BLINKER] LED 闪烁任务启动 (周期: 500ms)\r\n");

    for (;;)
    {
        HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
        osDelay(500);
    }
}

/* ================================================================
 * Task 2: 生产者任务
 * 优先级: osPriorityNormal
 * 演示:   osMessagePut() — 非阻塞消息发送
 *         每 200ms 生成一个递增值发送到消息队列
 * ================================================================ */
static void StartProducerTask(void const * argument)
{
    (void)argument;
    uint32_t counter = 0;

    printf("[PRODUCER] 生产者启动 (周期: %lu ms)\r\n", (uint32_t)PRODUCER_PERIOD_MS);

    for (;;)
    {
        counter++;
        if (osMessagePut(msgQueueHandle, counter, 0) != osOK)
        {
            printf("[PRODUCER] 队列满! 丢弃: %lu\r\n", counter);
        }
        osDelay(PRODUCER_PERIOD_MS);
    }
}

/* ================================================================
 * Task 3: 消费者任务
 * 优先级: osPriorityAboveNormal (高于 Producer 和 Worker)
 * 演示:   osMessageGet() — 阻塞等待消息
 *         高优先级: 收到消息后立即抢占 Producer 或 Worker
 * ================================================================ */
static void StartConsumerTask(void const * argument)
{
    (void)argument;
    osEvent  event;
    uint32_t received_value;

    printf("[CONSUMER] 消费者启动 (优先级: AboveNormal, 超时: %lu ms)\r\n",
           (uint32_t)CONSUMER_TIMEOUT_MS);

    for (;;)
    {
        event = osMessageGet(msgQueueHandle, CONSUMER_TIMEOUT_MS);

        if (event.status == osEventMessage)
        {
            received_value = (uint32_t)event.value.v;
            printf("[CONSUMER] 收到: %lu | 当前任务ID: 0x%08X\r\n",
                   received_value, (unsigned int)osThreadGetId());
        }
        else if (event.status == osEventTimeout)
        {
            printf("[CONSUMER] 超时 — Producer 可能被阻塞!\r\n");
        }
    }
}

/* ================================================================
 * Task 4: 工作线程任务
 * 优先级: osPriorityNormal (与 Producer 同优先级)
 * 演示:   同优先级任务的时间片轮转调度
 *         通过模拟 CPU 密集型运算占用时间片
 * ================================================================ */
static void StartWorkerTask(void const * argument)
{
    (void)argument;
    uint32_t          iteration = 0;
    volatile uint32_t dummy_work;

    printf("[WORKER] 工作线程启动 (周期: %lu ms)\r\n", (uint32_t)WORKER_PERIOD_MS);

    for (;;)
    {
        iteration++;

        /* 模拟 CPU 密集型运算 */
        for (volatile uint32_t i = 0; i < 50000; i++)
        {
            dummy_work = i * 3 + 1;
        }

        if (iteration % 50 == 0)
        {
            printf("[WORKER] 迭代 %lu 完成 (同优先级轮转)\r\n", iteration);
        }

        osDelay(WORKER_PERIOD_MS);
    }
}

/* ================================================================
 * Task 5: 系统监视器任务
 * 优先级: osPriorityLow (最低, 只在其他任务空闲时运行)
 * 演示:   FreeRTOS 调试 API:
 *         - xPortGetFreeHeapSize()       堆内存剩余
 *         - uxTaskGetStackHighWaterMark() 各任务栈高水位
 *         - vTaskList()                  任务状态快照
 * ================================================================ */
static void StartMonitorTask(void const * argument)
{
    (void)argument;
    UBaseType_t stack_hwm;
    char        task_list_buf[512];

    printf("[MONITOR] 系统监视器启动 (周期: %lu ms)\r\n", (uint32_t)MONITOR_PERIOD_MS);

    for (;;)
    {
        printf("\r\n========== 系统监视器 (tick=%lu) ==========\r\n",
               (uint32_t)xTaskGetTickCount());

        /* 堆内存 */
        printf("  [HEAP] 剩余: %lu / %lu B\r\n",
               (uint32_t)xPortGetFreeHeapSize(),
               (uint32_t)configTOTAL_HEAP_SIZE);

        /* 各任务栈高水位 (剩余栈空间, 值越小越危险) */
        #define PRINT_STACK_HWM(handle, name)                    \
            do {                                                 \
                if ((handle) != NULL) {                          \
                    stack_hwm = uxTaskGetStackHighWaterMark(     \
                        (TaskHandle_t)(handle));                 \
                    printf("  [STACK] %-8s: %lu words 剩余\r\n", \
                           name, (uint32_t)stack_hwm);           \
                }                                                \
            } while (0)

        PRINT_STACK_HWM(blinkerTaskHandle,  "Blinker");
        PRINT_STACK_HWM(producerTaskHandle, "Producer");
        PRINT_STACK_HWM(consumerTaskHandle, "Consumer");
        PRINT_STACK_HWM(workerTaskHandle,   "Worker");
        PRINT_STACK_HWM(monitorTaskHandle,  "Monitor");
        PRINT_STACK_HWM(pidTaskHandle,      "PID");

        /* 任务状态列表 (Name / State / Prio / Stack / Num) */
        vTaskList(task_list_buf);
        printf("  [TASKS]\r\n%s\r\n", task_list_buf);
        printf("=============================================\r\n\r\n");

        osDelay(MONITOR_PERIOD_MS);
    }
}

/* ================================================================
 * Task 6: PID 直流电机调速控制 (osPriorityNormal)
 * ── 使用 PID 算法控制模拟直流电机转速
 *     电机模型: G(s) = K_m / (τ·s + 1)
 *     K_m = 12 RPM/%     (每1%占空比产生12 RPM稳态转速)
 *     τ   = 0.3 秒       (机电时间常数)
 *     目标: 0 RPM → 1000 RPM
 *     PID 输出: PWM 占空比 0~100%
 *     演示: 周期性实时控制、PID 参数整定、抗积分饱和
 * ================================================================ */
static void StartPIDTask(void const * argument)
{
    (void)argument;
    PID_Controller pid;
    float dt       = (float)PID_PERIOD_MS / 1000.0f;  /* 采样周期 0.1s */
    float measured = 0.0f;     /* 当前转速 (RPM) — 模拟编码器反馈 */
    float pwm_duty = 0.0f;     /* PWM 占空比 (%) */
    float error    = 0.0f;
    uint32_t step  = 0;

    /* 初始化 PID: 目标转速 1000 RPM */
    PID_Init(&pid, PID_KP_DEFAULT, PID_KI_DEFAULT, PID_KD_DEFAULT, PID_TARGET_RPM);
    pid.output_min = 0.0f;
    pid.output_max = PID_PWM_MAX;

    printf("[PID] 直流电机 PID 调速启动\r\n");
    printf("[PID] 目标=%lu RPM | Kp=%.2f Ki=%.2f Kd=%.2f | τ=%.2fs Km=%.1f\r\n",
           (uint32_t)PID_TARGET_RPM, pid.Kp, pid.Ki, pid.Kd, MOTOR_TAU, MOTOR_GAIN);

    for (;;)
    {
        step++;

        /* ---- Step 1: PID 计算 PWM 占空比 ---- */
        pwm_duty = PID_Compute(&pid, measured, dt);

        /* ---- Step 2: 输出 PWM 到 PA0 引脚 (硬件!) ---- */
        SetMotorPWM(pwm_duty);

        /* ---- Step 3: 电机模型更新 (一阶欧拉法, 模拟编码器反馈) ---- */
        /* 稳态: RPM = K_m * duty
           动态: d(RPM)/dt = (K_m * duty - RPM) / τ */
        measured += dt * (MOTOR_GAIN * pwm_duty - measured) / MOTOR_TAU;

        /* ---- Step 4: 串口输出 (可用于 Serial Plotter 绘图) ---- */
        printf("[PID] t=%.2fs | 设定=%.0f | 转速=%.1f RPM | PWM=%.1f%% | 误差=%.1f\r\n",
               step * dt, pid.setpoint, measured, pwm_duty, pid.setpoint - measured);

        /* ---- Step 5: 稳态检测 (误差 < 1%) ---- */
        error = pid.setpoint - measured;
        if ((error < 0.0f ? -error : error) < (PID_TARGET_RPM * 0.01f))
        {
            printf("[PID] >>> 电机已达目标转速! 稳态误差=%.1f RPM <<<\r\n", error);
            printf("[PID] >>> PID 参数: Kp=%.2f Ki=%.2f Kd=%.2f\r\n",
                   pid.Kp, pid.Ki, pid.Kd);

            /* 改变目标转速, 演示调速响应 */
            static float next_target = 500.0f;
            pid.setpoint = next_target;
            printf("[PID] >>> 切换目标转速 → %.0f RPM <<<\r\n\r\n", next_target);
            next_target = (next_target == 500.0f) ? 1000.0f : 500.0f;

            /* 重置积分项避免突变 */
            pid.integral   = 0.0f;
            pid.prev_error = 0.0f;
        }

        osDelay(PID_PERIOD_MS);
    }
}
