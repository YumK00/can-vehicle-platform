// #include "spi.h"
// #include "stm32f1xx_hal_gpio.h"
// #include "stm32f1xx_hal_rcc.h"

// /**
//  * ============================================================
//  * 软件SPI（GPIO模拟SPI）驱动
//  * ============================================================
//  * 【SPI的四根线】(比I2C的两根线多)
//  *   CS(NSS)：片选，低电平选中从机(一个主机可带多个从机，靠CS区分)
//  *   SCLK   ：时钟，由主机产生
//  *   MOSI   ：Master Out Slave In，主机→从机的数据线
//  *   MISO   ：Master In Slave Out，从机→主机的数据线
//  *
//  * 【SPI是全双工】
//  *   发送和接收同时进行：主机每发出1个时钟，同时往MOSI推1位、从MISO收1位。
//  *   所以"写"和"读"是同一个动作——这也是为什么本驱动的核心函数叫
//  *   spi_WriteReadByte：发一个字节的同时收到一个字节。
//  *
//  *
//  * 【Mode 0】
//  *   SPI有4种模式(由CPOL、CPHA组合)。本驱动用Mode 0：
//  *     CPOL(CLK Polarity)=0：时钟空闲时是低电平
//  *     CPHA(CLK Phase)=0：数据在第一个边沿采样(读)，第二个边沿变化(写)
//  *
//  * 【引脚分配】(SPI1的默认引脚，都在GPIOA)
//  *   PA4 = CS，PA5 = SCLK，PA7 = MOSI，PA6 = MISO
//  */

// /* ========== 引脚宏定义 ==========
//  * CS、SCLK、MOSI 是主机输出，配推挽输出；
//  * MISO 是从机输入，配输入(带上拉)。
//  */
// #define SPI_CS_PIN    GPIO_PIN_4
// #define SPI_SCLK_PIN  GPIO_PIN_5
// #define SPI_MOSI_PIN  GPIO_PIN_7
// #define SPI_MISO_PIN  GPIO_PIN_6
// #define SPI_PORT      GPIOA

// /* 用宏把"写引脚电平"包装成好读的名字，代码里直接写 SPI_CS_LOW() 即可 */
// #define SPI_CS_LOW()    HAL_GPIO_WritePin(SPI_PORT, SPI_CS_PIN, GPIO_PIN_RESET)
// #define SPI_CS_HIGH()   HAL_GPIO_WritePin(SPI_PORT, SPI_CS_PIN, GPIO_PIN_SET)
// #define SPI_SCLK_LOW()  HAL_GPIO_WritePin(SPI_PORT, SPI_SCLK_PIN, GPIO_PIN_RESET)
// #define SPI_SCLK_HIGH() HAL_GPIO_WritePin(SPI_PORT, SPI_SCLK_PIN, GPIO_PIN_SET)
// #define SPI_MOSI_LOW()  HAL_GPIO_WritePin(SPI_PORT, SPI_MOSI_PIN, GPIO_PIN_RESET)
// #define SPI_MOSI_HIGH() HAL_GPIO_WritePin(SPI_PORT, SPI_MOSI_PIN, GPIO_PIN_SET)
// #define SPI_READ_MISO() HAL_GPIO_ReadPin(SPI_PORT, SPI_MISO_PIN)

// /**
//  * @brief  SPI初始化：配置CS/SCLK/MOSI为推挽输出、MISO为上拉输入，置空闲电平
//  * @note   引脚方向：
//  *         CS、SCLK、MOSI 是主机驱动的，用推挽输出(GPIO_MODE_OUTPUT_PP)。
//  *         MISO 是从机驱动的，主机只能读，用输入(GPIO_MODE_INPUT)，
//  *         加上拉(GPIO_PULLUP)防止从机没接时引脚悬空乱跳。
//  *         空闲状态：CS拉高(不选中从机)、SCLK拉低(Mode0的CPOL=0)。
//  * @retval None
//  */
// void spi_Init(void)
// {
//     GPIO_InitTypeDef gpio = {0};
//     __HAL_RCC_GPIOA_CLK_ENABLE(); // 开启GPIOA时钟

//     // CS PA4, SCLK PA5, MOSI PA7：普通推挽输出
//     gpio.Pin = SPI_CS_PIN | SPI_SCLK_PIN | SPI_MOSI_PIN;
//     gpio.Mode = GPIO_MODE_OUTPUT_PP;
//     gpio.Speed = GPIO_SPEED_FREQ_HIGH;
//     HAL_GPIO_Init(SPI_PORT, &gpio);

//     // MISO PA6：上拉输入
//     gpio.Pin = SPI_MISO_PIN;
//     gpio.Mode = GPIO_MODE_INPUT;
//     gpio.Pull = GPIO_PULLUP;
//     HAL_GPIO_Init(SPI_PORT, &gpio);

//     // 空闲状态：CS高(不选中)，SCLK低(Mode0 CPOL=0)
//     SPI_CS_HIGH();
//     SPI_SCLK_LOW();
// }

// /**
//  * @brief  拉低CS，选中从机(开始一次SPI通信)
//  * @note   SPI从机只在CS为低时才会响应SCLK。
//  * @retval None
//  */
// void spi_Start(void)
// {
//     SPI_CS_LOW();
// }

// /**
//  * @brief  拉高CS，释放从机(结束一次SPI通信)
//  * @retval None
//  */
// void spi_Stop(void)
// {
//     SPI_CS_HIGH();
// }

// /**
//  * @brief  同时收发1个字节(SPI全双工)
//  * @param  data：要发送给从机的8位数据
//  * @retval 同时从从机收到的8位数据
//  * @note   Mode 0时序，每个bit分三步：
//  *         1. 把data的这一位(高位在前)摆到MOSI上；
//  *         2. SCLK拉高(上升沿)——从机在这一刻采样MOSI，主机也在这一刻采样MISO；
//  *         3. SCLK拉低(下降沿)——数据线换下一位。
//  *         发完8位，同时也就收回了8位。
//  */
// uint8_t spi_WriteReadByte(uint8_t data)
// {
//     uint8_t rdata = 0x00;
//     for (uint8_t i = 0; i < 8; i++)
//     {
//         // 发1位：该bit为1就拉高MOSI，否则拉低。
//         // 注意别写成 data & (0x80>>i) == 1，==优先级高于&会算错。
//         if (data & (0x80 >> i))
//         {
//             SPI_MOSI_HIGH();
//         }
//         else
//         {
//             SPI_MOSI_LOW();
//         }
//         SPI_SCLK_HIGH();            // 上升沿：从机采样MOSI，主机同步采样MISO
//         if (SPI_READ_MISO())        // 收1位：MISO为高(1)就置rdata对应位
//         {
//             rdata |= (0x80 >> i);
//         }
//         SPI_SCLK_LOW();             // 下降沿：数据线换下一位
//     }
//     return rdata;
// }
