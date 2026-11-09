#ifndef periLib_H
#define periLib_H

#include <stdint.h>

// memory page size (4k)
#define PAGE_SIZE 0x1000
// peripheral physical memory address
#define BASE_PHYS 0x3F000000 // RPi 3
//#define BASE_PHYS 0xFE000000 // RPi 4
// peripheral bus address
#define BASE_BUS  0x7E000000

//-----GPIO-----
#define GPIO_OFFSET 0x200000
#define GPIO_BUS(r) (BASE_BUS + GPIO_OFFSET + (uint32_t)(r))
// GPIO register number (r) 
#define GPIO_REG(r) (*((volatile uint32_t *)gpio + (r)))
// set, clear and read GPIO pin (g)
#define GPIO_SET(g) (GPIO_REG(7 + (g) /32) = 1 << ((g) %32))
#define GPIO_CLR(g) (GPIO_REG(10 + (g) /32) = 1 << ((g) %32))
#define GPIO_LEV(g) (GPIO_REG(13 + (g) /32) & 1 << ((g) %32))
// set function of GPIO pin (g)
#define GPIO_FSEL(g, m) {FSEL_REG(g) &= ~(7 << FSEL_SHIFT(g)); FSEL_REG(g) |= (m) << FSEL_SHIFT(g);} 
#define FSEL_REG(g)   (GPIO_REG((g) /10))
#define FSEL_SHIFT(g) (((g) %10) *3)
// functions
#define GPIO_IN   0
#define GPIO_OUT  1
#define GPIO_ALT0 4
// GPIO registers
#define GPIO_GPSET0 0x1C
#define GPIO_GPCLR0 0x28
// pointer to GPIO in virtual memory
void *gpio;

//-----SPI0-----
#define SPI0_OFFSET 0x204000
#define SPI0_BUS(r) (BASE_BUS + SPI0_OFFSET + (uint32_t)(r)*4)
// SPI0 register number (r)
#define SPI0_REG(r) (*((volatile uint32_t *)spi0 + (r)))
// SPI0 registers
#define SPI0_CS   0
#define SPI0_FIFO 1
#define SPI0_CLK  2
#define SPI0_DLEN 3
#define SPI0_LTOH 4
#define SPI0_DC   5
// CS register fields
#define SPI0_CS_RXD    1<<17 // rx fifo contains data
#define SPI0_CS_DONE   1<<16 // 1: transfer complete
#define SPI0_CS_TA     1<<7  // transfer active
#define SPI0_CS_CLR_RX 1<<5  // clear rx fifo
#define SPI0_CS_CLR_TX 1<<4  // clear tx fifo
// macros
#define SPI0_CLR_FIFO {SPI0_REG(SPI0_CS) |= SPI0_CS_CLR_RX | SPI0_CS_CLR_TX;}
#define SPI0_SET_TA   {SPI0_REG(SPI0_CS) |= SPI0_CS_TA;}
#define SPI0_CLR_TA   {SPI0_REG(SPI0_CS) &= ~SPI0_CS_TA;}
#define SPI0_RXD      (SPI0_REG(SPI0_CS) & SPI0_CS_RXD)  // rx fifo contains data
#define SPI0_DONE     (SPI0_REG(SPI0_CS) & SPI0_CS_DONE) // transfer complete
// pointer to SPI0 in virtual memory
void *spi0;

