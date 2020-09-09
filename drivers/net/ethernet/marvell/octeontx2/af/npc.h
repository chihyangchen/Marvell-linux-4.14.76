/* SPDX-License-Identifier: GPL-2.0
 * Marvell OcteonTx2 RVU Admin Function driver
 *
 * Copyright (C) 2018 Marvell International Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef NPC_H
#define NPC_H

enum NPC_LID_E {
	NPC_LID_LA = 0,
	NPC_LID_LB,
	NPC_LID_LC,
	NPC_LID_LD,
	NPC_LID_LE,
	NPC_LID_LF,
	NPC_LID_LG,
	NPC_LID_LH,
};

#define NPC_LT_NA 0

enum npc_kpu_la_ltype {
	NPC_LT_LA_8023 = 1,
	NPC_LT_LA_ETHER,
	NPC_LT_LA_IH_NIX_ETHER,
	NPC_LT_LA_IH_8_ETHER,
	NPC_LT_LA_IH_4_ETHER,
	NPC_LT_LA_IH_2_ETHER,
	NPC_LT_LA_HIGIG2_ETHER,
	NPC_LT_LA_IH_NIX_HIGIG2_ETHER,
	NPC_LT_LA_CUSTOM0 = 0xE,
	NPC_LT_LA_CUSTOM1 = 0xF,
};

enum npc_kpu_lb_ltype {
	NPC_LT_LB_ETAG = 1,
	NPC_LT_LB_CTAG,
	NPC_LT_LB_STAG_QINQ,
	NPC_LT_LB_BTAG,
	NPC_LT_LB_ITAG,
	NPC_LT_LB_DSA,
	NPC_LT_LB_DSA_VLAN,
	NPC_LT_LB_EDSA,
	NPC_LT_LB_EDSA_VLAN,
	NPC_LT_LB_EXDSA,
	NPC_LT_LB_EXDSA_VLAN,
	NPC_LT_LB_CUSTOM0 = 0xE,
	NPC_LT_LB_CUSTOM1 = 0xF,
};

/* Don't modify ltypes up to IP6_EXT, otherwise length and checksum of IP
 * headers may not be checked correctly. IPv4 ltypes and IPv6 ltypes must
 * differ only at bit 0 so mask 0xE can be used to detect extended headers.
 */
enum npc_kpu_lc_ltype {
	NPC_LT_LC_PTP = 1,
	NPC_LT_LC_IP,
	NPC_LT_LC_IP_OPT,
	NPC_LT_LC_IP6,
	NPC_LT_LC_IP6_EXT,
	NPC_LT_LC_ARP,
	NPC_LT_LC_RARP,
	NPC_LT_LC_MPLS,
	NPC_LT_LC_NSH,
	NPC_LT_LC_FCOE,
	NPC_LT_LC_CUSTOM0 = 0xE,
	NPC_LT_LC_CUSTOM1 = 0xF,
};

/* Don't modify Ltypes upto SCTP, otherwise it will
 * effect flow tag calculation and thus RSS.
 */
enum npc_kpu_ld_ltype {
	NPC_LT_LD_TCP = 1,
	NPC_LT_LD_UDP,
	NPC_LT_LD_ICMP,
	NPC_LT_LD_SCTP,
	NPC_LT_LD_ICMP6,
	NPC_LT_LD_CUSTOM0,
	NPC_LT_LD_CUSTOM1,
	NPC_LT_LD_IGMP = 8,
	NPC_LT_LD_ESP,
	NPC_LT_LD_AH,
	NPC_LT_LD_GRE,
	NPC_LT_LD_NVGRE,
	NPC_LT_LD_NSH,
	NPC_LT_LD_TU_MPLS_IN_NSH,
	NPC_LT_LD_TU_MPLS_IN_IP,
};

enum npc_kpu_le_ltype {
	NPC_LT_LE_VXLAN = 1,
	NPC_LT_LE_GENEVE,
	NPC_LT_LE_GTPU = 4,
	NPC_LT_LE_VXLANGPE,
	NPC_LT_LE_GTPC,
	NPC_LT_LE_NSH,
	NPC_LT_LE_TU_MPLS_IN_GRE,
	NPC_LT_LE_TU_NSH_IN_GRE,
	NPC_LT_LE_TU_MPLS_IN_UDP,
	NPC_LT_LE_CUSTOM0 = 0xE,
	NPC_LT_LE_CUSTOM1 = 0xF,
};

