/*******************************************************************************

  Intel 10 Gigabit PCI Express Linux driver
  Copyright(c) 1999 - 2011 Intel Corporation.

  This program is free software; you can redistribute it and/or modify it
  under the terms and conditions of the GNU General Public License,
  version 2, as published by the Free Software Foundation.

  This program is distributed in the hope it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
  more details.

  You should have received a copy of the GNU General Public License along with
  this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.

  The full GNU General Public License is included in this distribution in
  the file called "COPYING".

  Contact Information:
  e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
  Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497

*******************************************************************************/

#include "ixgbe.h"

#ifdef CONFIG_DCB
#include <linux/dcbnl.h>
#include "ixgbe_dcb_82598.h"
#include "ixgbe_dcb_82599.h"

/* Callbacks for DCB netlink in the kernel */
#define BIT_DCB_MODE    0x01
#define BIT_PFC         0x02
#define BIT_PG_RX       0x04
#define BIT_PG_TX       0x08
#define BIT_APP_UPCHG   0x10
#define BIT_RESETLINK   0x40
#define BIT_LINKSPEED   0x80

/* Responses for the DCB_C_SET_ALL command */
#define DCB_HW_CHG_RST  0  /* DCB configuration changed with reset */
#define DCB_NO_HW_CHG   1  /* DCB configuration did not change */
#define DCB_HW_CHG      2  /* DCB configuration changed, no reset */

/**
 * ixgbe_get_tc_from_up - get the TC UP is mapped to
 * @netdev : the corresponding netdev
 * @up: the 802.1p user priority value
 *
 * Returns : TC, UP is mapped to
 */
static u8 ixgbe_get_tc_from_up(struct net_device *netdev, u8 up)
{
	struct ixgbe_adapter *adapter;
	u32 up2tc, i;

	adapter = netdev_priv(netdev);

	/* if up to tc mapping change is pending then use the cache */
	if (adapter->dcb_set_bitmap & BIT_PG_RX) {
		struct tc_configuration *tc_cfg;
		for (i = 0; i < MAX_TRAFFIC_CLASS; i++) {
			tc_cfg = &adapter->temp_dcb_cfg.tc_config[i];
			if (tc_cfg->path[0].up_to_tc_bitmap & (1 << up))
				return i;
		}
	} else {
		/* from user priority to the corresponding traffic class */
		switch (adapter->hw.mac.type) {
		case ixgbe_mac_82599EB:
		case ixgbe_mac_X540:
			up2tc = IXGBE_READ_REG(&adapter->hw, IXGBE_RTRUP2TC);
			up2tc >>= (up * IXGBE_RTRUP2TC_UP_SHIFT);
			up2tc &= (MAX_TRAFFIC_CLASS - 1);
			break;
		default:
			up2tc = up;
			break;
		}

		return (u8)up2tc;
	}
	return 0;
}


#ifdef IXGBE_FCOE
static u8 ixgbe_get_up_from_tc(struct net_device *netdev, u8 tc)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);
	struct tc_configuration *tc_cfg;

	tc_cfg = &adapter->dcb_cfg.tc_config[tc];
	return tc_cfg->path[0].up_to_tc_bitmap;
}
#endif /* IXGBE_FCOE */

int ixgbe_copy_dcb_cfg(struct ixgbe_dcb_config *src_dcb_cfg,
		       struct ixgbe_dcb_config *dst_dcb_cfg, int tc_max)
{
	struct tc_configuration *src_tc_cfg = NULL;
	struct tc_configuration *dst_tc_cfg = NULL;
	int i;

	if (!src_dcb_cfg || !dst_dcb_cfg)
		return -EINVAL;

