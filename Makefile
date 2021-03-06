

# Copyright (c) 2013 Andy Little 
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.

# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.

# You should have received a copy of the GNU General Public License
# along with this program. If not, see <http://www.gnu.org/licenses/>


############################################################
# **** you will need modify the paths in this section to the paths you saved the libraries in***


# the STM32F4 librraies are available from 
# http://www.st.com/st-web-ui/static/active/en/st_prod_software_internet/resource/technical/software/firmware/stm32f4_dsp_stdperiph_lib.zip
# Change this to the path where you saved the STM32F4 standard Peripheral libraries
STM32F4_INCLUDE_PATH = /opt/stm32f4/STM32F4xx_DSP_StdPeriph_Lib_V1.0.0/Libraries/

# The quan libraries are available from 
# https://github.com/kwikius/quan-trunk
# Change this to the the path twhere you saved the quan libraries
QUAN_INCLUDE_PATH = /home/andy/website/quan-trunk/

# The GCC ARM embedded toolchain (recommended) is available from
#  https://launchpad.net/gcc-arm-embedded
# If using this toolchain, the TOOLCHAIN_ID should be set to GCC_Arm_Embedded (The default)
TOOLCHAIN_ID = GCC_Arm_Embedded
# Otherwise if you are using the toolchain from
# https://github.com/prattmic/arm-cortex-m4-hardfloat-toolchain
# set the TOOLCHAIN_ID as follows
#TOOLCHAIN_ID = Michael_Pratt

# Change this to the path where you installed the  arm gcc compiler toolchain
TOOLCHAIN_PREFIX = /opt/gcc-arm-none-eabi-4_7-2013q2/

# Change this to your version of gcc. 
# (You can find the gcc version by invoking arm-noe-eabi-gcc --version in the $(TOOLCHAIN_PREFIX)/bin/ directory)
TOOLCHAIN_GCC_VERSION = 4.7.4

INIT_LIB_PREFIX = $(TOOLCHAIN_PREFIX)/lib/gcc/arm-none-eabi/$(TOOLCHAIN_GCC_VERSION)/armv7e-m/fpu/

CC      = $(TOOLCHAIN_PREFIX)bin/arm-none-eabi-g++
CC1     = $(TOOLCHAIN_PREFIX)bin/arm-none-eabi-gcc
LD      = $(TOOLCHAIN_PREFIX)bin/arm-none-eabi-g++
CP      = $(TOOLCHAIN_PREFIX)bin/arm-none-eabi-objcopy
OD      = $(TOOLCHAIN_PREFIX)bin/arm-none-eabi-objdump
SIZ     = $(TOOLCHAIN_PREFIX)bin/arm-none-eabi-size

STM32F4_LINKER_SCRIPT = stm32f4.ld

INCLUDES = -I$(STM32F4_INCLUDE_PATH)CMSIS/Include/ \
-I$(STM32F4_INCLUDE_PATH)CMSIS/Device/ST/STM32F4xx/Include/ \
-I$(STM32F4_INCLUDE_PATH)STM32F4xx_StdPeriph_Driver/inc/ \
-I$(QUAN_INCLUDE_PATH)

CFLAG_EXTRAS = -fno-math-errno -DQUAN_STM32F4 -DQUAN_NO_EXCEPTIONS -DSTM32F40_41xxx -DHSE_VALUE=8000000
CFLAG_EXTRAS += -Wl,-u,vsprintf -lm
CFLAG_EXTRAS += -DDEBUG
CFLAG_EXTRAS += -Wall
CFLAG_EXTRAS += -fmax-errors=1

PROCESSOR_FLAGS = -mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mthumb -mfloat-abi=hard

CFLAGS = -std=c++11 -fno-rtti -fno-exceptions -c -Os -g  $(INCLUDES) $(PROCESSOR_FLAGS) $(CFLAG_EXTRAS) \
-fdata-sections -ffunction-sections

INIT_LIBS = $(INIT_LIB_PREFIX)crti.o $(INIT_LIB_PREFIX)crtn.o

LFLAGS = -T$(STM32F4_LINKER_SCRIPT) -Os $(PROCESSOR_FLAGS) $(CFLAG_EXTRAS) $(INIT_LIBS) -nodefaultlibs -nostartfiles \
--specs=nano.specs -Wl,--gc-sections

CPFLAGS = -Obinary
ODFLAGS = -d

STARTUP = startup.s

local_objects = main.o eeprom_busy_wait.o serial_port.o eeprom_tx_irq.o \
eeprom_rx_irq.o i2c.o led.o setup.o spbrk.o system_init.o

objects  = $(local_objects) startup.o

all: test

Debug : test

clean:
	-rm -rf *.o *.elf *.bin *.lst

test: main.elf
	@ echo "...copying"
	$(CP) $(CPFLAGS) main.elf main.bin
	$(OD) $(ODFLAGS) main.elf > main.lst
	$(SIZ) -A main.elf

main.elf : $(objects) 
	@ echo "..linking"
	$(LD)   $(LFLAGS)  -o main.elf $(objects) 

$(local_objects): %.o : %.cpp
	$(CC) $(CFLAGS) $< -o $@

startup.o: $(STARTUP)
	$(CC) $(CFLAGS) -o startup.o $(STARTUP) 

upload : test
	st-flash write main.bin 0x8000000

upload_sp : test
	/home/andy/bin/stm32flash -b 115200 -f -v -w main.bin /dev/ttyUSB0

