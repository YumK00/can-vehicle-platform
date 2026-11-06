// #include "spii.h"
// #include "stm32f1xx_hal_spi.h"

// /**
//  * ============================================================
//  * 硬件SPI1驱动（W25Q64 FLASH）
//  * ============================================================
//  *
//  * 【硬件SPI 和 软件SPI 的区别】
//  *   软件SPI（spi.c）：用 GPIO 手动拉高拉低模拟时钟，一位一位地凑。
//  *                    好处是引脚随便选，坏处是占CPU、速度慢。
//  *   硬件SPI（本文件）：时钟由 SPI 外设硬件自动产生，CPU 只要把数据丢进
//  *                    寄存器就行。速度快、不占CPU，但引脚必须用 SPI 的
//  *                    固定复用引脚（F1的SPI1固定为 PA5/PA6/PA7）。
//  *
//  * 【关键：引脚复用(AF)】
//  *   用硬件SPI时，PA5/PA6/PA7 不再当普通IO用，而是"复用"给 SPI1 外设，
//  *   由 SPI 外设直接控制它们。
//  *     - SCK(PA5)、MOSI(PA7)：是主机输出的线 → 配成 AF_PP(复用推挽输出)
//  *     - MISO(PA6)：是主机输入的线(从机驱动) → 配成 INPUT(输入)
//  *   注意：一旦配成 AF_PP，用 HAL_GPIO_WritePin 去写 PA5/PA7 是无效的，
//  *         那几个引脚归 SPI 外设管了。
//  *
//  * 【SPI的四根线】
//  *   CS(NSS)：片选，低电平选中从机。本驱动用软件控制(SPI_NSS_SOFT)，
//  *            所以 CS 是普通推挽输出，自己在收发前后拉低拉高。
//  *   SCLK   ：时钟，硬件自动产生
//  *   MOSI   ：Master Out Slave In，主机→从机
//  *   MISO   ：Master In Slave Out，从机→主机
//  *
//  * 【SPI是全双工】
//  *   每发1位的同时就收1位，所以"读"和"写"是同一个动作。
//  *   这也是为什么核心函数叫 spii_WriteReadByte：发一个字节 = 收一个字节。
//  *
//  * 【Mode 0】
//  *   CPOL=0(CLK空闲为低) + CPHA=0(第一个边沿采样)：
//  *     CLKPolarity = SPI_POLARITY_LOW
//  *     CLKPhase    = SPI_PHASE_1EDGE
//  *
//  * 【引脚分配】(SPI1 的默认引脚，无重映射，全在 GPIOA)
//  *   PA4 = CS，PA5 = SCK，PA6 = MISO，PA7 = MOSI
//  */

// /* 全局的SPI句柄。HAL的所有SPI函数都要靠它找到是哪个SPI外设 */
// SPI_HandleTypeDef hspi1;

// /**
//  * @brief  硬件SPI1初始化
//  * @note   分三步：(1)配置GPIO复用 (2)填SPI参数结构体 (3)调用HAL_SPI_Init
//  * @retval None
//  */
// void spii_Init(){
//     /* ---------- (0) 开时钟 ----------
//      * 用哪个外设就要开哪个外设的时钟，这是STM32的规矩(不开时钟外设不工作)。
//      * SPI1 挂在 APB2 总线上。
//      */
//     __HAL_RCC_SPI1_CLK_ENABLE();
//     __HAL_RCC_GPIOA_CLK_ENABLE();

//     GPIO_InitTypeDef gpio = {0};

//     /* ---------- (1) 配置GPIO ---------- */

//     // SCK PA5、MOSI PA7：复用推挽输出(主机驱动的线)
//     // HAL_GPIO_Init 只改你.Pin里点名的引脚，所以分三次配置互不干扰。
//     gpio.Pin = GPIO_PIN_5 | GPIO_PIN_7;
//     gpio.Mode = GPIO_MODE_AF_PP;
//     gpio.Pull = GPIO_NOPULL;
//     gpio.Speed = GPIO_SPEED_FREQ_HIGH;   // F1没有VERY_HIGH，最高只有HIGH(50MHz)
//     HAL_GPIO_Init(GPIOA, &gpio);

//     // MISO PA6：输入(主机接收线，由从机驱动)
//     // 加上拉是为了从机没接/没驱动时引脚不乱跳(悬空引脚读出来是随机的)。
//     gpio.Pin = GPIO_PIN_6;
//     gpio.Mode = GPIO_MODE_INPUT;
//     gpio.Pull = GPIO_PULLUP;
//     HAL_GPIO_Init(GPIOA, &gpio);

//     // CS PA4：普通推挽输出(SPI_NSS_SOFT，片选要自己控制)
//     gpio.Pin = GPIO_PIN_4;
//     gpio.Mode = GPIO_MODE_OUTPUT_PP;
//     gpio.Pull = GPIO_NOPULL;
//     HAL_GPIO_Init(GPIOA, &gpio);
//     HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_SET);  // 空闲：CS拉高(不选中)

//     /* ---------- (2) 配置SPI外设参数 ---------- */
//     hspi1.Instance = SPI1;
//     hspi1.Init.Mode = SPI_MODE_MASTER;            // 主机模式(我们产生时钟)
//     hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;    // CPOL=0：空闲时钟为低  ┐ 两者
//     hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;        // CPHA=0：第一边沿采样  ┘ 合起来=Mode0
//     hspi1.Init.Direction = SPI_DIRECTION_2LINES;  // 双线全双工(MOSI+MISO都用)
//     hspi1.Init.DataSize = SPI_DATASIZE_8BIT;      // 一次收发8位(=1字节)
//     hspi1.Init.NSS = SPI_NSS_SOFT;                // 片选软件控制(硬件NSS会自己管CS，不用)
//     hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_64; // 时钟=72MHz/64=1.125MHz
//     hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;       // 高位先发(SPI的通用约定)
//     hspi1.Init.TIMode = SPI_TIMODE_DISABLE;       // 不用TI协议(那是给特定芯片的)
//     hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;  // 不用硬件CRC校验
//     hspi1.Init.CRCPolynomial = 10;                // CRC关了，这个值填什么都无所谓
//     HAL_SPI_Init(&hspi1);                         // 把上面参数写进SPI寄存器
// }

// /**
//  * @brief  同时收发1个字节(SPI全双工)
//  * @param  data：要发给从机的字节
//  * @retval 同时从从机收回来的字节
//  * @note   硬件SPI收发是一体的，HAL_SPI_TransmitReceive 就是把 data 的8位
//  *         推出去，同时把从机的8位收进 rdata。不需要手动翻时钟。
//  *         发1个字节通常要配1个"无效数据"(比如0xFF)来"换"回1个字节。
//  *         最后的 1000 是超时时间(毫秒)，防止硬件出问题时死等。
//  */
// uint8_t spii_WriteReadByte(uint8_t data){
//     uint8_t rdata = 0x00;
//     HAL_SPI_TransmitReceive(&hspi1, &data, &rdata, 1, 1000);
//     return rdata;
// }

// /**
//  * @brief  拉低CS，选中从机(开始一次SPI通信)
//  * @note   从机只在CS为低时才响应SCLK。
//  * @retval None
//  */
// void SPII_Start(void){
//     HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_RESET);
// }

// /**
//  * @brief  拉高CS，释放从机(结束一次SPI通信)
//  * @note   每条完整命令(如读ID)结束后必须拉高，否则芯片一直以为在通信中，
//  *         下一次命令会错位。忘了拉高是SPI最常见的bug。
//  * @retval None
//  */
// void SPII_Stop(void){
//     HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_SET);
// }