	for (i = DCB_PG_ATTR_TC_0; i < tc_max + DCB_PG_ATTR_TC_0; i++) {
		src_tc_cfg = &src_dcb_cfg->tc_config[i - DCB_PG_ATTR_TC_0];
		dst_tc_cfg = &dst_dcb_cfg->tc_config[i - DCB_PG_ATTR_TC_0];

		dst_tc_cfg->path[DCB_TX_CONFIG].prio_type =
				src_tc_cfg->path[DCB_TX_CONFIG].prio_type;

		dst_tc_cfg->path[DCB_TX_CONFIG].bwg_id =
				src_tc_cfg->path[DCB_TX_CONFIG].bwg_id;

		dst_tc_cfg->path[DCB_TX_CONFIG].bwg_percent =
				src_tc_cfg->path[DCB_TX_CONFIG].bwg_percent;

		dst_tc_cfg->path[DCB_TX_CONFIG].up_to_tc_bitmap =
				src_tc_cfg->path[DCB_TX_CONFIG].up_to_tc_bitmap;

		dst_tc_cfg->path[DCB_RX_CONFIG].prio_type =
				src_tc_cfg->path[DCB_RX_CONFIG].prio_type;

		dst_tc_cfg->path[DCB_RX_CONFIG].bwg_id =
				src_tc_cfg->path[DCB_RX_CONFIG].bwg_id;

		dst_tc_cfg->path[DCB_RX_CONFIG].bwg_percent =
				src_tc_cfg->path[DCB_RX_CONFIG].bwg_percent;

		dst_tc_cfg->path[DCB_RX_CONFIG].up_to_tc_bitmap =
				src_tc_cfg->path[DCB_RX_CONFIG].up_to_tc_bitmap;
	}

	for (i = DCB_PG_ATTR_BW_ID_0; i < DCB_PG_ATTR_BW_ID_MAX; i++) {
		dst_dcb_cfg->bw_percentage[DCB_TX_CONFIG]
			[i-DCB_PG_ATTR_BW_ID_0] = src_dcb_cfg->bw_percentage
				[DCB_TX_CONFIG][i-DCB_PG_ATTR_BW_ID_0];
		dst_dcb_cfg->bw_percentage[DCB_RX_CONFIG]
			[i-DCB_PG_ATTR_BW_ID_0] = src_dcb_cfg->bw_percentage
				[DCB_RX_CONFIG][i-DCB_PG_ATTR_BW_ID_0];
	}

	for (i = DCB_PFC_UP_ATTR_0; i < DCB_PFC_UP_ATTR_MAX; i++) {
		dst_dcb_cfg->tc_config[i - DCB_PFC_UP_ATTR_0].dcb_pfc =
			src_dcb_cfg->tc_config[i - DCB_PFC_UP_ATTR_0].dcb_pfc;
	}
	dst_dcb_cfg->pfc_mode_enable = src_dcb_cfg->pfc_mode_enable;

	return 0;
}

static u8 ixgbe_dcbnl_get_state(struct net_device *netdev)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);

	return !!(adapter->flags & IXGBE_FLAG_DCB_ENABLED);
}

static u8 ixgbe_dcbnl_set_state(struct net_device *netdev, u8 state)
{
	u8 err = 0;
	struct ixgbe_adapter *adapter = netdev_priv(netdev);

	if (state) {
		/* Turn on DCB */
		if (adapter->flags & IXGBE_FLAG_DCB_ENABLED)
			goto out;

		if (!(adapter->flags & IXGBE_FLAG_MSIX_ENABLED)) {
			e_err(drv, "Enable failed, needs MSI-X\n");
			err = 1;
			goto out;
		}

#ifndef HAVE_MQPRIO
		adapter->flags &= ~IXGBE_FLAG_RSS_ENABLED;

#endif
		adapter->flags |= IXGBE_FLAG_DCB_ENABLED;

		switch (adapter->hw.mac.type) {
		case ixgbe_mac_82598EB:
			adapter->last_lfc_mode = adapter->hw.fc.current_mode;
			adapter->hw.fc.requested_mode = ixgbe_fc_none;
			break;
		case ixgbe_mac_82599EB:
		case ixgbe_mac_X540:
			e_info(drv, "DCB enabled, disabling ATR\n");
			adapter->flags &= ~IXGBE_FLAG_FDIR_HASH_CAPABLE;
			break;
		default:
			break;
		}

		ixgbe_setup_tc(netdev, MAX_TRAFFIC_CLASS);
	} else {
		/* Turn off DCB */
		if (!(adapter->flags & IXGBE_FLAG_DCB_ENABLED))
			goto out;

		adapter->hw.fc.requested_mode = adapter->last_lfc_mode;
		adapter->temp_dcb_cfg.pfc_mode_enable = false;
		adapter->dcb_cfg.pfc_mode_enable = false;
		adapter->flags &= ~IXGBE_FLAG_DCB_ENABLED;
		adapter->flags |= IXGBE_FLAG_RSS_ENABLED;
		switch (adapter->hw.mac.type) {
		case ixgbe_mac_82599EB:
		case ixgbe_mac_X540:
			if (!(adapter->flags & IXGBE_FLAG_FDIR_PERFECT_CAPABLE))
				adapter->flags |= IXGBE_FLAG_FDIR_HASH_CAPABLE;
			break;
		default:
			break;
		}
		ixgbe_setup_tc(netdev, 0);
	}

out:
	return err;
}

