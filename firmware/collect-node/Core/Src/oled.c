#include "oled.h"
#include "iic.h"
#include "OLED_Font.h"

/**
  * @brief  OLED写命令
  * @param  Command 要写入的命令
  * @retval 无
  */
void OLED_WriteCommand(uint8_t Command)
{
	MyIIC_Start();
	MyIIC_SendByte(0x78);		// OLED I2C从机地址
	MyIIC_SendByte(0x00);		// 00=写命令
	MyIIC_SendByte(Command);
	MyIIC_Stop();
}

/**
  * @brief  OLED写数据
  * @param  Data 要写入的数据
  * @retval 无
  */
void OLED_WriteData(uint8_t Data)
{
	MyIIC_Start();
	MyIIC_SendByte(0x78);		// OLED I2C从机地址
	MyIIC_SendByte(0x40);		// 40=写数据
	MyIIC_SendByte(Data);
	MyIIC_Stop();
}

/**
  * @brief  OLED连续写命令(一次事务发多条命令)
  * @param  cmd 命令数组
  * @param  len 命令条数
  * @note   SSD1306 的"控制字"对它后面、同一次事务里的所有字节都生效，
  *         所以 0x00 之后可以连着塞多条命令，只需一次 Start/Stop。
  *         原来 OLED_SetCursor 用 3 次 OLED_WriteCommand = 3 次完整事务，
  *         合并成 1 次即省下 2 次 Start/数据/Stop 的开销。
  */
static void OLED_WriteCmdSeq(const uint8_t *cmd, uint8_t len)
{
	MyIIC_Start();
	MyIIC_SendByte(0x78);		// 从机地址
	MyIIC_SendByte(0x00);		// 00=写命令
	MyIIC_SendBytes(cmd, len);	// 一次性发完所有命令
	MyIIC_Stop();
}

/**
  * @brief  OLED连续写数据(一次事务发多个字节)
  * @param  data 数据首地址
  * @param  len  字节数
  * @note   芯片收到一个数据字节后列地址自动+1，所以连续写不需要
  *         每字节重发地址和控制字。原来写一个 8x16 字符要发 16 次
  *         单字节事务，现在上下半页各 1 次，共 2 次。
  */
static void OLED_WriteDataSeq(const uint8_t *data, uint16_t len)
{
	MyIIC_Start();
	MyIIC_SendByte(0x78);		// 从机地址
	MyIIC_SendByte(0x40);		// 40=写数据
	MyIIC_SendBytes(data, len);	// 一次性发完所有数据
	MyIIC_Stop();
}

/**
  * @brief  OLED设置光标位置
  * @param  Y 以左上角为原点，向下方向的页，范围：0~7
  * @param  X 以左上角为原点，向右方向的列，范围：0~127
  * @retval 无
  */
void OLED_SetCursor(uint8_t Y, uint8_t X)
{
	uint8_t cmd[3];
	cmd[0] = 0xB0 | Y;					//设置页Y
	cmd[1] = 0x10 | ((X & 0xF0) >> 4);	//X高4位
	cmd[2] = 0x00 | (X & 0x0F);			//X低4位
	OLED_WriteCmdSeq(cmd, 3);			//3条命令合成一次事务
}

/**
  * @brief  OLED清屏
  * @param  无
  * @retval 无
  */
void OLED_Clear(void)
{
	uint8_t i, j;
	for (j = 0; j < 8; j++)
	{
		OLED_SetCursor(j, 0);
		for(i = 0; i < 128; i++)
		{
			OLED_WriteData(0x00);
		}
	}
}

/**
  * @brief  OLED显示一个字符 8*16字体
  * @param  Line 行位置，范围：1~4
  * @param  Column 列位置，范围：1~16
  * @param  Char 要显示的一个字符，ASCII可见字符
  * @retval 无
  */
void OLED_ShowChar(uint8_t Line, uint8_t Column, char Char)
{
	OLED_SetCursor((Line - 1) * 2, (Column - 1) * 8);			//上半页
	OLED_WriteDataSeq(&OLED_F8x16[Char - ' '][0], 8);
	OLED_SetCursor((Line - 1) * 2 + 1, (Column - 1) * 8);		//下半页
	OLED_WriteDataSeq(&OLED_F8x16[Char - ' '][8], 8);
}

/**
  * @brief  OLED显示字符串
  * @param  Line 起始行，1~4
  * @param  Column 起始列，1~16
  * @param  String 字符串指针
  * @retval 无
  */
void OLED_ShowString(uint8_t Line, uint8_t Column, char *String)
{
	uint8_t i;
	for (i = 0; String[i] != '\0'; i++)
	{
		OLED_ShowChar(Line, Column + i, String[i]);
	}
}

/**
  * @brief  次方内部工具
  */
static uint32_t OLED_Pow(uint32_t X, uint32_t Y)
{
	uint32_t Result = 1;
	while (Y--)
	{
		Result *= X;
	}
	return Result;
}

/**
  * @brief  OLED显示无符号十进制数字
  * @param  Line 行1‑4
  * @param  Column 列1‑16
  * @param  Number 数字
  * @param  Length 显示位数
  * @retval 无
  */
