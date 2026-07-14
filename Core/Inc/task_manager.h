/**
  ******************************************************************************
  * @file    task_manager.h
  * @brief   线程管理模块 — 头文件
  *          线程注册表驱动: 添加新线程只需在 g_taskConfigTable 中加一行
  ******************************************************************************
  */

#ifndef __TASK_MANAGER_H
#define __TASK_MANAGER_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "cmsis_os.h"
#include "stm32f1xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"

/* ================================================================
 * 项目版本信息
 * ================================================================ */
#define PROJECT_NAME             "STM32F103 Bootloader Demo"
#define PROJECT_VERSION_STR      "v1.0.0"
#define PROJECT_AUTHOR           "WYZ4328"
#define PROJECT_DESCRIPTION      "FreeRTOS + PID Motor Control + IWDG"

/* ================================================================
 * FreeRTOS 优先级数值 (configMAX_PRIORITIES = 7 → 0~6)
 * 映射: fpriority = CMSIS_priority - osPriorityIdle
 * ================================================================ */
#define RTOS_PRIO_IDLE           0
#define RTOS_PRIO_LOW            1
#define RTOS_PRIO_BELOW_NORMAL   2
#define RTOS_PRIO_NORMAL         3
#define RTOS_PRIO_ABOVE_NORMAL   4
#define RTOS_PRIO_HIGH           5
#define RTOS_PRIO_REALTIME       6

/* ================================================================
 * 线程 ID 枚举 — 与注册表顺序严格一致
 * ================================================================ */
typedef enum {
    TASK_ID_DEFAULT = 0,
    TASK_ID_BLINKER,
    TASK_ID_PRODUCER,
    TASK_ID_CONSUMER,
    TASK_ID_WORKER,
    TASK_ID_MONITOR,
    TASK_ID_PID,
    TASK_ID_WATCHDOG,      /* 看门狗喂狗线程 (最高优先级) */
    TASK_ID_COUNT          /* 自动计数, 必须置于末尾 */
} TaskId_t;

/* ================================================================
 * 线程配置结构体 — 注册表中每行一个线程, 5 个字段全部必须
 *
 *   name       : 线程名, FreeRTOS 调试用, 最长 configMAX_TASK_NAME_LEN(16)
 *   entry      : 入口函数, 类型 void (*)(void *)
 *   param      : 传给入口函数的参数, 不同实例可用不同参数区分
 *   rtos_prio  : FreeRTOS 优先级 (0~configMAX_PRIORITIES-1, 本项目 0~6)
 *                注意: 这不是中断优先级! NVIC 中断优先级归 ISR 管, 线程管不了
 *   stack_words: 栈深度 (字, 1字=4字节), 实际栈 = stack_words × 4 字节
 * ================================================================ */
typedef struct {
    const char     *name;         /* 线程名称 (调试用)           */
    os_pthread      entry;        /* 入口函数                    */
    void           *param;        /* 线程参数 (传 NULL 表示无参) */
    uint32_t        rtos_prio;    /* FreeRTOS 优先级 (0~6)       */
    uint32_t        stack_words;  /* 栈深度 (字)                 */
} TaskConfig_t;

/* ================================================================
 * ╔══════════════════════════════════════════════════════════════╗
 * ║              线  程  注  册  表  (唯一定义点)               ║
 * ║       添加新线程 → 在表中加一行 + TaskId_t 中加枚举值       ║
 * ╚══════════════════════════════════════════════════════════════╝
 * ================================================================ */
extern const TaskConfig_t g_taskConfigTable[TASK_ID_COUNT];

/* 线程句柄数组 (索引 = TaskId_t, 创建后填充) */
extern TaskHandle_t g_taskHandles[TASK_ID_COUNT];

/* ================================================================
 * PID 控制器结构体
 * ================================================================ */
typedef struct {
    float Kp, Ki, Kd;
    float setpoint;
    float integral;
    float prev_error;
    float output_min, output_max;
} PID_Controller;

/* ================================================================
 * PID 参数 (直流电机调速)
 * ================================================================ */
#define PID_KP_DEFAULT           0.8f
#define PID_KI_DEFAULT           0.3f
#define PID_KD_DEFAULT           0.02f
#define PID_TARGET_RPM           1000.0f
#define PID_PWM_MAX              100.0f

/* ================================================================
 * 电机仿真模型: G(s) = K_m / (τ·s + 1)
 * ================================================================ */
#define MOTOR_GAIN               12.0f
#define MOTOR_TAU                0.3f

/* ================================================================
 * PWM 硬件输出 (TIM2_CH1 → PA0, 20kHz)
 * ================================================================ */
#define MOTOR_TIM               (&htim2)
#define MOTOR_TIM_CHANNEL       TIM_CHANNEL_1
#define MOTOR_TIM_ARR           399
#define MOTOR_PWM_FREQ_HZ       20000

/* ================================================================
 * 独立看门狗 (IWDG) — LSI≈40kHz, /64 → 625Hz, 重载625 → 1s
 * ================================================================ */
#define IWDG_PRESCALER           IWDG_PRESCALER_64
#define IWDG_RELOAD              625
#define IWDG_TIMEOUT_MS          1000

/* ================================================================
 * 消息队列 (Producer → Consumer)
 * ================================================================ */
#define MSG_QUEUE_SIZE           16

/* ================================================================
 * 各线程栈大小 / 周期 (供注册表引用)
 * ================================================================ */
#define DEFAULT_STACK_SIZE       128
#define BLINKER_STACK_SIZE       128
#define PRODUCER_STACK_SIZE      256
#define CONSUMER_STACK_SIZE      256
#define WORKER_STACK_SIZE        256
#define MONITOR_STACK_SIZE       512
#define PID_STACK_SIZE           256
#define WATCHDOG_STACK_SIZE       128

#define BLINKER_PERIOD_MS        500
#define PRODUCER_PERIOD_MS       200
#define CONSUMER_TIMEOUT_MS      500
#define WORKER_PERIOD_MS         300
#define MONITOR_PERIOD_MS        2000
#define PID_PERIOD_MS            100
#define WATCHDOG_PERIOD_MS        200

/* ================================================================
 * 全局资源句柄
 * ================================================================ */
extern osMessageQId      g_msgQueueHandle;
extern IWDG_HandleTypeDef hiwdg;
extern TIM_HandleTypeDef  htim2;

/* ================================================================
 * API
 * ================================================================ */
void  PrintBootBanner(void);
void  TaskManager_Init(void);
void  PID_Init(PID_Controller *pid, float Kp, float Ki, float Kd, float setpoint);
float PID_Compute(PID_Controller *pid, float measured_value, float dt);

#ifdef __cplusplus
}
#endif

#endif /* __TASK_MANAGER_H */
