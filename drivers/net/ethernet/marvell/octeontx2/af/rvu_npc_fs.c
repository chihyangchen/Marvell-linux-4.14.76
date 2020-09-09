// SPDX-License-Identifier: GPL-2.0
/* Marvell OcteonTx2 RVU Admin Function driver
 *
 * Copyright (C) 2019 Marvell International Ltd.
 */

#include <linux/bitfield.h>

#include "rvu_struct.h"
#include "rvu_reg.h"
#include "rvu.h"
#include "npc.h"

#define NPC_BYTESM		GENMASK_ULL(19, 16)
#define NPC_HDR_OFFSET		GENMASK_ULL(15, 8)
#define NPC_KEY_OFFSET		GENMASK_ULL(5, 0)
#define NPC_LDATA_EN		BIT_ULL(7)

static const char * const npc_flow_names[] = {
	[NPC_DMAC]	= "dmac",
	[NPC_SMAC]	= "smac",
	[NPC_ETYPE]	= "ether type",
	[NPC_OUTER_VID]	= "outer vlan id",
	[NPC_TOS]	= "tos",
	[NPC_SIP_IPV4]	= "ipv4 source ip",
	[NPC_DIP_IPV4]	= "ipv4 destination ip",
	[NPC_SIP_IPV6]	= "ipv6 source ip",
	[NPC_DIP_IPV6]	= "ipv6 destination ip",
	[NPC_SPORT_TCP]	= "tcp source port",
	[NPC_DPORT_TCP]	= "tcp destination port",
	[NPC_SPORT_UDP]	= "udp source port",
	[NPC_DPORT_UDP]	= "udp destination port",
	[NPC_UNKNOWN]	= "unknown",
};

const char *npc_get_field_name(u8 hdr)
{
	if (hdr >= ARRAY_SIZE(npc_flow_names))
		return npc_flow_names[NPC_UNKNOWN];

	return npc_flow_names[hdr];
}

/* Compute keyword masks and figure out the number of keywords a field
 * spans in the key.
 */
static void npc_set_kw_masks(struct npc_mcam *mcam, enum key_fields type,
			     u8 nr_bits, int start_kwi, int offset, u8 intf)
{
	struct npc_key_field *field = &mcam->rx_key_fields[type];
	u8 bits_in_kw;
	int max_kwi;

	if (mcam->banks_per_entry == 1)
		max_kwi = 1; /* NPC_MCAM_KEY_X1 */
	else if (mcam->banks_per_entry == 2)
		max_kwi = 3; /* NPC_MCAM_KEY_X2 */
	else
		max_kwi = 6; /* NPC_MCAM_KEY_X4 */

	if (intf == NIX_INTF_TX)
		field = &mcam->tx_key_fields[type];

	if (offset + nr_bits <= 64) {
		/* one KW only */
		if (start_kwi > max_kwi)
			return;
		field->kw_mask[start_kwi] |= (BIT_ULL(nr_bits) - 1) << offset;
		field->nr_kws = 1;
	} else if (offset + nr_bits > 64 &&
		   offset + nr_bits <= 128) {
		/* two KWs */
		if (start_kwi + 1 > max_kwi)
			return;
		/* first KW mask */
		bits_in_kw = 64 - offset;
		field->kw_mask[start_kwi] |= (BIT_ULL(bits_in_kw) - 1)
						<< offset;
		/* second KW mask i.e. mask for rest of bits */
		bits_in_kw = nr_bits + offset - 64;
		field->kw_mask[start_kwi + 1] |= BIT_ULL(bits_in_kw) - 1;
		field->nr_kws = 2;
	} else {
		/* three KWs */
		if (start_kwi + 2 > max_kwi)
			return;
		/* first KW mask */
		bits_in_kw = 64 - offset;
		field->kw_mask[start_kwi] |= (BIT_ULL(bits_in_kw) - 1)
						<< offset;
		/* second KW mask */
		field->kw_mask[start_kwi + 1] = ~0ULL;
		/* third KW mask i.e. mask for rest of bits */
		bits_in_kw = nr_bits + offset - 128;
		field->kw_mask[start_kwi + 2] |= BIT_ULL(bits_in_kw) - 1;
		field->nr_kws = 3;
	}
}

/* Helper function to figure out whether field exists in the key */
static bool npc_is_field_present(struct rvu *rvu, enum key_fields type, u8 intf)
{
	struct npc_mcam *mcam = &rvu->hw->mcam;
	struct npc_key_field *input;

	input  = &mcam->rx_key_fields[type];
	if (intf == NIX_INTF_TX)
		input  = &mcam->tx_key_fields[type];

	return input->nr_kws > 0;
}

static bool npc_is_same(struct npc_key_field *input,
			struct npc_key_field *field)
{
	int ret;

	ret = memcmp(&input->layer_mdata, &field->layer_mdata,
		     sizeof(struct npc_layer_mdata));
	return ret == 0;
}

static void npc_set_layer_mdata(struct npc_mcam *mcam, enum key_fields type,
				u64 cfg, u8 lid, u8 lt, u8 intf)
{
	struct npc_key_field *input = &mcam->rx_key_fields[type];

	if (intf == NIX_INTF_TX)
		input = &mcam->tx_key_fields[type];

	input->layer_mdata.hdr = FIELD_GET(NPC_HDR_OFFSET, cfg);
	input->layer_mdata.key = FIELD_GET(NPC_KEY_OFFSET, cfg);
	input->layer_mdata.len = FIELD_GET(NPC_BYTESM, cfg) + 1;
	input->layer_mdata.ltype = lt;
	input->layer_mdata.lid = lid;
}

static bool npc_check_overlap_fields(struct npc_key_field *input1,
				     struct npc_key_field *input2)
{
	int kwi;

	/* Fields with same layer id and different ltypes are mutually
	 * exclusive hence they can be overlapped
	 */
	if (input1->layer_mdata.lid == input2->layer_mdata.lid &&
	    input1->layer_mdata.ltype != input2->layer_mdata.ltype)
		return false;

	for (kwi = 0; kwi < NPC_MAX_KWS_IN_KEY; kwi++) {
		if (input1->kw_mask[kwi] & input2->kw_mask[kwi])
			return true;
	}

	return false;
}

/* Helper function to check whether given field overlaps with any other fields
 * in the key. Due to limitations on key size and the key extraction profile in
 * use higher layers can overwrite lower layer's header fields. Hence overlap
 * needs to be checked.
 */
