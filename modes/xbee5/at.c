/*
	libxbee - a C library to aid the use of Digi's XBee wireless modules
	          running in API mode.

	Copyright (C) 2009 onwards  Attie Grande (attie@attie.co.uk)

	libxbee is free software: you can redistribute it and/or modify it
	under the terms of the GNU Lesser General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	libxbee is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU Lesser General Public License for more details.

	You should have received a copy of the GNU Lesser General Public License
	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../internal.h"
#include "../../xbee_int.h"
#include "../../mode.h"
#include "../../pkt.h"
#include "../../frame.h"
#include "../common.h"
#include "at.h"
#include "io.h"

xbee_err xbee_s5_at_rx_func(struct xbee *xbee, void *arg, unsigned char identifier, struct xbee_buf *buf, struct xbee_frameInfo *frameInfo, struct xbee_conAddress *address, struct xbee_pkt **pkt) {
	struct xbee_pkt *iPkt;
	xbee_err ret;
	int addrLen;
	
	if (!xbee || !frameInfo || !buf || !address || !pkt) return XBEE_EMISSINGPARAM;
	
	if (buf->len < 1) return XBEE_ELENGTH;
	
	switch (buf->data[0]) {
		case 0x88: addrLen = 0;  break; /* Local AT  */
		case 0x97: addrLen = 10; break; /* Remote AT */
		default: return XBEE_EINVAL;
	}
	
	if (buf->len < addrLen + 5) return XBEE_ELENGTH;
	
	frameInfo->active = 1;
	frameInfo->id = buf->data[1];
	frameInfo->retVal = buf->data[addrLen + 4];
	
	if (frameInfo->retVal != 0) {
		*pkt = NULL;
		return XBEE_ENONE;
	}
	
	if ((ret = xbee_pktAlloc(&iPkt, NULL, buf->len - 5)) != XBEE_ENONE) return ret;
	iPkt->frameId = frameInfo->id;
	
	if (addrLen == 10) {
		address->addr64_enabled = 1;
		memcpy(address->addr64, &(buf->data[2]), 8);
	}
	
	/* (... - 5) + 2 -> API Identifier + Frame ID + Status */
	iPkt->dataLen = (buf->len - addrLen - 5);
	
	memcpy(iPkt->atCommand, &(buf->data[addrLen + 2]) , 2); /* copy in the AT command */
	if (iPkt->dataLen > 0) {
		memcpy(iPkt->data, &(buf->data[addrLen + 5]), iPkt->dataLen); /* copy in the response value (if any) */
	}
	iPkt->data[iPkt->dataLen] = '\0';
	
	if (!strncasecmp((char*)iPkt->atCommand, "IS", 2)) {
		xbee_s5_io_parseInputs(xbee, iPkt, iPkt->data, iPkt->dataLen);
	}
	
	*pkt = iPkt;
	
	return XBEE_ENONE;
}

xbee_err xbee_s5_at_tx_func(struct xbee *xbee, struct xbee_con *con, void *arg, unsigned char identifier, unsigned char frameId, struct xbee_conAddress *address, struct xbee_conSettings *settings, const unsigned char *buf, int len, struct xbee_buf **oBuf) {
	struct xbee_buf *iBuf;
	size_t bufLen;
	int pos;
	size_t memSize;
	
	if (!xbee || !address || !buf || !oBuf) return XBEE_EMISSINGPARAM;
	
	if (len < 2) return XBEE_ELENGTH; /* must have the AT command... */
	if (len > 22) return XBEE_ELENGTH; /* 22 is max NI + 2 */
	
	switch (identifier) {
		case 0x08: /* Local AT */
			if (settings->queueChanges) identifier = 0x09;
			break;
		case 0x17: /* Remote AT */
			if (!address->addr64_enabled) return XBEE_EINVAL;
			break;
		default: return XBEE_EINVAL;
	}
	
	memSize = 2;
	if (identifier == 0x17) memSize += 8 + 2 + 1; /* 64bit, 16bit and options */
	memSize += len;
	bufLen = memSize;
	
	memSize += sizeof(*iBuf);
	
	if ((iBuf = malloc(memSize)) == NULL) return XBEE_ENOMEM;
	
	pos = 0;
	iBuf->len = bufLen;
	iBuf->data[pos] = identifier;                          pos++;
	iBuf->data[pos] = frameId;                             pos++;
	if (identifier == 0x17) {
		memcpy(&(iBuf->data[pos]), address->addr64, 8);      pos += 8;
		iBuf->data[pos] = 0xFF;                              pos++;
		iBuf->data[pos] = 0xFE;                              pos++;
		iBuf->data[pos] = 0x00;
		if (!settings->queueChanges) {
			iBuf->data[pos] |= 0x02;
		}
		                                                     pos++;
	}
	
	memcpy(&(iBuf->data[pos]), buf, len);                  pos += len;
	iBuf->data[pos] = '\0';
	
	*oBuf = iBuf;
	
	return XBEE_ENONE;
}

/* ######################################################################### */

struct xbee_modeDataHandlerRx xbee_s5_localAt_rx  = {
	.identifier = 0x88,
	.func = xbee_s5_at_rx_func,
};
struct xbee_modeDataHandlerTx xbee_s5_localAt_tx  = {
	.identifier = 0x08,
	.func = xbee_s5_at_tx_func,
};
struct xbee_modeConType xbee_s5_localAt = {
	.name = "Local AT",
	.allowFrameId = 1,
	.useTimeout = 1,
	.timeout = {
		.tv_sec = 0,
		.tv_nsec = 250000000,
	},
	.addressRules = ADDR_NONE,
	.rxHandler = &xbee_s5_localAt_rx,
	.txHandler = &xbee_s5_localAt_tx,
};

/* ######################################################################### */

struct xbee_modeDataHandlerRx xbee_s5_remoteAt_rx  = {
	.identifier = 0x97,
	.func = xbee_s5_at_rx_func,
};
struct xbee_modeDataHandlerTx xbee_s5_remoteAt_tx  = {
	.identifier = 0x17,
	.func = xbee_s5_at_tx_func,
};
struct xbee_modeConType xbee_s5_remoteAt = {
	.name = "Remote AT",
	.allowFrameId = 1,
	.useTimeout = 1,
	.timeout = {
		.tv_sec = 0,
		.tv_nsec = 750000000,
	},
	.addressRules = ADDR_64_16OPT_NOEP,
	.rxHandler = &xbee_s5_remoteAt_rx,
	.txHandler = &xbee_s5_remoteAt_tx,
};
