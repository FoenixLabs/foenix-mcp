/***********************************************************************
 * LAN9221.C - driver for the SMSC LAN9221 Ethernet controller in the
 * Foenix A2560K (K25).
 *
 * The driver sends and receives complete Ethernet frames; it knows
 * nothing about IP. The CPU talks to the chip through registers and
 * two FIFOs (first-in-first-out queues inside the chip): the RX FIFO
 * holds received frames, the TX FIFO frames waiting to be sent.
 * See lan9221.h for how the A2560K connects the chip to the CPU, and
 * the LAN9221 datasheet for the registers.
 *
 * Part of FELINC, the Foenix Labs Ethernet Library in C. (c) R. Kint
 **********************************************************************/
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "lan9221.h"
#include "utils.h"
#include "prof.h"

/* 1 = two 16-bit accesses per 32-bit LAN register (A1=0, then A1=1).
 * REQUIRED on the real K25 board: a 32-bit CPU access returns the A1=0
 * word twice (BYTE_TEST read 0x43214321 instead of 0x87654321), contrary
 * to the hardware reference. 0 = one 32-bit access, only for a future
 * FPGA version that does split long accesses.
 */
#ifndef ETH_WORD_ACCESS
#define ETH_WORD_ACCESS	1
#endif

/* MAC_CSR_CMD register bits */
#define MAC_CSR_BUSY	(1UL<<31)
#define MAC_CSR_READ	(1UL<<30)

/* MII_ACC (PHY access) register bits */
#define MII_ACC_BUSY	(1UL<<0)
#define MII_ACC_WRITE	(1UL<<1)

/* Busy-wait limits. Every wait loop has a limit, so a chip that does
 * not answer can never hang the whole program. Each loop step is at
 * least one bus access.
 */
#define ETH_SPIN_SHORT	10000UL
#define ETH_SPIN_LONG	200000UL

/* Longest wait for the link to be negotiated, in jiffies (5 s) */
#define ETH_AUTONEG_TIMEOUT	(5*60)

/* GAVIN interrupt controller registers (K25 hardware reference) */
#define GAVIN_PENDING_GRP1	(*(volatile uint16_t *)0xFEC00102)
#define GAVIN_POL_GRP1		(*(volatile uint16_t *)0xFEC0010A)
#define GAVIN_EDGE_GRP1		(*(volatile uint16_t *)0xFEC00112)
#define GAVIN_MASK_GRP1		(*(volatile uint16_t *)0xFEC0011A)
#define GAVIN_LAN_BIT		(1U<<14)		/* interrupt 30 = bit 14 of group word 1 */
#define LAN_IRQ_VECTOR_ADDR	(0x4E*4)		/* address of vector $4E */

static utl_pool *frame_tx_pool;	/* buffers to build outgoing frames in */
static mac my_mac_address;
static eth_stats stats;
static uint16_t tx_tag;			/* number put in each frame's TX command, for debugging */

/***********************************************************************
 * swap32 (internal)
 * Reverses the byte order of a 32-bit value (0x12345678 -> 0x78563412).
 *
 * Input:
 *   v      the value
 * Output:
 *   the value with its 4 bytes in reverse order
 **********************************************************************/
static uint32_t swap32( uint32_t v )
{
	return	((v >> 24) & 0x000000FFUL) |
			((v >>  8) & 0x0000FF00UL) |
			((v <<  8) & 0x00FF0000UL) |
			((v << 24) & 0xFF000000UL);
}

/***********************************************************************
 * lan_read32, lan_write32 (internal)
 * Read or write 32 bits at a LAN9221 address, without changing the byte
 * order. FIFO data is used like this, because the A2560K bus already
 * delivers it in network order. The chip has a 16-bit bus, so a 32-bit
 * value takes two accesses: first at A1=0 (the high half for the CPU),
 * then at A1=1.
 * Everything is 'volatile': without it the compiler may merge the
 * repeated reads/writes of the same FIFO address into one.
 *
 * Input:
 *   offset  LAN9221 register or FIFO address (relative to ETH_BASE)
 *   value   (write only) the 32 bits to write
 * Output:
 *   (read only) the 32 bits read
 **********************************************************************/
#if ETH_WORD_ACCESS
static uint32_t lan_read32( uint32_t offset )
{
	volatile uint16_t *p = (volatile uint16_t *)(ETH_BASE + offset);
	uint32_t hi;

	hi = p[0];					/* separate statement: A1=0 must be read first */
	return (hi << 16) | p[1];
}

