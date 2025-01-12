/*
 * (C) Copyright 20018-2019
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 *
 * Description: spinor driver for General spinor operations
 * Author: wangwei <wangwei@allwinnertech.com>
 * Date: 2018-11-15 14:18:18
 */

#include <arch/spi.h>
#include <arch/spinor.h>
#include <private_boot0.h>
#include <private_toc.h>

#define SYSTEM_PAGE_SIZE 512
#define READ_LEN (8*1024*1024)
#define WRITE_LEN (128)
#define ERASE_LEN (4 * 1024)

#define JEDEC_MFR(id)		(id&0xff)

static u8 spinor_init_flag = 0;
/*
 * Clock Frequency for all commands (except Read), depend on flash, maybe 104M or 133M
 * Clock Frequency for READ(0x03) instructions is special, depend on flash, maybe only 50M or 80M
 * Default use FASTREAD(0x0b) which can work with sunxi spi clk(max 100M)
 */
static u8 read_cmd = CMD_READ_ARRAY_FAST;
static u8 spinor_4bytes_addr_mode;

static int is_valid_read_cmd(u8 cmd)
{
	if (SPINOR_OP_READ_1_1_2 == cmd
		|| SPINOR_OP_READ_1_1_4 == cmd
		|| SPINOR_OP_READ4_1_1_2 == cmd
		|| SPINOR_OP_READ4_1_1_4 == cmd
		|| CMD_READ_ARRAY_FAST == cmd
		|| CMD_READ_ARRAY_SLOW == cmd )
		return 1;
	return 0;
}

static int is_quad_read_cmd(u8 cmd)
{
	if (SPINOR_OP_READ_1_1_4 == cmd
		|| SPINOR_OP_READ4_1_1_4 == cmd)
		return 1;
	return 0;
}

#if 0
static int is_dual_read_cmd(u8 cmd)
{
	if (SPINOR_OP_READ_1_1_2 == cmd
		|| SPINOR_OP_READ4_1_1_2 == cmd)
		return 1;
	return 0;
}
#endif

static int read_sr(u8 *status)
{
	u8 cmd = CMD_READ_STATUS;

	return spi_xfer(1, &cmd, 1, status);
}

static int read_sr1(u8 *status)
{
	u8 cmd = CMD_READ_STATUS1;

	return spi_xfer(1, &cmd, 1, status);
}

static int spi_flash_ready(void)
{
	u8 sr;
	int ret;

	ret = read_sr(&sr);
	if (ret < 0)
		return ret;

	return !(sr & STATUS_WIP);
}

static int spi_flash_wait_till_ready(void)
{
	uint timeout = 0x10000;
	int ret;

	while (timeout --) {
		ret = spi_flash_ready();
		if (ret < 0)
			return ret;
		if (ret)
			return 0;
	}

	printf("SF: Timeout!\n");
	return -1;
}

static int spi_flash_write_en(void)
{
	u8 cmd = CMD_WRITE_ENABLE;
	u8 status;

	if (spi_xfer(1, &cmd, 0, NULL))
		return -1;

	read_sr(&status);

	return 0;
}

#ifdef CFG_SPINOR_INDIVIDUAL_LOCK
static int spi_flash_unlock_all(void)
{
	u8 cmd = CMD_GBULK;
	u8 status;

	spi_flash_write_en();

	if (spi_xfer(1, &cmd, 0, NULL))
		return -1;

	read_sr(&status);

	return 0;
}
#endif

static int write_sr(u8 status)
{
	u8 dout[4];

	spi_flash_write_en();

	dout[0] = CMD_WRITE_STATUS;
	dout[1] = status;

	if (spi_xfer(2, dout, 0, NULL))
		return -1;

	return spi_flash_wait_till_ready();

}

static int write_sr1(u8 status)
{
	u8 dout[4];

	spi_flash_write_en();

	dout[0] = CMD_WRITE_STATUS1;
	dout[1] = status;

	if (spi_xfer(2, dout, 0, NULL))
		return -1;

	return spi_flash_wait_till_ready();
}

static int read_cr(u8 *status)
{
	u8 cmd = CMD_READ_CONFIG;

	return spi_xfer(1, &cmd, 1, status);
}

/*
static int read_cr1(u8 *status)
{
	u8 cmd = CMD_READ_CONFIG1;
	return spi_xfer(1, &cmd, 1, status);
}*/

