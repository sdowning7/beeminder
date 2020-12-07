/* Hello World Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include "nvs.h"
#include "nvs_flash.h"
#include <unistd.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_spi_flash.h"
#include "esp_sleep.h"
#include "esp_bt.h"
#include "esp_gap_ble_api.h"
#include "esp_gattc_api.h"
#include "esp_gatt_defs.h"
#include "esp_bt_main.h"
#include "esp_gatt_common_api.h"
#include "esp_log.h"
#include "driver/uart.h"
#include "driver/rtc_io.h"
#include "driver/i2s.h"
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "freertos/queue.h"


#define GATTC_TAG                   "HIVE_CLIENT"
#define REMOTE_SERVICE_UUID         0x00FF
#define REMOTE_NOTIFY_CHAR_UUID     0xFF01
#define PROFILE_NUM                 1
#define PROFILE_A_APP_ID            0
#define INVALID_HANDLE              0
#define MAX_SEND                    496

#define HIVE_TASK_PERIOD            1000*30
#define HIVE_RETRY_PERIOD           1000

#define WEIGHT_GPIO                 18
#define WEIGHT_CLK                  19

#define I2S_SAMPLE_RATE             16000
#define I2S_SAMPLE_BITS             16
#define I2S_READ_LEN                16*1024

#define FLASH_SECTOR_SIZE           (0x1000)
#define FLASH_ADDR                  (0x200000)

#define FLASH_RECORD_SIZE           (I2S_SAMPLE_RATE*I2S_SAMPLE_BITS/8*5)
#define FLASH_ERASE_SIZE            (FLASH_RECORD_SIZE%FLASH_SECTOR_SIZE==0) ? FLASH_RECORD_SIZE : FLASH_RECORD_SIZE + (FLASH_SECTOR_SIZE - FLASH_RECORD_SIZE % FLASH_SECTOR_SIZE)
#define PARTITION_NAME              "storage"

#define _I2C_NUMBER(num) I2C_NUM_##num
#define I2C_NUMBER(num) _I2C_NUMBER(num)

#define DATA_LENGTH 16                  /*!< Data buffer length of test buffer */
#define DELAY_TIME_BETWEEN_ITEMS_MS 1000 /*!< delay time between different test items */

#define I2C_MASTER_SCL_IO 16               /*!< gpio number for I2C master clock */
#define I2C_MASTER_SDA_IO 17               /*!< gpio number for I2C master data  */
#define I2C_MASTER_NUM 0 /*!< I2C port number for master dev */
#define I2C_MASTER_FREQ_HZ 100000        /*!< I2C master clock frequency */

#define ESP_SLAVE_ADDR 0x38 /*!< ESP32 slave address, you can set any 7bit value */
#define WRITE_BIT I2C_MASTER_WRITE              /*!< I2C master write */
#define READ_BIT I2C_MASTER_READ                /*!< I2C master read */
#define ACK_CHECK_EN 0x1                        /*!< I2C master will check ack from slave*/
#define ACK_CHECK_DIS 0x0                       /*!< I2C master will not check ack from slave */
#define ACK_VAL 0x0                             /*!< I2C ack value */
#define NACK_VAL 0x1                            /*!< I2C nack value */


//static const char remote_device_name[] = "HIVE_SERVER";     //Use this one for connecting to ESP
static const char remote_device_name[] = "HIVE BASE";       //Use this one for connecting to Pi
static bool connect    = false;
static bool get_server = false;
static bool can_send_write = false;
static bool is_connect = false;

static esp_gattc_char_elem_t *char_elem_result   = NULL;
static esp_gattc_descr_elem_t *descr_elem_result = NULL;
static SemaphoreHandle_t gattc_semaphore;


/* Declare static functions */
static void esp_gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param);
static void esp_gattc_cb(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param);
static void gattc_profile_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param);
static void get_sensor_data();
static void send_data_to_server();
static void safe_send(uint16_t dsize, uint8_t* daddr);
void erase_flash(void);
void record_audio_to_flash();
void send_audio_from_flash();


static esp_bt_uuid_t remote_filter_service_uuid = {
    .len = ESP_UUID_LEN_16,
    .uuid = {.uuid16 = REMOTE_SERVICE_UUID,},
};

static esp_bt_uuid_t remote_filter_char_uuid = {
    .len = ESP_UUID_LEN_16,
    .uuid = {.uuid16 = REMOTE_NOTIFY_CHAR_UUID,},
};

static esp_bt_uuid_t notify_descr_uuid = {
    .len = ESP_UUID_LEN_16,
    .uuid = {.uuid16 = ESP_GATT_UUID_CHAR_CLIENT_CONFIG,},
};

static esp_ble_scan_params_t ble_scan_params = {
    .scan_type              = BLE_SCAN_TYPE_ACTIVE,
    .own_addr_type          = BLE_ADDR_TYPE_PUBLIC,
    .scan_filter_policy     = BLE_SCAN_FILTER_ALLOW_ALL,
    .scan_interval          = 0x50,
    .scan_window            = 0x30,
    .scan_duplicate         = BLE_SCAN_DUPLICATE_DISABLE
};

