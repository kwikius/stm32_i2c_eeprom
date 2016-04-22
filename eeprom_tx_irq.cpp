
#include <stm32f4xx.h>
#include <quan/stm32/i2c/typedefs.hpp>
#include <quan/stm32/i2c/detail/get_irq_number.hpp>
#include <quan/stm32/millis.hpp>
#include "serial_port.hpp"
#include "i2c.hpp"
#include "led.hpp"
#include <quan/conversion/itoa.hpp>

/*
   --- plug in arch per bus address ---
   * Acquire the bus and install plugin
   * do work
   * remove plugin and release bus
    Plugin part is the irq and dma functions

    add fun to release bus
    to use bus check busy first
    recored last write time to eeprom
    and use that to see if it is ok to read ( write takes 5 ms)
    

*/

namespace{

   void default_i2c3_dma_handler();

   void i2c_set_default_handlers();
   void i2c_set_event_handler( void(*pfn_event)());
   void i2c_set_error_handler( void(*pfn_event)());
   void i2c_set_dma_handler( void(*pfn_event)());

   bool eeprom_write_irq( uint16_t address, uint8_t const * data, uint32_t len);
   bool eeprom_read_irq( uint16_t address, uint8_t * data, uint32_t len);

   void panic(const char* str)
   {
      serial_port::write("PANIC : ");
      serial_port::write(str);
      serial_port::put('\n');
   }

   volatile bool bus_taken_token = false;

   // if bus not active then acquire some token representing the bus
//   bool get_i2c_bus()
//   {
//      if (bus_taken_token ){
//         return false;
//      }
//      if ( i2c::is_busy()){  // shouldnt get here
//         panic("i2c bus busy but bus token is free");
//      }
//      return bus_taken_token = true;
//   }
//
//   bool release_i2c_bus()
//   {
//      // check for bus not taken --> panic
//      // check for bus busy --> panic 
//      return bus_taken_token = false;
//   }

   // do an eeprom write
   // should work in irqs and dma only afap

   struct i2c_eeprom_writer{

      // return true if setup ok
      // else leave in good state and return false
      // wait till busy() return false
      static bool setup(uint8_t device_address,uint16_t data_address, uint8_t const* data, uint16_t len)
      {
         // check eeprom address < max
         // check data ptr not null
         // check len not 0
         // etc
         if (i2c::get_bus()){
            m_p_data = data;
            m_data_length = len;
            m_data_address[0] = static_cast<uint8_t>((data_address & 0xFF00) >> 8U);
            m_data_address[1] = static_cast<uint8_t>(data_address & 0xFF);
            m_device_address = device_address;
           
            setup_dma();
            i2c_set_error_handler(error_irq_handler);
            i2c_set_event_handler(sb1_irq_handler);
            // start the process..
            i2c::enable_event_interrupts(true);
            i2c::enable_dma_bit(true);
            i2c::set_start(true);
            return true;
         }else{
            panic("couldnt get bus");
            return false;
         }
         
      }
      // this alg may not be active but bus may still busy
      // so check for i2c_is_busy() too
   //   static bool active(){ return m_active_flag ;}

  private:

      // Start condition generated event . (EV5)
      // Clear by reading sr1 and
      // then writing dr with address of i2c device
      static void sb1_irq_handler()
      {
         i2c::get_sr1();
         i2c::send_address(m_device_address); 
         i2c_set_event_handler(dev_addr1_handler);
      }

      // device address sent event. EV6
      // Clear by reading SR1 followed by reading SR2.
      // send first bayte of data address
      static void dev_addr1_handler()
      {
         i2c::get_sr1();
         i2c::get_sr2();
         i2c::send_data(m_data_address[0]);
         i2c::enable_buffer_interrupts(true);
         i2c_set_event_handler(data_addr_lo_handler);
      }
      // txe on first data address
      // send second byte of data address
      static void data_addr_lo_handler()
      {
          i2c::send_data(m_data_address[1]);
          i2c_set_event_handler(dma_data_address_handler);
      }

