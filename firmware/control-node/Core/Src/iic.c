#include "stm32f103xb.h"
#include "stm32f1xx_hal.h"
#include "iic.h"
#include <stdint.h>

/**
 * ============================================================
 * 软件I2C（GPIO模拟I2C）驱动 —— 用于OLED显示屏
 * ============================================================
 * 【为什么叫"软件I2C"？】
 *   单片机实现I2C有两种方式：
 *   1) 硬件I2C：用STM32内部集成的I2C外设(比如驱动MPU6050用的I2C2)。
 *   2) 软件I2C：不碰硬件外设，直接操作GPIO手动拉高/拉低SCL、SDA，
 *   本文件就是软件I2C，占用 PB8=SCL、PB9=SDA，驱动OLED(地址0x78)。
 *
 * 【I2C总线三要素】
 *   1. 两根线：SCL(时钟)、SDA(数据)。都是开漏输出，必须外部上拉。
 *   2. 数据在SCL高电平时必须稳定，只能在SCL低电平时变化。
 *   3. 每个字节8位，高位(MSB)先发；第9个时钟是应答位(ACK)。
 *
 * 【为什么OLED用软件I2C而不用硬件I2C】
 *   硬件I2C(尤其F1系列)容易卡BUSY、坑比较多；软件I2C慢但稳定、好调，
 *   时序全在自己手里，出问题也好查。OLED刷新量不大，慢点无所谓。
 */

#define IIC_SCL_PORT GPIOB
#define IIC_SCL_PIN  GPIO_PIN_8
#define IIC_SDA_PIN  GPIO_PIN_9

/* CPU 主频(MHz)，用来把微秒换算成 DWT 的周期数 */
#define CPU_FREQ_MHZ  72

/**
 * @brief  初始化 DWT 周期计数器(CYCCNT)
 * @note   以前用"空循环数次数"来延时，有两个致命问题：
 *         1) -O0 下每次循环要走 6 条指令(变量全在栈上)，72 次实际是 ~11us 不是 1us；
 *         2) 换到 -O2 后，编译器发现空循环没有副作用，会把它整个删掉，延时直接变 0。
 *         DWT 的 CYCCNT 是内核里的一个"CPU 周期计数器"，读它来计时：
 *         和优化等级无关，1us 永远是 72 个周期。
 * @retval None
 */
static void DWT_Init(void)
{
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;  // 打开跟踪与调试单元(否则 DWT 不计数)
  DWT->CYCCNT = 0;                                  // 计数清零
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;              // 启动 CYCCNT
}

/**
 * @brief  精确微秒延时(基于 DWT 周期计数器)
 * @param  us：延时微秒数
 * @note   CYCCNT 是 32 位、每个 CPU 周期加 1。这里用无符号减法比较，
 *         即使计数器溢出回绕也能算出正确的差值(这是处理回绕的标准写法)。
 *         循环里读的是 volatile 的硬件寄存器，编译器不会把它优化掉。
 * @retval None
 */
static void Delay_us(uint32_t us)
{
  uint32_t start = DWT->CYCCNT;
  uint32_t ticks = us * CPU_FREQ_MHZ;          // 72MHz：1us = 72 个周期
  while ((DWT->CYCCNT - start) < ticks);
}

/**
 * @brief  写SDA数据线的电平
 * @param  bit：1=拉高SDA，0=拉低SDA
 * @note   I2C规定：SCL为高电平时SDA不能变(变了会被当成起始/停止信号)，
 *         所以每次改SDA后延时一下，让电平稳定下来，再去动SCL。
 * @retval None
 */
void MyIIC_W_SDA(uint8_t bit)
{
    if (bit)
    {
        HAL_GPIO_WritePin(IIC_SCL_PORT, IIC_SDA_PIN, GPIO_PIN_SET);
        Delay_us(1);
    }
    else
    {
        HAL_GPIO_WritePin(IIC_SCL_PORT, IIC_SDA_PIN, GPIO_PIN_RESET);
        Delay_us(1);
    }
}

/**
 * @brief  写SCL时钟线的电平
 * @param  bit：1=拉高SCL，0=拉低SCL
 * @retval None
 */
void MyIIC_W_SCL(uint8_t bit)
{
    if (bit)
    {
        HAL_GPIO_WritePin(IIC_SCL_PORT, IIC_SCL_PIN, GPIO_PIN_SET);
        Delay_us(1);
    }
    else
    {
        HAL_GPIO_WritePin(IIC_SCL_PORT, IIC_SCL_PIN, GPIO_PIN_RESET);
        Delay_us(1);
    }
}

/**
 * @brief  读SDA数据线的电平
 * @retval 1=SDA为高，0=SDA为低
 */
uint8_t MyIIC_R_SDA(void)
{
    return HAL_GPIO_ReadPin(IIC_SCL_PORT, IIC_SDA_PIN);
}

/**
 * @brief  软件I2C初始化：把PB8/PB9配成开漏输出，并把总线置为空闲
 * @note   开漏(GPIO_MODE_OUTPUT_OD)是I2C的硬件要求：
 *         开漏只能拉低、不能主动拉高，两条线靠外部上拉电阻拉到高电平，
 *         这样多个器件可以"线与"——谁拉低总线就是低，都不拉就是高。
 *         初始化最后SCL、SDA都置1(释放总线，靠上拉变成高=空闲)。
 * @retval None
 */