struct gattc_profile_inst {
    esp_gattc_cb_t gattc_cb;
    uint16_t gattc_if;
    uint16_t app_id;
    uint16_t conn_id;
    uint16_t service_start_handle;
    uint16_t service_end_handle;
    uint16_t char_handle;
    esp_bd_addr_t remote_bda;
};

//MEMS
static const int i2s_num = 0; // i2s port number

static const i2s_config_t i2s_config = {
    .mode = I2S_MODE_MASTER | I2S_MODE_RX,
    .sample_rate = I2S_SAMPLE_RATE,
    .bits_per_sample = I2S_SAMPLE_BITS,
    .channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT,
    .communication_format = I2S_COMM_FORMAT_I2S,
    .intr_alloc_flags = 0, // default interrupt priority
    .dma_buf_count = 8,
    .dma_buf_len = 64,
    .use_apll = false
};

static const i2s_pin_config_t pin_config = {
    .bck_io_num = 26, //ESP32 sends clock
    .ws_io_num = 25,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = 33 //ESP32 takes in data
};



typedef struct {
    unsigned long weight;
    unsigned long temp;
    unsigned long humid;
}sensor_data;

sensor_data data = {};
//uint8_t* audio_data;
int flash_wr_size = 0;

/* One gatt-based profile one app_id and one gattc_if, this array will store the gattc_if returned by ESP_GATTS_REG_EVT */
static struct gattc_profile_inst gl_profile_tab[PROFILE_NUM] = {
    [PROFILE_A_APP_ID] = {
        .gattc_cb = gattc_profile_event_handler,
        .gattc_if = ESP_GATT_IF_NONE,       /* Not get the gatt_if, so initial is ESP_GATT_IF_NONE */
    },
};