static void ixgbe_dcbnl_get_perm_hw_addr(struct net_device *netdev,
					 u8 *perm_addr)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);
	int i, j;

	memset(perm_addr, 0xff, MAX_ADDR_LEN);

	for (i = 0; i < netdev->addr_len; i++)
		perm_addr[i] = adapter->hw.mac.perm_addr[i];

	switch (adapter->hw.mac.type) {
	case ixgbe_mac_82599EB:
	case ixgbe_mac_X540:
		for (j = 0; j < netdev->addr_len; j++, i++)
			perm_addr[i] = adapter->hw.mac.san_addr[j];
		break;
	default:
		break;
	}
}

static void ixgbe_dcbnl_set_pg_tc_cfg_tx(struct net_device *netdev, int tc,
					 u8 prio, u8 bwg_id, u8 bw_pct,
					 u8 up_map)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);

	if (prio != DCB_ATTR_VALUE_UNDEFINED)
		adapter->temp_dcb_cfg.tc_config[tc].path[0].prio_type = prio;
	if (bwg_id != DCB_ATTR_VALUE_UNDEFINED)
		adapter->temp_dcb_cfg.tc_config[tc].path[0].bwg_id = bwg_id;
	if (bw_pct != DCB_ATTR_VALUE_UNDEFINED)
		adapter->temp_dcb_cfg.tc_config[tc].path[0].bwg_percent =
			bw_pct;
	if (up_map != DCB_ATTR_VALUE_UNDEFINED)
		adapter->temp_dcb_cfg.tc_config[tc].path[0].up_to_tc_bitmap =
			up_map;

	if ((adapter->temp_dcb_cfg.tc_config[tc].path[0].prio_type !=
	     adapter->dcb_cfg.tc_config[tc].path[0].prio_type) ||
	    (adapter->temp_dcb_cfg.tc_config[tc].path[0].bwg_id !=
	     adapter->dcb_cfg.tc_config[tc].path[0].bwg_id) ||
	    (adapter->temp_dcb_cfg.tc_config[tc].path[0].bwg_percent !=
	     adapter->dcb_cfg.tc_config[tc].path[0].bwg_percent) ||
	    (adapter->temp_dcb_cfg.tc_config[tc].path[0].up_to_tc_bitmap !=
	     adapter->dcb_cfg.tc_config[tc].path[0].up_to_tc_bitmap))
		adapter->dcb_set_bitmap |= BIT_PG_TX;
}

static void ixgbe_dcbnl_set_pg_bwg_cfg_tx(struct net_device *netdev, int bwg_id,
					  u8 bw_pct)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);

	adapter->temp_dcb_cfg.bw_percentage[0][bwg_id] = bw_pct;

	if (adapter->temp_dcb_cfg.bw_percentage[0][bwg_id] !=
	    adapter->dcb_cfg.bw_percentage[0][bwg_id])
		adapter->dcb_set_bitmap |= BIT_PG_TX;
}