enum npc_kpu_lf_ltype {
	NPC_LT_LF_TU_ETHER = 1,
	NPC_LT_LF_TU_PPP,
	NPC_LT_LF_TU_MPLS_IN_VXLANGPE,
	NPC_LT_LF_TU_NSH_IN_VXLANGPE,
	NPC_LT_LF_TU_MPLS_IN_NSH,
	NPC_LT_LF_TU_3RD_NSH,
	NPC_LT_LF_CUSTOM0 = 0xE,
	NPC_LT_LF_CUSTOM1 = 0xF,
};

enum npc_kpu_lg_ltype {
	NPC_LT_LG_TU_IP = 1,
	NPC_LT_LG_TU_IP6,
	NPC_LT_LG_TU_ARP,
	NPC_LT_LG_TU_ETHER_IN_NSH,
	NPC_LT_LG_CUSTOM0 = 0xE,
	NPC_LT_LG_CUSTOM1 = 0xF,
};

/* Don't modify Ltypes upto SCTP, otherwise it will
 * effect flow tag calculation and thus RSS.
 */
enum npc_kpu_lh_ltype {
	NPC_LT_LH_TU_TCP = 1,
	NPC_LT_LH_TU_UDP,
	NPC_LT_LH_TU_ICMP,
	NPC_LT_LH_TU_SCTP,
	NPC_LT_LH_TU_ICMP6,
	NPC_LT_LH_TU_IGMP = 8,
	NPC_LT_LH_TU_ESP,
	NPC_LT_LH_TU_AH,
	NPC_LT_LH_CUSTOM0 = 0xE,
	NPC_LT_LH_CUSTOM1 = 0xF,
};

enum npc_pkind_type {
	NPC_TX_HIGIG_PKIND = 60ULL,
	NPC_RX_HIGIG_PKIND,
	NPC_RX_EDSA_PKIND,
	NPC_TX_DEF_PKIND,
};

struct npc_kpu_profile_cam {
	u8 state;
	u8 state_mask;
	u16 dp0;
	u16 dp0_mask;
	u16 dp1;
	u16 dp1_mask;
	u16 dp2;
	u16 dp2_mask;
} __packed;

struct npc_kpu_profile_action {
	u8 errlev;
	u8 errcode;
	u8 dp0_offset;
	u8 dp1_offset;
	u8 dp2_offset;
	u8 bypass_count;
	u8 parse_done;
	u8 next_state;
	u8 ptr_advance;
	u8 cap_ena;
	u8 lid;
	u8 ltype;
	u8 flags;
	u8 offset;
	u8 mask;
	u8 right;
	u8 shift;
} __packed;

struct npc_kpu_profile {
	int cam_entries;
	int action_entries;
	struct npc_kpu_profile_cam *cam;
	struct npc_kpu_profile_action *action;
};

/* NPC KPU register formats */
struct npc_kpu_cam {
#if defined(__BIG_ENDIAN_BITFIELD)
	u64 rsvd_63_56     : 8;
	u64 state          : 8;
	u64 dp2_data       : 16;
	u64 dp1_data       : 16;
	u64 dp0_data       : 16;
#else
	u64 dp0_data       : 16;
	u64 dp1_data       : 16;
	u64 dp2_data       : 16;
	u64 state          : 8;
	u64 rsvd_63_56     : 8;
#endif
};

struct npc_kpu_action0 {
#if defined(__BIG_ENDIAN_BITFIELD)
	u64 rsvd_63_57     : 7;
	u64 byp_count      : 3;
	u64 capture_ena    : 1;
	u64 parse_done     : 1;
	u64 next_state     : 8;
	u64 rsvd_43        : 1;
	u64 capture_lid    : 3;
	u64 capture_ltype  : 4;
	u64 capture_flags  : 8;
	u64 ptr_advance    : 8;
	u64 var_len_offset : 8;
	u64 var_len_mask   : 8;
	u64 var_len_right  : 1;
	u64 var_len_shift  : 3;
#else
	u64 var_len_shift  : 3;
	u64 var_len_right  : 1;
	u64 var_len_mask   : 8;
	u64 var_len_offset : 8;
	u64 ptr_advance    : 8;
	u64 capture_flags  : 8;
	u64 capture_ltype  : 4;
	u64 capture_lid    : 3;
	u64 rsvd_43        : 1;
	u64 next_state     : 8;
	u64 parse_done     : 1;
	u64 capture_ena    : 1;
	u64 byp_count      : 3;
	u64 rsvd_63_57     : 7;
#endif
};