static void gattc_profile_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param)
{
    esp_ble_gattc_cb_param_t *p_data = (esp_ble_gattc_cb_param_t *)param;
    ESP_LOGE(GATTC_TAG, "NEW EVENT: %d", event);
    switch (event) {
    case ESP_GATTC_REG_EVT:{
        ESP_LOGI(GATTC_TAG, "REG_EVT");
        esp_err_t scan_ret = esp_ble_gap_set_scan_params(&ble_scan_params);
        if (scan_ret){
            ESP_LOGE(GATTC_TAG, "set scan params error, error code = %x", scan_ret);
        }
        break;
    }
    case ESP_GATTC_CONNECT_EVT:{
        is_connect = true;
        ESP_LOGI(GATTC_TAG, "ESP_GATTC_CONNECT_EVT conn_id %d, if %d", p_data->connect.conn_id, gattc_if);
        gl_profile_tab[PROFILE_A_APP_ID].conn_id = p_data->connect.conn_id;
        memcpy(gl_profile_tab[PROFILE_A_APP_ID].remote_bda, p_data->connect.remote_bda, sizeof(esp_bd_addr_t));
        ESP_LOGI(GATTC_TAG, "REMOTE BDA:");
        esp_log_buffer_hex(GATTC_TAG, gl_profile_tab[PROFILE_A_APP_ID].remote_bda, sizeof(esp_bd_addr_t));
        esp_err_t mtu_ret = esp_ble_gattc_send_mtu_req (gattc_if, p_data->connect.conn_id);
        if (mtu_ret){
            ESP_LOGE(GATTC_TAG, "config MTU error, error code = %x", mtu_ret);
        }
        break;
    }
    case ESP_GATTC_OPEN_EVT:{
        if (param->open.status != ESP_GATT_OK){
            ESP_LOGE(GATTC_TAG, "open failed, status %d", p_data->open.status);
            break;
        }
        ESP_LOGI(GATTC_TAG, "open success");
        break;
    }
    case ESP_GATTC_DIS_SRVC_CMPL_EVT:{
        if (param->dis_srvc_cmpl.status != ESP_GATT_OK){
            ESP_LOGE(GATTC_TAG, "discover service failed, status %d", param->dis_srvc_cmpl.status);
            break;
        }
        ESP_LOGI(GATTC_TAG, "discover service complete conn_id %d", param->dis_srvc_cmpl.conn_id);
        esp_ble_gattc_search_service(gattc_if, param->cfg_mtu.conn_id, &remote_filter_service_uuid);
        break;
    }
    case ESP_GATTC_CFG_MTU_EVT:{
        if (param->cfg_mtu.status != ESP_GATT_OK){
            ESP_LOGE(GATTC_TAG,"config mtu failed, error status = %x", param->cfg_mtu.status);
        }
        ESP_LOGI(GATTC_TAG, "ESP_GATTC_CFG_MTU_EVT, Status %d, MTU %d, conn_id %d", param->cfg_mtu.status, param->cfg_mtu.mtu, param->cfg_mtu.conn_id);
        break;
    }
    case ESP_GATTC_SEARCH_RES_EVT:{
        ESP_LOGI(GATTC_TAG, "SEARCH RES: conn_id = %x is primary service %d", p_data->search_res.conn_id, p_data->search_res.is_primary);
        ESP_LOGI(GATTC_TAG, "start handle %d end handle %d current handle value %d", p_data->search_res.start_handle, p_data->search_res.end_handle, p_data->search_res.srvc_id.inst_id);
        if (p_data->search_res.srvc_id.uuid.len == ESP_UUID_LEN_16 && p_data->search_res.srvc_id.uuid.uuid.uuid16 == REMOTE_SERVICE_UUID) {
            ESP_LOGI(GATTC_TAG, "service found");
            get_server = true;
            gl_profile_tab[PROFILE_A_APP_ID].service_start_handle = p_data->search_res.start_handle;
            gl_profile_tab[PROFILE_A_APP_ID].service_end_handle = p_data->search_res.end_handle;
            ESP_LOGI(GATTC_TAG, "UUID16: %x", p_data->search_res.srvc_id.uuid.uuid.uuid16);
        }
        break;
    }
    case ESP_GATTC_SEARCH_CMPL_EVT:{
        if (p_data->search_cmpl.status != ESP_GATT_OK){
            ESP_LOGE(GATTC_TAG, "search service failed, error status = %x", p_data->search_cmpl.status);
            break;
        }
        if(p_data->search_cmpl.searched_service_source == ESP_GATT_SERVICE_FROM_REMOTE_DEVICE) {
            ESP_LOGI(GATTC_TAG, "Get service information from remote device");
        } else if (p_data->search_cmpl.searched_service_source == ESP_GATT_SERVICE_FROM_NVS_FLASH) {
            ESP_LOGI(GATTC_TAG, "Get service information from flash");
        } else {
            ESP_LOGI(GATTC_TAG, "unknown service source");
        }
        ESP_LOGI(GATTC_TAG, "ESP_GATTC_SEARCH_CMPL_EVT");
        if (get_server){
            uint16_t count = 0;
            esp_gatt_status_t status = esp_ble_gattc_get_attr_count( gattc_if,
                                                                     p_data->search_cmpl.conn_id,
                                                                     ESP_GATT_DB_CHARACTERISTIC,
                                                                     gl_profile_tab[PROFILE_A_APP_ID].service_start_handle,
                                                                     gl_profile_tab[PROFILE_A_APP_ID].service_end_handle,
                                                                     INVALID_HANDLE,
                                                                     &count);
            if (status != ESP_GATT_OK){
                ESP_LOGE(GATTC_TAG, "esp_ble_gattc_get_attr_count error");
            }

            if (count > 0){
                char_elem_result = (esp_gattc_char_elem_t *)malloc(sizeof(esp_gattc_char_elem_t) * count);
                if (!char_elem_result){
                    ESP_LOGE(GATTC_TAG, "gattc no mem");
                }else{
                    status = esp_ble_gattc_get_char_by_uuid( gattc_if,
                                                             p_data->search_cmpl.conn_id,
                                                             gl_profile_tab[PROFILE_A_APP_ID].service_start_handle,
                                                             gl_profile_tab[PROFILE_A_APP_ID].service_end_handle,
                                                             remote_filter_char_uuid,
                                                             char_elem_result,
                                                             &count);
                    if (status != ESP_GATT_OK){
                        ESP_LOGE(GATTC_TAG, "esp_ble_gattc_get_char_by_uuid error");
                    }

                    /*  Every service have only one char in our hive, so we used first 'char_elem_result' */
                    if (count > 0 && (char_elem_result[0].properties & ESP_GATT_CHAR_PROP_BIT_NOTIFY)){
                        gl_profile_tab[PROFILE_A_APP_ID].char_handle = char_elem_result[0].char_handle;
                        esp_ble_gattc_register_for_notify (gattc_if, gl_profile_tab[PROFILE_A_APP_ID].remote_bda, char_elem_result[0].char_handle);
                    }
                }
                /* free char_elem_result */
                free(char_elem_result);
            }else{
                ESP_LOGE(GATTC_TAG, "no char found");
            }
        }
         break;
    }
    case ESP_GATTC_REG_FOR_NOTIFY_EVT:{
        ESP_LOGI(GATTC_TAG, "ESP_GATTC_REG_FOR_NOTIFY_EVT");
        if (p_data->reg_for_notify.status != ESP_GATT_OK){
            ESP_LOGE(GATTC_TAG, "REG FOR NOTIFY failed: error status = %d", p_data->reg_for_notify.status);
        }else{
            uint16_t count = 0;
            uint16_t notify_en = 1;
            esp_gatt_status_t ret_status = esp_ble_gattc_get_attr_count( gattc_if,
                                                                         gl_profile_tab[PROFILE_A_APP_ID].conn_id,
                                                                         ESP_GATT_DB_DESCRIPTOR,
                                                                         gl_profile_tab[PROFILE_A_APP_ID].service_start_handle,
                                                                         gl_profile_tab[PROFILE_A_APP_ID].service_end_handle,
                                                                         gl_profile_tab[PROFILE_A_APP_ID].char_handle,
                                                                         &count);
            if (ret_status != ESP_GATT_OK){
                ESP_LOGE(GATTC_TAG, "esp_ble_gattc_get_attr_count error");
            }
            if (count > 0){
                descr_elem_result = malloc(sizeof(esp_gattc_descr_elem_t) * count);
                if (!descr_elem_result){
                    ESP_LOGE(GATTC_TAG, "malloc error, gattc no mem");
                }else{
                    ret_status = esp_ble_gattc_get_descr_by_char_handle( gattc_if,
                                                                         gl_profile_tab[PROFILE_A_APP_ID].conn_id,
                                                                         p_data->reg_for_notify.handle,
                                                                         notify_descr_uuid,
                                                                         descr_elem_result,
                                                                         &count);
                    if (ret_status != ESP_GATT_OK){
                        ESP_LOGE(GATTC_TAG, "esp_ble_gattc_get_descr_by_char_handle error");
                    }
                    /* Every char has only one descriptor in our 'ESP_GATTS_DEMO' demo, so we used first 'descr_elem_result' */
                    if (count > 0 && descr_elem_result[0].uuid.len == ESP_UUID_LEN_16 && descr_elem_result[0].uuid.uuid.uuid16 == ESP_GATT_UUID_CHAR_CLIENT_CONFIG){
                        ret_status = esp_ble_gattc_write_char_descr( gattc_if,
                                                                     gl_profile_tab[PROFILE_A_APP_ID].conn_id,
                                                                     descr_elem_result[0].handle,
                                                                     sizeof(notify_en),
                                                                     (uint8_t *)&notify_en,
                                                                     ESP_GATT_WRITE_TYPE_RSP,
                                                                     ESP_GATT_AUTH_REQ_NONE);
                    }

                    if (ret_status != ESP_GATT_OK){
                        ESP_LOGE(GATTC_TAG, "esp_ble_gattc_write_char_descr error");
                    }

                    /* free descr_elem_result */
                    free(descr_elem_result);
                }
            }
            else{
                ESP_LOGE(GATTC_TAG, "decsr not found");
            }

        }
        break;
    }
    case ESP_GATTC_NOTIFY_EVT:{
        if (p_data->notify.is_notify){
            ESP_LOGI(GATTC_TAG, "ESP_GATTC_NOTIFY_EVT, receive notify value:");
        }else{
            ESP_LOGI(GATTC_TAG, "ESP_GATTC_NOTIFY_EVT, receive indicate value:");
        }
        esp_log_buffer_hex(GATTC_TAG, p_data->notify.value, p_data->notify.value_len);
        
        //Try send once
        //get_sensor_data();
        //TODO this wasnt working, need to fix.
        break;
    }
    case ESP_GATTC_WRITE_DESCR_EVT:{
        if (p_data->write.status != ESP_GATT_OK){
            ESP_LOGE(GATTC_TAG, "write descr failed, error status = %x", p_data->write.status);
            break;
        }
        ESP_LOGI(GATTC_TAG, "write descr success ");

        ESP_LOGW(GATTC_TAG, "CAN SEND WRITE: TRUE");
        can_send_write = true;
        xSemaphoreGive(gattc_semaphore);

        break;
    }
    case ESP_GATTC_SRVC_CHG_EVT:{
        esp_bd_addr_t bda;
        memcpy(bda, p_data->srvc_chg.remote_bda, sizeof(esp_bd_addr_t));
        ESP_LOGI(GATTC_TAG, "ESP_GATTC_SRVC_CHG_EVT, bd_addr:");
        esp_log_buffer_hex(GATTC_TAG, bda, sizeof(esp_bd_addr_t));
        break;
    }
    case ESP_GATTC_WRITE_CHAR_EVT:{
        if (p_data->write.status != ESP_GATT_OK){
            ESP_LOGE(GATTC_TAG, "write char failed, error status = %x", p_data->write.status);
            break;
        }
        ESP_LOGI(GATTC_TAG, "write char success ");
        break;
    }
    case ESP_GATTC_DISCONNECT_EVT:{
        connect = false;
        is_connect = false;
        get_server = false;
        ESP_LOGI(GATTC_TAG, "ESP_GATTC_DISCONNECT_EVT, reason = %d", p_data->disconnect.reason);
        break;
    }
    case ESP_GATTC_CONGEST_EVT:{
        if (param->congest.congested) {
            ESP_LOGW(GATTC_TAG, "CAN SEND WRITE: FALSE");
            can_send_write = false;
        } else {
            ESP_LOGW(GATTC_TAG, "CAN SEND WRITE: TRUE");
            can_send_write = true;
            xSemaphoreGive(gattc_semaphore);
        }
        break;
    }
    default:
        break;
    }
}