static void lan_write32( uint32_t offset, uint32_t value )
{
	volatile uint16_t *p = (volatile uint16_t *)(ETH_BASE + offset);

	p[0] = (uint16_t)(value >> 16);
	p[1] = (uint16_t)value;
}
#else
#define lan_read32(offset)			(*(volatile uint32_t *)(ETH_BASE + (offset)))
#define lan_write32(offset, value)	(*(volatile uint32_t *)(ETH_BASE + (offset)) = (value))
#endif

/***********************************************************************
 * eth_fifo_read, eth_fifo_write (internal)
 * Read or write a block of 32-bit words from the RX data FIFO or into
 * the TX data FIFO, in network byte order (like lan_read32/lan_write32).
 * These loops run for every byte that is received or sent, so they are
 * kept as short as possible: no function call per word, just the two
 * 16-bit FIFO accesses and the store. Measured on the A2560K: a loop
 * around lan_read32() took 4.2 us per word, mostly in the call.
 *
 * Input:
 *   buf      where the words go to (read) or come from (write); must be
 *            at an even address
 *   dwords   number of 32-bit words
 * Output:
 *   none
 **********************************************************************/
static void eth_fifo_read( void *buf, uint32_t dwords )
{
#if ETH_WORD_ACCESS
	volatile uint16_t *fifo = (volatile uint16_t *)(ETH_BASE + RX_DATA_FIFO);
	uint16_t *dst = (uint16_t *)buf;

	while( dwords-- > 0 )
	{
		*dst++ = fifo[0];		/* A1=0 first: the first two bytes */
		*dst++ = fifo[1];
	}
#else
	uint32_t *dst = (uint32_t *)buf;

	while( dwords-- > 0 )
		*dst++ = lan_read32( RX_DATA_FIFO );
#endif
}

static void eth_fifo_write( const void *buf, uint32_t dwords )
{
#if ETH_WORD_ACCESS
	volatile uint16_t *fifo = (volatile uint16_t *)(ETH_BASE + TX_DATA_FIFO);
	const uint16_t *src = (const uint16_t *)buf;

	while( dwords-- > 0 )
	{
		fifo[0] = *src++;		/* A1=0 first, as in lan_write32 */
		fifo[1] = *src++;
	}
#else
	const uint32_t *src = (const uint32_t *)buf;

	while( dwords-- > 0 )
		lan_write32( TX_DATA_FIFO, *src++ );
#endif
}

/***********************************************************************
 * eth_reg_read, eth_reg_write (internal)
 * Read or write a 32-bit LAN9221 control register. On the A2560K these
 * arrive byte-reversed (see lan9221.h), so the bytes are swapped back.
 *
 * Input:
 *   eth_reg  register address (ETH_... in lan9221.h)
 *   value    (write only) the value to write
 * Output:
 *   (read only) the register value
 **********************************************************************/
static uint32_t eth_reg_read( uint32_t eth_reg )
{
	return swap32( lan_read32(eth_reg) );
}

static void eth_reg_write( uint32_t eth_reg, uint32_t value )
{
	lan_write32( eth_reg, swap32(value) );
}

/***********************************************************************
 * eth_wait_clear, eth_wait_set (internal)
 * Wait until bits in a register are all 0 (wait_clear), or until at
 * least one of them is 1 (wait_set), for at most 'spins' reads.
 *
 * Input:
 *   eth_reg  register address
 *   mask     the bits to watch
 *   spins    maximum number of reads
 * Output:
 *   true when the condition was met, false on time-out
 **********************************************************************/
static bool eth_wait_clear( uint32_t eth_reg, uint32_t mask, uint32_t spins )
{
	while( eth_reg_read(eth_reg) & mask )
		if( --spins == 0 ) return false;
	return true;
}

static bool eth_wait_set( uint32_t eth_reg, uint32_t mask, uint32_t spins )
{
	while( !(eth_reg_read(eth_reg) & mask) )
		if( --spins == 0 ) return false;
	return true;
}

