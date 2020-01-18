#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include "fpioa.h"
#include "lcd.h"
#include "plic.h"
#include "sysctl.h"
#include "uarths.h"
#include "nt35310.h"
#include "utils.h"
#include "kpu.h"
#include "ultra_face.h"
#define INCBIN_STYLE INCBIN_STYLE_SNAKE
#define INCBIN_PREFIX
#include "incbin.h"

#define PLL0_OUTPUT_FREQ 832000000UL
#define PLL1_OUTPUT_FREQ 400000000UL
#define PLL2_OUTPUT_FREQ 45158400UL


extern const unsigned char gImage_image[] __attribute__((aligned(128)));

INCBIN(model, "slim-320.kmodel");
kpu_model_context_t task;

volatile uint8_t g_ai_done_flag;
static uint16_t lcd_gram[320 * 240] __attribute__((aligned(32)));


uint64_t get_time(void)
{
    uint64_t v_cycle = read_cycle();
    return v_cycle * 1000000 / sysctl_clock_get_freq(SYSCTL_CLOCK_CPU);
}
static int ai_done(void *ctx)
{
    g_ai_done_flag = 1;
    return 0;
}
static void io_mux_init(void)
{
    //camera
/*
    fpioa_set_function(47, FUNC_CMOS_PCLK);
    fpioa_set_function(46, FUNC_CMOS_XCLK);
    fpioa_set_function(45, FUNC_CMOS_HREF);
    fpioa_set_function(44, FUNC_CMOS_PWDN);
    fpioa_set_function(43, FUNC_CMOS_VSYNC);
    fpioa_set_function(42, FUNC_CMOS_RST);

    fpioa_set_function(41, FUNC_SCCB_SCLK);
    fpioa_set_function(40, FUNC_SCCB_SDA);
*/
    /* Init SPI IO map and function settings */
    fpioa_set_function(38, FUNC_GPIOHS0 + DCX_GPIONUM);
    fpioa_set_function(36, FUNC_SPI0_SS3);
    fpioa_set_function(39, FUNC_SPI0_SCLK);
    fpioa_set_function(37, FUNC_GPIOHS0 + RST_GPIONUM);

    sysctl_set_spi0_dvp_data(1);
}

static void io_set_power(void)
{
    /* Set dvp and spi pin to 1.8V */
    sysctl_set_power_mode(SYSCTL_POWER_BANK6, SYSCTL_POWER_V18);
    sysctl_set_power_mode(SYSCTL_POWER_BANK7, SYSCTL_POWER_V18);
}

static void drawboxes(uint32_t x1, uint32_t y1, uint32_t x2, uint32_t y2, uint32_t class, float prob)
{
    if (x1 >= 320)
        x1 = 319;
    if (x2 >= 320)
        x2 = 319;
    if (y1 >= 240)
        y1 = 239;
    if (y2 >= 240)
        y2 = 239;

    lcd_draw_rectangle(x1, y1, x2, y2, 2, WHITE);
}

void rgb888_to_lcd(uint8_t *src, uint16_t *dest, size_t width, size_t height)
{
    size_t i, chn_size = width * height;
    for (size_t i = 0; i < width * height; i++)
    {
        uint8_t r = src[i];
        uint8_t g = src[chn_size + i];
        uint8_t b = src[chn_size * 2 + i];

        uint16_t rgb = ((r & 0b11111000) << 8) | ((g & 0b11111100) << 3) | (b >> 3);
        size_t d_i = i % 2 ? (i - 1) : (i + 1);
        dest[d_i] = rgb;
    }
}

int main(void)
{
    /* Set CPU and dvp clk */
    sysctl_pll_set_freq(SYSCTL_PLL0, PLL0_OUTPUT_FREQ);
    sysctl_pll_set_freq(SYSCTL_PLL1, PLL1_OUTPUT_FREQ);
    sysctl_pll_set_freq(SYSCTL_PLL2, PLL2_OUTPUT_FREQ);
    uarths_init();
    
    io_mux_init();
    io_set_power();
    plic_init();

    /* LCD init */
    printf("LCD init\n");
    lcd_init();
    lcd_set_direction(DIR_YX_RLDU);
    lcd_clear(BLACK);

    
    
   
    /* enable global interrupt */
    sysctl_enable_irq();

    /* system start */
    printf("system started\n");
    printf("CPU working at : %d \n", sysctl_clock_get_freq(SYSCTL_CLOCK_CPU));

   
    // init kpu 
    if (kpu_load_kmodel(&task, model_data) != 0)
    {
        printf("\nmodel init error\n");
        while (1);
    }

    ultra_face_init(320, 240, 0.3, 0.05, -1);
while(1)
    {
        g_ai_done_flag = 0;
        //start to calculate 
        uint64_t start = get_time ();
        kpu_run_kmodel(&task, gImage_image, DMAC_CHANNEL5, ai_done, NULL);
 
        while(!g_ai_done_flag);
        uint64_t stop = get_time ();
        printf (" Inference time %ld \n" , (long)(stop - start) );
        start = get_time();
        stop = get_time();
        printf (" Elapsed time %ld \n" , (long)(stop - start) );
        float *boxes;
        float *scores;
        size_t output_size;
        kpu_get_output(&task, 1, &boxes, &output_size);
        kpu_get_output(&task, 0, &scores, &output_size);

        //display pic
        rgb888_to_lcd(gImage_image, lcd_gram, 320, 240);
        start = get_time();
        lcd_draw_picture(0, 0, 320, 240, lcd_gram);
        stop = get_time();
        printf (" Time to display the image on screen  %ld \n" , (long)(stop - start) );

        // draw boxs 
        start = get_time();
        ultra_face_detect(scores, boxes, drawboxes);
        stop = get_time();
        printf("Coming out from ultra_face_Detect");
       // printf (" Time to draw boxes on screen  %ld \n" , (long)(stop - start) );

    } 
    
}