static void esp_gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    uint8_t *adv_name = NULL;
    uint8_t adv_name_len = 0;
    switch (event) {
    case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT:{
        //the unit of the duration is second
        uint32_t duration = 30;
        esp_ble_gap_start_scanning(duration);
        break;
    }
    case ESP_GAP_BLE_SCAN_START_COMPLETE_EVT:{
        //scan start complete event to indicate scan start successfully or failed
        if (param->scan_start_cmpl.status != ESP_BT_STATUS_SUCCESS) {
            ESP_LOGE(GATTC_TAG, "scan start failed, error status = %x", param->scan_start_cmpl.status);
            break;
        }
        ESP_LOGI(GATTC_TAG, "scan start success");

        break;
    }
    case ESP_GAP_BLE_SCAN_RESULT_EVT:{
        esp_ble_gap_cb_param_t *scan_result = (esp_ble_gap_cb_param_t *)param;
        switch (scan_result->scan_rst.search_evt) {
        case ESP_GAP_SEARCH_INQ_RES_EVT:{
            esp_log_buffer_hex(GATTC_TAG, scan_result->scan_rst.bda, 6);
            ESP_LOGI(GATTC_TAG, "searched Adv Data Len %d, Scan Response Len %d", scan_result->scan_rst.adv_data_len, scan_result->scan_rst.scan_rsp_len);
            adv_name = esp_ble_resolve_adv_data(scan_result->scan_rst.ble_adv,
                                                ESP_BLE_AD_TYPE_NAME_CMPL, &adv_name_len);
            ESP_LOGI(GATTC_TAG, "searched Device Name Len %d", adv_name_len);
            esp_log_buffer_char(GATTC_TAG, adv_name, adv_name_len);
            ESP_LOGI(GATTC_TAG, "\n");

            if (adv_name != NULL) {
                if (strlen(remote_device_name) == adv_name_len && strncmp((char *)adv_name, remote_device_name, adv_name_len) == 0) {
                    ESP_LOGI(GATTC_TAG, "searched device %s\n", remote_device_name);
                    if (connect == false) {
                        connect = true;
                        ESP_LOGI(GATTC_TAG, "connect to the remote device.");
                        esp_ble_gap_set_prefer_conn_params(scan_result->scan_rst.bda, 32, 32, 0, 600);
                        esp_ble_gap_stop_scanning();
                        esp_ble_gattc_open(gl_profile_tab[PROFILE_A_APP_ID].gattc_if, scan_result->scan_rst.bda, scan_result->scan_rst.ble_addr_type, true);
                    }
                }
            }
            break;
        }
        case ESP_GAP_SEARCH_INQ_CMPL_EVT:{
            break;
        }
        default:
            break;
        }
        break;
    }
    case ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT:{
        if (param->scan_stop_cmpl.status != ESP_BT_STATUS_SUCCESS){
            ESP_LOGE(GATTC_TAG, "scan stop failed, error status = %x", param->scan_stop_cmpl.status);
            break;
        }
        ESP_LOGI(GATTC_TAG, "stop scan successfully");
        break;
    }
    case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:{
        if (param->adv_stop_cmpl.status != ESP_BT_STATUS_SUCCESS){
            ESP_LOGE(GATTC_TAG, "adv stop failed, error status = %x", param->adv_stop_cmpl.status);
            break;
        }
        ESP_LOGI(GATTC_TAG, "stop adv successfully");
        break;
    }
    case ESP_GAP_BLE_UPDATE_CONN_PARAMS_EVT:{
         ESP_LOGI(GATTC_TAG, "update connection params status = %d, min_int = %d, max_int = %d,conn_int = %d,latency = %d, timeout = %d",
                  param->update_conn_params.status,
                  param->update_conn_params.min_int,
                  param->update_conn_params.max_int,
                  param->update_conn_params.conn_int,
                  param->update_conn_params.latency,
                  param->update_conn_params.timeout);
        break;
    }
    default:
        break;
    }
}

