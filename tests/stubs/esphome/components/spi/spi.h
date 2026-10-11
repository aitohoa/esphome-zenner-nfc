// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <vector>
#include <string>
#include <deque>
#include <cassert>
#include <cstdio>
#define SPI2_HOST 1
namespace test_bus {
inline uint32_t now=0, ready_at=0;
inline bool busy=false, miss_high=false, nss_high=true;
inline unsigned frame=0, stall_frame=0, writes=0, transfers=0, resets=0;
inline uint8_t command=0, reg=0, ack=0xA;
inline bool rx_present=true;
inline uint32_t rx_status=(4u<<13)|1;
struct Reply { std::vector<uint8_t> command, data; uint32_t status; bool present=true; };
inline std::deque<Reply> replies;
inline std::vector<uint8_t> rx_data;
inline std::vector<std::vector<uint8_t>> rf_commands;
inline bool scripted=false;
inline uint8_t eeprom[256]{};
inline unsigned eeprom_writes=0;
inline bool ignore_eeprom_write=false, fail_eeprom_write=false, fail_dpc_read=false, corrupt_on_reset=false;
inline std::vector<uint8_t> eeprom_reads;
inline std::vector<uint32_t> rf_on_times, rf_off_times;
inline std::vector<std::string> logs;
inline void clean() {
 now=ready_at=frame=stall_frame=writes=transfers=resets=0;
 nss_high=true; busy=miss_high=false; command=reg=0; ack=0xA;rx_present=true;logs.clear();
 rx_status=(4u<<13)|1;replies.clear();rx_data.clear();rf_commands.clear();scripted=false;rf_on_times.clear();rf_off_times.clear();
 memset(eeprom,0,sizeof(eeprom));eeprom[0x11]=4;eeprom[0x12]=1;eeprom[0x13]=4;eeprom[0x15]=4;
 ignore_eeprom_write=fail_eeprom_write=fail_dpc_read=corrupt_on_reset=false;
 eeprom[0x59]=0x13;eeprom[0x5A]=0x20;eeprom[0x5B]=0x4E;eeprom_writes=0;eeprom_reads.clear();
}
}
namespace esphome {
inline uint32_t millis(){ return test_bus::now; }
inline void delay(unsigned ms){ test_bus::now+=ms; }
namespace gpio { constexpr int FLAG_OUTPUT=1, FLAG_INPUT=2; }
class GPIOPin {
 public:
 enum Kind { OTHER, NSS, BUSY, RST };
 Kind kind;bool value=true;
 explicit GPIOPin(Kind k=OTHER):kind(k){}
 void setup(){} void pin_mode(int){}
 void digital_write(bool v){
  value=v;
  if(kind==NSS)test_bus::nss_high=v;
  if(kind==NSS && v && test_bus::frame!=test_bus::stall_frame) test_bus::busy=false;
  if(kind==RST && !v){ test_bus::busy=false;test_bus::stall_frame=0;test_bus::resets++;
   if(test_bus::corrupt_on_reset && test_bus::eeprom_writes) test_bus::eeprom[0x59]^=1;
  }
 }
 bool digital_read(){
  if(kind==BUSY){
   if(test_bus::ready_at && test_bus::now>=test_bus::ready_at){test_bus::busy=false;test_bus::ready_at=0;}
   return test_bus::busy;
  }
  return value;
 }
};
namespace spi {
constexpr int BIT_ORDER_MSB_FIRST=0,CLOCK_POLARITY_LOW=0,CLOCK_PHASE_LEADING=0,DATA_RATE_5MHZ=5000000,DATA_RATE_2MHZ=2000000,MODE0=0;
class SPIComponent { public:
 inline static SPIComponent *instance=nullptr;
 SPIComponent(){instance=this;}
 void set_clk(GPIOPin*){} void set_miso(GPIOPin*){} void set_mosi(GPIOPin*){}
 void set_interface(int){} void set_interface_name(const char*){} void setup(){} void dump_config(){}
};
template<int,int,int,int> class SPIDevice { public:
 inline static SPIDevice *instance=nullptr;
 SPIDevice(){instance=this;}
 bool enabled=false;
 void set_bit_order(int){} void set_spi_parent(SPIComponent*){} void set_data_rate(int){} void set_mode(int){}
 void spi_setup(){} void spi_teardown(){} void enable(){assert(!enabled);enabled=true;} void disable(){assert(enabled);enabled=false;}
 void write_array(uint8_t* p,size_t n){
  assert(enabled && !test_bus::nss_high);
  test_bus::command=p[0];test_bus::reg=n>1?p[1]:0;
  if(p[0]==6){
   assert(n==3 && p[1]==0x59);test_bus::eeprom_writes++;
   if(!test_bus::ignore_eeprom_write)test_bus::eeprom[p[1]]=p[2];
  }
  if(p[0]==7)test_bus::eeprom_reads.push_back(p[1]);
  if(p[0]==0x16)test_bus::rf_on_times.push_back(test_bus::now);
  if(p[0]==0x17)test_bus::rf_off_times.push_back(test_bus::now);
  if(p[0]==9){
   std::vector<uint8_t> rf(p+2,p+n);
   test_bus::rf_commands.push_back(rf);
   if(test_bus::scripted){
    assert(!test_bus::replies.empty());
    auto reply=test_bus::replies.front();test_bus::replies.pop_front();
    if(reply.command!=rf){ fprintf(stderr,"RF mismatch at %u ms: got %02X expected %02X\n",test_bus::now,rf[0],reply.command[0]); }
    assert(reply.command==rf);
    test_bus::rx_data=reply.data;test_bus::rx_status=reply.status;test_bus::rx_present=reply.present;
   }
  }
  test_bus::writes++;test_bus::frame++;test_bus::busy=!test_bus::miss_high;
  if((p[0]==6 && test_bus::fail_eeprom_write) ||
     (p[0]==7 && p[1]==0x59 && test_bus::fail_dpc_read)) test_bus::busy=false;
 }
 void transfer_array(uint8_t* p,size_t n){
  assert(enabled && !test_bus::nss_high);
  test_bus::transfers++;test_bus::frame++;test_bus::busy=!test_bus::miss_high;
  uint32_t value=0;
  if(test_bus::command==4){
   if(test_bus::reg==2)value=0x304|(test_bus::rx_present?1:0); // IDLE + optional RX IRQ
   if(test_bus::reg==0x1d)value=1u<<24; // WaitTransmit
   if(test_bus::reg==0x13)value=test_bus::rx_status;
  } else if(test_bus::command==0x0a)value=test_bus::ack;
  memset(p,0,n);
  if(test_bus::command==7){
   assert(test_bus::reg+n<=256);memcpy(p,test_bus::eeprom+test_bus::reg,n);
  }else if(test_bus::command==0x0a && test_bus::scripted){
   assert(n<=test_bus::rx_data.size());memcpy(p,test_bus::rx_data.data(),n);
  }else{for(size_t i=0;i<n && i<4;i++)p[i]=uint8_t(value>>(8*i));}
 }
};
}
}
