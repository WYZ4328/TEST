/**
  ******************************************************************************
  * @file    task_manager.h
  * @brief   演示任务管理模块 — 头文件
  *          包含6个 FreeRTOS 演示任务的声明、PID 控制器结构体、
  *          以及所有任务共享的宏定义和句柄。
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

/* ================================================================
 * 项目版本信息
 * ================================================================ */
#define PROJECT_NAME             "STM32F103 Bootloader Demo"
#define PROJECT_VERSION_MAJOR    1
#define PROJECT_VERSION_MINOR    0
#define PROJECT_VERSION_PATCH    0
#define PROJECT_VERSION_STR      "v1.0.0"
#define PROJECT_AUTHOR           "WYZ4328"
#define PROJECT_DESCRIPTION      "FreeRTOS + PID Motor Control + IWDG"

/* ================================================================
 * PID 控制器状态结构体
 * ================================================================ */
typedef struct {
    float Kp;             /* 比例系数 */
    float Ki;             /* 积分系数 */
    float Kd;             /* 微分系数 */
    float setpoint;       /* 目标值 */
    float integral;       /* 积分累加 */
    float prev_error;     /* 上一次误差 (微分项用) */
    float output_min;     /* 输出下限 */
    float output_max;     /* 输出上限 */
} PID_Controller;

/* ================================================================
 * 任务栈大小定义 (单位: 字, Cortex-M3 1字=4字节)
 * ================================================================ */
#define BLINKER_STACK_SIZE       128
#define PRODUCER_STACK_SIZE      256
#define CONSUMER_STACK_SIZE      256
#define WORKER_STACK_SIZE        256
#define MONITOR_STACK_SIZE       512
#define PID_STACK_SIZE           256

/* ================================================================
 * 演示周期定义 (单位: 毫秒)
 * ================================================================ */
#define PRODUCER_PERIOD_MS       200
#define CONSUMER_TIMEOUT_MS      500
#define WORKER_PERIOD_MS         300
#define MONITOR_PERIOD_MS        2000
#define PID_PERIOD_MS            100

/* ================================================================
 * PID 默认参数 (直流电机调速)
 * 目标转速: 1000 RPM, PWM 输出: 0~100%
 * ================================================================ */
#define PID_KP_DEFAULT           0.8f     /* 比例系数 */
#define PID_KI_DEFAULT           0.3f     /* 积分系数 */
#define PID_KD_DEFAULT           0.02f    /* 微分系数 */
#define PID_TARGET_RPM           1000.0f  /* 目标转速 (RPM) */
#define PID_PWM_MAX              100.0f   /* PWM 占空比上限 (%) */

/* ================================================================
 * 电机 PWM 输出配置 (TIM2_CH1 → PA0)
 * TIM2 时钟 = APB1 = 8MHz (HSI, 无 PLL)
 * ARR = 399 → PWM 频率 = 8MHz / 400 = 20kHz
 * CCR = duty% × 4  (0% → 0,  100% → 400)
 * ================================================================ */
#define MOTOR_TIM               (&htim2)
#define MOTOR_TIM_CHANNEL       TIM_CHANNEL_1
#define MOTOR_TIM_ARR           399
#define MOTOR_PWM_FREQ_HZ       20000

/* ================================================================
 * 电机仿真模型参数
 * 一阶模型: G(s) = K_m / (τ·s + 1)
 * RPM = K_m * PWM_duty  (稳态)
 * ================================================================ */
#define MOTOR_GAIN               12.0f    /* K_m: 每1%占空比产生12 RPM */
#define MOTOR_TAU                0.3f     /* τ: 机电时间常数 (秒) */

/* ================================================================
 * 看门狗 (IWDG) 配置
 * LSI ≈ 40kHz, 预分频 64 → 625 Hz
 * 重装载 625 → 超时 = 625/625 = 1 秒
 * 空闲钩子 (vApplicationIdleHook) 中自动喂狗
 * ================================================================ */
#define IWDG_PRESCALER           IWDG_PRESCALER_64
#define IWDG_RELOAD              625
#define IWDG_TIMEOUT_MS          1000

/* ================================================================
 * 消息队列
 * ================================================================ */
#define MSG_QUEUE_SIZE           16

/* ================================================================
 * 全局句柄 (供 Monitor 任务和其他模块访问)
 * ================================================================ */
extern osThreadId  blinkerTaskHandle;
extern osThreadId  producerTaskHandle;
extern osThreadId  consumerTaskHandle;
extern osThreadId  workerTaskHandle;
extern osThreadId  monitorTaskHandle;
extern osThreadId  pidTaskHandle;
extern osMessageQId msgQueueHandle;
extern IWDG_HandleTypeDef hiwdg;
extern TIM_HandleTypeDef  htim2;

/* ================================================================
 * API 函数
 * ================================================================ */

/** 打印启动横幅 — 版本信息、FreeRTOS 版本、编译时间 */
void PrintBootBanner(void);

/** 初始化所有演示任务和消息队列 (由 main 调用) */
void TaskManager_Init(void);

/** PID 控制器初始化 */
void PID_Init(PID_Controller *pid, float Kp, float Ki, float Kd, float setpoint);

/** PID 控制器计算 */
float PID_Compute(PID_Controller *pid, float measured_value, float dt);

#ifdef __cplusplus
}
#endif

#endif /* __TASK_MANAGER_H */
