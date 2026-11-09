#include "periLib.h"

#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cairo.h>
#include <cairo-xlib.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>

// DREQ peripheral mapping
#define PCM_RX_PERMAP 3
// DMA channel
#define DMA_CHANNEL 5
// image dimensions
#define IMG_WIDTH  800
#define IMG_HEIGHT 600
// number of frames
#define FRAME_NUM 6
// memory for cb and data buffer
uint32_t dma_mem_size;
// start signal GPIO pin
#define GPIO_PIN_START 27
// multiplexer select GPIO pins
#define GPIO_PIN_S0  5
#define GPIO_PIN_S1  6
#define GPIO_PIN_S2 13

// GUI window cairo surface
cairo_surface_t *windowSurface;
// needed to handle window close
Atom wmDeleteMessage;

// function declarations
void pcmExit(int);

void setupPCM();
void setupPCMPins();
void resetPCMPins();
void startPCM();
void stopPCM();

void setupDMA();
void startDMA(CONBK *cb);
void stopDMA();

cairo_surface_t *cairoCreateX11Surface0(int, int);
void cairoCloseX11Surface(cairo_surface_t *sfc);
int cairoCheckEvent(cairo_surface_t*);

int main(int argc, char **argv) {	
	// free memory and reset io on ctrl-c
	signal(SIGINT, pcmExit);
	
	// set some variables
	// memory needed for one image frame
	uint32_t frame_size = (IMG_WIDTH * IMG_HEIGHT) / 8;
	// memory needed for all frames + cb
	dma_mem_size = (((FRAME_NUM * frame_size) / PAGE_SIZE) + 1) * PAGE_SIZE;

	// map peripherals
	if (mapPeripherals()) {
		pcmExit(0);
	}
	// allocate uncached memory for dma control blocks and data
	if (openMailbox()) {
			pcmExit(0);
	}
	if ((memoryHandle = vcAllocateMemory(dma_mem_size)) <= 0 ||
		(dma_mem_bus = vcLockMemory(memoryHandle)) == 0 ||
		(dma_mem_virtual = mapBusMem(dma_mem_bus, dma_mem_size)) == 0) {
			printf("error allocating memory\n");
			pcmExit(0);
	}
	
	printf("HDMI input gui test\n");
	// set initial selection (011)
	GPIO_FSEL(GPIO_PIN_S0, GPIO_OUT);
	GPIO_FSEL(GPIO_PIN_S1, GPIO_OUT);
	GPIO_FSEL(GPIO_PIN_S2, GPIO_OUT);
	GPIO_SET(GPIO_PIN_S0);
	GPIO_SET(GPIO_PIN_S1);
	GPIO_CLR(GPIO_PIN_S2);

	// set memory to 0
	memset(dma_mem_virtual, 0, dma_mem_size);
	// create and fill control blocks
	CONBK *controlBlocks = dma_mem_virtual;
	uint32_t *gpioSource1 = (uint32_t *)(controlBlocks + 12);
	uint32_t *gpioSource2 = gpioSource1 + 1;
	uint32_t *gpioSource3 = gpioSource2 + 1;
	uint32_t *gpioSource4 = gpioSource3 + 1;
	uint32_t *inData = gpioSource4 + 1;
	// fill pcm control blocks
	for (int i = 0; i < FRAME_NUM * 2; i += 2) {
		// pcm rx dreq signal for source reads, increment destination address, wait for axi bus write response
		(controlBlocks+i)->ti = (PCM_RX_PERMAP << DMA_TI_PERMAP) | DMA_TI_SRC_DREQ | DMA_TI_DEST_INC | DMA_TI_WAIT_RESP;
		(controlBlocks+i)->source_ad = PCM_BUS(PCM_FIFO);
		(controlBlocks+i)->dest_ad = VIRT_TO_BUS(inData + ((frame_size / 4) * (i / 2)));
		(controlBlocks+i)->txfr_len = frame_size;
	}
	// fill gpio control blocks
	for (int i = 1; i < FRAME_NUM * 2; i += 2) {
		(controlBlocks+i)->txfr_len = 4;
	}
	// link control blocks
	for (int i = 0; i < (FRAME_NUM * 2) - 1; i++) {
		(controlBlocks+i)->nextconbk = VIRT_TO_BUS(controlBlocks+i+1);
	}
	// set gpio source
	*gpioSource1 = 1 << GPIO_PIN_S0;
	*gpioSource2 = 1 << GPIO_PIN_S1;
	*gpioSource3 = 1 << GPIO_PIN_S2;
	*gpioSource4 = 1 << GPIO_PIN_START;
	// sequence (s2,s1,s0): 011, 010, 110, 111, 101, 100
	(controlBlocks+1)->source_ad = VIRT_TO_BUS(gpioSource1);
	(controlBlocks+1)->dest_ad = GPIO_BUS(GPIO_GPCLR0);
	(controlBlocks+3)->source_ad = VIRT_TO_BUS(gpioSource3);
	(controlBlocks+3)->dest_ad = GPIO_BUS(GPIO_GPSET0);
	(controlBlocks+5)->source_ad = VIRT_TO_BUS(gpioSource1);
	(controlBlocks+5)->dest_ad = GPIO_BUS(GPIO_GPSET0);
	(controlBlocks+7)->source_ad = VIRT_TO_BUS(gpioSource2);
	(controlBlocks+7)->dest_ad = GPIO_BUS(GPIO_GPCLR0);
	(controlBlocks+9)->source_ad = VIRT_TO_BUS(gpioSource1);
	(controlBlocks+9)->dest_ad = GPIO_BUS(GPIO_GPCLR0);
	(controlBlocks+11)->source_ad = VIRT_TO_BUS(gpioSource4);
	(controlBlocks+11)->dest_ad = GPIO_BUS(GPIO_GPCLR0);

	// setup start GPIO, set HIGH (needed up here, clk must be present to finish setup)
	GPIO_FSEL(GPIO_PIN_START, GPIO_OUT);
	GPIO_SET(GPIO_PIN_START);

	printf("pcm setup\n");
	
	// set up PCM peripheral and pins
	setupPCMPins();
	setupPCM();
	// set up DMA peripheral
	setupDMA();
	
	// start DMA, PCM
	printf("Starting DMA transfer...\n");
	startDMA(controlBlocks);
	startPCM();

	// wait for transfer to finish
	while (DMA_ACTIVE) {
		usleep(5000ul);
	}

	// write data to screen
    printf("Data received.\nCreating display window...\n");
	
	// get window surface 
	windowSurface = cairoCreateX11Surface0(IMG_WIDTH, IMG_HEIGHT);
	// format image data
	unsigned char *data = malloc(4 * IMG_WIDTH * IMG_HEIGHT);
	
	int frameDataSize = frame_size / sizeof(uint32_t);
    int k;
    // green 6
	k = 1;
    int j = 0;
	for (int i = 0; i < frameDataSize; i++) {
		for (uint32_t mask = 0x80000000; mask > 0; mask >>= 1) { // read msb first
			data[k] = (mask & inData[j]) ? 85 : 0;
			k += 4;
		}
		j++;
	}
	// green 7
	k = 1;
	for (int i = 0; i < frameDataSize; i++) {
		for (uint32_t mask = 0x80000000; mask > 0; mask >>= 1) { // read msb first
			data[k] += (mask & inData[j]) ? 170 : 0;
			k += 4;
		}
		j++;
	}
	// red 6
	k = 2;
	for (int i = 0; i < frameDataSize; i++) {
		for (uint32_t mask = 0x80000000; mask > 0; mask >>= 1) { // read msb first
			data[k] = (mask & inData[j]) ? 85 : 0;
			k += 4;
		}
		j++;
	}
	// red 7
	k = 2;
	for (int i = 0; i < frameDataSize; i++) {
		for (uint32_t mask = 0x80000000; mask > 0; mask >>= 1) { // read msb first
			data[k] += (mask & inData[j]) ? 170 : 0;
			k += 4;
		}
		j++;
	}	
	// blue 7
	k = 0;
	for (int i = 0; i < frameDataSize; i++) {
		for (uint32_t mask = 0x80000000; mask > 0; mask >>= 1) { // read msb first
			data[k] = (mask & inData[j]) ? 170 : 0;
			k += 4;
		}
		j++;
	}
	// blue 6
	k = 0;
	for (int i = 0; i < frameDataSize; i++) {
		for (uint32_t mask = 0x80000000; mask > 0; mask >>= 1) { // read msb first
			data[k] += (mask & inData[j]) ? 85 : 0;
			k += 4;
		}
		j++;
	}
	// alpha
	k = 3;
	frameDataSize = IMG_WIDTH * IMG_HEIGHT;
	for (int i = 0; i < frameDataSize; i++) {
		data[k] = 255;
		k += 4;
	}
	
	// create image surface
	cairo_surface_t *imageSurface = cairo_image_surface_create_for_data(data, CAIRO_FORMAT_ARGB32, IMG_WIDTH, IMG_HEIGHT, IMG_WIDTH * 4);
	// paint to window
	cairo_t *cr = cairo_create(windowSurface);
	cairo_set_source_surface(cr, imageSurface, 0, 0);
	cairo_paint(cr);
	cairo_destroy(cr);
	cairo_surface_destroy(imageSurface);
	//free(data);
	
    printf("Image shown.\nPress s to save image. Press q to quit.\nWaiting for input...\n");
	// wait for termination
	while (1) {
		int key = cairoCheckEvent(windowSurface);
		if (key == 's') {
			printf("Saving image to out.png\n");
			cairo_surface_write_to_png(windowSurface, "out.png");
		}
		if (key == 'q') {
			break;
		}
	}
 
    free(data);
	// free memory and reset io on exit
	pcmExit(0);
}