static int write_cr(u8 wc)
{
	u8 dout[4];
	u8 status;
	int ret;

	ret = read_sr(&status);
	if (ret < 0)
		return ret;

	spi_flash_write_en();

	dout[0] = CMD_WRITE_STATUS;
	dout[1] = status;
	dout[2] = wc;

	if(spi_xfer(3, dout, 0, NULL))
		return -1;

	return spi_flash_wait_till_ready();

}

/*
static int write_cr1(u8 status)
{
	u8 dout[4];

	spi_flash_write_en();

	dout[0] = CMD_WRITE_CONFIG1;
	dout[1] = status;

	if(spi_xfer(2, dout, 0, NULL))
		return -1;

	return spi_flash_wait_till_ready();
}*/

/*Order code“05H”/“35H”/“15H”,The corresponding status registerS7~S0 / S15~S8 / S16~S23.*/
static int GigaDevice_quad_enable(void)
{
	u8 qeb_status;
	int ret;

	ret = read_sr1(&qeb_status);
	if (ret < 0)
		return ret;

	if (qeb_status & STATUS_QEB_GIGA) {
//		printf("already enable\n");
		return 0;
	}

	ret = write_sr1(qeb_status | STATUS_QEB_GIGA);
	if (ret < 0)
		return ret;

	/* read SR and check it */
	ret = read_sr1(&qeb_status);
	if (!(ret >= 0 && (qeb_status & STATUS_QEB_GIGA))) {
		printf("SF: Macronix SR Quad bit not clear\n");
		return -1;
	}
	return ret;
}

static int macronix_quad_enable(void)
{
	u8 qeb_status;
	int ret;

	ret = read_sr(&qeb_status);
	if (ret < 0)
		return ret;

	if (qeb_status & STATUS_QEB_MXIC) {
//		printf("already enable\n");
		return 0;
	}

	ret = write_sr(qeb_status | STATUS_QEB_MXIC);
	if (ret < 0)
		return ret;

	/* read SR and check it */
	ret = read_sr(&qeb_status);
	if (!(ret >= 0 && (qeb_status & STATUS_QEB_MXIC))) {
		printf("SF: Macronix SR Quad bit not clear\n");
		return -1;
	}
	return ret;
}

static int spansion_quad_enable(void)
{
	u8 qeb_status;
	int ret;

	ret = read_cr(&qeb_status);
	if (ret < 0)
		return ret;

	if (qeb_status & STATUS_QEB_WINSPAN)
		return 0;

	ret = write_cr(qeb_status | STATUS_QEB_WINSPAN);
	if (ret < 0)
		return ret;

	/* read CR and check it */
	ret = read_cr(&qeb_status);
	if (!(ret >= 0 && (qeb_status & STATUS_QEB_WINSPAN))) {
		printf("SF: Spansion CR Quad bit not clear\n");
		return -1;
	}

	return ret;
}

/*
static int stmicro_quad_enable(void)
{
	u8 qeb_status;
	int ret;

	ret = read_cr1(&qeb_status);
	if (ret < 0)
		return ret;

	if (qeb_status & STATUS_QEB_STMICRO)
		return 0;

	ret = write_cr1(qeb_status | STATUS_QEB_STMICRO);
	if (ret < 0)
		return ret;

	// read CR and check it
	ret = read_cr1(&qeb_status);
	if (!(ret >= 0 && (qeb_status & STATUS_QEB_STMICRO))) {
		printf("SF: Spansion CR Quad bit not clear\n");
		return -1;
	}

	return ret;
}*/

static int set_quad_mode(u8 id_0, u8 id_1)
{
	switch (JEDEC_MFR(id_0)) {

	case SPI_FLASH_CFI_MFR_MACRONIX:
	case SPI_FLASH_CFI_MFR_XMC:
		if (JEDEC_MFR(id_1) >> 4 == 'b') {
			printf("SF: QEB is volatile for %02xb flash\n", JEDEC_MFR(id_0));
			return 0;
		}
		return macronix_quad_enable();
	case SPI_FLASH_CFI_MFR_GIGADEVICE:
	case SPI_FLASH_CFI_MFR_ADESTO:
		return GigaDevice_quad_enable();
	case SPI_FLASH_CFI_MFR_SPANSION:
	case SPI_FLASH_CFI_MFR_WINBOND:
	case SPI_FLASH_CFI_MFR_XTX:
		return spansion_quad_enable();

//	case SPI_FLASH_CFI_MFR_STMICRO:
//		printf("SF: QEB is volatile for %02x flash\n", JEDEC_MFR(id));
//		return stmicro_quad_enable();
//		return 0;
	default:
		printf("SF: Need set QEB func for %02x flash\n",
		       JEDEC_MFR(id_0));
		return -1;
	}
}