static bool npc_check_overlap(struct rvu *rvu, int blkaddr,
			      enum key_fields type, u8 start_lid, u8 intf)
{
	struct npc_mcam *mcam = &rvu->hw->mcam;
	struct npc_key_field *dummy, *input;
	int start_kwi, offset;
	u8 nr_bits, lid, lt, ld;
	u64 cfg;

	dummy = &mcam->rx_key_fields[NPC_UNKNOWN];
	input = &mcam->rx_key_fields[type];

	if (intf == NIX_INTF_TX) {
		dummy = &mcam->tx_key_fields[NPC_UNKNOWN];
		input = &mcam->tx_key_fields[type];
	}

	for (lid = start_lid; lid < NPC_MAX_LID; lid++) {
		for (lt = 0; lt < NPC_MAX_LT; lt++) {
			for (ld = 0; ld < NPC_MAX_LD; ld++) {
				cfg = rvu_read64(rvu, blkaddr,
						 NPC_AF_INTFX_LIDX_LTX_LDX_CFG
						 (intf, lid, lt, ld));
				if (!FIELD_GET(NPC_LDATA_EN, cfg))
					continue;
				memset(dummy, 0, sizeof(struct npc_key_field));
				npc_set_layer_mdata(mcam, NPC_UNKNOWN, cfg,
						    lid, lt, intf);
				/* exclude input */
				if (npc_is_same(input, dummy))
					continue;
				start_kwi = dummy->layer_mdata.key / 8;
				offset = (dummy->layer_mdata.key * 8) % 64;
				nr_bits = dummy->layer_mdata.len * 8;
				/* form KW masks */
				npc_set_kw_masks(mcam, NPC_UNKNOWN, nr_bits,
						 start_kwi, offset, intf);
				/* check any input field bits falls in any
				 * other field bits.
				 */
				if (npc_check_overlap_fields(dummy, input))
					return true;
			}
		}
	}

	return false;
}

static int npc_check_field(struct rvu *rvu, int blkaddr, enum key_fields type,
			   u8 intf)
{
	if (!npc_is_field_present(rvu, type, intf) ||
	    npc_check_overlap(rvu, blkaddr, type, 0, intf))
		return -ENOTSUPP;
	return 0;
}

static void npc_scan_parse_result(struct npc_mcam *mcam, u8 bit_number,
				  u8 key_nibble, u8 intf)
{
	u8 offset = (key_nibble * 4) % 64; /* offset within key word */
	u8 kwi = (key_nibble * 4) / 64; /* which word in key */
	u8 nr_bits = 4; /* bits in a nibble */
	u8 type;

	switch (bit_number) {
	case 0 ... 2:
		type = NPC_CHAN;
		break;
	case 3:
		type = NPC_ERRLEV;
		break;
	case 4 ... 5:
		type = NPC_ERRCODE;
		break;
	case 6:
		type = NPC_LXMB;
		break;
	/* check for LTYPE only as of now */
	case 9:
		type = NPC_LA;
		break;
	case 12:
		type = NPC_LB;
		break;
	case 15:
		type = NPC_LC;
		break;
	case 18:
		type = NPC_LD;
		break;
	case 21:
		type = NPC_LE;
		break;
	case 24:
		type = NPC_LF;
		break;
	case 27:
		type = NPC_LG;
		break;
	case 30:
		type = NPC_LH;
		break;
	default:
		return;
	};
	npc_set_kw_masks(mcam, type, nr_bits, kwi, offset, intf);
}

static void npc_handle_multi_layer_fields(struct rvu *rvu, int blkaddr, u8 intf)
{
	struct npc_mcam *mcam = &rvu->hw->mcam;
	struct npc_key_field *key_fields;
	/* Ether type can come from three layers
	 * (ethernet, single tagged, double tagged)
	 */
	struct npc_key_field *etype_ether;
	struct npc_key_field *etype_tag1;
	struct npc_key_field *etype_tag2;
	/* Outer VLAN TCI can come from two layers
	 * (single tagged, double tagged)
	 */
	struct npc_key_field *vlan_tag1;
	struct npc_key_field *vlan_tag2;
	u64 *features;
	u8 start_lid;
	int i;

	key_fields = mcam->rx_key_fields;
	features = &mcam->rx_features;

	if (intf == NIX_INTF_TX) {
		key_fields = mcam->tx_key_fields;
		features = &mcam->tx_features;
	}

	/* Handle header fields which can come from multiple layers like
	 * etype, outer vlan tci. These fields should have same position in
	 * the key otherwise to install a mcam rule more than one entry is
	 * needed which complicates mcam space management.
	 */
	etype_ether = &key_fields[NPC_ETYPE_ETHER];
	etype_tag1 = &key_fields[NPC_ETYPE_TAG1];
	etype_tag2 = &key_fields[NPC_ETYPE_TAG2];
	vlan_tag1 = &key_fields[NPC_VLAN_TAG1];
	vlan_tag2 = &key_fields[NPC_VLAN_TAG2];

	/* if key profile programmed does not extract ether type at all */
	if (!etype_ether->nr_kws && !etype_tag1->nr_kws && !etype_tag2->nr_kws)
		goto vlan_tci;

	/* if key profile programmed extracts ether type from one layer */
	if (etype_ether->nr_kws && !etype_tag1->nr_kws && !etype_tag2->nr_kws)
		key_fields[NPC_ETYPE] = *etype_ether;
	if (!etype_ether->nr_kws && etype_tag1->nr_kws && !etype_tag2->nr_kws)
		key_fields[NPC_ETYPE] = *etype_tag1;
	if (!etype_ether->nr_kws && !etype_tag1->nr_kws && etype_tag2->nr_kws)
		key_fields[NPC_ETYPE] = *etype_tag2;

	/* if key profile programmed extracts ether type from multiple layers */
	if (etype_ether->nr_kws && etype_tag1->nr_kws) {
		for (i = 0; i < NPC_MAX_KWS_IN_KEY; i++) {
			if (etype_ether->kw_mask[i] != etype_tag1->kw_mask[i])
				goto vlan_tci;
		}
		key_fields[NPC_ETYPE] = *etype_tag1;
	}
	if (etype_ether->nr_kws && etype_tag2->nr_kws) {
		for (i = 0; i < NPC_MAX_KWS_IN_KEY; i++) {
			if (etype_ether->kw_mask[i] != etype_tag2->kw_mask[i])
				goto vlan_tci;
		}
		key_fields[NPC_ETYPE] = *etype_tag2;
	}
	if (etype_tag1->nr_kws && etype_tag2->nr_kws) {
		for (i = 0; i < NPC_MAX_KWS_IN_KEY; i++) {
			if (etype_tag1->kw_mask[i] != etype_tag2->kw_mask[i])
				goto vlan_tci;
		}
		key_fields[NPC_ETYPE] = *etype_tag2;
	}

	/* check none of higher layers overwrite ether type */
	start_lid = key_fields[NPC_ETYPE].layer_mdata.lid + 1;
	if (npc_check_overlap(rvu, blkaddr, NPC_ETYPE, start_lid, intf))
		goto vlan_tci;
	*features |= BIT_ULL(NPC_ETYPE);
vlan_tci:
	/* if key profile does not extract outer vlan tci at all */
	if (!vlan_tag1->nr_kws && !vlan_tag2->nr_kws)
		goto done;

	/* if key profile extracts outer vlan tci from one layer */
	if (vlan_tag1->nr_kws && !vlan_tag2->nr_kws)
		key_fields[NPC_OUTER_VID] = *vlan_tag1;
	if (!vlan_tag1->nr_kws && vlan_tag2->nr_kws)
		key_fields[NPC_OUTER_VID] = *vlan_tag2;

	/* if key profile extracts outer vlan tci from multiple layers */
	if (vlan_tag1->nr_kws && vlan_tag2->nr_kws) {
		for (i = 0; i < NPC_MAX_KWS_IN_KEY; i++) {
			if (vlan_tag1->kw_mask[i] != vlan_tag2->kw_mask[i])
				goto done;
		}
		key_fields[NPC_OUTER_VID] = *vlan_tag2;
	}
	/* check none of higher layers overwrite outer vlan tci */
	start_lid = key_fields[NPC_OUTER_VID].layer_mdata.lid + 1;
	if (npc_check_overlap(rvu, blkaddr, NPC_OUTER_VID, start_lid, intf))
		goto done;
	*features |= BIT_ULL(NPC_OUTER_VID);
done:
	return;
}