//-----GUI-----
cairo_surface_t *cairoCreateX11Surface0(int x, int y) {
	Display *dsp;
	Drawable da;
	int screen;
	cairo_surface_t *sfc;

	if ((dsp = XOpenDisplay(NULL)) == NULL) {
		printf("Error opening display\n"); 
		exit(1);
	}
	screen = DefaultScreen(dsp);
	da = XCreateSimpleWindow(dsp, DefaultRootWindow(dsp), 0, 0, x, y, 0, 0, 0);
	XSelectInput(dsp, da, ButtonPressMask | KeyPressMask);
	wmDeleteMessage = XInternAtom(dsp, "WM_DELETE_WINDOW", False);
	XSetWMProtocols(dsp, da, &wmDeleteMessage, 1);
	XMapWindow(dsp, da);

	sfc = cairo_xlib_surface_create(dsp, da, DefaultVisual(dsp, screen), x, y);
	cairo_xlib_surface_set_size(sfc, x, y);

	return sfc;
}

void cairoCloseX11Surface(cairo_surface_t *sfc) {
	Display *dsp = cairo_xlib_surface_get_display(sfc);
	cairo_surface_destroy(sfc);
	XCloseDisplay(dsp);
}

int cairoCheckEvent(cairo_surface_t *sfc) {
	char keybuf[8];
	KeySym key;
	XEvent e;
	
	if (XPending(cairo_xlib_surface_get_display(sfc))) {
		XNextEvent(cairo_xlib_surface_get_display(sfc), &e);
	} else {
		return 0;
	}
	
	switch (e.type) {
		case ClientMessage:
			if (e.xclient.data.l[0] == wmDeleteMessage) {
				pcmExit(0);
			}
		case KeyPress:
			XLookupString(&e.xkey, keybuf, sizeof(keybuf), &key, NULL);
			//printf("Key: %s\n", keybuf);
			return key;
		default:
			return 0;
	}
}