/***********************************************************************
 * eth_mac_read, eth_mac_write (internal)
 * Read or write a register of the chip's MAC unit (ETH_MAC_...). These
 * cannot be accessed directly: the register number goes into
 * MAC_CSR_CMD and the data through MAC_CSR_DATA. The chip needs time
 * for each transfer, so we wait until it is no longer busy.
 *
 * Input:
 *   mac_register  MAC register number (ETH_MAC_... in lan9221.h)
 *   value         (write only) the value to write
 * Output:
 *   (read only) the register value
 **********************************************************************/
static uint32_t eth_mac_read( uint8_t mac_register )
{
	eth_wait_clear( ETH_MAC_CSR_CMD, MAC_CSR_BUSY, ETH_SPIN_SHORT );
	eth_reg_write( ETH_MAC_CSR_CMD, MAC_CSR_BUSY | MAC_CSR_READ | mac_register );
	eth_wait_clear( ETH_MAC_CSR_CMD, MAC_CSR_BUSY, ETH_SPIN_SHORT );	/* data valid now */
	return eth_reg_read( ETH_MAC_CSR_DATA );
}

static void eth_mac_write( uint8_t mac_register, uint32_t value )
{
	eth_wait_clear( ETH_MAC_CSR_CMD, MAC_CSR_BUSY, ETH_SPIN_SHORT );
	eth_reg_write( ETH_MAC_CSR_DATA, value );
	eth_reg_write( ETH_MAC_CSR_CMD, MAC_CSR_BUSY | mac_register );
	eth_wait_clear( ETH_MAC_CSR_CMD, MAC_CSR_BUSY, ETH_SPIN_SHORT );
}

/***********************************************************************
 * eth_mii_wait (internal)
 * Waits until the PHY interface (MII) has finished its last transfer.
 *
 * Input:
 *   none
 * Output:
 *   true when ready, false on time-out
 **********************************************************************/
static bool eth_mii_wait( void )
{
	uint32_t spins = ETH_SPIN_SHORT;

	while( eth_mac_read(ETH_MAC_MII_ACC) & MII_ACC_BUSY )
		if( --spins == 0 ) return false;
	return true;
}

/***********************************************************************
 * eth_phy_read (internal)
 * Reads a register of the PHY, the part of the chip that handles the
 * cable (link, speed, duplex). PHY registers are reached through two
 * MAC registers: MII_ACC selects the register, MII_DATA holds the data.
 *
 * Input:
 *   phy_register  PHY register number (ETH_MII_PHY_... in lan9221.h)
 * Output:
 *   the 16-bit register value
 **********************************************************************/
static uint16_t eth_phy_read( uint8_t phy_register )
{
	eth_mii_wait();
	eth_mac_write( ETH_MAC_MII_ACC,
		((uint32_t)ETH_PHY_ADDR << 11) | ((uint32_t)phy_register << 6) | MII_ACC_BUSY );
	eth_mii_wait();
	return (uint16_t)eth_mac_read( ETH_MAC_MII_DATA );
}

/***********************************************************************
 * eth_set_mac_address
 * Programs our MAC address into the chip's receive filter, so it passes
 * frames addressed to us. The A2560K has no EEPROM with an address, so
 * the driver must do this.
 * The datasheet layout for 12:34:56:78:9A:BC is ADDRL = 0x78563412,
 * ADDRH = 0x0000BC9A: the first byte on the wire is the lowest byte.
 *
 * Input:
 *   mac_addr  the 6-byte MAC address
 * Output:
 *   0
 **********************************************************************/
uint32_t eth_set_mac_address( mac *mac_addr )
{
	uint32_t addrl, addrh;

	addrl =	((uint32_t)mac_addr->addr[0])		|
			((uint32_t)mac_addr->addr[1] << 8)	|
			((uint32_t)mac_addr->addr[2] << 16)	|
			((uint32_t)mac_addr->addr[3] << 24);
	addrh =	((uint32_t)mac_addr->addr[4])		|
			((uint32_t)mac_addr->addr[5] << 8);

	eth_mac_write( ETH_MAC_ADDRH, addrh );
	eth_mac_write( ETH_MAC_ADDRL, addrl );
	return 0;
}

/***********************************************************************
 * eth_get_mac_address
 * Returns our MAC address.
 *
 * Input:
 *   none
 * Output:
 *   pointer to our 6-byte MAC address
 **********************************************************************/
mac *eth_get_mac_address( void )
{
	return &my_mac_address;
}