static void npc_scan_ldata(struct rvu *rvu, int blkaddr, u8 lid,
			   u8 lt, u64 cfg, u8 intf)
{
	struct npc_mcam *mcam = &rvu->hw->mcam;
	u8 hdr, key, nr_bytes, bit_offset;
	u8 la_ltype, la_start;
	/* starting KW index and starting bit position */
	int start_kwi, offset;

	nr_bytes = FIELD_GET(NPC_BYTESM, cfg) + 1;
	hdr = FIELD_GET(NPC_HDR_OFFSET, cfg);
	key = FIELD_GET(NPC_KEY_OFFSET, cfg);
	start_kwi = key / 8;
	offset = (key * 8) % 64;

	/* For Tx, Layer A has NIX_INST_HDR_S(64 bytes) preceding
	 * ethernet header.
	 */
	if (intf == NIX_INTF_TX) {
		la_ltype = NPC_LT_LA_IH_NIX_ETHER;
		la_start = 8;
	} else {
		la_ltype = NPC_LT_LA_ETHER;
		la_start = 0;
	}

#define NPC_SCAN_HDR(name, hlid, hlt, hstart, hlen)			       \
do {									       \
	if (lid == (hlid) && lt == (hlt)) {				       \
		if ((hstart) >= hdr &&					       \
		    ((hstart) + (hlen)) <= (hdr + nr_bytes)) {	               \
			bit_offset = (hdr + nr_bytes - (hstart) - (hlen)) * 8; \
			npc_set_layer_mdata(mcam, name, cfg, lid, lt, intf);   \
			npc_set_kw_masks(mcam, name, (hlen) * 8,	       \
					 start_kwi, offset + bit_offset, intf);\
		}							       \
	}								       \
} while (0)

	/* List LID, LTYPE, start offset from layer and length(in bytes) of
	 * packet header fields below.
	 * Example: Source IP is 4 bytes and starts at 12th byte of IP header
	 */
	NPC_SCAN_HDR(NPC_SIP_IPV4, NPC_LID_LC, NPC_LT_LC_IP, 12, 4);
	NPC_SCAN_HDR(NPC_DIP_IPV4, NPC_LID_LC, NPC_LT_LC_IP, 16, 4);
	NPC_SCAN_HDR(NPC_SPORT_UDP, NPC_LID_LD, NPC_LT_LD_UDP, 0, 2);
	NPC_SCAN_HDR(NPC_DPORT_UDP, NPC_LID_LD, NPC_LT_LD_UDP, 2, 2);
	NPC_SCAN_HDR(NPC_SPORT_TCP, NPC_LID_LD, NPC_LT_LD_TCP, 0, 2);
	NPC_SCAN_HDR(NPC_DPORT_TCP, NPC_LID_LD, NPC_LT_LD_TCP, 2, 2);
	NPC_SCAN_HDR(NPC_ETYPE_ETHER, NPC_LID_LA, NPC_LT_LA_ETHER, 12, 2);
	NPC_SCAN_HDR(NPC_ETYPE_TAG1, NPC_LID_LB, NPC_LT_LB_CTAG, 4, 2);
	NPC_SCAN_HDR(NPC_ETYPE_TAG2, NPC_LID_LB, NPC_LT_LB_STAG_QINQ, 8, 2);
	NPC_SCAN_HDR(NPC_VLAN_TAG1, NPC_LID_LB, NPC_LT_LB_CTAG, 2, 2);
	NPC_SCAN_HDR(NPC_VLAN_TAG2, NPC_LID_LB, NPC_LT_LB_STAG_QINQ, 2, 2);
	NPC_SCAN_HDR(NPC_DMAC, NPC_LID_LA, la_ltype, la_start, 6);
	NPC_SCAN_HDR(NPC_SMAC, NPC_LID_LA, la_ltype, la_start, 6);
	/* PF_FUNC is 2 bytes at 0th byte of NPC_LT_LA_IH_NIX_ETHER */
	NPC_SCAN_HDR(NPC_PF_FUNC, NPC_LID_LA, NPC_LT_LA_IH_NIX_ETHER, 0, 2);
}

