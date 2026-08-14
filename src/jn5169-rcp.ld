/****************************************************************************
 *
 * This software is owned by NXP B.V. and/or its supplier and is protected
 * under applicable copyright laws. All rights are reserved. We grant You,
 * and any third parties, a license to use this software solely and
 * exclusively on NXP products [NXP Microcontrollers such as JN5148, JN5142, JN5139]. 
 * You, and any third parties must reproduce the copyright and warranty notice
 * and any other legend of ownership on each copy or partial copy of the 
 * software.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * Copyright NXP B.V. 2012. All rights reserved
 *
 ***************************************************************************/

/* >>> AppBuildStart.ld */
/****************************************************************************
 *
 * This software is owned by NXP B.V. and/or its supplier and is protected
 * under applicable copyright laws. All rights are reserved. We grant You,
 * and any third parties, a license to use this software solely and
 * exclusively on NXP products [NXP Microcontrollers such as JN5148, JN5142, JN5139]. 
 * You, and any third parties must reproduce the copyright and warranty notice
 * and any other legend of ownership on each copy or partial copy of the 
 * software.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * Copyright NXP B.V. 2012. All rights reserved
 *
 ***************************************************************************/

/* >>> BootLoaderFunctions.ld */
exception_vsr_table = 0x00080038;
pSPIflashFncTable = 0x04000000;
/* <<< BootLoaderFunctions.ld */
/* >>> FixedLocationBootFunctions.ld */
restore_state = 0x00000044;
bZSPIWaitWIPClear = 0x0000004a;
u16ZSPIflashRDID = 0x00000050;
u8SelectFlashChipType = 0x00000056;
memcpy = 0x0000005c;
vBootFlashProg = 0x00000066;


/* <<< FixedLocationBootFunctions.ld */

EXTERN(AppWarmStart)
EXTERN(AppColdStart)

/* Set stack and minimum heap sizes unless they have already been set by makefiles */
__minimum_heap_size = DEFINED(__minimum_heap_size) ? __minimum_heap_size : 3350;
__stack_size = DEFINED(__stack_size) ? __stack_size : 2048;

_SwConfig =  (DEFINED(g_bSWConf_Debug)) | (DEFINED(g_bSWConf_AltDebugPort)<<1);

MEMORY
{
        flash  : ORIGIN = 0x00080000, LENGTH = 0x80000
        ram    : ORIGIN = 0x0400004c, LENGTH = 0x07fb4
}

/* Now building all code at once, so include vectors. MAC address is embedded
   in build for now */
SECTIONS
{
        .version ABSOLUTE(ORIGIN(flash)-4):
        {
        /*
        000b - Chip Type 5169 
        03   - 32K Ram
        0f   - 512K Flash
		    */
        LONG(0x0f03000b)
        } > flash 
				       
        .bir ABSOLUTE(ORIGIN(flash)):
        {
	      _flash_start = ABSOLUTE(.);
	      _flash_beg = ABSOLUTE(.);
        /* Magic number */
        LONG(0x12345678)
        LONG(0x11223344)
        LONG(0x55667788)
        
        /* Configuration A (32K, DRE, 16MHz) */
        BYTE(0x08)

        /* Configuration B (version 1) */
        BYTE(0x01)
        
        /* Application ID */
        SHORT(0x0000)
        } > flash
        
        .flashheader :
        {
        /* Encryption IV */
        LONG(0x00000000)
        LONG(0x00000000)
        LONG(0x00000000)
        SHORT(0x0000)

        /*SW Config*/
        /*
        Bit
        0    Debug En (1 Enable 0 Disable)
        1    0 - JTAG Debug on DIO4-7
             1 - JTAG Debug on DIO12-15
        2-15 Unused
		    */
        SHORT(ABSOLUTE(_SwConfig))

        _enc_start = ABSOLUTE(.);
        /*Flash Image Length*/
        LONG(ABSOLUTE(_image_len))

        /* Data start & length */
        LONG(ABSOLUTE(_flash_data_start))
        SHORT((ABSOLUTE(_ram_data_start) / 4) & 0xffff)
        SHORT((ABSOLUTE(_ram_data_len)   / 4) & 0xffff)

        /* BSS start & length */
        SHORT((ABSOLUTE(_bss_start) / 4) & 0xffff)
        SHORT((ABSOLUTE(_bss_len)   / 4) & 0xffff)

        /* Entry points */
        LONG(ABSOLUTE(AppWarmStart))
        LONG(ABSOLUTE(AppColdStart))
        } > flash
        _enc_offset = (_enc_start & 0x0000000F);

}
/* <<< AppBuildStart.ld */
/* >>> AppBuildExceptionHandlers.ld */
/****************************************************************************
 *
 * This software is owned by NXP B.V. and/or its supplier and is protected
 * under applicable copyright laws. All rights are reserved. We grant You,
 * and any third parties, a license to use this software solely and
 * exclusively on NXP products [NXP Microcontrollers such as JN5148, JN5142, JN5139]. 
 * You, and any third parties must reproduce the copyright and warranty notice
 * and any other legend of ownership on each copy or partial copy of the 
 * software.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * Copyright NXP B.V. 2012. All rights reserved
 *
 ***************************************************************************/