struct npc_kpu_action1 {
#if defined(__BIG_ENDIAN_BITFIELD)
	u64 rsvd_63_36     : 28;
	u64 errlev         : 4;
	u64 errcode        : 8;
	u64 dp2_offset     : 8;
	u64 dp1_offset     : 8;
	u64 dp0_offset     : 8;
#else
	u64 dp0_offset     : 8;
	u64 dp1_offset     : 8;
	u64 dp2_offset     : 8;
	u64 errcode        : 8;
	u64 errlev         : 4;
	u64 rsvd_63_36     : 28;
#endif
};

struct npc_kpu_pkind_cpi_def {
#if defined(__BIG_ENDIAN_BITFIELD)
	u64 ena            : 1;
	u64 rsvd_62_59     : 4;
	u64 lid            : 3;
	u64 ltype_match    : 4;
	u64 ltype_mask     : 4;
	u64 flags_match    : 8;
	u64 flags_mask     : 8;
	u64 add_offset     : 8;
	u64 add_mask       : 8;
	u64 rsvd_15        : 1;
	u64 add_shift      : 3;
	u64 rsvd_11_10     : 2;
	u64 cpi_base       : 10;
#else
	u64 cpi_base       : 10;
	u64 rsvd_11_10     : 2;
	u64 add_shift      : 3;
	u64 rsvd_15        : 1;
	u64 add_mask       : 8;
	u64 add_offset     : 8;
	u64 flags_mask     : 8;
	u64 flags_match    : 8;
	u64 ltype_mask     : 4;
	u64 ltype_match    : 4;
	u64 lid            : 3;
	u64 rsvd_62_59     : 4;
	u64 ena            : 1;
#endif
};

struct nix_rx_action {
#if defined(__BIG_ENDIAN_BITFIELD)
	u64	rsvd_63_61	:3;
	u64	flow_key_alg	:5;
	u64	match_id	:16;
	u64	index		:20;
	u64	pf_func		:16;
	u64	op		:4;
#else
	u64	op		:4;
	u64	pf_func		:16;
	u64	index		:20;
	u64	match_id	:16;
	u64	flow_key_alg	:5;
	u64	rsvd_63_61	:3;
#endif
};

struct nix_tx_action {
#if defined(__BIG_ENDIAN_BITFIELD)
	u64	rsvd_63_48	:16;
	u64	match_id	:16;
	u64	index		:20;
	u64	rsvd_11_8	:8;
	u64	op		:4;
#else
	u64	op		:4;
	u64	rsvd_11_8	:8;
	u64	index		:20;
	u64	match_id	:16;
	u64	rsvd_63_48	:16;
#endif
};

/* NPC_AF_INTFX_KEX_CFG field masks */
#define NPC_PARSE_NIBBLE		GENMASK_ULL(30, 0)

/* NIX Receive Vtag Action Structure */
#define RX_VTAG0_VALID_BIT		BIT_ULL(15)
#define RX_VTAG0_TYPE_MASK		GENMASK_ULL(14, 12)
#define RX_VTAG0_LID_MASK		GENMASK_ULL(10, 8)
#define RX_VTAG0_RELPTR_MASK		GENMASK_ULL(7, 0)
#define RX_VTAG1_VALID_BIT		BIT_ULL(47)
#define RX_VTAG1_TYPE_MASK		GENMASK_ULL(46, 44)
#define RX_VTAG1_LID_MASK		GENMASK_ULL(42, 40)
#define RX_VTAG1_RELPTR_MASK		GENMASK_ULL(39, 32)

/* NIX Transmit Vtag Action Structure */
#define TX_VTAG0_DEF_MASK		GENMASK_ULL(25, 16)
#define TX_VTAG0_OP_MASK		GENMASK_ULL(13, 12)
#define TX_VTAG0_LID_MASK		GENMASK_ULL(10, 8)
#define TX_VTAG0_RELPTR_MASK		GENMASK_ULL(7, 0)
#define TX_VTAG1_DEF_MASK		GENMASK_ULL(57, 48)
#define TX_VTAG1_OP_MASK		GENMASK_ULL(45, 44)
#define TX_VTAG1_LID_MASK		GENMASK_ULL(42, 40)
#define TX_VTAG1_RELPTR_MASK		GENMASK_ULL(39, 32)

