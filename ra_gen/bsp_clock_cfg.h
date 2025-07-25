/* generated configuration header file - do not edit */
#ifndef BSP_CLOCK_CFG_H_
#define BSP_CLOCK_CFG_H_
#define BSP_CFG_CLOCKS_SECURE (0)
#define BSP_CFG_CLOCKS_OVERRIDE (0)
#define BSP_CFG_XTAL_HZ (20000000) /* XTAL 20000000Hz */
#define BSP_CFG_HOCO_FREQUENCY (2) /* HOCO 20MHz */
#define BSP_CFG_PLL_SOURCE (BSP_CLOCKS_SOURCE_CLOCK_HOCO) /* PLL Src: HOCO */
#define BSP_CFG_PLL_DIV (BSP_CLOCKS_PLL_DIV_2) /* PLL Div /2 */
#define BSP_CFG_PLL_MUL BSP_CLOCKS_PLL_MUL(72,0) /* PLL Mul x60-79|Mul x72|PLL Mul x72.00 */
#define BSP_CFG_PLL_FREQUENCY_HZ (720000000) /* PLL 720000000Hz */
#define BSP_CFG_PLODIVP (BSP_CLOCKS_PLL_DIV_2) /* PLL1P Div /2 */
#define BSP_CFG_PLL1P_FREQUENCY_HZ (360000000) /* PLL1P 360000000Hz */
#define BSP_CFG_PLODIVQ (BSP_CLOCKS_PLL_DIV_2) /* PLL1Q Div /2 */
#define BSP_CFG_PLL1Q_FREQUENCY_HZ (360000000) /* PLL1Q 360000000Hz */
#define BSP_CFG_PLODIVR (BSP_CLOCKS_PLL_DIV_2) /* PLL1R Div /2 */
#define BSP_CFG_PLL1R_FREQUENCY_HZ (360000000) /* PLL1R 360000000Hz */
#define BSP_CFG_PLL2_SOURCE (BSP_CLOCKS_SOURCE_CLOCK_HOCO) /* PLL2 Src: HOCO */
#define BSP_CFG_PLL2_DIV (BSP_CLOCKS_PLL_DIV_2) /* PLL2 Div /2 */
#define BSP_CFG_PLL2_MUL BSP_CLOCKS_PLL_MUL(144,0) /* PLL2 Mul x140-159|Mul x144|PLL2 Mul x144.00 */
#define BSP_CFG_PLL2_FREQUENCY_HZ (1440000000) /* PLL2 1440000000Hz */
#define BSP_CFG_PL2ODIVP (BSP_CLOCKS_PLL_DIV_6) /* PLL2P Div /6 */
#define BSP_CFG_PLL2P_FREQUENCY_HZ (240000000) /* PLL2P 240000000Hz */
#define BSP_CFG_PL2ODIVQ (BSP_CLOCKS_PLL_DIV_4) /* PLL2Q Div /4 */
#define BSP_CFG_PLL2Q_FREQUENCY_HZ (360000000) /* PLL2Q 360000000Hz */
#define BSP_CFG_PL2ODIVR (BSP_CLOCKS_PLL_DIV_4) /* PLL2R Div /4 */
#define BSP_CFG_PLL2R_FREQUENCY_HZ (360000000) /* PLL2R 360000000Hz */
#define BSP_CFG_CLOCK_SOURCE (BSP_CLOCKS_SOURCE_CLOCK_PLL1P) /* Clock Src: PLL1P */
#define BSP_CFG_CLKOUT_SOURCE (BSP_CLOCKS_SOURCE_CLOCK_MOCO) /* CLKOUT Src: MOCO */
#define BSP_CFG_SCICLK_SOURCE (BSP_CLOCKS_SOURCE_CLOCK_HOCO) /* SCICLK Src: HOCO */
#define BSP_CFG_SPICLK_SOURCE (BSP_CLOCKS_SOURCE_CLOCK_HOCO) /* SPICLK Src: HOCO */
#define BSP_CFG_CANFDCLK_SOURCE (BSP_CLOCKS_CLOCK_DISABLED) /* CANFDCLK Disabled */
#define BSP_CFG_UCLK_SOURCE (BSP_CLOCKS_SOURCE_CLOCK_PLL2P) /* USBCLK Src: PLL2P */
#define BSP_CFG_OCTACLK_SOURCE (BSP_CLOCKS_SOURCE_CLOCK_PLL1P) /* OCTACLK Src: PLL1P */
#define BSP_CFG_CPUCLK_DIV (BSP_CLOCKS_SYS_CLOCK_DIV_1) /* CPUCLK Div /1 */
#define BSP_CFG_ICLK_DIV (BSP_CLOCKS_SYS_CLOCK_DIV_3) /* ICLK Div /3 */
#define BSP_CFG_PCLKA_DIV (BSP_CLOCKS_SYS_CLOCK_DIV_3) /* PCLKA Div /3 */
#define BSP_CFG_PCLKB_DIV (BSP_CLOCKS_SYS_CLOCK_DIV_6) /* PCLKB Div /6 */
#define BSP_CFG_PCLKC_DIV (BSP_CLOCKS_SYS_CLOCK_DIV_6) /* PCLKC Div /6 */
#define BSP_CFG_PCLKD_DIV (BSP_CLOCKS_SYS_CLOCK_DIV_3) /* PCLKD Div /3 */
#define BSP_CFG_PCLKE_DIV (BSP_CLOCKS_SYS_CLOCK_DIV_3) /* PCLKE Div /3 */
#define BSP_CFG_BCLK_DIV (BSP_CLOCKS_SYS_CLOCK_DIV_12) /* BCLK Div /12 */
#define BSP_CFG_FCLK_DIV (BSP_CLOCKS_SYS_CLOCK_DIV_6) /* FCLK Div /6 */
#define BSP_CFG_CLKOUT_DIV (BSP_CLOCKS_SYS_CLOCK_DIV_1) /* CLKOUT Div /1 */
#define BSP_CFG_SCICLK_DIV (BSP_CLOCKS_SCI_CLOCK_DIV_4) /* SCICLK Div /4 */
#define BSP_CFG_SPICLK_DIV (BSP_CLOCKS_SPI_CLOCK_DIV_4) /* SPICLK Div /4 */
#define BSP_CFG_CANFDCLK_DIV (BSP_CLOCKS_CANFD_CLOCK_DIV_8) /* CANFDCLK Div /8 */
#define BSP_CFG_UCLK_DIV (BSP_CLOCKS_USB_CLOCK_DIV_5) /* USBCLK Div /5 */
#define BSP_CFG_OCTACLK_DIV (BSP_CLOCKS_OCTA_CLOCK_DIV_8) /* OCTACLK Div /8 */
#endif /* BSP_CLOCK_CFG_H_ */