//-----PCM-----
void setupPCM() {
	__sync_synchronize();
	// enable ram, enable pcm
	PCM_REG(PCM_CS) = PCM_CS_STBY | PCM_CS_EN;
	__sync_synchronize();
	// clk en, data not packed, clk slave, clk inverted, fs master, FLEN=63, FSLEN=0
	PCM_REG(PCM_MODE) = PCM_MODE_CLKM | PCM_MODE_CLKI | 0xFC00;
	// both channels enabled, 32bit/channel, ch2 at 0, ch1 at 32
	PCM_REG(PCM_RXC) = PCM_RXC_CH1WEX | PCM_RXC_CH1EN | 0x2000000 | 0x80000 | PCM_RXC_CH2WEX | PCM_RXC_CH2EN | 0x8;
	PCM_REG(PCM_TXC) = 0;
	__sync_synchronize();
	// clear fifo, set sync bit
	PCM_REG(PCM_CS) |= PCM_CS_SYNC | PCM_CS_RXCLR;
	while (!PCM_SYNC); // wait for sync to appear set (takes two pcm clock cycles to echo back)
	__sync_synchronize();
	// enable dma dreq
	PCM_REG(PCM_CS) |= PCM_CS_DMAEN;
	// fifo lenght: 64 words, dreq at 2 words, panic at 48 words
	PCM_REG(PCM_DREQ) = (0x2F << PCM_DREQ_RX_PANIC) | (0x1 << PCM_DREQ_RX);
	__sync_synchronize();
}