struct npc_mcam_kex {
	/* MKEX Profle Header */
	u64 mkex_sign; /* "mcam-kex-profile" (8 bytes/ASCII characters) */
	u8 name[MKEX_NAME_LEN];   /* MKEX Profile name */
	u64 cpu_model;   /* Format as profiled by CPU hardware */
	u64 kpu_version; /* KPU firmware/profile version */
	u64 reserved; /* Reserved for extension */

	/* MKEX Profle Data */
	u64 keyx_cfg[NPC_MAX_INTF]; /* NPC_AF_INTF(0..1)_KEX_CFG */
	/* NPC_AF_KEX_LDATA(0..1)_FLAGS_CFG */
	u64 kex_ld_flags[NPC_MAX_LD];
	/* NPC_AF_INTF(0..1)_LID(0..7)_LT(0..15)_LD(0..1)_CFG */
	u64 intf_lid_lt_ld[NPC_MAX_INTF][NPC_MAX_LID][NPC_MAX_LT][NPC_MAX_LD];
	/* NPC_AF_INTF(0..1)_LDATA(0..1)_FLAGS(0..15)_CFG */
	u64 intf_ld_flags[NPC_MAX_INTF][NPC_MAX_LD][NPC_MAX_LFL];
} __packed;

struct npc_kpu_fwdata {
	int	entries;
	/* What follows is:
	 * struct npc_kpu_profile_cam[entries];
	 * struct npc_kpu_profile_action[entries];
	 */
	u8	data[0];
} __packed;

struct npc_lt_def {
	u8	ltype_mask;
	u8	ltype_match;
	u8	lid;
} __packed;

struct npc_lt_def_ipsec {
	u8	ltype_mask;
	u8	ltype_match;
	u8	lid;
	u8	spi_offset;
	u8	spi_nz;
} __packed;

struct npc_lt_def_cfg {
	struct npc_lt_def	rx_ol2;
	struct npc_lt_def	rx_oip4;
	struct npc_lt_def	rx_iip4;
	struct npc_lt_def	rx_oip6;
	struct npc_lt_def	rx_iip6;
	struct npc_lt_def	rx_otcp;
	struct npc_lt_def	rx_itcp;
	struct npc_lt_def	rx_oudp;
	struct npc_lt_def	rx_iudp;
	struct npc_lt_def	rx_osctp;
	struct npc_lt_def	rx_isctp;
	struct npc_lt_def_ipsec	rx_ipsec[2];
	struct npc_lt_def	pck_ol2;
	struct npc_lt_def	pck_oip4;
	struct npc_lt_def	pck_oip6;
	struct npc_lt_def	pck_iip4;
} __packed;

/* Loadable KPU profile firmware data */
struct npc_kpu_profile_fwdata {
#define KPU_SIGN	0x00666f727075706b
#define KPU_NAME_LEN	32
/** Maximum number of custom KPU entries supported by the built-in profile. */
#define KPU_MAX_CST_ENT	2
	/* KPU Profle Header */
	u64	signature; /* "kpuprof\0" (8 bytes/ASCII characters) */
	u8	name[KPU_NAME_LEN]; /* KPU Profile name */
	u64	version; /* KPU profile version */
	u8	kpus;
	u8	reserved[7];

	/* Default MKEX profile to be used with this KPU profile. May be
	 * overridden with mkex_profile module parameter. Format is same as for
	 * the MKEX profile to streamline processing.
	 */
	struct npc_mcam_kex	mkex;
	/* LTYPE values for specific HW offloaded protocols. */
	struct npc_lt_def_cfg	lt_def;
	/* Dynamically sized data:
	 *  Custom KPU CAM and ACTION configuration entries.
	 * struct npc_kpu_fwdata kpu[kpus];
	 */
	u8	data[0];
} __packed;

struct rvu_npc_mcam_rule {
	struct flow_msg packet;
	struct flow_msg mask;
	u8 intf;
	union {
		struct nix_tx_action tx_action;
		struct nix_rx_action rx_action;
	};
	u64 vtag_action;
	struct list_head list;
	u64 features;
	u16 owner;
	u16 entry;
	u16 cntr;
	bool has_cntr;
	u8 default_rule;
	bool enable;
	bool vfvlan_cfg;
};

#endif /* NPC_H */