static void ixgbe_dcbnl_set_pg_tc_cfg_rx(struct net_device *netdev, int tc,
					 u8 prio, u8 bwg_id, u8 bw_pct,
					 u8 up_map)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);

	if (prio != DCB_ATTR_VALUE_UNDEFINED)
		adapter->temp_dcb_cfg.tc_config[tc].path[1].prio_type = prio;
	if (bwg_id != DCB_ATTR_VALUE_UNDEFINED)
		adapter->temp_dcb_cfg.tc_config[tc].path[1].bwg_id = bwg_id;
	if (bw_pct != DCB_ATTR_VALUE_UNDEFINED)
		adapter->temp_dcb_cfg.tc_config[tc].path[1].bwg_percent =
			bw_pct;
	if (up_map != DCB_ATTR_VALUE_UNDEFINED)
		adapter->temp_dcb_cfg.tc_config[tc].path[1].up_to_tc_bitmap =
			up_map;

	if ((adapter->temp_dcb_cfg.tc_config[tc].path[1].prio_type !=
	     adapter->dcb_cfg.tc_config[tc].path[1].prio_type) ||
	    (adapter->temp_dcb_cfg.tc_config[tc].path[1].bwg_id !=
	     adapter->dcb_cfg.tc_config[tc].path[1].bwg_id) ||
	    (adapter->temp_dcb_cfg.tc_config[tc].path[1].bwg_percent !=
	     adapter->dcb_cfg.tc_config[tc].path[1].bwg_percent) ||
	    (adapter->temp_dcb_cfg.tc_config[tc].path[1].up_to_tc_bitmap !=
	     adapter->dcb_cfg.tc_config[tc].path[1].up_to_tc_bitmap))
		adapter->dcb_set_bitmap |= BIT_PG_RX;
}

static void ixgbe_dcbnl_set_pg_bwg_cfg_rx(struct net_device *netdev, int bwg_id,
					  u8 bw_pct)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);

	adapter->temp_dcb_cfg.bw_percentage[1][bwg_id] = bw_pct;

	if (adapter->temp_dcb_cfg.bw_percentage[1][bwg_id] !=
	    adapter->dcb_cfg.bw_percentage[1][bwg_id])
		adapter->dcb_set_bitmap |= BIT_PG_RX;
}

static void ixgbe_dcbnl_get_pg_tc_cfg_tx(struct net_device *netdev, int tc,
					 u8 *prio, u8 *bwg_id, u8 *bw_pct,
					 u8 *up_map)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);

	*prio = adapter->dcb_cfg.tc_config[tc].path[0].prio_type;
	*bwg_id = adapter->dcb_cfg.tc_config[tc].path[0].bwg_id;
	*bw_pct = adapter->dcb_cfg.tc_config[tc].path[0].bwg_percent;
	*up_map = adapter->dcb_cfg.tc_config[tc].path[0].up_to_tc_bitmap;
}

static void ixgbe_dcbnl_get_pg_bwg_cfg_tx(struct net_device *netdev, int bwg_id,
					  u8 *bw_pct)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);

	*bw_pct = adapter->dcb_cfg.bw_percentage[0][bwg_id];
}

static void ixgbe_dcbnl_get_pg_tc_cfg_rx(struct net_device *netdev, int tc,
					 u8 *prio, u8 *bwg_id, u8 *bw_pct,
					 u8 *up_map)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);

	*prio = adapter->dcb_cfg.tc_config[tc].path[1].prio_type;
	*bwg_id = adapter->dcb_cfg.tc_config[tc].path[1].bwg_id;
	*bw_pct = adapter->dcb_cfg.tc_config[tc].path[1].bwg_percent;
	*up_map = adapter->dcb_cfg.tc_config[tc].path[1].up_to_tc_bitmap;
}

static void ixgbe_dcbnl_get_pg_bwg_cfg_rx(struct net_device *netdev, int bwg_id,
					  u8 *bw_pct)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);

	*bw_pct = adapter->dcb_cfg.bw_percentage[1][bwg_id];
}