void OLED_ShowNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length)
{
	uint8_t i;
	for (i = 0; i < Length; i++)
	{
		OLED_ShowChar(Line, Column + i, Number / OLED_Pow(10, Length - i - 1) % 10 + '0');
	}
}

/**
  * @brief  OLED显示带符号十进制数字
  */
void OLED_ShowSignedNum(uint8_t Line, uint8_t Column, int32_t Number, uint8_t Length)
{
	uint8_t i;
	uint32_t Number1;
	if (Number >= 0)
	{
		OLED_ShowChar(Line, Column, '+');
		Number1 = Number;
	}
	else
	{
		OLED_ShowChar(Line, Column, '-');
		Number1 = (uint32_t)(-Number);
	}
	for (i = 0; i < Length; i++)
	{
		OLED_ShowChar(Line, Column + i + 1, Number1 / OLED_Pow(10, Length - i - 1) % 10 + '0');
	}
}

/**
  * @brief  OLED显示十六进制数
  */
void OLED_ShowHexNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length)
{
	uint8_t i, SingleNumber;
	for (i = 0; i < Length; i++)
	{
		SingleNumber = Number / OLED_Pow(16, Length - i - 1) % 16;
		if (SingleNumber < 10)
		{
			OLED_ShowChar(Line, Column + i, SingleNumber + '0');
		}
		else
		{
			OLED_ShowChar(Line, Column + i, SingleNumber - 10 + 'A');
		}
	}
}

/**
  * @brief  OLED显示二进制数
  */
void OLED_ShowBinNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length)
{
	uint8_t i;
	for (i = 0; i < Length; i++)
	{
		OLED_ShowChar(Line, Column + i, Number / OLED_Pow(2, Length - i - 1) % 2 + '0');
	}
}

// 绘图缓冲区 128*64/8 =1024字节
static uint8_t OLED_Buffer[1024];

void OLED_ClearBuffer(void)
{
	for (uint16_t i = 0; i < 1024; i++)
	{
		OLED_Buffer[i] = 0x00;
	}
}

void OLED_DrawPixel(uint8_t x, uint8_t y, uint8_t color)
{
	if (x >= 128 || y >= 64) return;
	uint16_t index = x + (y / 8) * 128;
	uint8_t bit = 1 << (y % 8);
	if (color)
	{
		OLED_Buffer[index] |= bit;
	}
	else
	{
		OLED_Buffer[index] &= ~bit;
	}
}

void OLED_DrawFilledCircle(int16_t cx, int16_t cy, uint8_t r)
{
	for (int16_t dx = -r; dx <= r; dx++)
	{
		for (int16_t dy = -r; dy <= r; dy++)
		{
			if ((dx*dx + dy*dy) <= ((int16_t)r * r))
			{
				int16_t px = cx + dx;
				int16_t py = cy + dy;
				if (px >= 0 && px < 128 && py >= 0 && py < 64)
				{
					OLED_DrawPixel((uint8_t)px, (uint8_t)py, 1);
				}
			}
		}
	}
}

/**
 * @brief 将缓冲区全部刷新到OLED屏幕
 */
void OLED_Update(void)
{
	for (uint8_t page = 0; page < 8; page++)
	{
		OLED_SetCursor(page, 0);
		for (uint16_t x = 0; x < 128; x++)
		{
			OLED_WriteData(OLED_Buffer[x + page * 128]);
		}
	}
}

/**
  * @brief  OLED初始化
  * @note 依赖 MyIIC_Init()，main调用OLED_Init内部会调用I2C初始化
  * @retval 无
  */
void OLED_Init(void)
{
	HAL_Delay(20);			//上电等待

	MyIIC_Init();			//软件I2C初始化 PB8‑SCL PB9‑SDA

	OLED_WriteCommand(0xAE);	//关闭显示

	OLED_WriteCommand(0xD5);	//设置显示时钟分频比/振荡器频率
	OLED_WriteCommand(0x80);

	OLED_WriteCommand(0xA8);	//设置多路复用率
	OLED_WriteCommand(0x3F);

	OLED_WriteCommand(0xD3);	//设置显示偏移
	OLED_WriteCommand(0x00);

	OLED_WriteCommand(0x40);	//设置显示开始行

	OLED_WriteCommand(0xA1);	//设置左右方向，0xA1正常 0xA0左右反置

	OLED_WriteCommand(0xC8);	//设置上下方向，0xC8正常 0xC0上下反置
	OLED_WriteCommand(0xDA);	//设置COM引脚硬件配置
	OLED_WriteCommand(0x12);

	OLED_WriteCommand(0x81);	//设置对比度控制
	OLED_WriteCommand(0xCF);
	OLED_WriteCommand(0xD9);	//设置预充电周期
	OLED_WriteCommand(0xF1);
	OLED_WriteCommand(0xDB);	//设置VCOMH取消选择级别
	OLED_WriteCommand(0x30);
	OLED_WriteCommand(0xA4);	//设置整个显示打开/关闭
	OLED_WriteCommand(0xA6);	//设置正常/倒转显示
	OLED_WriteCommand(0x8D);	//设置充电泵
	OLED_WriteCommand(0x14);
	OLED_WriteCommand(0xAF);	//开启显示

	OLED_Clear();				//OLED清屏
	OLED_ClearBuffer();
	OLED_Update();
}
