/***********************************************************************
 * LAN9221.H - driver for the SMSC LAN9221 Ethernet controller in the
 * Foenix A2560K (K25), see lan9221.c
 *
 * How the A2560K connects the chip (K25 hardware reference, chapter
 * "Ethernet (LAN9221) $FEC0_0600", checked on the real board):
 * - $FEC0_0600-$FEC0_06FF: the chip's registers and FIFOs
 *   ($FEC0_0700-$FEC0_07FF is the same chip with FIFO_SEL high: every
 *   access goes to the data FIFOs; not used by this driver)
 * - the chip has a 16-bit bus, so a 32-bit register takes two 16-bit
 *   accesses. The hardware reference says a 32-bit CPU access does both,
 *   but on the real board it reads the first half twice, so the driver
 *   does the two accesses itself.
 * - the FPGA swaps the two bytes inside every 16-bit access. As a result
 *   FIFO data arrives in network byte order (nothing to do), while
 *   32-bit control registers arrive byte-reversed (the driver swaps
 *   them back).
 * - the FPGA keeps the chip's timing rules itself, so the datasheet's
 *   dummy BYTE_TEST reads between accesses are not needed.
 *
 * Part of FELINC, the Foenix Labs Ethernet Library in C. (c) R. Kint
 **********************************************************************/
#ifndef __LAN9221_H__
#define __LAN9221_H__

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#include "felinc.h"		/* ssize_t */
#include "ethernet.h"

/* Ethernet frame types (the ether_type field) */
#define ETHER_TYPE_ARP	0x0806	/* EtherType: ARP (questions and answers) */
#define ETHER_TYPE_IP4	0x0800	/* EtherType: IPv4 */

#define ETH_BASE		0xFEC00600	/* A2560K; other Foenix machines may differ */

/* 1 = receive every frame on the LAN (for debugging). Normally 0: the
 * chip only passes frames for our MAC address and broadcasts.
 */
#ifndef ETH_PROMISCUOUS
#define ETH_PROMISCUOUS	0
#endif

/* LAN9221 registers and FIFOs (addresses relative to ETH_BASE) */
#define RX_DATA_FIFO 	0x00   	/* start of range - all the way up to $1f */
#define TX_DATA_FIFO	0x20   	/* start of range - all the way up to $3f */

#define ETH_RX_STATUS_FIFO 	0x40
#define ETH_RX_STATUS_PEEK	0x44
#define ETH_TX_STATUS_FIFO 	0x48
#define ETH_TX_STATUS_PEEK	0x4c

#define ETH_ID_REV      0x50
#define ETH_IRQ_CFG     0x54
#define ETH_INT_STS     0x58
#define ETH_INT_EN      0x5c
/* RESERVED       = $60 */
#define ETH_BYTE_TEST   0x64
#define ETH_FIFO_INT    0x68
#define ETH_RX_CFG      0x6c
#define ETH_TX_CFG      0x70
#define ETH_HW_CFG      0x74
#define ETH_RX_DP_CTL   0x78
#define ETH_RX_FIFO_INF 0x7c
#define ETH_TX_FIFO_INF 0x80
#define ETH_PMT_CTRL    0x84
#define ETH_GPIO_CFG    0x88
#define ETH_GPT_CFG     0x8c
#define ETH_GPT_CNT     0x90
/* RESERVED       = $94 */
#define ETH_WORD_SWAP   0x98
#define ETH_FREE_RUN    0x9c
#define ETH_RX_DROP     0xa0
#define ETH_MAC_CSR_CMD 0xa4
#define ETH_MAC_CSR_DATA	0xa8
#define ETH_AFC_CFG     0xac
#define ETH_E2P_CMD     0xb0
#define ETH_E2P_DATA    0xb4
/* RESERVED       = $b8 - $fc */

/* Register bits we use */
#define HW_CFG_SRST			(1UL<<0)	/* soft reset, self clearing */
#define HW_CFG_MBO			(1UL<<20)	/* must be one */
#define PMT_CTRL_READY		(1UL<<0)
#define TX_CFG_STOP_TX		(1UL<<0)	/* stop after the current frame, self clearing */
#define TX_CFG_TX_ON		(1UL<<1)
#define TX_CFG_TXSAO		(1UL<<2)	/* TX status FIFO may overrun (never stalls TX) */
#define RX_DP_CTL_FFWD		(1UL<<31)	/* fast-forward (discard) current RX frame */
#define INT_STS_TXE			(1UL<<13)	/* transmitter error */
#define INT_STS_RXE			(1UL<<14)	/* receiver error */
#define IRQ_CFG_IRQ_TYPE	(1UL<<0)	/* 1 = push-pull */
#define IRQ_CFG_IRQ_POL		(1UL<<4)	/* 1 = active high */
#define IRQ_CFG_IRQ_EN		(1UL<<8)
#define INT_EN_RSFL			(1UL<<3)	/* RX status FIFO level */

