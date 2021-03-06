#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "sm_config.h"
#include "sm_common.h"
#include "sm_port_evt.h"
#include "sm_port.h"

#if (SM_PORT_PLATFORM == SM_PORT_NORDIC)

#include "sdk_config.h"
#include "sdk_common.h"
#include "nrf_error.h"
#include "ble_err.h"
#include "nrf.h"
#include "nordic_common.h"
#include "ble_gattc.h"
#include "ble_db_discovery.h"
#include "ble_srv_common.h"
#include "nrf_ipc.h"

extern sm_bdaddr_t g_nrf_peer_bd;
static uint16_t g_nrf_proxyc_data_in_hdl;
static uint16_t g_nrf_proxyc_data_out_hdl;
static uint16_t g_nrf_proxyc_data_out_cccd_hdl;
static ble_db_discovery_t g_nfr_ble_db_discovery;

static void _nrf_proxyc_db_disc_handler(ble_db_discovery_evt_t * disc_evt)
{
    if (   (disc_evt->evt_type != BLE_DB_DISCOVERY_COMPLETE)
        || (disc_evt->params.discovered_db.srv_uuid.uuid != MESH_PROXY_SVC_UUID)
        || (disc_evt->params.discovered_db.srv_uuid.type != BLE_UUID_TYPE_BLE))
    {
        return;
    }

    for (uint32_t i = 0; i < disc_evt->params.discovered_db.char_count; i++)
    {
        ble_uuid_t uuid = disc_evt->params.discovered_db.charateristics[i].characteristic.uuid;

        if (uuid.uuid == MESH_PROXY_CHAR_DATA_OUT_VAL_UUID && uuid.type == BLE_UUID_TYPE_BLE)
        {
            g_nrf_proxyc_data_out_hdl = disc_evt->params.discovered_db.charateristics[i].characteristic.handle_value;
            g_nrf_proxyc_data_out_cccd_hdl = disc_evt->params.discovered_db.charateristics[i].cccd_handle;
            
            ble_gattc_write_params_t write_params =
            {
                .write_op   = BLE_GATT_OP_WRITE_REQ,
                .handle     = g_nrf_proxyc_data_out_cccd_hdl,
                .len        = 2,
                .offset     = 0,
            };
            uint8_t val[2];
            val[0] = 0x01;
            val[1] = 0x00;
            write_params.p_value = val;
            
            sd_ble_gattc_write(disc_evt->conn_handle, &write_params);
        }
        
        if (uuid.uuid == MESH_PROXY_CHAR_DATA_IN_VAL_UUID && uuid.type == BLE_UUID_TYPE_BLE)
        {
            g_nrf_proxyc_data_in_hdl = disc_evt->params.discovered_db.charateristics[i].characteristic.handle_value;
        }
    }
    
}

static void _nrf_proxyc_on_ble_gap_evt_connected(ble_gap_evt_t const * gap_evt)
{
    ble_uuid_t prov_uuid;

    prov_uuid.type = BLE_UUID_TYPE_BLE;
    prov_uuid.uuid = MESH_PROXY_SVC_UUID;
    
    sd_ble_gap_scan_stop();
    sd_ble_gap_adv_stop();

    ble_uuid_t ble_uuid;
    ble_uuid.type = BLE_UUID_TYPE_BLE;
    ble_uuid.uuid = MESH_PROXY_SVC_UUID;
    ble_db_discovery_evt_register(&ble_uuid);
    ble_db_discovery_init(_nrf_proxyc_db_disc_handler);

    sm_memset(&g_nfr_ble_db_discovery, 0x00, sizeof(g_nfr_ble_db_discovery));
    ble_db_discovery_evt_register(&prov_uuid);
    ble_db_discovery_start(&g_nfr_ble_db_discovery, gap_evt->conn_handle);
    
}

static void _nrf_proxyc_on_ble_gap_evt_disconnected(ble_gap_evt_t const * gap_evt)
{
}