static void esp_gattc_cb(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param)
{
    /* If event is register event, store the gattc_if for each profile */
    if (event == ESP_GATTC_REG_EVT) {
        if (param->reg.status == ESP_GATT_OK) {
            gl_profile_tab[param->reg.app_id].gattc_if = gattc_if;
        } else {
            ESP_LOGI(GATTC_TAG, "reg app failed, app_id %04x, status %d",
                    param->reg.app_id,
                    param->reg.status);
            return;
        }
    }

    /* If the gattc_if equal to profile A, call profile A cb handler,
     * so here call each profile's callback */
    do {
        int idx;
        for (idx = 0; idx < PROFILE_NUM; idx++) {
            if (gattc_if == ESP_GATT_IF_NONE || /* ESP_GATT_IF_NONE, not specify a certain gatt_if, need to call every profile cb function */
                    gattc_if == gl_profile_tab[idx].gattc_if) {
                if (gl_profile_tab[idx].gattc_cb) {
                    gl_profile_tab[idx].gattc_cb(event, gattc_if, param);
                }
            }
        }
    } while (0);
}

static void safe_send(uint16_t dsize, uint8_t* daddr) {
    while(1){
        if (!can_send_write) {
            ESP_LOGE(GATTC_TAG, "SEMAPHORE");
            int res = xSemaphoreTake(gattc_semaphore, portMAX_DELAY); 
            assert(res == pdTRUE);
        } 
        else {
            if (is_connect) {
                int free_buff_num = esp_ble_get_cur_sendable_packets_num(gl_profile_tab[PROFILE_A_APP_ID].conn_id);
                if(free_buff_num > 0) {            
                    ESP_LOGE(GATTC_TAG, "BUFFER IS OPEN LETS SEND %d BYTES", dsize);      
                    esp_ble_gattc_write_char(gl_profile_tab[PROFILE_A_APP_ID].gattc_if,
                                                    gl_profile_tab[PROFILE_A_APP_ID].conn_id,
                                                    gl_profile_tab[PROFILE_A_APP_ID].char_handle,
                                                    dsize, 
                                                    daddr,
                                                    ESP_GATT_WRITE_TYPE_NO_RSP,
                                                    ESP_GATT_AUTH_REQ_NONE);
                    break;
                } else {   
                    vTaskDelay( 10 / portTICK_PERIOD_MS );
                }
                
            }
            
        }
    }
}