static void npc_set_features(struct rvu *rvu, int blkaddr, u8 intf)
{
	struct npc_mcam *mcam = &rvu->hw->mcam;
	u64 *features = &mcam->rx_features;
	u64 tcp_udp;
	int err, hdr;

	if (intf == NIX_INTF_TX)
		features = &mcam->tx_features;

	for (hdr = NPC_DMAC; hdr < NPC_HEADER_FIELDS_MAX; hdr++) {
		err = npc_check_field(rvu, blkaddr, hdr, intf);
		if (!err)
			*features |= BIT_ULL(hdr);
	}

	tcp_udp = BIT_ULL(NPC_SPORT_TCP) | BIT_ULL(NPC_SPORT_UDP) |
		  BIT_ULL(NPC_DPORT_TCP) | BIT_ULL(NPC_DPORT_UDP);

	/* for tcp/udp corresponding layer type should be in the key */
	if (*features & tcp_udp)
		if (npc_check_field(rvu, blkaddr, NPC_LD, intf))
			*features &= ~tcp_udp;

	/* for vlan corresponding layer type should be in the key */
	if (*features & BIT_ULL(NPC_OUTER_VID))
		if (npc_check_field(rvu, blkaddr, NPC_LB, intf))
			*features &= ~BIT_ULL(NPC_OUTER_VID);
}

/* Scan key extraction profile and record how fields of our interest
 * fill the key structure. Also verify Channel and DMAC exists in
 * key and not overwritten by other header fields.
 */
static int npc_scan_kex(struct rvu *rvu, int blkaddr, u8 intf)
{
	struct npc_mcam *mcam = &rvu->hw->mcam;
	u8 lid, lt, ld, bitnr;
	u8 key_nibble = 0;
	u64 cfg;

	/* Scan and note how parse result is going to be in key.
	 * A bit set in PARSE_NIBBLE_ENA corresponds to a nibble from
	 * parse result in the key. The enabled nibbles from parse result
	 * will be concatenated in key.
	 */
	cfg = rvu_read64(rvu, blkaddr, NPC_AF_INTFX_KEX_CFG(intf));
	cfg &= NPC_PARSE_NIBBLE;
	for_each_set_bit(bitnr, (unsigned long *)&cfg, 31) {
		npc_scan_parse_result(mcam, bitnr, key_nibble, intf);
		key_nibble++;
	}

	/* Scan and note how layer data is going to be in key */
	for (lid = 0; lid < NPC_MAX_LID; lid++) {
		for (lt = 0; lt < NPC_MAX_LT; lt++) {
			for (ld = 0; ld < NPC_MAX_LD; ld++) {
				cfg = rvu_read64(rvu, blkaddr,
						 NPC_AF_INTFX_LIDX_LTX_LDX_CFG
						 (intf, lid, lt, ld));
				if (!FIELD_GET(NPC_LDATA_EN, cfg))
					continue;
				npc_scan_ldata(rvu, blkaddr, lid, lt, cfg,
					       intf);
			}
		}
	}

	return 0;
}

static int npc_scan_verify_kex(struct rvu *rvu, int blkaddr)
{
	int err;

	err = npc_scan_kex(rvu, blkaddr, NIX_INTF_RX);
	if (err)
		return err;

	err = npc_scan_kex(rvu, blkaddr, NIX_INTF_TX);
	if (err)
		return err;

	/* Channel is mandatory */
	if (!npc_is_field_present(rvu, NPC_CHAN, NIX_INTF_RX)) {
		dev_err(rvu->dev, "Channel not present in Key\n");
		return -EINVAL;
	}
	/* check that none of the fields overwrite channel */
	if (npc_check_overlap(rvu, blkaddr, NPC_CHAN, 0, NIX_INTF_RX)) {
		dev_err(rvu->dev, "Channel cannot be overwritten\n");
		return -EINVAL;
	}
	/* DMAC should be present in key for unicast filter to work */
	if (!npc_is_field_present(rvu, NPC_DMAC, NIX_INTF_RX)) {
		dev_err(rvu->dev, "DMAC not present in Key\n");
		return -EINVAL;
	}
	/* check that none of the fields overwrite DMAC */
	if (npc_check_overlap(rvu, blkaddr, NPC_DMAC, 0, NIX_INTF_RX)) {
		dev_err(rvu->dev, "DMAC cannot be overwritten\n");
		return -EINVAL;
	}

	npc_set_features(rvu, blkaddr, NIX_INTF_TX);
	npc_set_features(rvu, blkaddr, NIX_INTF_RX);
	npc_handle_multi_layer_fields(rvu, blkaddr, NIX_INTF_TX);
	npc_handle_multi_layer_fields(rvu, blkaddr, NIX_INTF_RX);

	return 0;
}

int npc_flow_steering_init(struct rvu *rvu, int blkaddr)
{
	struct npc_mcam *mcam = &rvu->hw->mcam;

	INIT_LIST_HEAD(&mcam->mcam_rules);

	return npc_scan_verify_kex(rvu, blkaddr);
}

static int npc_check_unsupported_flows(struct rvu *rvu, u64 features, u8 intf)
{
	struct npc_mcam *mcam = &rvu->hw->mcam;
	u64 *mcam_features = &mcam->rx_features;
	u64 unsupported;
	u8 bit;

	if (intf == NIX_INTF_TX)
		mcam_features = &mcam->tx_features;

	unsupported = (*mcam_features ^ features) & ~(*mcam_features);
	if (unsupported) {
		dev_info(rvu->dev, "Unsupported flow(s):\n");
		for_each_set_bit(bit, (unsigned long *)&unsupported, 64)
			dev_info(rvu->dev, "%s ", npc_get_field_name(bit));
		return -ENOTSUPP;
	}

	return 0;
}

/* npc_update_entry - Based on the masks generated during
 * the key scanning, updates the given entry with value and
 * masks for the field of interest. Maximum 16 bytes of a packet
 * header can be extracted by HW hence lo and hi are sufficient.
 * When field bytes are less than or equal to 8 then hi should be
 * 0 for value and mask.
 *
 * If exact match of value is required then mask should be all 1's.
 * If any bits in mask are 0 then corresponding bits in value are
 * dont care.
 */
