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

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define MAX30102_ADDR 0x57 /* 7-bit I2C 地址 */
#define FRAME_HEAD1 0xAA
#define FRAME_HEAD2 0x55
#define FRAME_TYPE_HR 0x01 /* 心率原始数据 */
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
I2C_HandleTypeDef hi2c1;

UART_HandleTypeDef huart1;

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_I2C1_Init(void);
static void MX_USART1_UART_Init(void);
/* USER CODE BEGIN PFP */
static void MAX30102_WriteReg(uint8_t reg, uint8_t val);
static uint8_t MAX30102_ReadReg(uint8_t reg);
static void MAX30102_Init(void);
static void Send_HeartFrame(uint32_t ir_raw, uint32_t red_raw);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
/*MAX30102 写寄存器 */
static void MAX30102_WriteReg(uint8_t reg, uint8_t val) {
  HAL_I2C_Mem_Write(&hi2c1, (MAX30102_ADDR << 1), reg, I2C_MEMADD_SIZE_8BIT,
                    &val, 1, 100);
}
/* MAX30102 读寄存器 */
static uint8_t MAX30102_ReadReg(uint8_t reg) {
  uint8_t val = 0;
  HAL_I2C_Mem_Read(&hi2c1, (MAX30102_ADDR << 1), reg, I2C_MEMADD_SIZE_8BIT,
                   &val, 1, 100);
  return val;
}
/* MAX30102 初始化序列（完全按手册寄存器表） */
static void MAX30102_Init(void) {
  /* 1. 软件复位 (Mode Config 0x09: RESET=1) */
  MAX30102_WriteReg(0x09, 0x40);
  HAL_Delay(50);
  /* 2. 清 FIFO 指针，确保从空 FIFO 开始 */
  MAX30102_WriteReg(0x04, 0x00); /* FIFO_WR_PTR */
  MAX30102_WriteReg(0x05, 0x00); /* OVF_COUNTER */
  MAX30102_WriteReg(0x06, 0x00); /* FIFO_RD_PTR */
  /* 3. FIFO Config (0x08):
     SMP_AVE[7:5]=000(不平均)
     FIFO_ROLLOVER_EN[4]=0
     FIFO_A_FULL[3:0]=0x0F */
  MAX30102_WriteReg(0x08, 0x0F);
  /* 4. SpO2 Config (0x0A):
     SPO2_ADC_RGE[6:5]=00(2048nA)
     SPO2_SR[4:2]=001(100sps)
     LED_PW[1:0]=01(118μs / 16-bit) */
  MAX30102_WriteReg(0x0A, 0x05);
  /* 5. LED 电流 ~6.2mA (0x1F)，先用小电流避免发烫 */
  MAX30102_WriteReg(0x0C, 0x1F); /* LED1 (Red) */
  MAX30102_WriteReg(0x0D, 0x1F); /* LED2 (IR)  */
  /* 6. 进入 SpO2 模式 (0x09: MODE[2:0]=011) */
  MAX30102_WriteReg(0x09, 0x03);
}
/* 组帧并通过 UART1 发送给 6ULL */
static void Send_HeartFrame(uint32_t ir_raw, uint32_t red_raw) {
  uint8_t frame[13];
  uint8_t crc = 0;
  frame[0] = FRAME_HEAD1;   /* 0xAA */
  frame[1] = FRAME_HEAD2;   /* 0x55 */
  frame[2] = FRAME_TYPE_HR; /* 0x01 */
  frame[3] = 0x08;          /* Payload 长度: IR(4B) + Red(4B) */
  /* IR: 大端 */
  frame[4] = (ir_raw >> 24) & 0xFF;
  frame[5] = (ir_raw >> 16) & 0xFF;
  frame[6] = (ir_raw >> 8) & 0xFF;
  frame[7] = (ir_raw >> 0) & 0xFF;
  /* Red: 大端 */
  frame[8] = (red_raw >> 24) & 0xFF;
  frame[9] = (red_raw >> 16) & 0xFF;
  frame[10] = (red_raw >> 8) & 0xFF;
  frame[11] = (red_raw >> 0) & 0xFF;
  /* CRC8 = 前12字节累加和取低8位 */
  for (int i = 0; i < 12; i++)
    crc += frame[i];
  frame[12] = crc;
  HAL_UART_Transmit(&huart1, frame, sizeof(frame), 100);
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_I2C1_Init();
  MX_USART1_UART_Init();
  /* USER CODE BEGIN 2 */
  MAX30102_Init();
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */
    //HAL_UART_Transmit(&huart1, (uint8_t *)"hello\r\n", 7, 100);

    uint8_t wr_ptr = MAX30102_ReadReg(0x04); /* FIFO_WR_PTR */
    uint8_t rd_ptr = MAX30102_ReadReg(0x06); /* FIFO_RD_PTR */
    /* FIFO 深度 32，指针 5-bit，用 &0x1F 处理自然回绕 */
    uint8_t num_samples = (wr_ptr - rd_ptr) & 0x1F;
    if (num_samples > 0) {
      uint8_t fifo_buf[32 * 6];
      /* 限制单次读取数量，防止数组越界 */
      if (num_samples > 32)
        num_samples = 32;
      /* Burst read FIFO_DATA (0x07): 读地址不递增，但内部 RD_PTR 自动推进 */
      HAL_I2C_Mem_Read(&hi2c1, (MAX30102_ADDR << 1), 0x07, I2C_MEMADD_SIZE_8BIT,
                       fifo_buf, num_samples * 6, 200);
      /* 逐样本解析：每样本 6 字节 = Red(3B) + IR(3B)，左对齐 18-bit */
      for (int i = 0; i < num_samples; i++) {
        uint8_t *p = &fifo_buf[i * 6];
        /* 提取 18-bit 原始值（MSB 在 bit17） */
        uint32_t red = ((uint32_t)p[0] << 10) | ((uint32_t)p[1] << 2) |
                       ((p[2] >> 6) & 0x03);
        uint32_t ir = ((uint32_t)p[3] << 10) | ((uint32_t)p[4] << 2) |
                      ((p[5] >> 6) & 0x03);
        Send_HeartFrame(ir, red);
      }
    }
    HAL_Delay(10); /* 100Hz 采样，10ms 轮询一次 */
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
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief I2C1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C1_Init(void)
{

  /* USER CODE BEGIN I2C1_Init 0 */

  /* USER CODE END I2C1_Init 0 */

  /* USER CODE BEGIN I2C1_Init 1 */

  /* USER CODE END I2C1_Init 1 */
  hi2c1.Instance = I2C1;
  hi2c1.Init.ClockSpeed = 100000;
  hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C1_Init 2 */

  /* USER CODE END I2C1_Init 2 */

}

/**
  * @brief USART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
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
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