/***********************************************************************
 * eth_check_chip_errors (internal)
 * Counts and clears the chip's receiver/transmitter error flags. They
 * stay set until cleared, so without this one error would hide all
 * later ones.
 *
 * Input:
 *   none
 * Output:
 *   none (stats.chip_errors is updated)
 **********************************************************************/
static void eth_check_chip_errors( void )
{
	uint32_t sts = eth_reg_read( ETH_INT_STS );

	if( sts & (INT_STS_RXE | INT_STS_TXE) )
	{
		stats.chip_errors++;
		eth_reg_write( ETH_INT_STS, sts & (INT_STS_RXE | INT_STS_TXE) );	/* writing 1 clears */
	}
}

/***********************************************************************
 * eth_tx_status_drain (internal)
 * The chip leaves a status word for every frame it sent. This reads all
 * of them and counts the failed ones. (With TXSAO set the transmitter
 * keeps working even if we never read them, but the counts would be
 * lost.)
 *
 * Input:
 *   none
 * Output:
 *   none (stats.tx_errors is updated)
 **********************************************************************/
static void eth_tx_status_drain( void )
{
	uint32_t used = (eth_reg_read( ETH_TX_FIFO_INF ) >> 16) & 0xFF;

	while( used-- )
		if( eth_reg_read( ETH_TX_STATUS_FIFO ) & TX_STS_ES )
			stats.tx_errors++;
}

/***********************************************************************
 * eth_rx_discard (internal)
 * Throws away the data of a received frame we do not want. Every frame
 * we took the status word of MUST be read or thrown away completely:
 * otherwise the next "status word" we read would really be packet data
 * and all following frames would be garbage.
 *
 * Input:
 *   dwords   size of the frame data in 32-bit words
 * Output:
 *   none
 **********************************************************************/
static void eth_rx_discard( uint32_t dwords )
{
	if( dwords >= 4 )
	{
		/* Let the chip skip it ("fast-forward"); allowed from 4 words */
		eth_reg_write( ETH_RX_DP_CTL, RX_DP_CTL_FFWD );
		eth_wait_clear( ETH_RX_DP_CTL, RX_DP_CTL_FFWD, ETH_SPIN_SHORT );
	}
	else
	{
		while( dwords-- ) (void)lan_read32( RX_DATA_FIFO );
	}
}

/***********************************************************************
 * eth_rx_fifo_listen
 * Takes one received frame from the chip, if there is one. Frames with
 * errors and frames too big for the buffer are thrown away.
 *
 * Input:
 *   buffer         where to copy the frame (header first)
 *   buffer_length  size of buffer in bytes
 * Output:
 *   > 0  length of the frame in bytes (without the 4-byte checksum)
 *   0    no frame waiting
 *   -1   a frame arrived but was thrown away (see the statistics)
 **********************************************************************/
ssize_t eth_rx_fifo_listen( eth_frame *buffer, size_t buffer_length )
{
	uint32_t status, frame_len, data_len, dwords, stored, i, v;
	uint32_t full_dwords = buffer_length / 4;

	eth_check_chip_errors();

	/* RXSUSED: number of received frames (status words) waiting */
	if( ((eth_reg_read( ETH_RX_FIFO_INF ) >> 16) & 0xFF) == 0 )
		return 0;

	/* Reading the status word removes it: from here on the frame data
	 * must be read or discarded (see eth_rx_discard).
	 */
	status = eth_reg_read( ETH_RX_STATUS_FIFO );
	frame_len = (status >> RX_STS_LEN_SHIFT) & RX_STS_LEN_MASK;	/* includes the 4-byte checksum */
	dwords = (frame_len + 3) >> 2;

	if( (status & (RX_STS_ES | RX_STS_FILTER_FAIL)) ||
		frame_len < sizeof(eth_frame_header) + 4 )
	{
		stats.rx_errors++;
		eth_rx_discard( dwords );
		return -1;
	}

	data_len = frame_len - 4;
	if( data_len > buffer_length )
	{
		stats.rx_too_big++;
		eth_rx_discard( dwords );
		return -1;
	}

	/* The whole words that fit in the buffer go straight in. The rest
	 * (the frame's checksum at the end, or the padding of the last word)
	 * may not fit: of that, only what fits is stored.
	 */
	stored = dwords < full_dwords ? dwords : full_dwords;
	eth_fifo_read( buffer, stored );
	for( i = stored; i < dwords; i++ )
	{
		v = lan_read32( RX_DATA_FIFO );
		if( i * 4 < buffer_length )
			memcpy( (uint8_t *)buffer + i * 4, &v, buffer_length - i * 4 );
	}

	stats.rx_frames++;
	return (ssize_t)data_len;
}

