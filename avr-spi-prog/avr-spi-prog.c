#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <err.h>
#include <errno.h>
#include <unistd.h>

#include "bcm2835.h"

// Default parameters (defined in bcm2835)
uint8_t mode = BCM2835_SPI_MODE0; 
uint16_t clk_div = BCM2835_SPI_CLOCK_DIVIDER_65536;
uint8_t cs = BCM2835_SPI_CS0;
uint8_t polarity = LOW;

uint8_t *buffer = 0;
uint32_t buffer_length = 0;
uint32_t current_offset = 0;

#define AVR_LEN_CMD		4
uint8_t AVR_CMD_PROG_ENABLE[] =	{ 0xac, 0x53, 0, 0 };
uint8_t AVR_CMD_READ_MEM[] =	{ 0x20, 0, 0, 0 };
uint8_t AVR_CMD_WRITE_BUF[] =	{ 0x40, 0, 0, 0 };
uint8_t AVR_CMD_WRITE_PAGE[] =	{ 0x4C, 0, 0, 0 };
uint8_t AVR_CMD_CHIP_ERASE[] = { 0xac, 0x80, 0, 0 };

uint8_t AVR_CMD_READ_SIG_0[] = { 0x30, 0x00, 0x00, 0 };
uint8_t AVR_CMD_READ_SIG_1[] = { 0x30, 0x00, 0x01, 0 };
uint8_t AVR_CMD_READ_SIG_2[] = { 0x30, 0x00, 0x02, 0 };

uint8_t AVR_CMD_READ_FUSE_HI[] = { 0x58, 0x08, 0, 0 };
uint8_t AVR_CMD_READ_FUSE_LO[] = { 0x50, 0x00, 0, 0 };
uint8_t AVR_CMD_READ_LOCK[] = { 0x58, 0x00, 0, 0 };

uint8_t AVR_CMD_WRITE_FUSE_HI[] = { 0xac, 0xa8, 0, 0 };
uint8_t AVR_CMD_WRITE_FUSE_LO[] = { 0xac, 0xa0, 0, 0 };

uint8_t xfer_register(uint8_t *cmd);

void
load_data(const char *filename) 
{
	int fd;	
	struct stat sbuf;

	if (stat(filename, &sbuf))
		errx(errno, "Error opening %s", filename);

	buffer_length = sbuf.st_size;
	buffer = (uint8_t *)malloc(buffer_length); 

	if ( (fd = open(filename, O_RDONLY)) < 0) 
		err(errno, "Error opening %s", filename);

	if (read(fd, buffer, buffer_length) != buffer_length)
		err(errno, "Error reading input file");

	close(fd);
}

uint32_t
read_reg_signature()
{
	uint32_t value;
	value = 0;
	value |= xfer_register(AVR_CMD_READ_SIG_2);
	value <<= 8;
	value |= xfer_register(AVR_CMD_READ_SIG_1);
	value <<= 8;
	value |= xfer_register(AVR_CMD_READ_SIG_0);
	return value;
}

uint32_t
read_reg_fuse()
{
	uint32_t value;
	value = 0;
	value |= xfer_register(AVR_CMD_READ_FUSE_HI);
	value <<= 8;
	value |= xfer_register(AVR_CMD_READ_FUSE_LO);
	return value;
}


void
write_reg_fuse(uint16_t fuse)
{
	errx(1, "Write Fuse unimplemented\n");
	/*
		** BUGGY CODE -- Will Brick your AVR **
		uint32_t value;
		value = fuse;
		fprintf(stderr, "Writing Fuse: 0x%x\n", value);
		AVR_CMD_READ_FUSE_HI[3] = (fuse >> 8);
		AVR_CMD_READ_FUSE_LO[3] = (fuse & 0xff);
		xfer_register(AVR_CMD_WRITE_FUSE_LO);
		xfer_register(AVR_CMD_READ_FUSE_HI);
		value = read_reg_fuse();
		fprintf(stderr, "Device Fuse 0x%x\n", value);
	*/
}


void
issue_avr_cmd(uint8_t *cmd)
{
	uint8_t rbuf[AVR_LEN_CMD];	
	bcm2835_spi_transfernb(cmd, rbuf, AVR_LEN_CMD);
}

void
init_device()
{
	uint32_t value;

	if (!bcm2835_init())
		err(ENOTSUP, "Error initializing GPIO (permissions?)");
	if (!bcm2835_spi_begin())
		err(ENOTSUP, "Error initializing SPI (permissions?)");
	bcm2835_spi_setDataMode(mode);
	bcm2835_spi_setClockDivider(clk_div);
	bcm2835_spi_chipSelect(cs);
	bcm2835_spi_setChipSelectPolarity(cs, polarity);

	issue_avr_cmd(AVR_CMD_PROG_ENABLE);

	value = read_reg_signature();
	fprintf(stderr, "Device Signature 0x%x\n", value);

	value = read_reg_fuse();
	fprintf(stderr, "Device Fuse 0x%x\n", value);

	value = xfer_register(AVR_CMD_READ_LOCK);
	value &=0x3f;
	fprintf(stderr, "Device Lock 0x%x\n", value);
}



const uint32_t PAGE_SIZE_WORDS = 64;
const uint32_t PAGE_SIZE = 64 * 2;

#define MIN(x,y)	((x <= y) ? x : y)