static void npc_update_entry(struct rvu *rvu, enum key_fields type,
			     struct mcam_entry *entry, u64 val_lo,
			     u64 val_hi, u64 mask_lo, u64 mask_hi, u8 intf)
{
	struct npc_mcam *mcam = &rvu->hw->mcam;
	struct mcam_entry dummy = { {0} };
	struct npc_key_field *field;
	u64 kw1, kw2, kw3;
	u8 shift;
	int i;

	field = &mcam->rx_key_fields[type];
	if (intf == NIX_INTF_TX)
		field = &mcam->tx_key_fields[type];

	if (!field->nr_kws)
		return;

	for (i = 0; i < NPC_MAX_KWS_IN_KEY; i++) {
		if (!field->kw_mask[i])
			continue;
		/* place key value in kw[x] */
		shift = __ffs64(field->kw_mask[i]);
		/* update entry value */
		kw1 = (val_lo << shift) & field->kw_mask[i];
		dummy.kw[i] = kw1;
		/* update entry mask */
		kw1 = (mask_lo << shift) & field->kw_mask[i];
		dummy.kw_mask[i] = kw1;

		if (field->nr_kws == 1)
			break;
		/* place remaining bits of key value in kw[x + 1] */
		if (field->nr_kws == 2) {
			/* update entry value */
			kw2 = (val_lo >> (64 - shift)) | (val_hi << shift);
			kw2 &= field->kw_mask[i + 1];
			dummy.kw[i + 1] = kw2;
			/* update entry mask */
			kw2 = (mask_lo >> (64 - shift)) | (mask_hi << shift);
			kw2 &= field->kw_mask[i + 1];
			dummy.kw_mask[i + 1] = kw2;
			break;
		}
		/* place remaining bits of key value in kw[x + 1], kw[x + 2] */
		if (field->nr_kws == 3) {
			/* update entry value */
			kw2 = (val_lo >> (64 - shift)) | (val_hi << shift);
			kw2 &= field->kw_mask[i + 1];
			kw3 = (val_hi >> (64 - shift));
			kw3 &= field->kw_mask[i + 2];
			dummy.kw[i + 1] = kw2;
			dummy.kw[i + 2] = kw3;
			/* update entry mask */
			kw2 = (mask_lo >> (64 - shift)) | (mask_hi << shift);
			kw2 &= field->kw_mask[i + 1];
			kw3 = (mask_hi >> (64 - shift));
			kw3 &= field->kw_mask[i + 2];
			dummy.kw_mask[i + 1] = kw2;
			dummy.kw_mask[i + 2] = kw3;
			break;
		}
	}
	/* dummy is ready with values and masks for given key
	 * field now clear and update input entry with those
	 */
	for (i = 0; i < NPC_MAX_KWS_IN_KEY; i++) {
		if (!field->kw_mask[i])
			continue;
		entry->kw[i] &= ~field->kw_mask[i];
		entry->kw_mask[i] &= ~field->kw_mask[i];

		entry->kw[i] |= dummy.kw[i];
		entry->kw_mask[i] |= dummy.kw_mask[i];
	}
}

static void npc_update_flow(struct rvu *rvu, struct mcam_entry *entry,
			    u64 features, struct flow_msg *pkt,
			    struct flow_msg *mask,
			    struct rvu_npc_mcam_rule *output, u8 intf)
{
	u64 dmac_mask = ether_addr_to_u64(mask->dmac);
	u64 smac_mask = ether_addr_to_u64(mask->smac);
	u64 dmac_val = ether_addr_to_u64(pkt->dmac);
	u64 smac_val = ether_addr_to_u64(pkt->smac);
	struct flow_msg *opkt = &output->packet;
	struct flow_msg *omask = &output->mask;

	if (!features)
		return;

#define NPC_WRITE_FLOW(field, member, val_lo, val_hi, mask_lo, mask_hi)	      \
do {									      \
	if (features & BIT_ULL(field)) {				      \
		npc_update_entry(rvu, field, entry, val_lo, val_hi,	      \
				 mask_lo, mask_hi, intf);		      \
		memcpy(&opkt->member, &pkt->member, sizeof(pkt->member));     \
		memcpy(&omask->member, &mask->member, sizeof(mask->member));  \
	}								      \
} while (0)

	 /* For tcp/udp LTYPE should be present in entry */
	if (features & (BIT_ULL(NPC_SPORT_TCP) | BIT_ULL(NPC_DPORT_TCP)))
		npc_update_entry(rvu, NPC_LD, entry, NPC_LT_LD_TCP,
				 0, ~0ULL, 0, intf);
	if (features & (BIT_ULL(NPC_SPORT_UDP) | BIT_ULL(NPC_DPORT_UDP)))
		npc_update_entry(rvu, NPC_LD, entry, NPC_LT_LD_UDP,
				 0, ~0ULL, 0, intf);
	if (features & BIT_ULL(NPC_OUTER_VID))
		npc_update_entry(rvu, NPC_LB, entry,
				 NPC_LT_LB_STAG_QINQ | NPC_LT_LB_CTAG, 0,
				 NPC_LT_LB_STAG_QINQ & NPC_LT_LB_CTAG, 0, intf);

	NPC_WRITE_FLOW(NPC_DMAC, dmac, dmac_val, 0, dmac_mask, 0);
	NPC_WRITE_FLOW(NPC_SMAC, smac, smac_val, 0, smac_mask, 0);
	NPC_WRITE_FLOW(NPC_ETYPE, etype, ntohs(pkt->etype), 0,
		       ntohs(mask->etype), 0);
	NPC_WRITE_FLOW(NPC_SIP_IPV4, ip4src, ntohl(pkt->ip4src), 0,
		       ntohl(mask->ip4src), 0);
	NPC_WRITE_FLOW(NPC_DIP_IPV4, ip4dst, ntohl(pkt->ip4dst), 0,
		       ntohl(mask->ip4dst), 0);
	NPC_WRITE_FLOW(NPC_SPORT_TCP, sport, ntohs(pkt->sport), 0,
		       ntohs(mask->sport), 0);
	NPC_WRITE_FLOW(NPC_SPORT_UDP, sport, ntohs(pkt->sport), 0,
		       ntohs(mask->sport), 0);
	NPC_WRITE_FLOW(NPC_DPORT_TCP, dport, ntohs(pkt->dport), 0,
		       ntohs(mask->dport), 0);
	NPC_WRITE_FLOW(NPC_DPORT_UDP, dport, ntohs(pkt->dport), 0,
		       ntohs(mask->dport), 0);
	NPC_WRITE_FLOW(NPC_OUTER_VID, vlan_tci, ntohs(pkt->vlan_tci), 0,
		       ntohs(mask->vlan_tci), 0);
}

