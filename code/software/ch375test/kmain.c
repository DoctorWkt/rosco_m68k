// Code to test the CH375 device
// (c) 2024 Warren Toomey GPL3

#include <stdio.h>
#include <stdint.h>

// These are in the asmcode.asm file
extern void cpu_delay(int ms);
extern void irq5_install();
extern void send_ch375_cmd(uint8_t cmd);
extern void send_ch375_data(uint8_t cmd);
extern uint8_t read_ch375_data(void);
extern uint8_t get_ch375_status(void);

// The buffer for I/O
uint8_t buf[512];

// CH375 commands
#define CMD_RESET_ALL    	0x05
#define CMD_SET_USB_MODE 	0x15
#define CMD_GET_STATUS   	0x22
#define CMD_RD_USB_DATA  	0x28
#define CMD_WR_USB_DATA  	0x2B
#define CMD_DISK_INIT    	0x51
#define CMD_DISK_SIZE    	0x53
#define CMD_DISK_READ    	0x54
#define CMD_DISK_RD_GO   	0x55
#define CMD_DISK_WRITE   	0x56
#define CMD_DISK_WR_GO   	0x57
#define CMD_DISK_READY   	0x59

// CH375 status results
#define USB_INT_SUCCESS    	0x14
#define USB_INT_CONNECT    	0x15
#define USB_INT_DISCONNECT	0x16
#define USB_INT_DISK_READ  	0x1D
#define USB_INT_DISK_WRITE 	0x1E

// Wait to get a valid status from the CH375.
uint8_t get_valid_ch375_status(void) {
  uint8_t status;

  while (1) {
    status = get_ch375_status();
    if (status != 0xff)
      break;
  }
  return (status);
}

// Given a pointer to a 512-byte buffer and
// an LBA number, read the block into the
// buffer. Return 1 on success, 0 otherwise.
uint8_t read_block(uint8_t * buf, uint32_t lba) {
  uint8_t i, status, cnt;

  // Check we have a buffer
  if (buf == NULL)
    return (0);

  // Send the disk read command followed by the
  // LBA in little-endian format, then ask for
  // one block.
  send_ch375_cmd(CMD_DISK_READ);
  send_ch375_data(lba & 0xff);
  send_ch375_data((lba >> 8) & 0xff);
  send_ch375_data((lba >> 16) & 0xff);
  send_ch375_data((lba >> 24) & 0xff);
  send_ch375_data(1);

  // Loop eight times reading in
  // 64 bytes of data each time.
  for (i = 0; i < 8; i++) {

    // Get the result of the command
    status = get_valid_ch375_status();
    if (status == USB_INT_DISK_READ) {
      // Now read the data
      send_ch375_cmd(CMD_RD_USB_DATA);
      cnt = read_ch375_data();
      while (cnt--)
	*buf++ = read_ch375_data();

      // After 64 bytes, tell the CH375
      // to get the next set of data
      send_ch375_cmd(CMD_DISK_RD_GO);
    }
  }

  // Get the status after reading the block
  status = get_valid_ch375_status();
  if (status == USB_INT_SUCCESS)
    return (1);
  else
    return (0);

}

// Given a pointer to a 512-byte buffer and
// an LBA number, write the buffer into the
// block. Return 1 on success, 0 otherwise.
uint8_t write_block(uint8_t * buf, uint32_t lba) {
  uint8_t i, status, cnt;

  // Check we have a buffer
  if (buf == NULL)
    return (0);

  // Send the disk write command followed by the
  // LBA in little-endian format, then ask to
  // send one block.
  send_ch375_cmd(CMD_DISK_WRITE);
  send_ch375_data(lba & 0xff);
  send_ch375_data((lba >> 8) & 0xff);
  send_ch375_data((lba >> 16) & 0xff);
  send_ch375_data((lba >> 24) & 0xff);
  send_ch375_data(1);

  // Loop eight times writing out
  // 64 bytes of data each time.
  for (i = 0; i < 8; i++) {

    // Get the result of the command
    status = get_valid_ch375_status();
    if (status == USB_INT_DISK_WRITE) {
      // Now send the data
      send_ch375_cmd(CMD_WR_USB_DATA);
      cnt = 64;
      send_ch375_data(cnt);
      while (cnt--)
	send_ch375_data(*buf++);

      // After 64 bytes, tell the CH375
      // to get the next set of data
      send_ch375_cmd(CMD_DISK_WR_GO);
    }
  }

  // Get the status after reading the block
  status = get_valid_ch375_status();
  if (status == USB_INT_SUCCESS)
    return (1);
  else
    return (0);
}