static int spinor_read_id(u8 *id)
{
	u8 cmd = CMD_READ_ID;

	return spi_xfer(1, &cmd, 3, id);
}

u8 id[4] = {0};
u8 unlock_flag = 0;

int bp_unlock_all(void)
{
	/*
	 * Flash powers up read-only, so clear BP# bits.
	 *
	 * Note on some flash (like Macronix), QE (quad enable) bit is in the
	 * same status register as BP# bits, and we need preserve its original
	 * value during a reboot cycle as this is required by some platforms
	 * (like Intel ICH SPI controller working under descriptor mode).
	 */

	/*
	 * Clear sr can make sure software protection bits(BPx) cleared.
	 * But clear sr have some problems:
	 *     1) cost time.
	 *     2) power failure occer when set sr may break the sr value.
	 *     3) sr have life cycle limit just like normal nor cells.
	 *
	 * So don't call me unless actually need to erase/write
	 */

	if (JEDEC_MFR(id[0]) == SPI_FLASH_CFI_MFR_ATMEL ||
	   (JEDEC_MFR(id[0]) == SPI_FLASH_CFI_MFR_SST) ||
	   (JEDEC_MFR(id[0]) == SPI_FLASH_CFI_MFR_ESMT) ||
	   (JEDEC_MFR(id[0]) == SPI_FLASH_CFI_MFR_GIGADEVICE) ||
	   (JEDEC_MFR(id[0]) == SPI_FLASH_CFI_MFR_MACRONIX)) {
		u8 sr = 0, sr_new = 0;
		u8 sr1 = 0, sr1_new = 0;

		printf("bp unlock all\n");

		if (JEDEC_MFR(id[0]) == SPI_FLASH_CFI_MFR_GIGADEVICE) {
			read_sr1(&sr1);
			sr1_new = sr1 & (~STATUS_CMP_GIGA);

			if (sr1_new != sr1) {
				write_sr1(sr1_new);
				read_sr1(&sr1_new);
				printf("sr1: 0x%02x --> 0x%02x\n", sr1, sr1_new);
			}
		}

		read_sr(&sr);

		if (JEDEC_MFR(id[0]) == SPI_FLASH_CFI_MFR_MACRONIX)
			sr_new = sr & STATUS_QEB_MXIC;
		else
			sr_new = 0;

		if (sr_new != sr) {
			write_sr(sr_new);
			read_sr(&sr_new);
			if (sr_new != sr)
			printf("sr: 0x%02x --> 0x%02x\n", sr, sr_new);
		}
	}

	return 0;
}

#ifdef CFG_SPINOR_INDIVIDUAL_LOCK
int individual_block_unlock_all(void)
{
	if ((JEDEC_MFR(id[0]) == SPI_FLASH_CFI_MFR_WINBOND) ||
	   (JEDEC_MFR(id[0]) == SPI_FLASH_CFI_MFR_MACRONIX) ||
	   (JEDEC_MFR(id[0]) == SPI_FLASH_CFI_MFR_XTX) ||
	   (JEDEC_MFR(id[0]) == SPI_FLASH_CFI_MFR_XMC)) {
		printf("block unlock all\n");
		spi_flash_unlock_all();
	}

	return 0;
}
#endif
static void spinor_set_readcmd(u8 *readcmd, boot_spinor_info_t *spinor_info)
{
	if (spinor_info->read_mode) {
		switch (spinor_info->read_mode) {
		case SPINOR_QUAD_MODE:
			*readcmd = CMD_READ_QUAD_OUTPUT_FAST;
			break;
		case SPINOR_DUAL_MODE:
			*readcmd = CMD_READ_DUAL_OUTPUT_FAST;
			break;
		case SPINOR_SINGLE_MODE:
			*readcmd = CMD_READ_ARRAY_FAST;
			break;
		};
	}
}