static struct rvu_npc_mcam_rule *rvu_mcam_find_rule(struct npc_mcam *mcam,
						    u16 entry)
{
	struct rvu_npc_mcam_rule *iter;

	list_for_each_entry(iter, &mcam->mcam_rules, list) {
		if (iter->entry == entry)
			return iter;
	}

	return NULL;
}

static void rvu_mcam_add_rule(struct npc_mcam *mcam,
			      struct rvu_npc_mcam_rule *rule)
{
	struct list_head *head = &mcam->mcam_rules;
	struct rvu_npc_mcam_rule *iter;

	list_for_each_entry(iter, &mcam->mcam_rules, list) {
		if (iter->entry > rule->entry)
			break;
		head = &iter->list;
	}

	list_add(&rule->list, head);
}

static void rvu_mcam_remove_counter_from_rule(struct rvu *rvu, u16 pcifunc,
					      struct rvu_npc_mcam_rule *rule)
{
	struct npc_mcam_oper_counter_req free_req = { 0 };
	struct msg_rsp free_rsp;

	if (!rule->has_cntr)
		return;

	free_req.hdr.pcifunc = pcifunc;
	free_req.cntr = rule->cntr;

	rvu_mbox_handler_npc_mcam_free_counter(rvu, &free_req, &free_rsp);
	rule->has_cntr = false;
}

static void rvu_mcam_add_counter_to_rule(struct rvu *rvu, u16 pcifunc,
					 struct rvu_npc_mcam_rule *rule,
					 struct npc_install_flow_rsp *rsp)
{
	struct npc_mcam_alloc_counter_req cntr_req = { 0 };
	struct npc_mcam_alloc_counter_rsp cntr_rsp = { 0 };
	int err;

	cntr_req.hdr.pcifunc = pcifunc;
	cntr_req.contig = true;
	cntr_req.count = 1;

	/* we try to allocate a counter to track the stats of this
	 * rule. If counter could not be allocated then proceed
	 * without counter because counters are limited than entries.
	 */
	err = rvu_mbox_handler_npc_mcam_alloc_counter(rvu, &cntr_req,
						      &cntr_rsp);
	if (!err && cntr_rsp.count) {
		rule->cntr = cntr_rsp.cntr;
		rule->has_cntr = true;
		rsp->counter = rule->cntr;
	} else {
		rsp->counter = err;
	}
}

static void npc_update_rx_entry(struct rvu *rvu, struct rvu_pfvf *pfvf,
				struct mcam_entry *entry,
				struct npc_install_flow_req *req, u16 target)
{
	struct nix_rx_action action;

	npc_update_entry(rvu, NPC_CHAN, entry, req->channel, 0,
			 ~0ULL, 0, NIX_INTF_RX);

	*(u64 *)&action = 0x00;
	action.pf_func = target;
	action.op = req->op;
	action.index = req->index;
	action.match_id = req->match_id;
	action.flow_key_alg = req->flow_key_alg;

	if (req->op == NIX_RX_ACTION_DEFAULT && pfvf->def_rule)
		action = pfvf->def_rule->rx_action;

	entry->action = *(u64 *)&action;

	/* VTAG0 starts at 0th byte of LID_B.
	 * VTAG1 starts at 4th byte of LID_B.
	 */
	entry->vtag_action = FIELD_PREP(RX_VTAG0_VALID_BIT, req->vtag0_valid) |
			     FIELD_PREP(RX_VTAG0_TYPE_MASK, req->vtag0_type) |
			     FIELD_PREP(RX_VTAG0_LID_MASK, NPC_LID_LB) |
			     FIELD_PREP(RX_VTAG0_RELPTR_MASK, 0) |
			     FIELD_PREP(RX_VTAG1_VALID_BIT, req->vtag1_valid) |
			     FIELD_PREP(RX_VTAG1_TYPE_MASK, req->vtag1_type) |
			     FIELD_PREP(RX_VTAG1_LID_MASK, NPC_LID_LB) |
			     FIELD_PREP(RX_VTAG1_RELPTR_MASK, 4);
}

static void npc_update_tx_entry(struct rvu *rvu, struct rvu_pfvf *pfvf,
				struct mcam_entry *entry,
				struct npc_install_flow_req *req, u16 target)
{
	struct nix_tx_action action;

	npc_update_entry(rvu, NPC_PF_FUNC, entry, htons(target),
			 0, ~0ULL, 0, NIX_INTF_TX);

	*(u64 *)&action = 0x00;
	action.op = req->op;
	action.index = req->index;
	action.match_id = req->match_id;

	entry->action = *(u64 *)&action;

	/* VTAG0 starts at 0th byte of LID_B.
	 * VTAG1 starts at 4th byte of LID_B.
	 */
	entry->vtag_action = FIELD_PREP(TX_VTAG0_DEF_MASK, req->vtag0_def) |
			     FIELD_PREP(TX_VTAG0_OP_MASK, req->vtag0_op) |
			     FIELD_PREP(TX_VTAG0_LID_MASK, NPC_LID_LA) |
			     FIELD_PREP(TX_VTAG0_RELPTR_MASK, 20) |
			     FIELD_PREP(TX_VTAG1_DEF_MASK, req->vtag1_def) |
			     FIELD_PREP(TX_VTAG1_OP_MASK, req->vtag1_op) |
			     FIELD_PREP(TX_VTAG1_LID_MASK, NPC_LID_LA) |
			     FIELD_PREP(TX_VTAG1_RELPTR_MASK, 24);
}