static void ixgbe_dcbnl_set_pfc_cfg(struct net_device *netdev, int priority,
				    u8 setting)
{
	u8 tc = priority;
	struct ixgbe_adapter *adapter = netdev_priv(netdev);

	tc = ixgbe_get_tc_from_up(netdev, priority);

	adapter->temp_dcb_cfg.tc_config[tc].dcb_pfc = setting;
	if (adapter->temp_dcb_cfg.tc_config[tc].dcb_pfc !=
	    adapter->dcb_cfg.tc_config[tc].dcb_pfc) {
		adapter->dcb_set_bitmap |= BIT_PFC;
		adapter->temp_dcb_cfg.pfc_mode_enable = true;
	}
}

static void ixgbe_dcbnl_get_pfc_cfg(struct net_device *netdev, int priority,
				    u8 *setting)
{
	u8 tc = priority;
	struct ixgbe_adapter *adapter = netdev_priv(netdev);

	tc = ixgbe_get_tc_from_up(netdev, priority);
	*setting = adapter->dcb_cfg.tc_config[tc].dcb_pfc;
}

static u8 ixgbe_dcbnl_set_all(struct net_device *netdev)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);
	int ret;
#ifdef IXGBE_FCOE
	u8 up = ixgbe_get_up_from_tc(netdev, adapter->fcoe.tc);
#endif /* IXGBE_FCOE */

	ret = ixgbe_copy_dcb_cfg(&adapter->temp_dcb_cfg, &adapter->dcb_cfg,
				 MAX_TRAFFIC_CLASS);
	if (ret)
		return DCB_NO_HW_CHG;

#ifdef IXGBE_FCOE
	if (up && (up != (1 << adapter->fcoe.up)))
		adapter->dcb_set_bitmap |= BIT_APP_UPCHG;

	/*
	 * Only take down the adapter if an app change occured. FCoE
	 * may shuffle tx rings in this case and this can not be done
	 * without a reset currently.
	 */
	if (adapter->dcb_set_bitmap & BIT_APP_UPCHG) {
		while (test_and_set_bit(__IXGBE_RESETTING, &adapter->state))
			usleep_range(1000, 2000);

		ixgbe_fcoe_setapp(adapter, 1 << adapter->fcoe.up);

		if (netif_running(netdev))
#ifdef HAVE_NET_DEVICE_OPS
			netdev->netdev_ops->ndo_stop(netdev);
#else
			netdev->stop(netdev);
#endif
		ixgbe_clear_interrupt_scheme(adapter);
	}
#endif /* IXGBE_FCOE */

	if (adapter->dcb_cfg.pfc_mode_enable) {
		switch (adapter->hw.mac.type) {
		case ixgbe_mac_82599EB:
		case ixgbe_mac_X540:
			if (adapter->hw.fc.current_mode != ixgbe_fc_pfc)
				adapter->last_lfc_mode =
						  adapter->hw.fc.current_mode;
			break;
		default:
			break;
		}
		adapter->hw.fc.requested_mode = ixgbe_fc_pfc;
	} else {
		switch (adapter->hw.mac.type) {
		case ixgbe_mac_82598EB:
			adapter->hw.fc.requested_mode = ixgbe_fc_none;
			break;
		case ixgbe_mac_82599EB:
		case ixgbe_mac_X540:
			adapter->hw.fc.requested_mode = adapter->last_lfc_mode;
			break;
		default:
			break;
		}
	}

	if (adapter->dcb_set_bitmap & BIT_APP_UPCHG) {
		ixgbe_init_interrupt_scheme(adapter);
		if (netif_running(netdev))
#ifdef HAVE_NET_DEVICE_OPS
			netdev->netdev_ops->ndo_open(netdev);
#else
			netdev->open(netdev);
#endif
		ret = DCB_HW_CHG_RST;
	}

	if (adapter->dcb_set_bitmap & BIT_PFC) {
		ixgbe_dcb_config_pfc(&adapter->hw, &adapter->dcb_cfg);
		ret = DCB_HW_CHG;
	}

	if (adapter->dcb_set_bitmap & (BIT_PG_TX | BIT_PG_RX)) {
		ixgbe_dcb_config_rx_arbiter_82599(&adapter->hw,
						  &adapter->dcb_cfg);
		ixgbe_dcb_config_tx_desc_arbiter_82599(&adapter->hw,
						       &adapter->dcb_cfg);
		ixgbe_dcb_config_tx_data_arbiter_82599(&adapter->hw,
						       &adapter->dcb_cfg);
	}

	if (adapter->dcb_cfg.pfc_mode_enable)
		adapter->hw.fc.current_mode = ixgbe_fc_pfc;

	if (adapter->dcb_set_bitmap & BIT_APP_UPCHG)
		clear_bit(__IXGBE_RESETTING, &adapter->state);
	adapter->dcb_set_bitmap = 0x00;
	return ret;
}