void kmain() {
  uint8_t status, cnt;
  uint32_t size;
  uint32_t blksize;

  // Say hello before we start
  printf("About to initialise the CH375\n");

  // Install the IRQ3 handler
  irq5_install();
  printf("All interrupts now enabled\n");

  // Send the reset command and wait 50mS
  send_ch375_cmd(CMD_RESET_ALL);
  cpu_delay(50);

  // Now set the USB mode to 6. This should
  // cause a level 3 interrupt which will
  // update the CH375 status in memory.
  send_ch375_cmd(CMD_SET_USB_MODE);
  send_ch375_data(6);
  printf("USB mode 6 now set\n");

  // Print out the CH375 status.
  // We expect to get USB_INT_CONNECT
  status = get_valid_ch375_status();
  if (status != USB_INT_CONNECT)
    goto panic;
  printf("After set USB mode, status is 0x%x\n", status);

  // Now initialise the disk. In the real world, this
  // might return USB_INT_DISCONNECT. In this case,
  // the code would prompt the user to attach a USB key
  // and try again.
  send_ch375_cmd(CMD_DISK_INIT);
  status = get_valid_ch375_status();
  if (status != USB_INT_SUCCESS)
    goto panic;
  printf("After disk init, status is 0x%x\n", status);

  // Get the disk's size. The sample code seems to indicate
  // that this can fail. If it does, wait 250mS and try again
  send_ch375_cmd(CMD_DISK_SIZE);
  status = get_ch375_status();
  if (status != USB_INT_SUCCESS) {
    cpu_delay(250);
    send_ch375_cmd(CMD_DISK_SIZE);
    status = get_ch375_status();
  }

  printf("After disk size, status is 0x%x\n", status);
  if (status != USB_INT_SUCCESS)
    goto panic;

  // Ask to receive the actual data. Check that
  // there are eight bytes to read
  send_ch375_cmd(CMD_RD_USB_DATA);
  cnt = read_ch375_data();
  printf("%d bytes to read following the disk size cmd\n", cnt);

  size = (read_ch375_data() << 24) + (read_ch375_data() << 16) +
    (read_ch375_data() << 8) + read_ch375_data();
  blksize = (read_ch375_data() << 24) + (read_ch375_data() << 16) +
    (read_ch375_data() << 8) + read_ch375_data();
  printf("Ths disk has %ld blocks each sized %ld bytes\n", size, blksize);

  // Read block zero on and
  // print it out if OK
  status = read_block(buf, 0);
  if (status == 1) {
    printf("Block zero read OK\n");
    for (int i = 0; i < 512; i++)
      printf("%c", buf[i]);
  } else {
    printf("Block zero read fail\n");
  }

  // Fill the buffer with dummy data
  for (int i = 0; i < 512; i++)
    buf[i] = 'X';

  // Now write the buffer to block one
  printf("\nAbout to write block one\n");
  status = write_block(buf, 1);
  if (status == 1) {
    printf("Block one write OK\n");
  } else {
    printf("Block one write fail\n");
  }

  // Busy loop for now
  printf("\nCH375 test complete, looping ...\n");
  while (1) {
    cpu_delay(1000);
  }

panic:
  printf("panic: we got back status %d, 0x%x\n", status, status);
  while (1) {
    cpu_delay(1000);
  }
}