void erase_flash(void) {
    const esp_partition_t *data_partition = NULL;
    data_partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
            ESP_PARTITION_SUBTYPE_DATA_FAT, PARTITION_NAME);
    if (data_partition != NULL) {
        //printf("partiton addr: 0x%08x; size: %d; label: %s\n", data_partition->address, data_partition->size, data_partition->label);
        ESP_LOGI(GATTC_TAG, "partiton addr: 0x%08x; size: %d; label: %s\n", data_partition->address, data_partition->size, data_partition->label);
    }
    //printf("Erase size: %d Bytes\n", FLASH_ERASE_SIZE);
    ESP_LOGI(GATTC_TAG, "Erase size: %d Bytes\n", FLASH_ERASE_SIZE);
    ESP_ERROR_CHECK(esp_partition_erase_range(data_partition, 0, FLASH_ERASE_SIZE));
}

void record_audio_to_flash() {
    const esp_partition_t *data_partition = NULL;
    data_partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
            ESP_PARTITION_SUBTYPE_DATA_FAT, PARTITION_NAME);
    if (data_partition != NULL) {
        //printf("partiton addr: 0x%08x; size: %d; label: %s\n", data_partition->address, data_partition->size, data_partition->label);
        ESP_LOGI(GATTC_TAG, "partiton addr: 0x%08x; size: %d; label: %s\n", data_partition->address, data_partition->size, data_partition->label);
    } else {
        ESP_LOGE(GATTC_TAG, "Partition error: can't find partition name: %s\n", PARTITION_NAME);
        vTaskDelete(NULL);
    }
    //1. Erase flash
    erase_flash();
    int i2s_read_len = I2S_READ_LEN;
    flash_wr_size = 0;
    size_t bytes_read;

    char* i2s_read_buff = (char*) calloc(i2s_read_len, sizeof(char));
    uint8_t* flash_write_buff = (uint8_t*) calloc(i2s_read_len, sizeof(char));
    
    //i2s_adc_enable(EXAMPLE_I2S_NUM);
    ESP_LOGI(GATTC_TAG, "Recording start");
    while (flash_wr_size < FLASH_RECORD_SIZE) {
        //read data from I2S bus, in this case, from ADC.
        i2s_read(i2s_num, (void*) i2s_read_buff, i2s_read_len, &bytes_read, portMAX_DELAY);
        
        //example_disp_buf((uint8_t*) i2s_read_buff, 64);
        //save original data from I2S(ADC) into flash.
        esp_partition_write(data_partition, flash_wr_size, i2s_read_buff, i2s_read_len);
        flash_wr_size += i2s_read_len;
        //ets_printf("Sound recording %u%%\n", flash_wr_size * 100 / FLASH_RECORD_SIZE);
    }
    ESP_LOGI(GATTC_TAG, "Recording complete");
    //i2s_adc_disable(EXAMPLE_I2S_NUM);

    free(i2s_read_buff);
    i2s_read_buff = NULL;
    free(flash_write_buff);
    flash_write_buff = NULL;
}

void send_audio_from_flash() {
    //int i2s_read_len = I2S_READ_LEN;
    uint8_t* flash_read_buff = (uint8_t*) calloc(MAX_SEND, sizeof(char));
    //uint8_t* i2s_write_buff = (uint8_t*) calloc(i2s_read_len, sizeof(char));


    const esp_partition_t *data_partition = NULL;
    data_partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
            ESP_PARTITION_SUBTYPE_DATA_FAT, PARTITION_NAME);
    if (data_partition != NULL) {
        //printf("partiton addr: 0x%08x; size: %d; label: %s\n", data_partition->address, data_partition->size, data_partition->label);
        ESP_LOGI(GATTC_TAG, "partiton addr: 0x%08x; size: %d; label: %s\n", data_partition->address, data_partition->size, data_partition->label);
    } else {
        ESP_LOGE(GATTC_TAG, "Partition error: can't find partition name: %s\n", PARTITION_NAME);
        vTaskDelete(NULL);
    }
    ESP_LOGI(GATTC_TAG,"Sending from flash start");
    for (int rd_offset = 0; rd_offset < flash_wr_size; rd_offset += MAX_SEND) {
        //read I2S(ADC) original data from flash
        esp_partition_read(data_partition, rd_offset, flash_read_buff, MAX_SEND);
        //process data and scale to 8bit for I2S DAC.
        //example_i2s_adc_data_scale(i2s_write_buff, flash_read_buff, FLASH_SECTOR_SIZE);
        //send data
        //i2s_write(EXAMPLE_I2S_NUM, i2s_write_buff, FLASH_SECTOR_SIZE, &bytes_written, portMAX_DELAY);
        safe_send(MAX_SEND, flash_read_buff);
        vTaskDelay(10/portTICK_PERIOD_MS);
        // bool sent = false;
        // while(!sent) {
        //     int free_buff_num = esp_ble_get_cur_sendable_packets_num(gl_profile_tab[PROFILE_A_APP_ID].conn_id);
        //     if(free_buff_num > 0){

        //         esp_ble_gattc_write_char(gl_profile_tab[PROFILE_A_APP_ID].gattc_if,
        //                                         gl_profile_tab[PROFILE_A_APP_ID].conn_id,
        //                                         gl_profile_tab[PROFILE_A_APP_ID].char_handle,
        //                                         i2s_read_len, 
        //                                         flash_read_buff,
        //                                         ESP_GATT_WRITE_TYPE_NO_RSP,
        //                                         ESP_GATT_AUTH_REQ_NONE);
        //         printf("playing: %d %%\n", rd_offset * 100 / flash_wr_size);
        //         sent = true;
        //     }
        //     else {
        //         printf("waiting...\n");
        //         vTaskDelay(HIVE_RETRY_PERIOD / portTICK_PERIOD_MS);
        //     }
        // }
    }
    ESP_LOGI(GATTC_TAG,"Sending from flash end");
}

