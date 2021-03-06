/*
* This file is part of the stmbl project.
*
* Copyright (C) 2013-2015 Rene Hopf <renehopf@mac.com>
* Copyright (C) 2013-2015 Nico Stute <crinq@crinq.de>
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "stm32f4xx_conf.h"
#include "printf.h"
#include "scanf.h"
#include "hal.h"
#include "setup.h"
#include "eeprom.h"
#include "link.h"
#include <math.h>

#include "stm32_ub_usb_cdc.h"

volatile uint16_t rxbuf;
GLOBAL_HAL_PIN(g_dc_cur);
GLOBAL_HAL_PIN(g_dc_volt);
GLOBAL_HAL_PIN(g_hv_temp);
GLOBAL_HAL_PIN(g_iu);
GLOBAL_HAL_PIN(g_iv);
GLOBAL_HAL_PIN(g_iw);
GLOBAL_HAL_PIN(rt_time);

int __errno;
volatile double systime_s = 0.0;
void Wait(unsigned int ms);

//Mitsubishi absolute encoder position and buffer
volatile int menc_pos = -1;
volatile uint16_t menc_buf[10];

//5 kHz interrupt for hal. at this point all ADCs have been sampled,
//see setup_res() in setup.c if you are interested in the magic behind this.
void DMA2_Stream0_IRQHandler(void){
   DMA_ClearITPendingBit(DMA2_Stream0, DMA_IT_TCIF0);
   GPIO_SetBits(GPIOB,GPIO_Pin_8);
   int freq = 5000;
   float period = 1.0 / freq;
   //GPIO_ResetBits(GPIOB,GPIO_Pin_3);//messpin
   systime_s += period;

   unsigned int start = SysTick->VAL;

   for(int i = 0; i < hal.rt_func_count; i++){//run all realtime hal functions
      hal.rt[i](period);
   }
   unsigned int end = SysTick->VAL;
   if(start > end){
      PIN(rt_time) = ((float)(start - end)) / RCC_Clocks.HCLK_Frequency;
   }
   GPIO_ResetBits(GPIOB,GPIO_Pin_8);
}

//feedback port UART RX not empty. used for Mitsubishi absolute encoders.
void USART3_IRQHandler(){
   GPIO_ResetBits(GPIOB,GPIO_Pin_12);//reset tx enable
   uint16_t buf;
   if(USART_GetITStatus(USART3, USART_IT_RXNE)){
      USART_ClearITPendingBit(USART3, USART_IT_RXNE);
      USART_ClearFlag(USART3, USART_FLAG_RXNE);
      buf = USART3->DR;
      if(menc_pos >= 0 && menc_pos <= 8){
         menc_buf[menc_pos++] = buf;//append data to buffer
      }
   }
   if(USART_GetITStatus(USART3, USART_IT_TC)){
      USART_ClearITPendingBit(USART3, USART_IT_TC);
      USART_ClearFlag(USART3, USART_FLAG_TC);
      buf = USART3->DR;
   }
}

//UART RX not empty interrupt for f1. sets all the hal pins measured on the f1.
void UART_DRV_IRQ(){
   static int32_t datapos = -1;
   static from_hv_t from_hv;
   USART_ClearITPendingBit(UART_DRV, USART_IT_RXNE);
   USART_ClearFlag(UART_DRV, USART_FLAG_RXNE);
   rxbuf = UART_DRV->DR;

   if(rxbuf == 0x154){//start condition
      datapos = 0;
   }else if(datapos >= 0 && datapos < sizeof(from_hv_t)){
      ((uint8_t*)&from_hv)[datapos++] = (uint8_t)rxbuf;//append data to buffer
   }
   if(datapos == sizeof(from_hv_t)){//all data received
      datapos = -1;
      PIN(g_dc_cur) = TOFLOAT(from_hv.dc_cur);
      PIN(g_dc_volt) = TOFLOAT(from_hv.dc_volt);
      PIN(g_hv_temp) = TOFLOAT(from_hv.hv_temp);
#ifdef TROLLER
      PIN(g_iu) = TOFLOAT(from_hv.a);
      PIN(g_iv) = TOFLOAT(from_hv.b);
      PIN(g_iw) = TOFLOAT(from_hv.c);
#endif
   }
}

int main(void)
{
   float period = 0.0;
   float lasttime = 0.0;

   setup();

   #include "comps/adc.comp"
   #include "comps/fault.comp"
   #include "comps/enc_cmd.comp"
   //#include "comps/enc_fb.comp"

   #include "comps/en.comp"
   #include "comps/res.comp"
   //#include "comps/encm.comp"
   #include "comps/sim.comp"
   #include "comps/stp.comp"

   #include "comps/rev.comp"
   #include "comps/rev.comp"

   #include "comps/cauto.comp"

   #include "comps/pderiv.comp"
   #include "comps/pderiv.comp"

   #include "comps/pid.comp"

   #include "comps/rev.comp"

   //#include "comps/dq.comp"
   #include "comps/curpid.comp"
   #include "comps/pmsm.comp"
   //#include "comps/mot.comp"
   #include "comps/idq.comp"

   #include "comps/pwm2uart.comp"

   //#include "comps/absavg.comp"

   #include "comps/term.comp"
   #include "comps/led.comp"
   #include "comps/fan.comp"
   #include "comps/brake.comp"


   set_comp_type("net");
   HAL_PIN(enable) = 0.0;
   HAL_PIN(cmd) = 0.0;
   HAL_PIN(fb) = 0.0;
   HAL_PIN(fb_error) = 0.0;
   HAL_PIN(cmd_d) = 0.0;
   HAL_PIN(fb_d) = 0.0;
   HAL_PIN(dc_cur) = 0.0;
   HAL_PIN(ac_cur) = 0.0;
   HAL_PIN(dc_volt) = 0.0;
   HAL_PIN(hv_temp) = 0.0;
   HAL_PIN(core_temp0) = 0.0;
   HAL_PIN(core_temp1) = 0.0;
   HAL_PIN(motor_temp) = 0.0;
   HAL_PIN(iu) = 0.0;
   HAL_PIN(iv) = 0.0;
   HAL_PIN(iw) = 0.0;
   HAL_PIN(rt_calc_time) = 0.0;

   set_comp_type("conf");
   HAL_PIN(r) = 0.0;
   HAL_PIN(l) = 0.0;
   HAL_PIN(j) = 0.0;
   HAL_PIN(psi) = 0.0;
   HAL_PIN(polecount) = 0.0;
   HAL_PIN(fb_polecount) = 0.0;
   HAL_PIN(fb_offset) = 0.0;
   HAL_PIN(pos_p) = 0.0;
   HAL_PIN(vel_p) = 1.0;
   HAL_PIN(acc_p) = 0.0;
   HAL_PIN(acc_pi) = 0.0;
   HAL_PIN(cur_p) = 0.0;
   HAL_PIN(cur_i) = 0.0;
   HAL_PIN(cur_ff) = 1.0;
   HAL_PIN(cur_ind) = 0.0;
   HAL_PIN(max_vel) = 0.0;
   HAL_PIN(max_acc) = 0.0;
   HAL_PIN(max_force) = 0.0;
   HAL_PIN(max_dc_cur) = 5.0;
   HAL_PIN(max_ac_cur) = 0.0;
   HAL_PIN(fb_type) = 0.0;
   HAL_PIN(cmd_type) = 0.0;
   HAL_PIN(fb_rev) = 0.0;
   HAL_PIN(cmd_rev) = 0.0;
   HAL_PIN(out_rev) = 0.0;
   HAL_PIN(fb_res) = 1.0;
   HAL_PIN(cmd_res) = 2000.0;
   HAL_PIN(sin_offset) = 0.0;
   HAL_PIN(cos_offset) = 0.0;
   HAL_PIN(sin_gain) = 1.0;
   HAL_PIN(cos_gain) = 1.0;

   HAL_PIN(max_dc_volt) = 370.0;
   HAL_PIN(max_hv_temp) = 90.0;
   HAL_PIN(max_core_temp) = 55.0;
   HAL_PIN(max_motor_temp) = 100.0;
   HAL_PIN(max_pos_error) = M_PI / 2.0;
   HAL_PIN(high_dc_volt) = 350.0;
   HAL_PIN(low_dc_volt) = 12.0;
   HAL_PIN(high_hv_temp) = 70.0;
   HAL_PIN(high_motor_temp) = 80.0;
   HAL_PIN(fan_hv_temp) = 60.0;
   HAL_PIN(fan_core_temp) = 450.0;
   HAL_PIN(fan_motor_temp) = 60.0;
   HAL_PIN(autophase) = 1.0;
   HAL_PIN(max_sat) = 0.2;


   g_dc_cur_hal_pin = map_hal_pin("net0.dc_cur");
   g_dc_volt_hal_pin = map_hal_pin("net0.dc_volt");
   g_hv_temp_hal_pin = map_hal_pin("net0.hv_temp");
   g_iu_hal_pin = map_hal_pin("net0.iu");
   g_iv_hal_pin = map_hal_pin("net0.iv");
   g_iw_hal_pin = map_hal_pin("net0.iw");
   rt_time_hal_pin = map_hal_pin("net0.rt_calc_time");

   for(int i = 0; i < hal.init_func_count; i++){
      hal.init[i]();
   }

   link_pid();

   //set_bergerlahr();
   //set_mitsubishi();
   //set_festo();
   //set_manutec();
   //set_rexroth();
   //set_sanyo();
   //set_bosch1();
   //set_bosch1();
   set_hauser();
   //set_sanyo();
   //set_br();


   set_cmd_enc();
   //set_cmd_stp();
   //set_cmd_lcnc();
   //set_cur_cmd();


   TIM_Cmd(TIM8, ENABLE);//int

   UB_USB_CDC_Init();

   while(1)//run non realtime stuff
   {
      Wait(2);
      period = systime/1000.0 + (1.0 - SysTick->VAL/RCC_Clocks.HCLK_Frequency)/1000.0 - lasttime;
      lasttime = systime/1000.0 + (1.0 - SysTick->VAL/RCC_Clocks.HCLK_Frequency)/1000.0;
      for(int i = 0; i < hal.nrt_func_count; i++){//run all non realtime hal functions
         hal.nrt[i](period);
      }
   }
}

void Wait(unsigned int ms){
   volatile unsigned int t = systime + ms;
   while(t >= systime){
   }
}