static int npc_install_flow(struct rvu *rvu, int blkaddr, u16 target,
			       int nixlf, struct rvu_pfvf *pfvf,
			       struct npc_install_flow_req *req,
			       struct npc_install_flow_rsp *rsp, bool enable,
			       bool pf_set_vfs_mac)
{
	u64 features, installed_features, missing_features = 0;
	struct rvu_npc_mcam_rule *def_rule = pfvf->def_rule;
	struct npc_mcam_write_entry_req write_req = { 0 };
	bool new = false, msg_from_vf;
	struct npc_mcam *mcam = &rvu->hw->mcam;
	struct rvu_npc_mcam_rule dummy = { 0 };
	struct rvu_npc_mcam_rule *rule;
	u16 owner = req->hdr.pcifunc;
	struct msg_rsp write_rsp;
	struct mcam_entry *entry;
	int entry_index, err;

	msg_from_vf = !!(owner & RVU_PFVF_FUNC_MASK);

	installed_features = req->features;
	features = req->features;
	entry = &write_req.entry_data;
	entry_index = req->entry;

	npc_update_flow(rvu, entry, features, &req->packet, &req->mask, &dummy,
			req->intf);

	if (req->intf == NIX_INTF_RX)
		npc_update_rx_entry(rvu, pfvf, entry, req, target);
	else
		npc_update_tx_entry(rvu, pfvf, entry, req, target);

	/* Default unicast rules do not exist for TX */
	if (req->intf == NIX_INTF_TX)
		goto find_rule;

	if (def_rule)
		missing_features = (def_rule->features ^ features) &
					def_rule->features;

	if (req->default_rule && req->append) {
		/* add to default rule */
		if (missing_features)
			npc_update_flow(rvu, entry, missing_features,
					&def_rule->packet, &def_rule->mask,
					&dummy, req->intf);
		enable = rvu_npc_write_default_rule(rvu, blkaddr,
						    nixlf, target,
						    NIX_INTF_RX, entry,
						    &entry_index);
		installed_features = req->features | missing_features;
	} else if (req->default_rule && !req->append) {
		/* overwrite default rule */
		enable = rvu_npc_write_default_rule(rvu, blkaddr,
						    nixlf, target,
						    NIX_INTF_RX, entry,
						    &entry_index);
	} else if (msg_from_vf) {
		/* normal rule - include default rule also to it for VF */
		npc_update_flow(rvu, entry, missing_features, &def_rule->packet,
				&def_rule->mask, &dummy, req->intf);
		installed_features = req->features | missing_features;
	}
find_rule:
	rule = rvu_mcam_find_rule(mcam, entry_index);
	if (!rule) {
		rule = kzalloc(sizeof(*rule), GFP_KERNEL);
		if (!rule)
			return -ENOMEM;
		new = true;
	}
	/* no counter for default rule */
	if (req->default_rule)
		goto update_rule;

	/* allocate new counter if rule has no counter */
	if (req->set_cntr && !rule->has_cntr)
		rvu_mcam_add_counter_to_rule(rvu, owner, rule, rsp);

	/* if user wants to delete an existing counter for a rule then
	 * free the counter
	 */
	if (!req->set_cntr && rule->has_cntr)
		rvu_mcam_remove_counter_from_rule(rvu, owner, rule);

	write_req.hdr.pcifunc = owner;
	write_req.entry = req->entry;
	write_req.intf = req->intf;
	write_req.enable_entry = (u8)enable;
	/* if counter is available then clear and use it */
	if (req->set_cntr && rule->has_cntr) {
		rvu_write64(rvu, blkaddr, NPC_AF_MATCH_STATX(rule->cntr), 0x00);
		write_req.set_cntr = 1;
		write_req.cntr = rule->cntr;
	}

	err = rvu_mbox_handler_npc_mcam_write_entry(rvu, &write_req,
						    &write_rsp);
	if (err) {
		rvu_mcam_remove_counter_from_rule(rvu, owner, rule);
		if (new)
			kfree(rule);
		return err;
	}
update_rule:
	memcpy(&rule->packet, &dummy.packet, sizeof(rule->packet));
	memcpy(&rule->mask, &dummy.mask, sizeof(rule->mask));
	rule->entry = entry_index;
	memcpy(&rule->rx_action, &entry->action, sizeof(struct nix_rx_action));
	if (req->intf == NIX_INTF_TX)
		memcpy(&rule->tx_action, &entry->action,
		       sizeof(struct nix_tx_action));
	rule->vtag_action = entry->vtag_action;
	rule->features = installed_features;
	rule->default_rule = req->default_rule;
	rule->owner = owner;
	rule->enable = enable;
	rule->intf = req->intf;

	if (new)
		rvu_mcam_add_rule(mcam, rule);
	if (req->default_rule)
		pfvf->def_rule = rule;

	/* VF's MAC address is being changed via PF  */
	if (pf_set_vfs_mac) {
		ether_addr_copy(pfvf->default_mac, req->packet.dmac);
		ether_addr_copy(pfvf->mac_addr, req->packet.dmac);
	}

	if (pfvf->pf_set_vf_cfg && req->vtag0_type == NIX_AF_LFX_RX_VTAG_TYPE7)
		rule->vfvlan_cfg = true;

	return 0;
}

int rvu_mbox_handler_npc_install_flow(struct rvu *rvu,
				      struct npc_install_flow_req *req,
				      struct npc_install_flow_rsp *rsp)
{
	bool from_vf = !!(req->hdr.pcifunc & RVU_PFVF_FUNC_MASK);
	int blkaddr, nixlf, err;
	struct rvu_pfvf *pfvf;
	bool pf_set_vfs_mac = false;
	bool enable = true;
	u16 target;

	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NPC, 0);
	if (blkaddr < 0) {
		dev_err(rvu->dev, "%s: NPC block not implemented\n", __func__);
		return -ENODEV;
	}

	if (from_vf && req->default_rule)
		return NPC_MCAM_PERM_DENIED;

	/* Each PF/VF info is maintained in struct rvu_pfvf.
	 * rvu_pfvf for the target PF/VF needs to be retrieved
	 * hence modify pcifunc accordingly.
	 */

	/* AF installing for a PF/VF */
	if (!req->hdr.pcifunc)
		target = req->vf;
	/* PF installing for its VF */
	else if (!from_vf && req->vf) {
		target = (req->hdr.pcifunc & ~RVU_PFVF_FUNC_MASK) | req->vf;
		pf_set_vfs_mac = req->default_rule &&
				(req->features & BIT_ULL(NPC_DMAC));
	}
	/* msg received from PF/VF */
	else
		target = req->hdr.pcifunc;

	if (npc_check_unsupported_flows(rvu, req->features, req->intf))
		return -ENOTSUPP;

	if (npc_mcam_verify_channel(rvu, target, req->intf, req->channel))
		return -EINVAL;

	pfvf = rvu_get_pfvf(rvu, target);

	/* PF installing for its VF */
	if (req->hdr.pcifunc && !from_vf && req->vf)
		pfvf->pf_set_vf_cfg = 1;

	/* update req destination mac addr */
	if ((req->features & BIT_ULL(NPC_DMAC)) &&
	    req->intf == NIX_INTF_RX &&
	    is_zero_ether_addr(req->packet.dmac)) {
		ether_addr_copy(req->packet.dmac, pfvf->mac_addr);
		u64_to_ether_addr(0xffffffffffffull, req->mask.dmac);
	}

	err = nix_get_nixlf(rvu, target, &nixlf, NULL);

	/* If interface is uninitialized then do not enable entry */
	if (err || (!req->default_rule && !pfvf->def_rule))
		enable = false;

	/* Packets reaching NPC in Tx path implies that a
	 * NIXLF is properly setup and transmitting.
	 * Hence rules can be enabled for Tx.
	 */
	if (req->intf == NIX_INTF_TX)
		enable = true;

	/* Do not allow requests from uninitialized VFs */
	if (from_vf && !enable)
		return -EINVAL;

	/* If message is from VF then its flow should not overlap with
	 * reserved unicast flow.
	 */
	if (from_vf && pfvf->def_rule && req->intf == NIX_INTF_RX &&
	    pfvf->def_rule->features & req->features)
		return -EINVAL;

	return npc_install_flow(rvu, blkaddr, target, nixlf, pfvf,
				req, rsp, enable, pf_set_vfs_mac);
}