static int spinor_enter_4bytes_addr(int enable)
{
	int command = 0;
	u8 buf = 0;

	if (enable)
		command = 0xB7;
	else
		command = 0xE9;

	spi_xfer(1, (void *)&command, 0, NULL);

	read_cr(&buf);
	if ((buf >> 5)  & 0x1)
		printf("4byte mode ok\n");
	else {
		printf("4byte mode error\n");
		return 0;
	}
	return 1;
}

int spinor_init(int stage)
{

	int ret = 0;

	if(spinor_init_flag == 1)
		return 0;

	boot_spinor_info_t *spinor_info = NULL;
#ifdef CFG_SUNXI_SBOOT
	u8 readcmd = toc0_config->storage_data[0];
#else
	u8 readcmd = BT0_head.prvt_head.storage_data[0];
	spinor_info = (boot_spinor_info_t *)BT0_head.prvt_head.storage_data;
#endif
	if (spinor_info)
		spinor_set_readcmd(&readcmd, spinor_info);

	if(spi_init())
		return -1;

	ret = spinor_read_id(id);

	if(ret || id[0] == 0x0)
		return -1;

	if(is_valid_read_cmd(readcmd)) {
		if (!spinor_info) {
			if (readcmd == SPINOR_OP_READ4_1_1_2)
				readcmd = SPINOR_OP_READ_1_1_2;
			if (readcmd == SPINOR_OP_READ4_1_1_4)
				readcmd = SPINOR_OP_READ_1_1_4;
			read_cmd = readcmd;
		} else if (spinor_info->flash_size > 16 &&
				spinor_enter_4bytes_addr(1)) {
			spinor_4bytes_addr_mode = 1;/* set 4byte opcodes*/
			if (readcmd == SPINOR_OP_READ_1_1_2)
				readcmd = SPINOR_OP_READ4_1_1_2;
			if (readcmd == SPINOR_OP_READ_1_1_4)
				readcmd = SPINOR_OP_READ4_1_1_4;
			read_cmd = readcmd;
		} else {
			if (readcmd == SPINOR_OP_READ4_1_1_2)
				readcmd = SPINOR_OP_READ_1_1_2;
			if (readcmd == SPINOR_OP_READ4_1_1_4)
				readcmd = SPINOR_OP_READ_1_1_4;
			read_cmd = readcmd;
		}
	}

	if ((id[0] == 0xc2) && (id[1] == 0x20) && (id[2] == 0x1a)) {
		/* FIXME */
		printf("fix cmd %02x to %02x\n", read_cmd, CMD_READ_ARRAY_FAST);
		read_cmd = CMD_READ_ARRAY_FAST;
	}
	if ((id[0] == 0xc2) && (id[1] == 0x20) && (id[2] == 0x18)) {
		/* FIXME */
		printf("fix cmd %02x to %02x\n", read_cmd, CMD_READ_QUAD_OUTPUT_FAST);
		read_cmd = CMD_READ_QUAD_OUTPUT_FAST;
	}

	printf("spinor id is: %02x %02x %02x, read cmd: %02x\n",
		id[0], id[1], id[2], read_cmd);

	if (is_quad_read_cmd(read_cmd))
		set_quad_mode(id[0], id[1]);

	spinor_init_flag = 1;

	return 0;
}

int spinor_exit(int stage)
{
	spi_exit();
	spinor_init_flag = 0;
	return 0;
}

int spinor_unlock(void)
{
	if (unlock_flag == 0) {
		bp_unlock_all();
#ifdef CFG_SPINOR_INDIVIDUAL_LOCK
		individual_block_unlock_all();
#endif
		unlock_flag = 1;
	}

	return 0;
}

static void spinor_config_addr_mode(u8 *sdata, uint page_addr, uint *num, u8 cmd)
{
	if ((sdata == NULL) || (num == NULL)) {
		return;
	}

	if (spinor_4bytes_addr_mode == 0) {
		*num = 4;
		sdata[0] = cmd;
		sdata[1] = (page_addr >> 16) & 0xff;
		sdata[2] = (page_addr >> 8) & 0xff;
		sdata[3] = page_addr & 0xff;
		sdata[4] = 0;
	} else if (spinor_4bytes_addr_mode == 1) {
		*num = 5;
		sdata[0] = cmd;
		sdata[1] = (page_addr >> 24) & 0xff;
		sdata[2] = (page_addr >> 16) & 0xff;
		sdata[3] = (page_addr >> 8) & 0xff;
		sdata[4] = page_addr & 0xff;
		sdata[5] = 0;
	}
}