/* Now building all code at once, so include vectors. MAC address is embedded
   in build for now */


EXTERN(vException_BusError)
EXTERN(vException_UnalignedAccess)
EXTERN(vException_IllegalInstruction)
EXTERN(vException_SysCall)
EXTERN(vException_Trap)
EXTERN(vException_StackOverflow)
EXTERN(_exception_external_interrupt)
PROVIDE(vException_BusError           = restore_state);
PROVIDE(vException_UnalignedAccess    = restore_state);
PROVIDE(vException_IllegalInstruction = restore_state);
PROVIDE(vException_SysCall            = restore_state);
PROVIDE(vException_Trap               = restore_state);
PROVIDE(vException_StackOverflow      = restore_state);

SECTIONS
{

        . = ABSOLUTE(exception_vsr_table);
        
        .vsr_table :
        {
        LONG(ABSOLUTE(vException_BusError))
        LONG(ABSOLUTE(vException_UnalignedAccess))
        LONG(ABSOLUTE(vException_IllegalInstruction))
        LONG(ABSOLUTE(vException_SysCall))
        LONG(ABSOLUTE(vException_Trap))
        LONG(ABSOLUTE(vException_StackOverflow))
        } > flash

        .vsr_handlers :
        {
            KEEP(*(.vsr_handlers))
        } > flash

}
/* <<< AppBuildExceptionHandlers.ld */
/* >>> AppBuildEnd.ld */
/****************************************************************************
 *
 * This software is owned by NXP B.V. and/or its supplier and is protected
 * under applicable copyright laws. All rights are reserved. We grant You,
 * and any third parties, a license to use this software solely and
 * exclusively on NXP products [NXP Microcontrollers such as JN5148, JN5142, JN5139].
 * You, and any third parties must reproduce the copyright and warranty notice
 * and any other legend of ownership on each copy or partial copy of the
 * software.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * Copyright NXP B.V. 2012. All rights reserved
 *
 ***************************************************************************/

EXTERN(u32HeapStart);

/* Now building all code at once, so include vectors. MAC address is embedded
   in build for now */
SECTIONS
{

        .rodata :
        {
            *(.rodata*)
        } > flash

        .text :
        {
            *(.text*)
        } > flash

        _flash_end = ABSOLUTE(.);

        . = ORIGIN(ram);

        .data ALIGN(0x04) : AT ((LOADADDR(.text) + SIZEOF(.text) + 3) & ~ 3)
        {
            _ram_data_start = ABSOLUTE(.);
            *(.data*)
            *(.data.*)
            . = ALIGN (4);
            _ram_data_end = ABSOLUTE(.);
        } > ram

        .bss ALIGN (0x04) :
        {
            _bss_start = ABSOLUTE(.);
            *(.bss*)
            *(.bss.*)
            *(.scommon*)
            *(.scommon.*)
            *(COMMON)
            . = ALIGN (4);
            _bss_end = ABSOLUTE(.);
        } > ram

        /* reserve minimum heap size */
        .heap ALIGN (0x4) :
        {
            _heap_start = ABSOLUTE(.);
            . += __minimum_heap_size;
        } > ram

        .stack ABSOLUTE (ORIGIN(ram) + LENGTH(ram) - __stack_size):
        {
            /* make end of heap the bottom of the stack */
            _heap_end = ABSOLUTE(.);
            _stack_low_water_mark = ABSOLUTE(.);
            . += __stack_size;
        } > ram

        /* Test to check if the .data initialisation values in flash has gone
           past the end of the available space; the linker doesn't throw an
           error if that has happened. The following forces a byte to be
           stored after the .data initialisation values, but it is marked as
           NOLOAD so it never gets into the binary */
        .test (((LOADADDR(.text) + SIZEOF(.text) + 3) & ~ 3) + SIZEOF(.data)) (NOLOAD):
        {
            BYTE(0xff)
        } > flash
}

_flash_data_start = LOADADDR(.data);
_ram_data_len = (_ram_data_end - _ram_data_start + 3) & ~ 3;
_bss_len      = (_bss_end      - _bss_start      + 3) & ~ 3;
_image_len    = (_flash_end - _flash_start + _ram_data_len + 3) & ~ 3;
_ram_top      = ABSOLUTE(ORIGIN(ram)) + LENGTH(ram);
ram_top      = ABSOLUTE(ORIGIN(ram)) + LENGTH(ram);

_app_ram_start = 0x01001580; /* Page 5, word 8 (16-byte words, 16 words per page) */
PROVIDE(_heap_location  = u32HeapStart);
PROVIDE(heap_location  = u32HeapStart);

ENTRY(AppColdStart);

/* <<< AppBuildEnd.ld */

/* Для newlib (basim glue.o): конец кучи и вершина стека */
PROVIDE(end    = _heap_start);
PROVIDE(_stack = ORIGIN(ram) + LENGTH(ram));