static u8 ixgbe_dcbnl_getcap(struct net_device *netdev, int capid, u8 *cap)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);
	u8 rval = 0;

	if (adapter->flags & IXGBE_FLAG_DCB_ENABLED) {
		switch (capid) {
		case DCB_CAP_ATTR_PG:
			*cap = true;
			break;
		case DCB_CAP_ATTR_PFC:
			*cap = true;
			break;
		case DCB_CAP_ATTR_UP2TC:
			*cap = false;
			break;
		case DCB_CAP_ATTR_PG_TCS:
			*cap = 0x80;
			break;
		case DCB_CAP_ATTR_PFC_TCS:
			*cap = 0x80;
			break;
		case DCB_CAP_ATTR_GSP:
			*cap = true;
			break;
		case DCB_CAP_ATTR_BCN:
			*cap = false;
			break;
		default:
			rval = -EINVAL;
			break;
		}
	} else {
		rval = -EINVAL;
	}

	return rval;
}

static u8 ixgbe_dcbnl_getnumtcs(struct net_device *netdev, int tcid, u8 *num)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);
	u8 rval = 0;

	if (adapter->flags & IXGBE_FLAG_DCB_ENABLED) {
		switch (tcid) {
		case DCB_NUMTCS_ATTR_PG:
			*num = adapter->dcb_cfg.num_tcs.pg_tcs;
			break;
		case DCB_NUMTCS_ATTR_PFC:
			*num = adapter->dcb_cfg.num_tcs.pfc_tcs;
			break;
		default:
			rval = -EINVAL;
			break;
		}
	} else {
		rval = -EINVAL;
	}

	return rval;
}

static u8 ixgbe_dcbnl_setnumtcs(struct net_device *netdev, int tcid, u8 num)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);
	u8 rval = 0;

	if (adapter->flags & IXGBE_FLAG_DCB_ENABLED) {
		switch (tcid) {
		case DCB_NUMTCS_ATTR_PG:
			adapter->dcb_cfg.num_tcs.pg_tcs = num;
			break;
		case DCB_NUMTCS_ATTR_PFC:
			adapter->dcb_cfg.num_tcs.pfc_tcs = num;
			break;
		default:
			rval = -EINVAL;
			break;
		}
	} else {
		rval = -EINVAL;
	}

	return rval;
}

static u8 ixgbe_dcbnl_getpfcstate(struct net_device *netdev)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);

	return adapter->dcb_cfg.pfc_mode_enable;
}

static void ixgbe_dcbnl_setpfcstate(struct net_device *netdev, u8 state)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);

	adapter->temp_dcb_cfg.pfc_mode_enable = state;
	if (adapter->temp_dcb_cfg.pfc_mode_enable != 
	    adapter->dcb_cfg.pfc_mode_enable)
		adapter->dcb_set_bitmap |= BIT_PFC;
	return;
}

#ifdef HAVE_DCBNL_OPS_GETAPP
/**
 * ixgbe_dcbnl_getapp - retrieve the DCBX application user priority
 * @netdev : the corresponding netdev
 * @idtype : identifies the id as ether type or TCP/UDP port number
 * @id: id is either ether type or TCP/UDP port number
 *
 * Returns : on success, returns a non-zero 802.1p user priority bitmap
 * otherwise returns 0 as the invalid user priority bitmap to indicate an
 * error.
 */