      // send the data using dma
      static void dma_data_address_handler()
      {
          i2c::enable_event_interrupts(false);
          i2c::enable_buffer_interrupts(false);
          i2c::enable_dma_stream(false);
          i2c_set_dma_handler(dma_data_end_handler);
          i2c::set_dma_tx_buffer(m_p_data,m_data_length);
          i2c::clear_dma_stream_flags();
          i2c::enable_dma_stream(true);
      }

      // dma handler called when last byte of data sent
      static void dma_data_end_handler()
      {
         i2c::enable_dma_stream(false);
         i2c::enable_dma_bit(false);
         i2c::clear_dma_stream_tcif();
         i2c::enable_event_interrupts(true);
         i2c_set_event_handler(stop1_handler);  
      }

      // btf at end of last byte transfer
      static void stop1_handler()
      {
         i2c::enable_event_interrupts(false);
         i2c::set_stop(true);
         i2c_set_default_handlers();
         i2c::release_bus();
      }

      static void error_irq_handler()
      {
         panic ("i2c error");
      }

      static void setup_dma()
      {
         // i2c3 tx on DMA1_Stream4.CH3
         quan::stm32::rcc::get()->ahb1enr |= (1 << 21); // DMA stream 1
         for ( uint8_t i = 0; i < 20; ++i){
            asm volatile ("nop" : : :);
         }
         DMA_Stream_TypeDef* dma_stream = DMA1_Stream4;
         constexpr uint32_t  dma_channel = 3;
         constexpr uint32_t  dma_priority = 0b00; // low
         constexpr uint32_t  msize = 0b00; // 8 bit mem loc
         constexpr uint32_t  psize = 0b00; // 8 bit periph loc 
         dma_stream->CR = (dma_stream->CR & ~(0b111 << 25U)) | ( dma_channel << 25U); //(CHSEL) select channel
         dma_stream->CR = (dma_stream->CR & ~(0b11 << 16U)) | (dma_priority << 16U); // (PL) priority
         dma_stream->CR = (dma_stream->CR & ~(0b11 << 13U)) | (msize << 13U); // (MSIZE) 8 bit memory transfer
         dma_stream->CR = (dma_stream->CR & ~(0b11 << 11U)) | (psize << 11U); // (PSIZE) 16 bit transfer
         dma_stream->CR |= (1 << 10);// (MINC)
         dma_stream->CR &= ~(1 << 9);// (PINC)
         dma_stream->CR = (dma_stream->CR & ~(0b11 << 6U)) | (0b01 << 6U) ; // (DIR ) memory to peripheral
         dma_stream->CR |= ( 1 << 4) ; // (TCIE)
         dma_stream->PAR = (uint32_t)&I2C3->DR;  // periph addr
         NVIC_SetPriority(DMA1_Stream4_IRQn,15);  // low prio
         NVIC_EnableIRQ(DMA1_Stream4_IRQn);

//       to get stream running
//         dma_stream->M0AR = (uint32_t)x; // buffer address
//         dma_stream->NDTR = j;           // num data
//         DMA1->HIFCR |= (0b111101 << 0) ; // clear flags for Dma1 Stream 4
//         constexpr uint8_t cr2_dmaen = 11;
//         i2c_type::get()->cr2.bb_setbit<cr2_dmaen>(); set dma bit in i2c3
//         DMA1_Stream4->CR |= (1 << 0); // (EN)  enable DMA stream
      }
      
//      static void teardown()
//      {
//         //i2c_set_default_handlers();
//         m_active_flag = false;
//         
//      }

      static uint8_t const * m_p_data;
      static uint16_t m_data_length;
      static uint8_t  m_data_address[2]; // could do for dma in dma avail memmory
      static uint8_t  m_device_address;
     // static volatile bool   m_active_flag;
   };