static int npc_delete_flow(struct rvu *rvu, u16 entry, u16 pcifunc)
{
	struct npc_mcam_ena_dis_entry_req dis_req = { 0 };
	struct npc_mcam *mcam = &rvu->hw->mcam;
	struct rvu_npc_mcam_rule *rule;
	struct msg_rsp dis_rsp;
	int err;

	rule = rvu_mcam_find_rule(mcam, entry);
	if (!rule)
		return -ENOENT;

	if (rule->default_rule)
		return 0;

	if (rule->has_cntr)
		rvu_mcam_remove_counter_from_rule(rvu, pcifunc, rule);

	dis_req.hdr.pcifunc = pcifunc;
	dis_req.entry = entry;
	err = rvu_mbox_handler_npc_mcam_dis_entry(rvu, &dis_req, &dis_rsp);
	if (err)
		return err;

	list_del(&rule->list);
	kfree(rule);

	return 0;
}

int rvu_mbox_handler_npc_delete_flow(struct rvu *rvu,
				     struct npc_delete_flow_req *req,
				     struct msg_rsp *rsp)
{
	struct npc_mcam *mcam = &rvu->hw->mcam;
	struct rvu_npc_mcam_rule *iter, *tmp;
	u16 pcifunc = req->hdr.pcifunc;
	int err;

	if (req->end) {
		list_for_each_entry_safe(iter, tmp, &mcam->mcam_rules, list) {
			if (iter->owner == pcifunc &&
			    iter->entry >= req->start &&
			    iter->entry <= req->end) {
				err = npc_delete_flow(rvu, iter->entry,
						      pcifunc);
				if (err)
					return err;
			}
		}
		return 0;
	}

	if (!req->all)
		return npc_delete_flow(rvu, req->entry, pcifunc);

	list_for_each_entry_safe(iter, tmp, &mcam->mcam_rules, list) {
		if (iter->owner == pcifunc) {
			err = npc_delete_flow(rvu, iter->entry, pcifunc);
			if (err)
				return err;
		}
	}

	return 0;
}

static int npc_update_dmac_value(struct rvu *rvu, int npcblkaddr,
				 struct rvu_npc_mcam_rule *rule,
				 struct rvu_pfvf *pfvf)
{
	struct npc_mcam_write_entry_req write_req = { 0 };
	struct mcam_entry *entry = &write_req.entry_data;
	struct npc_mcam *mcam = &rvu->hw->mcam;
	struct npc_mcam_read_entry_rsp wrsp;
	struct msg_rsp rsp;

	ether_addr_copy(rule->packet.dmac, pfvf->mac_addr);

	npc_read_mcam_entry(rvu, mcam, npcblkaddr, rule->entry,
			    entry, &wrsp.intf,
			    &wrsp.enable);

	npc_update_entry(rvu, NPC_DMAC, entry,
			 ether_addr_to_u64(pfvf->mac_addr), 0,
			 0xffffffffffffull, 0, wrsp.intf);

	write_req.hdr.pcifunc = rule->owner;
	write_req.entry = rule->entry;

	return rvu_mbox_handler_npc_mcam_write_entry(rvu, &write_req, &rsp);
}

void npc_mcam_enable_flows(struct rvu *rvu, u16 target)
{
	struct rvu_pfvf *pfvf = rvu_get_pfvf(rvu, target);
	struct npc_mcam_ena_dis_entry_req ena_req = { 0 };
	struct npc_mcam *mcam = &rvu->hw->mcam;
	struct rvu_npc_mcam_rule *rule;
	int blkaddr, bank, err;
	struct msg_rsp rsp;
	u64 def_action;

	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NPC, 0);
	if (blkaddr < 0) {
		dev_err(rvu->dev, "%s: NPC block not implemented\n", __func__);
		return;
	}

	list_for_each_entry(rule, &mcam->mcam_rules, list) {
		if (rule->intf == NIX_INTF_RX &&
		    rule->rx_action.pf_func == target && !rule->enable) {
			if (rule->default_rule) {
				npc_enable_mcam_entry(rvu, mcam, blkaddr,
						      rule->entry, true);
				rule->enable = true;
				continue;
			}

			if (rule->vfvlan_cfg)
				npc_update_dmac_value(rvu, blkaddr, rule, pfvf);

			if (rule->rx_action.op == NIX_RX_ACTION_DEFAULT) {
				if (!pfvf->def_rule)
					continue;
				/* Use default unicast entry action */
				rule->rx_action = pfvf->def_rule->rx_action;
				def_action = *(u64 *)&pfvf->def_rule->rx_action;
				bank = npc_get_bank(mcam, rule->entry);
				rvu_write64(rvu, blkaddr,
					    NPC_AF_MCAMEX_BANKX_ACTION
					    (rule->entry, bank), def_action);
			}
			ena_req.hdr.pcifunc = rule->owner;
			ena_req.entry = rule->entry;
			err = rvu_mbox_handler_npc_mcam_ena_entry(rvu, &ena_req,
								  &rsp);
			if (err)
				continue;
			rule->enable = true;
		}
	}
}