void setupPCMPins() {
	__sync_synchronize();
	GPIO_FSEL(18, GPIO_ALT0); // CLK
	GPIO_FSEL(20, GPIO_ALT0); // DIN
	__sync_synchronize();
}

void resetPCMPins() {
	__sync_synchronize();
	GPIO_FSEL(18, GPIO_IN); // CLK
	GPIO_FSEL(20, GPIO_IN); // DIN
	__sync_synchronize();
}

void startPCM() {
	__sync_synchronize();
	PCM_REG(PCM_CS) |= PCM_CS_RXON; // set RXON
	__sync_synchronize();
}

void stopPCM() {
	__sync_synchronize();
	PCM_REG(PCM_CS) &= ~PCM_CS_RXON; // clear RXON
	__sync_synchronize();
}

//-----DMA-----
void setupDMA() { // TODO set AXI prio?
	__sync_synchronize();
	DMA_ENABLE |= (1 << DMA_CHANNEL); // enable dma channel
	DMA_REG(DMA_CS, DMA_CHANNEL) = DMA_CS_RESET; // reset dma channel
	__sync_synchronize();
}

void startDMA(CONBK *cb) {
	__sync_synchronize();
	DMA_REG(DMA_CONBLK_AD, DMA_CHANNEL) = VIRT_TO_BUS(cb);
	DMA_REG(DMA_CS, DMA_CHANNEL) = DMA_CS_END; // clear end flag
	DMA_REG(DMA_DEBUG, DMA_CHANNEL) = 7; // clear error bits
	DMA_REG(DMA_CS, DMA_CHANNEL) = DMA_CS_ACTIVE; // start dma
	__sync_synchronize();
}

void stopDMA() {
	__sync_synchronize();
	DMA_REG(DMA_CS, DMA_CHANNEL) = DMA_CS_RESET; // reset dma channel
	__sync_synchronize();
}	

//-----EXIT-----
void pcmExit(int sig) {
	printf("Cleaning up and exiting\n"); // TODO
	// free gui memory
	if (windowSurface) {
		cairoCloseX11Surface(windowSurface);
	}
	// stop peripherals
	if (pcm) stopPCM();
	if (dma) stopDMA();
	// reset GPIO
	if (gpio) {
		resetPCMPins();
		GPIO_FSEL(GPIO_PIN_START, GPIO_IN);
		GPIO_FSEL(GPIO_PIN_S0, GPIO_IN);
		GPIO_FSEL(GPIO_PIN_S1, GPIO_IN);
		GPIO_FSEL(GPIO_PIN_S2, GPIO_IN);
	}
	// free/unmap memory
	if (dma_mem_virtual) munmap(dma_mem_virtual, dma_mem_size);
	vcUnlockMemory(memoryHandle);
	vcReleaseMemory(memoryHandle);
	closeMailbox();
	unmapPeripherals();
	// exit
	exit(0);
}
