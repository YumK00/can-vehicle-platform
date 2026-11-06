/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "cmsis_os.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
/* ============================================================
 * 【块1：包含头文件】
 *  在这里写头文件。
 * ============================================================ */
#include "oled.h"
#include "interrupt.h"
#include "stm32f103xb.h"
#include "stm32f1xx_hal_gpio.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/_intsup.h>
#include "stm32f1xx_hal_uart.h"
#include "adc_dma.h"
#include "serial.h"
#include "can.h"
#include "protocol.h"
#include "motor.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
/* ============================================================
 * 【块2：自定义类型定义】
 *  在这里 typedef 你自己的结构体、枚举等。
 * ============================================================ */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* ============================================================
 * 【块3：宏定义】
 *  在这里 #define 你自己的常量、引脚别名等。
 * ============================================================ */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */
/* ============================================================
 * 【块4：带参数的宏】
 *  在这里写函数式宏。
 * ============================================================ */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
/* ============================================================
 * 【块5：全局/静态变量】
 *  在这里定义本文件用的全局变量或 static 变量。
 *  中断里修改、主循环里读的变量必须加 volatile。
 * ============================================================ */
  uint32_t g_oled_counter = 0;// 主循环计数器，用于 OLED 显示


/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
void MX_FREERTOS_Init(void);
/* USER CODE BEGIN PFP */
/* ============================================================
 * 【块6：函数声明】
 *  在这里声明你自己写的、定义在 main.c 后面的函数。
 * ============================================================ */
void SystemClock_Config(void);

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
/* ============================================================
 * 【块7：自定义函数实现】
 *  在这里写你自己的函数（定义在 main() 之前也可以，
 *  但放在这里结构更清晰，配合块6的声明）。
 * ============================================================ */




/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */
  /* ============================================================
   * 【块8：HAL_Init 之前】
   *  极少用到。可以在这里做最早期的硬件操作，
   *  注意：此时 HAL 还没初始化，不能调 HAL 函数。
   * ============================================================ */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
    HAL_Init();

  /* USER CODE BEGIN Init */
  /* ============================================================
   * 【块9：HAL_Init 之后，时钟配置之前】
   *  可以在这里做一些不依赖系统时钟的初始化。
   * ============================================================ */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */
  /* ============================================================
   * 【块10：时钟配置之后，外设初始化之前】
   *  系统时钟已经配好，可以做需要时钟的早期操作。
   *  一般留空。
   * ============================================================ */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  /* USER CODE BEGIN 2 */
  /* ============================================================
   * 【块11：外设初始化之后，进入主循环之前】★最常用★
   * ============================================================ */
    OLED_Init();
    ADC_DMA_Init();      //PA0=ADC1_IN0、PA1=ADC1_IN1，扫描+连续+DMA循环搬运
    Serial_Init();//PA2=TX、PA3=RX，波特率115200，8N1
    LED_Init();          // LED(PB15)初始化，用于观察 FreeRTOS 任务是否在跑
    Key_Init();          // 按键(PB12)初始化：配成下降沿触发的外部中断
    Motor_Init();          // PA4和PA5控制方向，PA6=TIM3_CH1，1kHz PWM输出，上电占空比0(电机不转)
    CAN_Init();         // PA11=CAN_RX、PA12=CAN_TX，波特率250K
  /* USER CODE END 2 */

  /* Init scheduler */
  osKernelInitialize();  /* Call init function for freertos objects (in cmsis_os2.c) */
  MX_FREERTOS_Init();

  /* Start scheduler */
  osKernelStart();

  /* We should never get here as control is now taken by the scheduler */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV2;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */
/* ============================================================
 * 【块14：main.c 末尾，Error_Handler 之前】
 *  在这里写你自己的函数实现（如果不想写在块7里）。
 *  也可以放中断回调函数的重写，比如：
 *    void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) { ... }
 *  不过你的回调已经写在 interrupt.c 里了，这里留空即可。
 * ============================================================ */

/* USER CODE END 4 */

/**
  * @brief  Period elapsed callback in non blocking mode
  * @note   This function is called  when TIM4 interrupt took place, inside
  * HAL_TIM_IRQHandler(). It makes a direct call to HAL_IncTick() to increment
  * a global variable "uwTick" used as application time base.
  * @param  htim : TIM handle
  * @retval None
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  /* USER CODE BEGIN Callback 0 */

  /* USER CODE END Callback 0 */
  if (htim->Instance == TIM4)
  {
    HAL_IncTick();
  }
  /* USER CODE BEGIN Callback 1 */

  /* USER CODE END Callback 1 */
}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* ============================================================
   * 【块15：错误处理】
   *  HAL 初始化失败时会进入这里。
   *  可以加 LED 闪烁指示错误，方便调试。
   *  例如：
   *    while (1)
   *    {
   *        HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
   *        HAL_Delay(200);
   *    }
   * ============================================================ */
  __disable_irq();
  while (1)
  {
    HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_8);
    HAL_Delay(200);
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* ============================================================
   * 【块16：参数断言失败】
   *  只有定义了 USE_FULL_ASSERT 才会编译这个函数。
   *  HAL 库的 assert_param 检查失败时调用。
   *  可以在这里打印出错的文件名和行号，方便调试。
   *  例如：printf("Assert failed: %s line %lu\r\n", file, line);
   * ============================================================ */
  (void)file;
  (void)line;
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