//-----PCM-----
#define PCM_OFFSET 0x203000
#define PCM_BUS(r) (BASE_BUS + PCM_OFFSET + (uint32_t)(r)*4)
// PCM register number (r)
#define PCM_REG(r) (*((volatile uint32_t *)pcm + (r)))
// PCM registers
#define PCM_CS   0
#define PCM_FIFO 1
#define PCM_MODE 2
#define PCM_RXC  3
#define PCM_TXC  4
#define PCM_DREQ 5
// CS register fields
#define PCM_CS_STBY    1<<25 // ram standby
#define PCM_CS_SYNC    1<<24 // pcm clock sync helper (echos written value back after 2 clock cycles)
#define PCM_CS_RXSEX   1<<23 // rx sign extend
#define PCM_CS_RXD     1<<20 // rx fifo contains data
#define PCM_CS_RXR     1<<18 // rx fifo needs reading
#define PCM_CS_RXERR   1<<16 // rx fifo error
#define PCM_CS_RXSYNC  1<<14 // rx fifo sync
#define PCM_CS_DMAEN    1<<9 // dma dreq enable
#define PCM_CS_RXCLR    1<<4 // clear rx fifo (takes 2 clock cycles -> sync)
#define PCM_CS_RXON     1<<1 // enable rx
#define PCM_CS_EN       1<<0 // enable pcm interface
// Mode register fields
#define PCM_MODE_CLK_DIS 1<<28 // pcm clock disable (0 = enabled)
#define PCM_MODE_CLKM    1<<23 // clock mode (0 = master)
#define PCM_MODE_CLKI    1<<22 // clock invert (1 = inverted)
#define PCM_MODE_FSM     1<<21 // frame sync mode (0 = master)
// RXC register fields
#define PCM_RXC_CH1WEX   1<<31 // ch1 width extension
#define PCM_RXC_CH1EN    1<<30 // ch1 enable
#define PCM_RXC_CH2WEX   1<<15 // ch2 width extension
#define PCM_RXC_CH2EN    1<<14 // ch2 enable
// DREQ register fields
#define PCM_DREQ_RX_PANIC 16 // rx fifo panic level
#define PCM_DREQ_RX        0 // rx fifo dreq level
// macros for status bits
#define PCM_RXD  (PCM_REG(PCM_CS) & PCM_CS_RXD)  // rx fifo contains data
#define PCM_SYNC (PCM_REG(PCM_CS) & PCM_CS_SYNC) // sync helper
// pointer to PCM in virtual memory
void *pcm;

//-----DMA-----
#define DMA_OFFSET  0x007000
// DMA register number (r) on channel (c) (for channels 0-14)
#define DMA_REG(r, c) (*((volatile uint32_t *)dma + (c)*64 + (r)))
// DMA registers
#define DMA_CS        0
#define DMA_CONBLK_AD 1
#define DMA_TXFR_LEN  5 // read to get number of outstanding transfers
#define DMA_DEBUG     8
#define DMA_ENABLE    (*((volatile uint32_t *)dma + 1020))
// CS register fields
#define DMA_CS_RESET  1<<31 // reset dma channel
#define DMA_CS_END     1<<1 // clear end flag
#define DMA_CS_ACTIVE  1<<0 // activate dma
// TI register fields
#define DMA_TI_PERMAP      16 // peripheral number to pace dma transfer (offset)
#define DMA_TI_SRC_DREQ 1<<10 // dreq will gate source reads
#define DMA_TI_DEST_INC  1<<4 // increment destination address
#define DMA_TI_WAIT_RESP 1<<3 // wait for write response on axi bus

// macros for status bits
#define DMA_ACTIVE (DMA_REG(DMA_CS, DMA_CHANNEL) & DMA_CS_ACTIVE) // set while dma is running

// pointer to DMA in virtual memory
void *dma;
// memory for control blocks and buffers
void *dma_mem_virtual;
void *dma_mem_bus;
#define VIRT_TO_BUS(a) ((uint32_t)(a) - (uint32_t)dma_mem_virtual + (uint32_t)dma_mem_bus)
// DMA control block
typedef struct {
	uint32_t ti,
			source_ad,
			dest_ad,
			txfr_len,
			stride,
			nextconbk,
			reserved1,
			reserved2;
} CONBK __attribute__ ((aligned(32)));

//-----GPU MAILBOX-----
// video core mailbox file descriptor
int mailboxFd, memoryHandle;
// mailbox message
typedef struct {
	uint32_t size,        // length (bytes)
			request,      // request/response code
			tag,          // tag identifier
			bufferLength, // buffer length (bytes)
			tagRequest;   // bit 31: tag request/response(0/1)
	uint32_t data[27];    // data (max 108 bytes)
} MBX_MSG __attribute__ ((aligned(16)));

// memory allocation flags
#define MEM_FLAG_DIRECT 1<<2 // uncached
#define MEM_FLAG_ZERO   1<<4 // initialise buffer to zeros

// function declarations
int mapPeripherals();
void unmapPeripherals();
void *mapBusMem(void *, uint32_t);
void *mapBlock(int, uint32_t, uint32_t);

int openMailbox();
void closeMailbox();
uint32_t *vcMessageMailbox(MBX_MSG *, uint32_t);
uint32_t vcAllocateMemory(uint32_t);
void *vcLockMemory(int);
void vcUnlockMemory(int);
void vcReleaseMemory(int);

#endif