/**
 * @brief i2c master initialization
 */

static esp_err_t i2c_master_read_slave(i2c_port_t i2c_num, uint8_t *data_rd, size_t size){
    if (size == 0) {
        return ESP_OK;
    }
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (ESP_SLAVE_ADDR << 1) | READ_BIT, ACK_CHECK_EN);
    if (size > 1) {
        i2c_master_read(cmd, data_rd, size - 1, ACK_VAL);
    }
    i2c_master_read_byte(cmd, data_rd + size - 1, NACK_VAL);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(i2c_num, cmd, 1000 / portTICK_RATE_MS);
    i2c_cmd_link_delete(cmd);
    return ret;
}

static esp_err_t i2c_master_write_slave(i2c_port_t i2c_num, uint8_t *data_wr, size_t size)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (ESP_SLAVE_ADDR << 1) | WRITE_BIT, ACK_CHECK_EN);
    i2c_master_write(cmd, data_wr, size, ACK_CHECK_EN);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(i2c_num, cmd, 1000 / portTICK_RATE_MS);
    i2c_cmd_link_delete(cmd);
    return ret;
}

static esp_err_t i2c_master_init(void)
{
    int i2c_master_port = I2C_MASTER_NUM;
    i2c_config_t conf;
    conf.mode = I2C_MODE_MASTER;
    conf.sda_io_num = I2C_MASTER_SDA_IO;
    conf.sda_pullup_en = GPIO_PULLUP_DISABLE;
    conf.scl_io_num = I2C_MASTER_SCL_IO;
    conf.scl_pullup_en = GPIO_PULLUP_DISABLE;
    conf.master.clk_speed = I2C_MASTER_FREQ_HZ;
    i2c_param_config(i2c_master_port, &conf);
    return i2c_driver_install(i2c_master_port, conf.mode, 0, 0, 0);
}

static void get_sensor_data()
{
    ESP_LOGI(GATTC_TAG, "ENTER DATA COLLECTION");
    //gather the data from sensors
    uint8_t *th_data = (uint8_t *)malloc(DATA_LENGTH);
    uint8_t *data_wr = (uint8_t *)malloc(DATA_LENGTH);
    uint8_t *th_status = (uint8_t *)malloc(DATA_LENGTH);
    //weight sensor
    unsigned long weight = 0;
    vTaskDelay(1/portTICK_PERIOD_MS);
    for(char i = 0; i < 24; i++) {
        gpio_set_level(WEIGHT_CLK, 1);
        weight = weight<<1;
        gpio_set_level(WEIGHT_CLK, 0);
        weight+=gpio_get_level(WEIGHT_GPIO);
    }
    weight = weight ^ 0x800000;

    //hardware specific handshake command
    data_wr[0] = 0xAC;
    data_wr[1] = 0x33;
    data_wr[2] = 0x00;

    ESP_LOGI(GATTC_TAG, "COLLECTING DATA");
    i2c_master_write_slave(0, data_wr, 3);
    usleep(80000);
    *th_status = 0;
    while((*th_status&0x80) == 0) {
        i2c_master_read_slave(0, th_status, 1);
        if ((*th_status&0x80) == 0) {
            i2c_master_read_slave(0, th_data, 6);
        }
        usleep(1000);
    }

    ESP_LOGI(GATTC_TAG, "CALCULATING DATA");
    //humidity calculations
    data.humid = th_data[1];
    data.humid <<= 8;
    data.humid += th_data[2];
    data.humid <<=4;
    data.humid += th_data[3] >> 4;
    data.humid = data.humid/10485776;

    //temperature calculations
    data.temp = th_data[3]&0x0F;
    data.temp <<= 8;
    data.temp += th_data[4];
    data.temp <<=8;
    data.temp += th_data[5];
    data.temp = ((data.temp/10485776*200)-50)*9/5+32;

    data.weight = weight;    
    //Record the audio data to flash
    record_audio_to_flash();
}