int spinor_read(uint start, uint sector_cnt, void *buffer)
{
	u32 page_addr;
	u8  cmd[6] = {0};
	u32 todo = 0, offset = 0;
	u32 txnum = 0, rxnum;
	u8 *buf = (u8 *)buffer;
	u32 len = sector_cnt*512;
	u32 addr = start*512;
	/* printf("spinor_read, addr:0x%x, size:%d\n", addr, len); */

	while (len) {
		todo = (len > READ_LEN ? READ_LEN: len);
		page_addr = (addr + offset);

		spinor_config_addr_mode(cmd, page_addr, &txnum, read_cmd);

		if(cmd[0] != CMD_READ_ARRAY_SLOW) {
			txnum += 1; /*dummy byte*/
		}
		rxnum = todo;

		if (spi_xfer(txnum, cmd, rxnum, buf+offset)) {
			return -1;
		}

		len -= todo;
		offset += todo;
	}
	return 0;
}

int spinor_erase(uint start, uint sector_cnt)
{
	unsigned long page_addr;
	int ret;
	u8 cmd[4];

	u32 offset = 0;
	u32 len = sector_cnt*512;
	u32 addr = start*512;
	u32 txnum, rxnum;

	spinor_unlock();

	/* printf("spinor_erase, addr:0x%x, size:%d\n", addr, len); */
	if (addr % ERASE_LEN) {
		printf("addr not align, addr:%d, ERASE_LEN:%d\n", addr, ERASE_LEN);
		return -1;
	}
	if (len % ERASE_LEN) {
		printf("len not align, len:%d, ERASE_LEN:%d\n", len, ERASE_LEN);
		return -1;
	}

	cmd[0] = CMD_ERASE_4K;
	while (len) {
		page_addr = (addr + offset);
		cmd[1] = (page_addr >> 16) & 0xff;
		cmd[2] = (page_addr >> 8) & 0xff;
		cmd[3] = page_addr & 0xff;
		txnum = 4;
		rxnum = 0;

		ret = spi_flash_write_en();
		if (ret < 0) {
			printf("enabling write failed\n");
			break;
		}
		ret = spi_xfer(txnum, cmd, rxnum, 0);
		if (ret < 0) {
			printf("erase failed\n");
			break;
		}

		ret = spi_flash_wait_till_ready();
		if (ret)
			break;

		len -= ERASE_LEN;
		offset += ERASE_LEN;
	}

	return ret;

}

int spinor_random_write(uint addr, uint len, void *buffer)
{
	unsigned long page_addr;
	int ret;
	u8 buf[WRITE_LEN + 4] = {0};
	u8* cmd = buf;
	u32 todo = 0, offset = 0;
	u32 txnum;

	spinor_unlock();

	/* printf("spinor_random_write, addr:0x%x, size:%d\n", addr, len); */

	cmd[0] = CMD_PAGE_PROGRAM;
	while (len) {
		todo = (len > WRITE_LEN ? WRITE_LEN: len);
		page_addr = (addr + offset);

		cmd[1] = (page_addr >> 16) & 0xff;
		cmd[2] = (page_addr >> 8) & 0xff;
		cmd[3] = page_addr & 0xff;
		memcpy((void *)buf + 4, buffer + offset, todo);
		txnum = todo + 4;

		ret = spi_flash_write_en();
		if (ret < 0) {
			printf("enabling write failed\n");
			break;
		}
		ret = spi_xfer(txnum, buf, 0, NULL);
		if (ret < 0) {
			printf("write failed\n");
			break;
		}

		ret = spi_flash_wait_till_ready();
		if (ret)
			break;

		len -= todo;
		offset += todo;
	}

	return ret;
}

int spinor_write(uint start, uint sector_cnt, void *buffer)
{
	int ret;
	u32 addr = start * 512;
	u32 len = sector_cnt * 512;

	ret = spinor_random_write(addr, len, buffer);

	return ret;
}
