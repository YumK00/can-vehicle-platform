// #include "adc.h"
// #include "stm32_hal_legacy.h"
// #include "stm32f1xx_hal_gpio.h"
// #include "stm32f1xx_hal_rcc.h"
// #include "stm32f1xx_hal_rcc_ex.h"   // __HAL_RCC_ADC_CONFIG / RCC_ADCPCLK2_DIVx
// #include "stm32f1xx_hal_adc_ex.h"   // HAL_ADCEx_Calibration_Start

// // ADC1句柄：全局变量，保存ADC1配置与运行状态
// ADC_HandleTypeDef hadc1;

// /**
//  * @brief  ADC1单次转换初始化，读取PA0(ADC1_IN0)上的模拟量
//  * @note   硬件过程：模拟电压 → PA0(GPIO模拟模式) → ADC采样保持 → 逐次逼近(SAR)量化
//  *         → 12位数字量存入DR数据寄存器 → 软件读取
//  *
//  *         ADC配置流程：
//  *         1. RCC开启GPIOA/ADC1时钟，并配置ADC时钟分频(ADCPRE)；
//  *         2. PA0配置为模拟输入GPIO_MODE_ANALOG(关闭数字输入电路)；
//  *         3. 配置ADC工作模式：单通道、单次转换、软件触发；
//  *         4. 配置规则组转换序列：通道0、Rank1、采样时间；
//  *         5. 自校准，消除内部采样电容误差。
//  *
//  *         时钟说明：STM32F1的ADC时钟ADCCLK不能超过14MHz。
//  *         本工程APB2=72MHz，ADCPRE分频6 → ADCCLK = 72/6 = 12MHz，符合要求。
//  *         一次转换时间 = 采样时间55.5 + 量化12.5 = 68个ADCCLK周期 ≈ 5.7us。
//  *
//  *         单次转换含义：ScanConvMode关闭(只转一个通道)、ContinuousConvMode关闭
//  *         (触发一次转一次，不连续)、ExternalTrigConv软件触发(调HAL_ADC_Start开始)。

//  *         这里因为是单通道，非连续转换，非扫描模式，所以HAL_ADC_Start(&hadc1)需要放在read里
//  *         每次都需要触发如果是连续转换模式的话，只需要在Init里触发一次即可
//  * @retval None
//  */
// void ADC_Init(void)
// {
//     // 开启GPIOA时钟，PA0为ADC1_IN0模拟输入引脚
//     __HAL_RCC_GPIOA_CLK_ENABLE();
//     // 开启ADC1外设时钟
//     __HAL_RCC_ADC1_CLK_ENABLE();

//     // ADC时钟分频：APB2=72MHz / 6 = 12MHz，F1的ADC时钟不能超过14MHz
//     __HAL_RCC_ADC_CONFIG(RCC_ADCPCLK2_DIV6);

//     // ==========GPIO配置：PA0模拟输入==========
//     GPIO_InitTypeDef gpio = {0};
//     gpio.Pin = GPIO_PIN_0;              // PA0 = ADC1_IN0
//     gpio.Mode = GPIO_MODE_ANALOG;       // 模拟输入模式：ADC专用，关闭内部数字输入电路
//     gpio.Pull = GPIO_NOPULL;            // 模拟模式无上下拉
//     gpio.Speed = GPIO_SPEED_HIGH;
//     HAL_GPIO_Init(GPIOA, &gpio);

//     // ==========ADC工作模式配置==========
//     hadc1.Instance = ADC1;                          // 指定ADC实例为ADC1
//     hadc1.Init.ScanConvMode = ADC_SCAN_DISABLE;     // 关闭扫描模式：只转换一个通道
//     hadc1.Init.ContinuousConvMode = DISABLE; // 单次转换：触发一次转一次，不连续
//     hadc1.Init.DiscontinuousConvMode = DISABLE; // 关闭间断模式(单通道用不上)
//     hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START; // 软件触发：调用HAL_ADC_Start时开始转换
//     hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;     // 数据右对齐：12位结果存DR低12位(0~4095)
//     hadc1.Init.NbrOfConversion = 1;                 // 规则组转换序列长度=1个通道
//     HAL_ADC_Init(&hadc1);

//     // ==========ADC自校准==========
//     // F1首次使用前必须校准，内部对采样电容做校准，否则读数有固定偏移
//     //校准可大幅减小因内部电容器组的变化而造成的准精度误差（参考手册这么写的）
//     // (效果等同于标准外设库里的ADC_Cmd/ADC_ResetCalibration/ADC_GetCalibrationStatus那几步)
//     HAL_ADCEx_Calibration_Start(&hadc1);

//     // ==========规则组转换序列配置==========
//     ADC_ChannelConfTypeDef sConfig = {0};
//     sConfig.Channel = ADC_CHANNEL_0;               // 转换通道0 = PA0
//     sConfig.Rank = ADC_REGULAR_RANK_1;             // 规则组第1个(也是唯一一个)转换
//     sConfig.SamplingTime = ADC_SAMPLETIME_55CYCLES_5; // 采样时间55.5个时钟周期
//     HAL_ADC_ConfigChannel(&hadc1, &sConfig);
// }

// /**
//  * @brief  读取一次ADC1转换结果(阻塞式单次转换)
//  * @note   流程：启动转换 → 轮询等待EOC转换结束标志 → 读DR数据寄存器 → 停止转换。
//  *         每次调用只触发一次转换，返回12位原始值(0~4095)，
//  *         实际电压 = 返回值 * 3.3V / 4095。
//  * @retval uint16_t：ADC转换结果(0~4095)
//  */
// uint16_t ADC_Read(void)
// {
//     HAL_ADC_Start(&hadc1);                        // 启动一次ADC转换(软件触发)
//     HAL_ADC_PollForConversion(&hadc1, 100);       // 轮询等待转换完成，超时100ms
//     uint16_t adc_val = HAL_ADC_GetValue(&hadc1);  // 读取DR数据寄存器中的12位结果
//     HAL_ADC_Stop(&hadc1);                         // 停止ADC(单次转换读完即停)
//     return adc_val;                               // 返回原始值0~4095
// }