static u8 ixgbe_dcbnl_getapp(struct net_device *netdev, u8 idtype, u16 id)
{
	u8 rval = 0;
#ifdef HAVE_DCBNL_IEEE
	struct dcb_app app = {
				.selector = idtype,
				.protocol = id,
			     };

	rval = dcb_getapp(netdev, &app);
#endif

	switch (idtype) {
	case DCB_APP_IDTYPE_ETHTYPE:
#ifdef IXGBE_FCOE
		if (id == ETH_P_FCOE)
			rval = ixgbe_fcoe_getapp(netdev_priv(netdev));
#endif
		break;
	case DCB_APP_IDTYPE_PORTNUM:
		break;
	default:
		break;
	}

	return rval;
}

/**
 * ixgbe_dcbnl_setapp - set the DCBX application user priority
 * @netdev : the corresponding netdev
 * @idtype : identifies the id as ether type or TCP/UDP port number
 * @id: id is either ether type or TCP/UDP port number
 * @up: the 802.1p user priority bitmap
 *
 * Returns : 0 on success or 1 on error
 */
static u8 ixgbe_dcbnl_setapp(struct net_device *netdev,
                             u8 idtype, u16 id, u8 up)
{
	int err = 0;
#ifdef HAVE_DCBNL_IEEE
	struct dcb_app app;

	app.selector = idtype;
	app.protocol = id;
	app.priority = up;
	err = dcb_setapp(netdev, &app);
#endif

	switch (idtype) {
	case DCB_APP_IDTYPE_ETHTYPE:
#ifdef IXGBE_FCOE
		if (id == ETH_P_FCOE) {
			struct ixgbe_adapter *adapter = netdev_priv(netdev);

			adapter->fcoe.up = ffs(up) - 1;
		}
#endif
		break;
	case DCB_APP_IDTYPE_PORTNUM:
		break;
	default:
		break;
	}

	return err;
}
#endif /* HAVE_DCBNL_OPS_GETAPP */

struct dcbnl_rtnl_ops dcbnl_ops = {
	.getstate	= ixgbe_dcbnl_get_state,
	.setstate	= ixgbe_dcbnl_set_state,
	.getpermhwaddr	= ixgbe_dcbnl_get_perm_hw_addr,
	.setpgtccfgtx	= ixgbe_dcbnl_set_pg_tc_cfg_tx,
	.setpgbwgcfgtx	= ixgbe_dcbnl_set_pg_bwg_cfg_tx,
	.setpgtccfgrx	= ixgbe_dcbnl_set_pg_tc_cfg_rx,
	.setpgbwgcfgrx	= ixgbe_dcbnl_set_pg_bwg_cfg_rx,
	.getpgtccfgtx	= ixgbe_dcbnl_get_pg_tc_cfg_tx,
	.getpgbwgcfgtx	= ixgbe_dcbnl_get_pg_bwg_cfg_tx,
	.getpgtccfgrx	= ixgbe_dcbnl_get_pg_tc_cfg_rx,
	.getpgbwgcfgrx	= ixgbe_dcbnl_get_pg_bwg_cfg_rx,
	.setpfccfg	= ixgbe_dcbnl_set_pfc_cfg,
	.getpfccfg	= ixgbe_dcbnl_get_pfc_cfg,
	.setall		= ixgbe_dcbnl_set_all,
	.getcap		= ixgbe_dcbnl_getcap,
	.getnumtcs	= ixgbe_dcbnl_getnumtcs,
	.setnumtcs	= ixgbe_dcbnl_setnumtcs,
	.getpfcstate	= ixgbe_dcbnl_getpfcstate,
	.setpfcstate	= ixgbe_dcbnl_setpfcstate,
#ifdef HAVE_DCBNL_OPS_GETAPP
	.getapp		= ixgbe_dcbnl_getapp,
	.setapp		= ixgbe_dcbnl_setapp,
#endif
};
#endif