void MyIIC_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  __HAL_RCC_GPIOB_CLK_ENABLE();
  DWT_Init();   // Delay_us 依赖它计时，必须先初始化

  GPIO_InitStruct.Pin = IIC_SCL_PIN | IIC_SDA_PIN;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;   // 开漏输出(I2C标准)
  GPIO_InitStruct.Pull = GPIO_NOPULL;           // 靠外部上拉(OLED模块板上自带)
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(IIC_SCL_PORT, &GPIO_InitStruct);

  MyIIC_W_SCL(1);   // SCL置高
  MyIIC_W_SDA(1);   // SDA置高 → 总线进入空闲状态
}

/**
 * @brief  产生I2C起始信号(START)
 * @note   时序：SCL为高电平时，SDA从高→低跳变，表示"通信开始"。
 *         之后把SCL拉低，准备开始传数据(数据只在SCL低时变化)。
 * @retval None
 */
void MyIIC_Start(void)
{
  MyIIC_W_SDA(1);   // 1. SDA先拉高
  MyIIC_W_SCL(1);   // 2. SCL拉高，此刻SDA、SCL都为高
  MyIIC_W_SDA(0);   // 3. SCL为高期间SDA拉低 → 这就是起始信号!
  MyIIC_W_SCL(0);   // 4. SCL拉低，占住总线，准备发数据
}

/**
 * @brief  产生I2C停止信号(STOP)
 * @note   时序：SCL为高电平时，SDA从低→高跳变，表示"通信结束"。
 * @retval None
 */
void MyIIC_Stop(void)
{
  MyIIC_W_SDA(0);   // 1. SDA先拉低
  MyIIC_W_SCL(1);   // 2. SCL拉高
  MyIIC_W_SDA(1);   // 3. SCL为高期间SDA拉高 → 这就是停止信号!
}

/**
 * @brief  发送1个字节
 * @param  byte：要发送的8位数据
 * @note   时序：每发1位，先把SDA摆好，再SCL拉高(从机此刻采样走1位)，
 *         然后SCL拉低准备下一位。高位(bit7)先发。
 *         发完8位后给第9个时钟(应答时钟)，本函数不判断从机是否应答
 * @retval None
 */
void MyIIC_SendByte(uint8_t byte)
{
  for (int i = 0; i < 8; i++)
  {
    // 高位先发：0x80>>i 依次取出 bit7、bit6、...、bit0
    MyIIC_W_SDA((byte & (0x80 >> i)) ? 1 : 0);
    MyIIC_W_SCL(1);   // SCL高：从机在此刻采样SDA
    MyIIC_W_SCL(0);   // SCL低：准备下一位
  }
  MyIIC_W_SCL(1);     // 第9个时钟(应答位)，这里不读应答信号
  MyIIC_W_SCL(0);
}

/**
 * @brief  连续发送多个字节(不产生 Start/Stop)
 * @param  buf：数据首地址
 * @param  len：字节数
 * @note   配合 Start/Stop 使用，把"一大串数据"塞进同一次 I2C 事务。
 *         SSD1306 收到一个字节后列地址会自动+1，所以连续写不用每字节
 *         重新发地址和控制字 —— 这正是消掉"每字节一次事务"的关键。
 * @retval None
 */
void MyIIC_SendBytes(const uint8_t *buf, uint16_t len)
{
  for (uint16_t i = 0; i < len; i++)
  {
    MyIIC_SendByte(buf[i]);
  }
}

/**
 * @brief  接收1个字节
 * @retval 收到的8位数据
 * @note   时序：主机先释放SDA(拉高，交给从机来驱动)，然后每拉高一次SCL，
 *         从机就把1位放到SDA上，主机读走。高位在前。
 * @retval None
 */
uint8_t MyIIC_ReceiveByte(void)
{
    MyIIC_W_SDA(1);   // 释放SDA线，交给从机驱动
    uint8_t byte = 0x00;
    for (int i = 0; i < 8; i++)
    {
        MyIIC_W_SCL(1);                               // SCL高：从机把1位放到SDA
        if (MyIIC_R_SDA() == 1)  byte |= (0x80 >> i); // 读到1就拼进byte对应位
        MyIIC_W_SCL(0);
    }
    return byte;
}

/**
 * @brief  发送应答信号(主机→从机)
 * @param  ack：0=应答(ACK，继续收)，1=非应答(NACK，结束接收)
 * @note   主机读完数据后，用ACK告诉从机"再来一个字节"，
 *         用NACK告诉从机"不要了，到此为止"。
 * @retval None
 */
void MyIIC_SendAck(uint8_t ack)
{
    MyIIC_W_SDA(ack);   // 0=拉低(ACK)，1=拉高(NACK)
    MyIIC_W_SCL(1);
    MyIIC_W_SCL(0);
}

/**
 * @brief  等待从机应答(主机发完数据后调用)
 * @retval 0=从机应答了(ACK)，1=从机没应答(NACK，说明出问题了)
 * @note   时序：主机释放SDA，拉高SCL发出第9个时钟，
 *         若从机应答会把SDA拉低，主机读SDA=0即ACK，=1即NACK。
 */
uint8_t MyIIC_WaitAck(void)
{
    uint8_t ack;
    MyIIC_W_SDA(1);      // 主机释放SDA，看从机拉不拉低
    MyIIC_W_SCL(1);      // 第9个时钟
    ack = MyIIC_R_SDA(); // 读SDA：0=从机拉低=应答，1=没应答
    MyIIC_W_SCL(0);
    return ack;
}