/* RX status word */
#define RX_STS_FILTER_FAIL	(1UL<<30)
#define RX_STS_LEN_SHIFT	16
#define RX_STS_LEN_MASK		0x3FFFUL
#define RX_STS_ES			(1UL<<15)	/* error summary */

/* TX status word */
#define TX_STS_ES			(1UL<<15)	/* error summary */

/* ETH_MAC module registers       */
/* Used indirectly through the    */
/* ETH_MAC_CSR_CMD/DATA registers */
#define ETH_MAC_MAC_CR  0x1
#define ETH_MAC_ADDRH   0x2
#define ETH_MAC_ADDRL   0x3
#define ETH_MAC_HASHH   0x4
#define ETH_MAC_HASHL   0x5
#define ETH_MAC_MII_ACC 0x6
#define ETH_MAC_MII_DATA	0x7
#define ETH_MAC_FLOW    0x8
#define ETH_MAC_VLAN1   0x9
#define ETH_MAC_VLAN2   0xa
#define ETH_MAC_WUFF    0xb
#define ETH_MAC_WUCSR   0xc
#define ETH_MAC_COE_CR  0xd
#define ETH_MAC_MAX     0xe

/* MAC_CR bits */
#define MAC_CR_RXEN			(1UL<<2)
#define MAC_CR_TXEN			(1UL<<3)
#define MAC_CR_PRMS			(1UL<<18)	/* promiscuous */
#define MAC_CR_MCPAS		(1UL<<19)	/* pass all multicast */
#define MAC_CR_FDPX			(1UL<<20)	/* full duplex */

/* PHY registers - in decimal! Just like the datasheet      */
/* these must be accessed through ETH_MAC_MII_ACC           */
/* for reading and writing (data in/from ETH_MAC_MII_DATA   */
/* The PHY registers are 16 bit; the internal PHY address is 1 */
#define ETH_PHY_ADDR		1
#define ETH_MII_PHY_BCR		0
#define ETH_MII_PHY_BSR		1
#define ETH_MII_PHY_ID1		2
#define ETH_MII_PHY_ID2		3
#define ETH_MII_PHY_ANAR	4
#define ETH_MII_PHY_ANLPAR	5
#define ETH_MII_PHY_ANER	6
#define ETH_MII_PHY_MCSR	17
#define ETH_MII_PHY_SMR		18
#define ETH_MII_PHY_SCSI	27
#define ETH_MII_PHY_ISR		29
#define ETH_MII_PHY_IMR		30
#define ETH_MII_PHY_PSCSR	31

#define PHY_BSR_LINK		(1U<<2)
#define PHY_BSR_AN_DONE		(1U<<5)

/* Starting bit numbers of the fields in TX_CMD_A */
#define TX_CMD_A_INT_ON_COMPLETE    31
#define TX_BUFFER_END_ALIGNMENT     24
#define TX_CMD_A_DATA_START_OFFSET  16
#define TX_CMD_A_FS                 13
#define TX_CMD_A_LS                 12
#define TX_CMD_A_BUFSIZE            0

/* Starting bit numbers of the fields in TX_CMD_B */
#define TX_CMD_B_PACKET_TAG         16
#define TX_CMD_B_CHECKSUM_ENABLE    14
#define TX_CMD_B_ADD_CRC_DISABLE    13
#define TX_CMD_B_DISABLE_FRAME_PAD  12
#define TX_CMD_B_PACKET_LENGTH      0

/* Driver statistics, read with eth_get_stats() */
typedef struct eth_stats
{
	uint32_t rx_frames;		/* good frames handed to the stack */
	uint32_t rx_errors;		/* frames with error status (discarded) */
	uint32_t rx_too_big;	/* frames larger than the caller's buffer (discarded) */
	uint32_t rx_dropped;	/* frames the chip dropped (RX_DROP, FIFO full) */
	uint32_t tx_frames;		/* frames written to the TX FIFO */
	uint32_t tx_errors;		/* TX status words with error summary set */
	uint32_t tx_no_space;	/* frames not sent: TX FIFO stayed full */
	uint32_t chip_errors;	/* INT_STS RXE / TXE events */
	bool link_up;
	bool full_duplex;
	bool speed_100;
} eth_stats;

int16_t eth_init( eth_controller *this_controller, mac *mac_address );
void eth_shutdown( void );
uint32_t eth_set_mac_address( mac *mac_address );
mac *eth_get_mac_address( void );

ssize_t eth_rx_fifo_listen( eth_frame *buffer,
								size_t return_buffer_length );
int16_t eth_send_frame( const uint8_t *dest_mac, uint16_t ether_type,
							const void *data_to_send, uint16_t data_length );
void eth_send_broadcast( uint16_t ether_type, const void *data_to_send,
							uint16_t data_length );
uint8_t mac_string_to_mac( mac *my_mac, char *mac_string );

const eth_stats *eth_get_stats( void );
void eth_print_stats( void );

/* Experimental, not used yet */
void eth_install_interrupt( void (*interrupt_handler)(void) );
#endif