   uint8_t const *   i2c_eeprom_writer::m_p_data = nullptr;
   uint16_t          i2c_eeprom_writer::m_data_length = 0U;
   uint8_t           i2c_eeprom_writer::m_data_address[] = {0U,0U};
   uint8_t           i2c_eeprom_writer::m_device_address = 0U;
  // volatile bool     i2c_eeprom_writer::m_active_flag = false;

} // ~namespace

/*
   test function
*/
char data_out[] = {"healthy"};  // the data to write n.b in dma available memory
bool eeprom_tx_irq_test()
{
    static constexpr uint8_t eeprom_addr = 0b10100000;
    i2c_eeprom_writer::setup( eeprom_addr ,5U,(uint8_t const*)data_out,8);

    auto now = quan::stm32::millis();
    typedef decltype(now) ms;
    while(i2c::is_busy() && ((quan::stm32::millis() - now) < ms{500U})){;}
 
    if (i2c::is_busy()){
         panic("looks like irq write hung");

         uint32_t const dma_flags = DMA1->HISR & 0x3F;

         char buffer [20];
         quan::itoasc(dma_flags,buffer,16);
         serial_port::write("dma flags = 0x");
         serial_port::write(buffer);
         serial_port::write("\n");


         uint32_t ndata = DMA1_Stream4->NDTR;
         quan::itoasc(ndata,buffer,10);
         serial_port::write("ndtr = ");
         serial_port::write(buffer);
         serial_port::write("\n");
         
         return false;
    }
   // i2c::enable_dma_stream(false);
    now = quan::stm32::millis();
    while( (quan::stm32::millis() - now) < ms{6U}){;}
    return true;
}

namespace {

   void default_i2c3_event_irq_handler()
   {
       panic("i2c event def called");
      // shouldnt be called
      // clear irq flags
      // print panic message
   }
   void default_i2c3_error_irq_handler()
   {
      panic("i2c error def called");
      // clear error flags
      // reset i2c3 interface
      // print panic message
   }

   void default_i2c3_dma_handler()
   {
       panic("i2c dma def called");
       // shouldnt be called
       // clear flags and print panic message
   }

   void (* volatile pfn_i2c3_event_irq_handler)() = default_i2c3_event_irq_handler;
   void (* volatile pfn_i2c3_error_irq_handler)() = default_i2c3_error_irq_handler;
   void (* volatile pfn_i2c3_dma_handler)() = default_i2c3_dma_handler;

   void i2c_set_dma_handler( void(*pfn_event)())
   {
      pfn_i2c3_dma_handler = pfn_event;
   }

   void i2c_set_event_handler( void(*pfn_event)())
   {
      pfn_i2c3_event_irq_handler = pfn_event;
   }

   void i2c_set_error_handler( void(*pfn_event)())
   {
      pfn_i2c3_error_irq_handler = pfn_event;
   }

   void i2c_set_default_handlers()
   {
      pfn_i2c3_event_irq_handler = default_i2c3_event_irq_handler;
      pfn_i2c3_error_irq_handler = default_i2c3_error_irq_handler;
      pfn_i2c3_dma_handler       = default_i2c3_dma_handler;
   }
}

extern "C" void DMA1_Stream4_IRQHandler() __attribute__ ( (interrupt ("IRQ")));

extern "C" void DMA1_Stream4_IRQHandler()
{
   pfn_i2c3_dma_handler();
}

extern "C" void I2C3_EV_IRQHandler() __attribute__ ((interrupt ("IRQ")));
extern "C" void I2C3_EV_IRQHandler()
{
   pfn_i2c3_event_irq_handler();
}

extern "C" void I2C3_ER_IRQHandler() __attribute__ ((interrupt ("IRQ")));
extern "C" void I2C3_ER_IRQHandler()
{
   pfn_i2c3_error_irq_handler();
}