void
write_page(uint32_t page_nr, uint32_t len, uint8_t *data) 
{
	uint8_t cmd[AVR_LEN_CMD];
	uint8_t result[AVR_LEN_CMD];
	memcpy(cmd, AVR_CMD_WRITE_BUF, AVR_LEN_CMD);

	/* convert len to words */
	len /= 2; /* HITHERTO: programin len is down rounded to 2 */

	uint8_t i = 0;
	for (i = 0; i < len; ++i) {
		cmd[0] = 0x40;
		cmd[1] = 0;
		cmd[2] = i;
		cmd[3] = data[i * 2]; 
		bcm2835_spi_transfernb(cmd, result, AVR_LEN_CMD);

		cmd[0] = 0x48;
		cmd[1] = 0;
		cmd[2] = i;
		cmd[3] = data[i * 2 + 1]; 
		bcm2835_spi_transfernb(cmd, result, AVR_LEN_CMD);
	}

	uint32_t page = page_nr * PAGE_SIZE_WORDS;
	memcpy(cmd, AVR_CMD_WRITE_PAGE, AVR_LEN_CMD);
	cmd[1] = (page >> 8) & 0xff;
	cmd[2] = page & 0xff;
	cmd[3] = 0;
	bcm2835_spi_transfernb(cmd, result, AVR_LEN_CMD);
}

uint8_t
xfer_register(uint8_t *cmd)
{
	uint8_t data[AVR_LEN_CMD];
	bcm2835_spi_transfernb(cmd, data, AVR_LEN_CMD);
	return data[AVR_LEN_CMD - 1];
}

uint8_t*
read_page(uint32_t page_nr) 
{
	uint8_t cmd[AVR_LEN_CMD];
	uint8_t data[AVR_LEN_CMD];
	memcpy(cmd, AVR_CMD_READ_MEM, AVR_LEN_CMD);
	uint32_t page = page_nr * PAGE_SIZE_WORDS;

	uint8_t *buf = (uint8_t*)malloc(PAGE_SIZE);
	uint8_t i = 0;
	for (i = 0; i < PAGE_SIZE_WORDS; ++i) {
		uint32_t base = page + i;
		uint8_t lo =  base & 0xff;
		uint8_t hi = (base >> 8) & 0xff;
		cmd[0] = 0x20;
		cmd[1] = hi;
		cmd[2] = lo;
		bcm2835_spi_transfernb(cmd, data, AVR_LEN_CMD);
		uint8_t dlo = data[AVR_LEN_CMD - 1];
		cmd[0] = 0x28;
		bcm2835_spi_transfernb(cmd, data, AVR_LEN_CMD);
		uint8_t dhi = data[AVR_LEN_CMD - 1];
		buf[i*2] = dlo;
		buf[i*2+1] = dhi;
	}
	return buf;
}

int
page_isblank(uint8_t *page)
{
	uint8_t i = 0;
	while (i < PAGE_SIZE && page[i] == 0xff)
		++i;
	return (i == PAGE_SIZE);
}

void
erase()
{
	fprintf(stderr, "Erasing Chip Memory\n");
	issue_avr_cmd(AVR_CMD_CHIP_ERASE);
}

void
program(const char *filename, uint32_t base_offset, int erase)
{
	fprintf(stderr, "Base Address: 0x%x\n", base_offset);
	fprintf(stderr, "Program File: %s\n", filename);
	load_data(filename);

	int verify = 0;
	while(current_offset < buffer_length) {
		uint32_t page_nr = (base_offset + current_offset) / PAGE_SIZE; 
		uint32_t page_len = MIN(buffer_length - 
						current_offset, PAGE_SIZE);
		uint8_t *new_page = buffer + current_offset;
		if (!page_isblank(new_page)) {
			fprintf(stderr, "Reading Page: %d\n", page_nr);
			uint8_t *exist_page = read_page(page_nr);
			int flag = memcmp(exist_page, new_page, page_len);
			free(exist_page);
			if (flag == 0) {
				if (verify == 1)
					fprintf(stderr,
						"Page Verified: %d\n", page_nr);
				else
					fprintf(stderr,
						"Skipping Page: %d\n", page_nr);
			} else {
				fprintf(stderr,
					"Writing Page: %d\n", page_nr);
				write_page(page_nr, page_len, new_page);
				usleep(1000);
				verify = 1;
				/** loop to trigger a verify **/
				continue;	
			}
		} else {
			fprintf(stderr,
				"Skipping Blank Input Page: %d\n", page_nr);
		}
		current_offset += page_len;
		verify = 0;
	}
}

const char *prog_filename = 0;
uint32_t base_address = 0;
uint16_t fuse_value;
int erase_flag = 0;
int fuse_flag = 0;

void
parse_args(int argc, char **argv)
{
	char *eptr;
	int opt;

	while ((opt = getopt(argc, argv, "p:b:ef:")) != -1) {
               switch (opt) {
		case 'f':
			fuse_value = (strtoul(optarg, &eptr, 0) & 0xffff);
			if (*eptr != 0)
				errx(1, "Invalid base address: %s\n", optarg);
			fuse_flag = 1;
			break;
		case 'e':
			erase_flag = 1;
			break;
		case 'p':
			prog_filename = optarg;
			break;
		case 'b':
			base_address = strtol(optarg, &eptr, 0);
			if (*eptr != 0)
				errx(1, "Invalid base address: %s\n", optarg);
			break;
		default: /* '?' */
			fprintf(stderr, "Usage: %s [-e] [-p <filename>]\n", 
				argv[0]);
			exit(1);
               }
           }
}

int
main(int argc, char **argv) 
{
	parse_args(argc, argv);

	init_device();

	if (erase_flag)
		erase();

	if (fuse_flag)
		write_reg_fuse(fuse_value);


	if (prog_filename != 0)
		program(prog_filename, base_address, erase_flag);

	return -1;
}