static void _nrf_proxyc_on_ble_gattc_evt_write_rsp(ble_gattc_evt_t const * gattc_evt)
{
    if (g_nrf_proxyc_data_in_hdl == gattc_evt->params.write_rsp.handle)
    {
        nrf_ipc_prov_proxy_sent_param_t sent;
        sent.status = true;
        sent.conn_handle = gattc_evt->conn_handle;
        nrf_ipc_write(NRF_IPC_TAG_PROXYC_SENT, (nrf_ipc_write_param_t*)&sent);
    }
    else if (g_nrf_proxyc_data_out_cccd_hdl == gattc_evt->params.write_rsp.handle)
    {
        sd_ble_gattc_exchange_mtu_request(gattc_evt->conn_handle, MESH_PROXY_SVC_MTU);
    }
}

static void _nrf_proxyc_on_ble_gattc_evt_hvx(ble_gattc_evt_t* gattc_evt)
{
    ble_gattc_evt_hvx_t* hvx_evt = &gattc_evt->params.hvx;

    if (g_nrf_proxyc_data_out_hdl == hvx_evt->handle)
    {
        nrf_ipc_prov_proxy_data_t data;
        data.data = hvx_evt->data;
        data.data_len = hvx_evt->len;
        data.conn_handle = gattc_evt->conn_handle;
        nrf_ipc_write(NRF_IPC_TAG_PROXYC_DATA_IN, (nrf_ipc_write_param_t*)&data);
    }
}

void nrf_proxyc_on_ble_evt(ble_evt_t* ble_evt)
{
    ble_gap_evt_t* gap_evt;
    ble_gattc_evt_t* gattc_evt;

    switch (ble_evt->header.evt_id)
    {
        case BLE_GAP_EVT_CONNECTED:
            gap_evt = &ble_evt->evt.gap_evt;
            _nrf_proxyc_on_ble_gap_evt_connected(gap_evt);
            break;

        case BLE_GAP_EVT_DISCONNECTED:
            gap_evt = &ble_evt->evt.gap_evt;
            _nrf_proxyc_on_ble_gap_evt_disconnected(gap_evt);
            break;
        case BLE_GATTC_EVT_WRITE_RSP:
            gattc_evt = (ble_gattc_evt_t*)&ble_evt->evt.gattc_evt;
            _nrf_proxyc_on_ble_gattc_evt_write_rsp(gattc_evt);
            break;
        case BLE_GATTC_EVT_HVX:
            _nrf_proxyc_on_ble_gattc_evt_hvx(&ble_evt->evt.gattc_evt);
            break;
        case BLE_GATTC_EVT_EXCHANGE_MTU_RSP:
            nrf_ipc_connected_param_t conn;
            conn.bd = &g_nrf_peer_bd;
            conn.conn_hdl = ble_evt->evt.gattc_evt.conn_handle;
            nrf_ipc_write(NRF_IPC_TAG_CONNECTED, (nrf_ipc_write_param_t*)&conn);
        default:
            ble_db_discovery_on_ble_evt(&g_nfr_ble_db_discovery, ble_evt);
            break;
    }
}


void smport_proxy_add_client(void)
{
}

void smport_proxy_client_send_pdu(uint16_t conn_hdl, uint8_t* data, uint16_t len)
{
    uint32_t err;
    ble_gattc_write_params_t write_params =
    {
        .write_op   = BLE_GATT_OP_WRITE_CMD,
        //.flags      = BLE_GATT_EXEC_WRITE_FLAG_PREPARED_WRITE,
        .handle     = g_nrf_proxyc_data_in_hdl,
        .len        = len,
        .offset     = 0,
    };
    uint8_t* val = smport_malloc(len, SM_MEM_NON_RETENTION);
    sm_memcpy(val, data, len);
    write_params.p_value = val;
    
    err = sd_ble_gattc_write(conn_hdl, &write_params);
    
    smport_free(val);
}

uint16_t smport_proxy_client_get_mtu(uint16_t conn_hdl)
{
    return MESH_PROXY_SVC_MTU;
}

#endif