/***********************************************************************
 * eth_tx_frame (internal)
 * Writes one complete frame into the TX FIFO; the chip sends it. The
 * chip adds the frame checksum and pads frames shorter than the
 * Ethernet minimum itself.
 *
 * Input:
 *   frame    the frame, starting with the destination MAC address
 *            (must start at an even address)
 *   length   frame length in bytes, without checksum
 * Output:
 *   0 on success, -1 if the frame was not sent (bad length, or the TX
 *   FIFO stayed full)
 **********************************************************************/
static int16_t eth_tx_frame( const uint8_t *frame, uint16_t length )
{
	uint32_t dwords = ((uint32_t)length + 3) >> 2;
	uint32_t needed = dwords * 4 + 8;		/* data + the two command words */
	uint32_t spins = ETH_SPIN_LONG;
	uint32_t cmd_a, cmd_b, whole, last;

	if( length == 0 || length > sizeof(eth_frame_header) + MTU )
		return -1;

	eth_tx_status_drain();

	/* Writing more than fits would corrupt the FIFO: wait for room.
	 * TDFREE (bits 15:0) = free bytes in the TX FIFO.
	 */
	while( (eth_reg_read( ETH_TX_FIFO_INF ) & 0xFFFF) < needed )
	{
		if( --spins == 0 )
		{
			stats.tx_no_space++;
			return -1;
		}
	}

	/* Each frame is preceded by two command words. We send every frame
	 * as one buffer: first and last segment at once.
	 */
	cmd_a = (1UL << TX_CMD_A_FS) | (1UL << TX_CMD_A_LS) | length;
	cmd_b = ((uint32_t)tx_tag++ << TX_CMD_B_PACKET_TAG) | length;
	eth_reg_write( TX_DATA_FIFO, cmd_a );	/* 32-bit values: register byte order */
	eth_reg_write( TX_DATA_FIFO, cmd_b );

	/* The data, in 32-bit words. The last word may be partial: copy it
	 * separately so we never read past the end of the frame.
	 */
	whole = (uint32_t)(length >> 2);
	eth_fifo_write( frame, whole );
	if( length & 3 )
	{
		last = 0;
		memcpy( &last, frame + (whole << 2), length & 3 );
		lan_write32( TX_DATA_FIFO, last );
	}

	stats.tx_frames++;
	return 0;
}

/***********************************************************************
 * eth_send_frame
 * Sends data as one Ethernet frame to a MAC address. The controller's
 * send_frame callback points here.
 *
 * Input:
 *   dest_mac      6-byte MAC address of the receiver
 *   ether_type    type of the contents: ETHER_TYPE_IP4 (0x0800),
 *                 ETHER_TYPE_ARP (0x0806, used for all ARP frames)
 *   data_to_send  the frame contents
 *   data_length   length of the contents in bytes (at most MTU)
 * Output:
 *   0 on success, -1 if the frame was not sent
 **********************************************************************/
int16_t eth_send_frame( const uint8_t *dest_mac, uint16_t ether_type,
							const void *data_to_send, uint16_t data_length )
{
	eth_frame *frame;
	int16_t result;

	if( data_length > MTU ) return -1;

	frame = (eth_frame *)utl_pool_get( frame_tx_pool );
	if( frame == NULL ) return -1;

	memcpy( frame->header.dest_addr, dest_mac, sizeof(mac) );
	memcpy( frame->header.src_addr, &my_mac_address, sizeof(mac) );
	frame->header.ether_type = ether_type;
	utl_copy( frame->data, data_to_send, data_length );

	{
		PROF_START( tx );
		result = eth_tx_frame( (uint8_t *)frame,
						(uint16_t)(sizeof(eth_frame_header) + data_length) );
		PROF_STOP( PROF_TX_FIFO, tx );
	}

	utl_pool_put( frame_tx_pool, frame );
	return result;
}

/***********************************************************************
 * eth_send_broadcast
 * Sends data as one Ethernet frame to every machine on the LAN
 * (destination FF:FF:FF:FF:FF:FF). The controller's send_broadcast
 * callback points here.
 *
 * Input:
 *   ether_type    type of the contents (see eth_send_frame)
 *   data_to_send  the frame contents
 *   data_length   length of the contents in bytes (at most MTU)
 * Output:
 *   none
 **********************************************************************/
