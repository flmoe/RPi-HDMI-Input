#include <stdio.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include "periLib.h"

//-----MEMORY-----
// returns 0 on success, -1 on failure
int mapPeripherals() {
	// open /dev/mem
	int memFd;
	if ((memFd = open("/dev/mem", O_RDWR | O_SYNC)) < 0) {
		printf("error opening /dev/mem\n");
		return -1;
	}
	// map peripherals
	gpio = mapBlock(memFd, BASE_PHYS + GPIO_OFFSET, PAGE_SIZE);
	pcm  = mapBlock(memFd, BASE_PHYS + PCM_OFFSET, PAGE_SIZE);
	dma  = mapBlock(memFd, BASE_PHYS + DMA_OFFSET, PAGE_SIZE);
	spi0 = mapBlock(memFd, BASE_PHYS + SPI0_OFFSET, PAGE_SIZE);
	// close /dev/mem
	close(memFd);
	return 0;
}

void unmapPeripherals() {
	if (gpio) {
		munmap(gpio, PAGE_SIZE);
	}
	if (pcm) {
		munmap(pcm, PAGE_SIZE);
	}
	if (dma) {
		munmap(dma, PAGE_SIZE);
	}
	if (spi0) {
		munmap(spi0, PAGE_SIZE);
	}
}

// returns mem pointer on success, -1 on failure
void *mapBusMem(void *bus_memory, uint32_t size) {
	// open /dev/mem
	int memFd;
	if ((memFd = open("/dev/mem", O_RDWR | O_SYNC)) < 0) {
		printf("error opening /dev/mem\n");
		return (void *)-1;
	}
	void *virt = mapBlock(memFd, (uint32_t)bus_memory & ~0xC0000000, size);
	// close /dev/mem
	close(memFd);
	return virt;
}

// size must be page aligned // TODO read about
// returns mem pointer on success, -1 on failure
void *mapBlock(int memFd, uint32_t address, uint32_t size) {
	void *virt = mmap(0, size, PROT_WRITE | PROT_READ, MAP_SHARED, memFd, address);
	if (virt == MAP_FAILED) {
		printf("error mapping peripherals\n");
		close(memFd);
		return (void *)-1;
	}
	return virt;
}

//-----GPU MAILBOX-----
// returns 0 on success, -1 on failure
int openMailbox() {
	if ((mailboxFd = open("/dev/vcio", 0)) < 0) {
		printf("error opening /dev/vcio\n");
		return -1;
	}
	return 0;
}

void closeMailbox() {
	if (mailboxFd >= 0) {
		close(mailboxFd);
	}
}

uint32_t *vcMessageMailbox(MBX_MSG *messagePtr, uint32_t dataLength) {
	// 0x0 end tag
	messagePtr->data[dataLength / 4] = 0;
	messagePtr->size = messagePtr->bufferLength + 24;
	// request code 0x0: process request
	messagePtr->request = 0;
	if (ioctl(mailboxFd, _IOWR(100, 0, void *), messagePtr) < 0) {
		printf("vc ioctl failed\n");
		return 0;
	}
	if (messagePtr->request != 0x80000000) {
		printf("vc ioctl error\n");
		return 0;
	}
	if (messagePtr->request == 0x80000001) {
		printf("vc ioctl error parsing request\n");
		return 0;
	}
	return messagePtr->data;
}

/**
 * Allocates one page of memory
 * size must be page aligned
 */
uint32_t vcAllocateMemory(uint32_t size) {
	MBX_MSG msg = {.tag = 0x3000C, .bufferLength = 12, .data = {size, PAGE_SIZE, MEM_FLAG_DIRECT | MEM_FLAG_ZERO}};
	return vcMessageMailbox(&msg, 12)[0];
}

void *vcLockMemory(int memHandle) {
	MBX_MSG msg = {.tag = 0x3000D, .bufferLength = 4, .data = {memHandle}};
	return (memHandle ? (void *) vcMessageMailbox(&msg, 4)[0]: 0);
}

void vcUnlockMemory(int memHandle) {
	if (!memHandle) return;
	MBX_MSG msg = {.tag = 0x3000E, .bufferLength = 4, .data = {memHandle}};
	vcMessageMailbox(&msg, 4);
}

void vcReleaseMemory(int memHandle) {
	if (!memHandle) return;
	MBX_MSG msg = {.tag = 0x3000F, .bufferLength = 4, .data = {memHandle}};
	vcMessageMailbox(&msg, 4);
}