static void send_data_to_server()
{             
    
    safe_send(sizeof(data), (uint8_t*)&data);
    vTaskDelay(1/portTICK_PERIOD_MS);
    
    send_audio_from_flash();
    vTaskDelay(1000/portTICK_PERIOD_MS);
    
    uint32_t end_data = 0xFAFAFAFA;
    ESP_LOGW(GATTC_TAG,"SENDING ENDING DATA");
    safe_send(sizeof(uint32_t), (uint8_t*)&end_data);
    
    
    // esp_ble_gattc_write_char(gl_profile_tab[PROFILE_A_APP_ID].gattc_if,
    //                                 gl_profile_tab[PROFILE_A_APP_ID].conn_id,
    //                                 gl_profile_tab[PROFILE_A_APP_ID].char_handle,
    //                                 sizeof(data), 
    //                                 (uint8_t*)&data,
    //                                 ESP_GATT_WRITE_TYPE_NO_RSP,
    //                                 ESP_GATT_AUTH_REQ_NONE);
    // esp_ble_gattc_write_char(gl_profile_tab[PROFILE_A_APP_ID].gattc_if,
    //                                 gl_profile_tab[PROFILE_A_APP_ID].conn_id,
    //                                 gl_profile_tab[PROFILE_A_APP_ID].char_handle,
    //                                 sizeof(data), 
    //                                 audio_data,
    //                                 ESP_GATT_WRITE_TYPE_NO_RSP,
    //                                 ESP_GATT_AUTH_REQ_NONE);
}

static void hive_report_task(void *params)
{
    while(1) {
        ESP_LOGI(GATTC_TAG, "HIVE TASK START");
        get_sensor_data();
        ESP_LOGI(GATTC_TAG, "Sensor Data:\n weight %lu", data.weight);
        send_data_to_server();
        ESP_LOGI(GATTC_TAG, "HIVE TASK END");
        vTaskDelay(HIVE_TASK_PERIOD / portTICK_PERIOD_MS);
    }

}


void app_main(void)
{
    // Initialize NVS.
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret) {
        ESP_LOGE(GATTC_TAG, "%s initialize controller failed: %s\n", __func__, esp_err_to_name(ret));
        return;
    }

    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret) {
        ESP_LOGE(GATTC_TAG, "%s enable controller failed: %s\n", __func__, esp_err_to_name(ret));
        return;
    }

    ret = esp_bluedroid_init();
    if (ret) {
        ESP_LOGE(GATTC_TAG, "%s init bluetooth failed: %s\n", __func__, esp_err_to_name(ret));
        return;
    }

    ret = esp_bluedroid_enable();
    if (ret) {
        ESP_LOGE(GATTC_TAG, "%s enable bluetooth failed: %s\n", __func__, esp_err_to_name(ret));
        return;
    }

    //register the  callback function to the gap module
    ret = esp_ble_gap_register_callback(esp_gap_cb);
    if (ret){
        ESP_LOGE(GATTC_TAG, "%s gap register failed, error code = %x\n", __func__, ret);
        return;
    }

    //register the callback function to the gattc module
    ret = esp_ble_gattc_register_callback(esp_gattc_cb);
    if(ret){
        ESP_LOGE(GATTC_TAG, "%s gattc register failed, error code = %x\n", __func__, ret);
        return;
    }

    ret = esp_ble_gattc_app_register(PROFILE_A_APP_ID);
    if (ret){
        ESP_LOGE(GATTC_TAG, "%s gattc app register failed, error code = %x\n", __func__, ret);
    }
    esp_err_t local_mtu_ret = esp_ble_gatt_set_local_mtu(500);
    if (local_mtu_ret){
        ESP_LOGE(GATTC_TAG, "set local  MTU failed, error code = %x", local_mtu_ret);
    }

    gpio_config_t io_conf;
    //disable interrupt
    io_conf.intr_type = GPIO_PIN_INTR_DISABLE;
    //set as output mode
    io_conf.mode = GPIO_MODE_OUTPUT;
    //bit mask of the pins that you want to set,e.g.GPIO18/19
    io_conf.pin_bit_mask = ((1ULL<<WEIGHT_CLK) | (1ULL<<WEIGHT_GPIO));
    //disable pull-down mode
    io_conf.pull_down_en = 0;
    //disable pull-up mode
    io_conf.pull_up_en = 0;
    //configure GPIO with the given settings
    gpio_config(&io_conf);

    gpio_pad_select_gpio(WEIGHT_GPIO);
    gpio_set_direction(WEIGHT_GPIO, GPIO_MODE_INPUT);
    gpio_pad_select_gpio(WEIGHT_CLK);
    gpio_set_direction(WEIGHT_CLK, GPIO_MODE_OUTPUT);

    i2s_driver_install(i2s_num, &i2s_config, 0, NULL);   //install and start i2s driver
    i2s_set_pin(i2s_num, &pin_config);
    i2s_set_sample_rates(i2s_num, 22050); //set sample rates
    // audio_data = (uint8_t*) malloc(HIVE_AUDIO_READ_LEN); 
    // if (audio_data == 0) {
    //     ESP_LOGI(GATTC_TAG, "MALLOC FAILED");
    //     return;
    // }

    ESP_ERROR_CHECK(i2c_master_init());
    //calibration command for temp/humidity sensor
    uint8_t *data_wr = (uint8_t *)malloc(DATA_LENGTH);
    *data_wr = 0xBE;
    i2c_master_write_slave(0, data_wr, 1);
    usleep(10000);

    gattc_semaphore = xSemaphoreCreateBinary();
    if (!gattc_semaphore) {
        ESP_LOGE(GATTC_TAG, "%s, init fail, the gattc semaphore create fail.", __func__);
        return;
    }
    xTaskCreate(&hive_report_task, "hive_report_task", 4096, NULL, 3, NULL);

}