void eth_send_broadcast( uint16_t ether_type, const void *data_to_send,
							uint16_t data_length )
{
	static const uint8_t broadcast_mac[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

	eth_send_frame( broadcast_mac, ether_type, data_to_send, data_length );
}

/***********************************************************************
 * eth_wait_ready (internal)
 * Waits until the chip reports it is ready, waking it up if it is in a
 * power-saving state.
 *
 * Input:
 *   none
 * Output:
 *   true when ready, false on time-out
 **********************************************************************/
static bool eth_wait_ready( void )
{
	if( eth_reg_read( ETH_PMT_CTRL ) & PMT_CTRL_READY ) return true;

	eth_reg_write( ETH_BYTE_TEST, 0 );		/* any write to BYTE_TEST wakes the chip */
	return eth_wait_set( ETH_PMT_CTRL, PMT_CTRL_READY, ETH_SPIN_LONG );
}

/***********************************************************************
 * eth_phy_update (internal)
 * Waits for the PHY to agree on speed and duplex with the switch on the
 * other end of the cable ("auto-negotiation") and stores the result.
 * The MAC must use the same duplex mode, or collisions and lost frames
 * follow. Without a cable this takes ETH_AUTONEG_TIMEOUT and we assume
 * half duplex, which works with any partner.
 *
 * Input:
 *   none
 * Output:
 *   none (stats.link_up, full_duplex and speed_100 are set)
 **********************************************************************/
static void eth_phy_update( void )
{
	uint32_t start = utl_jiffies();
	uint32_t spins = ETH_SPIN_LONG;		/* in case the jiffies do not run */
	uint16_t bsr, pscsr;

	do
	{
		bsr = eth_phy_read( ETH_MII_PHY_BSR );
		if( bsr & PHY_BSR_AN_DONE ) break;
	} while( (utl_jiffies() - start) < ETH_AUTONEG_TIMEOUT && --spins );

	/* The link bit remembers a lost link until read: read again for now */
	bsr = eth_phy_read( ETH_MII_PHY_BSR );
	stats.link_up = (bsr & PHY_BSR_LINK) != 0;

	if( bsr & PHY_BSR_AN_DONE )
	{
		/* PSCSR bits 4:2 = 001 10M half, 101 10M full, 010 100M half, 110 100M full */
		pscsr = eth_phy_read( ETH_MII_PHY_PSCSR );
		stats.full_duplex = (pscsr & (1U << 4)) != 0;
		stats.speed_100 = (pscsr & (1U << 3)) != 0;
	}
	else
	{
		stats.full_duplex = false;
		stats.speed_100 = false;
	}
}

/***********************************************************************
 * eth_init
 * Starts the LAN9221: resets it, sets our MAC address, waits for the
 * link and switches the transmitter and receiver on. Also fills in the
 * controller's description and callback functions.
 *
 * Input:
 *   this_controller  controller struct to fill in
 *   mac_address      our 6-byte MAC address
 * Output:
 *   0 on success, -1 if the chip does not respond, the bus byte order
 *   is wrong or there is not enough memory
 **********************************************************************/
int16_t eth_init( eth_controller *this_controller, mac *mac_address )
{
	uint32_t value, mac_cr;

	memset( &stats, 0, sizeof(stats) );
	tx_tag = 0;

	if( !eth_wait_ready() )
	{
		utl_print("\nETH_INIT: LAN9221 not ready");
		return -1;
	}

	/* BYTE_TEST always reads 0x87654321. Any other value means our
	 * register access is wrong, and nothing else would work either.
	 */
	value = eth_reg_read( ETH_BYTE_TEST );
	if( value != 0x87654321UL )
	{
		utl_print("\nETH_INIT: endian error reading BYTE_TEST register.");
		utl_print("\nExpected 0x87654321, but we got 0x%08lX", (unsigned long)value );
		return -1;
	}

	/* Start from a known state, whatever ran before us (this also
	 * resets the PHY, which restarts auto-negotiation).
	 */
	eth_reg_write( ETH_HW_CFG, HW_CFG_SRST );
	if( !eth_wait_clear( ETH_HW_CFG, HW_CFG_SRST, ETH_SPIN_LONG ) || !eth_wait_ready() )
	{
		utl_print("\nETH_INIT: soft reset time-out");
		return -1;
	}

	/* The datasheet requires HW_CFG bit 20 to be 1; keep the other bits */
	eth_reg_write( ETH_HW_CFG, eth_reg_read( ETH_HW_CFG ) | HW_CFG_MBO );

	/* FELINC polls, so no interrupts; clear any old status bits */
	eth_reg_write( ETH_INT_EN, 0 );
	eth_reg_write( ETH_IRQ_CFG, 0 );
	eth_reg_write( ETH_INT_STS, 0xFFFFFFFFUL );

	value = eth_reg_read( ETH_ID_REV ) >> 16;		/* chip ID, 0x9221 */
	sprintf( this_controller->device_id, "%04lX", (unsigned long)value );

	memcpy( &my_mac_address, mac_address, sizeof(mac) );
	eth_set_mac_address( mac_address );
	memcpy( &(this_controller->mac_addr), mac_address, sizeof(mac) );

	eth_phy_update();

	/* Not promiscuous: the chip only passes frames for our MAC address
	 * and broadcasts, so the 68040 does not have to look at all traffic
	 * on the LAN. ETH_PROMISCUOUS=1 receives everything (for debugging).
	 */
	mac_cr = MAC_CR_TXEN | MAC_CR_RXEN;
	if( stats.full_duplex ) mac_cr |= MAC_CR_FDPX;
#if ETH_PROMISCUOUS
	mac_cr |= MAC_CR_PRMS | MAC_CR_MCPAS;
#endif
	eth_mac_write( ETH_MAC_MAC_CR, mac_cr );

	/* TXSAO: the transmitter keeps working even if the TX status FIFO
	 * fills up, so a forgotten status word can never stop sending.
	 */
	eth_reg_write( ETH_TX_CFG, TX_CFG_TX_ON | TX_CFG_TXSAO );

	eth_reg_write( ETH_GPIO_CFG, 0x70000000UL );	/* green and yellow LEDs on the connector */

	utl_print("\nETH_INIT: LAN%s, link %s, %s Mbps, %s duplex, MAC filter H=%04lX L=%08lX\n",
		this_controller->device_id,
		stats.link_up ? "up" : "DOWN",
		stats.speed_100 ? "100" : "10",
		stats.full_duplex ? "full" : "half",
		(unsigned long)eth_mac_read( ETH_MAC_ADDRH ),
		(unsigned long)eth_mac_read( ETH_MAC_ADDRL ) );

	/* Last, so a start that fails above leaves no memory taken */
	frame_tx_pool = utl_pool_create( sizeof(eth_frame), 2 );
	if( frame_tx_pool == NULL )
	{
		utl_print("\nETH_INIT: not enough memory");
		eth_shutdown();
		return -1;
	}

	this_controller->listen = eth_rx_fifo_listen;
	this_controller->send_frame = eth_send_frame;
	this_controller->send_broadcast = eth_send_broadcast;
	return 0;
}

/***********************************************************************
 * eth_shutdown
 * Stops the controller: the frame being sent is finished, then sending
 * and receiving are switched off, and the send buffers are given back.
 * The link stays up (the PHY is not touched), so eth_init() can start
 * the chip again later.
 *
 * Input:
 *   none
 * Output:
 *   none
 **********************************************************************/
void eth_shutdown( void )
{
	/* STOP_TX lets the transmitter finish its frame; TX_ON goes to 0
	 * when it has stopped.
	 */
	eth_reg_write( ETH_TX_CFG, eth_reg_read( ETH_TX_CFG ) | TX_CFG_STOP_TX );
	eth_wait_clear( ETH_TX_CFG, TX_CFG_TX_ON, ETH_SPIN_LONG );

	eth_mac_write( ETH_MAC_MAC_CR,
		eth_mac_read( ETH_MAC_MAC_CR ) & ~(MAC_CR_TXEN | MAC_CR_RXEN) );

	utl_pool_destroy( frame_tx_pool );
	frame_tx_pool = NULL;
}

/***********************************************************************
 * eth_get_stats
 * Returns the driver statistics (frames, errors, link state).
 *
 * Input:
 *   none
 * Output:
 *   pointer to the statistics
 **********************************************************************/
const eth_stats *eth_get_stats( void )
{
	/* RX_DROP counts frames the chip had to drop because its RX FIFO
	 * was full. Reading it resets it to 0, so we add it up.
	 */
	stats.rx_dropped += eth_reg_read( ETH_RX_DROP );
	return &stats;
}

/***********************************************************************
 * eth_print_stats
 * Prints the driver statistics on the screen.
 *
 * Input:
 *   none
 * Output:
 *   none
 **********************************************************************/
void eth_print_stats( void )
{
	const eth_stats *s = eth_get_stats();

	utl_print("\n*** LAN9221: link %s, %s Mbps, %s duplex",
		s->link_up ? "up" : "DOWN", s->speed_100 ? "100" : "10",
		s->full_duplex ? "full" : "half");
	utl_print("\n    RX frames %lu, errors %lu, too big %lu, dropped by chip %lu",
		(unsigned long)s->rx_frames, (unsigned long)s->rx_errors,
		(unsigned long)s->rx_too_big, (unsigned long)s->rx_dropped);
	utl_print("\n    TX frames %lu, errors %lu, FIFO full %lu, chip errors %lu\n",
		(unsigned long)s->tx_frames, (unsigned long)s->tx_errors,
		(unsigned long)s->tx_no_space, (unsigned long)s->chip_errors);
}

/***********************************************************************
 * mac_string_to_mac
 * Reads a MAC address from text like "00:0D:80:61:94:AB".
 * Note: the text is changed while it is read (strtok puts 0 bytes in
 * it), so pass a copy if you still need the text afterwards.
 *
 * Input:
 *   my_mac      where to store the 6-byte MAC address
 *   mac_string  the text
 * Output:
 *   0 on success, 1 if the text does not hold 6 parts
 **********************************************************************/
uint8_t mac_string_to_mac( mac *my_mac, char *mac_string )
{
	char *token;
	uint8_t i = 0;

	if( mac_string == NULL ) return 1;

	token = strtok( mac_string, ":" );
	while( i < 6 && token != NULL )
	{
		my_mac->addr[i++] = (uint8_t)utl_hex_to_uint32( token );
		token = strtok( NULL, ":" );
	}
	return (i == 6) ? 0 : 1;
}

/***********************************************************************
 * eth_install_interrupt
 * EXPERIMENTAL, NOT USED YET, UNTESTED.
 * Makes the chip raise an interrupt when a frame has arrived, as an
 * alternative to polling. On the K25 the LAN interrupt is GAVIN
 * interrupt 30 (level 3, vector $4E). GAVIN reacts to the rising edge
 * of the pin, so the chip's interrupt output is set to active high.
 * GAVIN gives one interrupt per edge: the handler must take ALL waiting
 * frames, then clear INT_STS and GAVIN_PENDING_GRP1.
 *
 * Input:
 *   interrupt_handler  the function to call on the interrupt
 * Output:
 *   none
 **********************************************************************/
void eth_install_interrupt( void (*interrupt_handler)(void) )
{
	uint16_t value;

	eth_reg_write( ETH_IRQ_CFG, 0 );
	utl_poke32( LAN_IRQ_VECTOR_ADDR, (uint32_t)interrupt_handler );

	eth_reg_write( ETH_FIFO_INT, 0 );				/* interrupt from the first frame on */
	eth_reg_write( ETH_INT_STS, 0xFFFFFFFFUL );
	eth_reg_write( ETH_INT_EN, INT_EN_RSFL );
	eth_reg_write( ETH_IRQ_CFG, IRQ_CFG_IRQ_EN | IRQ_CFG_IRQ_POL | IRQ_CFG_IRQ_TYPE );

	/* Through a local copy: Calypsi turns "REG = REG | bit" on a
	 * volatile register into a one-byte bset/bclr, and a byte write
	 * may be ignored by the hardware. This way the whole word is
	 * read and written.
	 */
	value = GAVIN_EDGE_GRP1;
	GAVIN_EDGE_GRP1 = value | GAVIN_LAN_BIT;			/* edge triggered */
	value = GAVIN_POL_GRP1;
	GAVIN_POL_GRP1 = value | GAVIN_LAN_BIT;				/* on the rising edge */
	GAVIN_PENDING_GRP1 = GAVIN_LAN_BIT;					/* forget an old event */
	value = GAVIN_MASK_GRP1;
	GAVIN_MASK_GRP1 = value & ~GAVIN_LAN_BIT;			/* enable */
}
