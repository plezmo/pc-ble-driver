/**
 * Copyright (c) 2014 - 2018, Nordic Semiconductor ASA
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form, except as embedded into a Nordic
 *    Semiconductor ASA integrated circuit in a product or a software update for
 *    such product, must reproduce the above copyright notice, this list of
 *    conditions and the following disclaimer in the documentation and/or other
 *    materials provided with the distribution.
 *
 * 3. Neither the name of Nordic Semiconductor ASA nor the names of its
 *    contributors may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * 4. This software, with or without modification, must only be used with a
 *    Nordic Semiconductor ASA integrated circuit.
 *
 * 5. Any software provided in binary form under this license must not be reverse
 *    engineered, decompiled, modified and/or disassembled.
 *
 * THIS SOFTWARE IS PROVIDED BY NORDIC SEMICONDUCTOR ASA "AS IS" AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY, NONINFRINGEMENT, AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL NORDIC SEMICONDUCTOR ASA OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
#include "app_ble_gap.h"
#include "nrf_error.h"

#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <iostream>

#include <sd_rpc_types.h>

typedef struct
{
    bool active;
    uint8_t adv_handle;
    uint8_t *p_adv_data;
    uint8_t *p_scan_rsp_data;
} adv_set_t;

/**
 * @brief This structure keeps GAP states for one adapter
 */
typedef struct
{
    // GAP connection - BLE security keys table for storage.
    ser_ble_gap_app_keyset_t app_keys_table[SER_MAX_CONNECTIONS]{};
#if NRF_SD_BLE_API_VERSION >= 6
    // Advertisement sets - BLE advertisement sets
    adv_set_t adv_sets[BLE_GAP_ADV_SET_COUNT_MAX]{};
    // Buffer for scan data received
    ble_data_t scan_data = {nullptr, 0};
    int scan_data_id{0};
    void *ble_gap_adv_buf_addr[APP_BLE_GAP_ADV_BUF_COUNT]{};
#endif // NRF_SD_BLE_API_VERSION >= 6
} adapter_ble_gap_state_t;

/**
 * @brief This map keeps the GAP states (see @ref adapter_ble_gap_state_t) for adapters used
 */
static std::map<void *, std::shared_ptr<adapter_ble_gap_state_t>> adapters_gap_state;

/**
 * @brief This structure keeps information related to an adapter key
 *
 * An adapter key is used to tell the codecs what adapter_ble_gap_state_t to
 * use during encoding and decoding.
 *
 */
typedef struct
{
    /**
     * @brief adapter_id that points to the @ref adapter_ble_gap_state_t [adapter GAP state]
     * currently used by codecs
     */
    void *adapter_id{nullptr};

    /**
     * @brief Mutex that protects changing the adapter GAP state while encoding/decoding is in
     * progress
     */
    std::mutex codec_mutex;
} adapter_codec_context_t;

/**
 * @brief Adapter key used for encoding/decoding request reply commands
 */
static adapter_codec_context_t current_request_reply_context;

/**
 * @brief Adapter key used for decoding events
 */
static adapter_codec_context_t current_event_context;

uint32_t app_ble_gap_state_create(void *adapter_id)
{
    if (adapters_gap_state.count(adapter_id) == 1)
    {
        return NRF_ERROR_SD_RPC_INVALID_STATE;
    }

    adapters_gap_state[adapter_id] = std::make_shared<adapter_ble_gap_state_t>();

    return NRF_SUCCESS;
}

uint32_t app_ble_gap_state_delete(void *adapter_id)
{
    if (adapters_gap_state.erase(adapter_id) != 1)
    {
        return NRF_ERROR_SD_RPC_INVALID_STATE;
    }

    // TODO: if this key is used in request_reply_key
    // TODO: or event_key, remove them from the keys
    // TODO: and unlock any keys

    return NRF_SUCCESS;
}

void app_ble_gap_set_current_adapter_id(void *adapter_id,
                                        const app_ble_gap_adapter_codec_context_t key_type)
{
    if (key_type == EVENT_CODEC_CONTEXT)
    {
        current_event_context.codec_mutex.lock();
        current_event_context.adapter_id = adapter_id;
    }
    else if (key_type == REQUEST_REPLY_CODEC_CONTEXT)
    {
        current_request_reply_context.codec_mutex.lock();
        current_request_reply_context.adapter_id = adapter_id;
    }
}

void app_ble_gap_unset_current_adapter_id(const app_ble_gap_adapter_codec_context_t key_type)
{
    if (key_type == EVENT_CODEC_CONTEXT)
    {
        current_event_context.codec_mutex.unlock();
        current_event_context.adapter_id = nullptr;
    }
    else if (key_type == REQUEST_REPLY_CODEC_CONTEXT)
    {
        current_request_reply_context.codec_mutex.unlock();
        current_request_reply_context.adapter_id = nullptr;
    }
}

uint32_t
app_ble_gap_check_current_adapter_set(const app_ble_gap_adapter_codec_context_t codec_context)
{
    if (codec_context == EVENT_CODEC_CONTEXT)
    {
        return current_event_context.adapter_id != nullptr;
    }
    else if (codec_context == REQUEST_REPLY_CODEC_CONTEXT)
    {
        return current_request_reply_context.adapter_id != nullptr;
    }

    return false;
}

uint32_t app_ble_gap_sec_keys_storage_create(uint16_t conn_handle, uint32_t *p_index)
{
    if (!app_ble_gap_check_current_adapter_set(REQUEST_REPLY_CODEC_CONTEXT))
    {
        return NRF_ERROR_SD_RPC_INVALID_STATE;
    }

    try
    {
        const auto gap_state = adapters_gap_state.at(current_request_reply_context.adapter_id);

        // Assumption: conn_handle is always starting from 0 and up to SER_MAX_CONNECTIONS (not
        // including)
        for (auto i = 0; i < SER_MAX_CONNECTIONS; i++)
        {
            auto &keys = gap_state->app_keys_table[i];

            if (!keys.conn_active)
            {
                keys.conn_active = 1;
                keys.conn_handle = conn_handle;
                *p_index         = i;
                return NRF_SUCCESS;
            }
        }
    }
    catch (const std::out_of_range &)
    {
        return NRF_ERROR_SD_RPC_INVALID_STATE;
    }

    return NRF_ERROR_NO_MEM;
}

uint32_t app_ble_gap_sec_keys_storage_destroy(const uint16_t conn_handle)
{
    if (!app_ble_gap_check_current_adapter_set(EVENT_CODEC_CONTEXT))
    {
        return NRF_ERROR_SD_RPC_INVALID_STATE;
    }

    try
    {
        const auto gap_state = adapters_gap_state.at(current_event_context.adapter_id);

        for (auto &keys : gap_state->app_keys_table)
        {
            if (keys.conn_handle == conn_handle)
            {
                keys.conn_active = 0;
                return NRF_SUCCESS;
            }
        }

        return NRF_ERROR_NO_MEM;
    }
    catch (const std::out_of_range &)
    {
        return NRF_ERROR_SD_RPC_INVALID_STATE;
    }
}

uint32_t app_ble_gap_sec_keys_find(const uint16_t conn_handle, uint32_t *p_index)
{
    if (!app_ble_gap_check_current_adapter_set(EVENT_CODEC_CONTEXT))
    {
        return NRF_ERROR_SD_RPC_INVALID_STATE;
    }

    try
    {
        const auto gap_state = adapters_gap_state.at(current_event_context.adapter_id);

        for (auto i = 0; i < SER_MAX_CONNECTIONS; i++)
        {
            auto &keys = gap_state->app_keys_table[i];
            if ((keys.conn_handle == conn_handle) && (keys.conn_active == 1))
            {
                *p_index = i;
                return NRF_SUCCESS;
            }
        }

        return NRF_ERROR_NOT_FOUND;
    }
    catch (const std::out_of_range &)
    {
        return NRF_ERROR_SD_RPC_INVALID_STATE;
    }
}

uint32_t app_ble_gap_sec_keys_get(const uint32_t index, ble_gap_sec_keyset_t **keyset)
{
    if (!app_ble_gap_check_current_adapter_set(EVENT_CODEC_CONTEXT))
    {
        return NRF_ERROR_SD_RPC_INVALID_STATE;
    }

    try
    {
        const auto gap_state = adapters_gap_state.at(current_event_context.adapter_id);
        *keyset              = &(gap_state->app_keys_table[index].keyset);
        return NRF_SUCCESS;
    }
    catch (const std::out_of_range &)
    {
        return NRF_ERROR_SD_RPC_INVALID_STATE;
    }
}

uint32_t app_ble_gap_sec_keys_update(const uint32_t index, const ble_gap_sec_keyset_t *keyset)
{
    if (!app_ble_gap_check_current_adapter_set(REQUEST_REPLY_CODEC_CONTEXT))
    {
        return NRF_ERROR_SD_RPC_INVALID_STATE;
    }

    try
    {
        const auto gap_state = adapters_gap_state.at(current_request_reply_context.adapter_id);
        std::memcpy(&(gap_state->app_keys_table[index].keyset), keyset,
                    sizeof(ble_gap_sec_keyset_t));
        return NRF_SUCCESS;
    }
    catch (const std::out_of_range &)
    {
        return NRF_ERROR_SD_RPC_INVALID_STATE;
    }
}

uint32_t app_ble_gap_state_reset()
{
    if (!app_ble_gap_check_current_adapter_set(REQUEST_REPLY_CODEC_CONTEXT))
    {
        return NRF_ERROR_SD_RPC_INVALID_STATE;
    }

    try
    {
        const auto gap_state = adapters_gap_state.at(current_request_reply_context.adapter_id);

        for (auto &keyset : gap_state->app_keys_table)
        {
            keyset.conn_active = false;
        }

#if NRF_SD_BLE_API_VERSION >= 6
        for (auto &adv_set : gap_state->adv_sets)
        {
            adv_set.active = false;
        }

        gap_state->scan_data = {nullptr, 0};
#endif // NRF_SD_BLE_API_VERSION >= 6
    }
    catch (const std::out_of_range &)
    {
        return NRF_ERROR_SD_RPC_INVALID_STATE;
    }

    return NRF_SUCCESS;
}

#if NRF_SD_BLE_API_VERSION >= 6
static adv_set_data_t adv_set_data[] = {{BLE_GAP_ADV_SET_HANDLE_NOT_SET, nullptr, nullptr}};

uint32_t app_ble_gap_scan_data_set(ble_data_t const *p_data)
{
    if (!app_ble_gap_check_current_adapter_set(REQUEST_REPLY_CODEC_CONTEXT))
    {
        return NRF_ERROR_SD_RPC_INVALID_STATE;
    }

    try
    {
        const auto gap_state = adapters_gap_state.at(current_request_reply_context.adapter_id);

        if (gap_state->scan_data.p_data != nullptr)
        {
            return NRF_ERROR_BUSY;
        }

        memcpy(&(gap_state->scan_data), p_data, sizeof(ble_data_t));
        return NRF_SUCCESS;
    }
    catch (const std::out_of_range &)
    {
        return NRF_ERROR_SD_RPC_INVALID_STATE;
    }
}

uint32_t app_ble_gap_scan_data_fetch_clear(ble_data_t *p_data)
{
    if (!app_ble_gap_check_current_adapter_set(EVENT_CODEC_CONTEXT))
    {
        return NRF_ERROR_SD_RPC_INVALID_STATE;
    }

    try
    {
        const auto gap_state = adapters_gap_state.at(current_event_context.adapter_id);
        std::memcpy(p_data, &(gap_state->scan_data), sizeof(ble_data_t));

        if (gap_state->scan_data.p_data != nullptr)
        {
            gap_state->scan_data.p_data = nullptr;
            return NRF_SUCCESS;
        }

        return NRF_ERROR_NOT_FOUND;
    }
    catch (const std::out_of_range &)
    {
        return NRF_ERROR_SD_RPC_INVALID_STATE;
    }
}

uint32_t app_ble_gap_adv_set_register(uint8_t adv_handle, uint8_t *p_adv_data,
                                      uint8_t *p_scan_rsp_data)
{
    if (!app_ble_gap_check_current_adapter_set(REQUEST_REPLY_CODEC_CONTEXT))
    {
        return NRF_ERROR_SD_RPC_INVALID_STATE;
    }

    try
    {
        const auto gap_state = adapters_gap_state.at(current_request_reply_context.adapter_id);

        for (auto &m_adv_set : gap_state->adv_sets)
        {
            if (!m_adv_set.active)
            {
                m_adv_set.active          = true;
                m_adv_set.adv_handle      = adv_handle;
                m_adv_set.p_adv_data      = p_adv_data;
                m_adv_set.p_scan_rsp_data = p_scan_rsp_data;
                return NRF_SUCCESS;
            }
        }

        return NRF_ERROR_NOT_FOUND;
    }
    catch (const std::out_of_range &)
    {
        return NRF_ERROR_SD_RPC_INVALID_STATE;
    }
}

uint32_t app_ble_gap_adv_set_unregister(uint8_t adv_handle, uint8_t **pp_adv_data,
                                        uint8_t **pp_scan_rsp_data)
{
    if (!app_ble_gap_check_current_adapter_set(EVENT_CODEC_CONTEXT))
    {
        return NRF_ERROR_SD_RPC_INVALID_STATE;
    }

    try
    {
        const auto gap_state = adapters_gap_state.at(current_event_context.adapter_id);

        for (auto &m_adv_set : gap_state->adv_sets)
        {
            if (m_adv_set.active && (m_adv_set.adv_handle == adv_handle))
            {
                m_adv_set.active  = false;
                *pp_adv_data      = m_adv_set.p_adv_data;
                *pp_scan_rsp_data = m_adv_set.p_scan_rsp_data;
                return NRF_SUCCESS;
            }
        }

        return NRF_ERROR_NOT_FOUND;
    }
    catch (const std::out_of_range &)
    {
        return NRF_ERROR_SD_RPC_INVALID_STATE;
    }
}

int app_ble_gap_adv_buf_register(void *p_buf)
{
    if (!app_ble_gap_check_current_adapter_set(REQUEST_REPLY_CODEC_CONTEXT))
    {
        std::cerr << "PROGRAM LOGIC ERROR: app_ble_gap_adv_buf_register not called from context "
                     "REQUEST_REPLY_CODEC_CONTEXT, terminating"
                  << std::endl;
        std::terminate();
    }

    if (p_buf == nullptr)
    {
        return 0;
    }

    try
    {
        const auto gap_state = adapters_gap_state.at(current_request_reply_context.adapter_id);

        auto id = 1;

        // Find available location in ble_gap_adv_buf_addr for
        // store this new buffer pointer.
        for (auto &addr : gap_state->ble_gap_adv_buf_addr)
        {
            if ((addr == nullptr) || (addr == p_buf))
            {
                addr = p_buf;
                return id;
            }
            id++;
        }

        return -1;
    }
    catch (const std::out_of_range &)
    {
        return -1;
    }
}

int app_ble_gap_adv_buf_addr_unregister(void *p_buf)
{
    if (!app_ble_gap_check_current_adapter_set(REQUEST_REPLY_CODEC_CONTEXT))
    {
        std::cerr << "PROGRAM LOGIC ERROR: app_ble_gap_adv_buf_register not called from context "
                     "REQUEST_REPLY_CODEC_CONTEXT, terminating"
                  << std::endl;
        std::terminate();
    }

    if (p_buf == nullptr)
    {
        return 0;
    }

    try
    {
        const auto gap_state = adapters_gap_state.at(current_request_reply_context.adapter_id);

        auto id = 1;

        // Find available location in ble_gap_adv_buf_addr for
        // store this new buffer pointer.
        for (auto &addr : gap_state->ble_gap_adv_buf_addr)
        {
            if (addr == p_buf)
            {
                addr = nullptr;
                return id;
            }
        }

        return -1;
    }
    catch (const std::out_of_range &)
    {
        return -1;
    }
}

void *app_ble_gap_adv_buf_unregister(const int id, const bool event_context)
{
    if (!app_ble_gap_check_current_adapter_set(event_context ? EVENT_CODEC_CONTEXT
                                                             : REQUEST_REPLY_CODEC_CONTEXT))
    {
        return nullptr;
    }

    if (id == 0)
    {
        return nullptr;
    }

    const auto gap_state =
        adapters_gap_state.at(event_context ? current_event_context.adapter_id
                                            : current_request_reply_context.adapter_id);

    auto ret                                = gap_state->ble_gap_adv_buf_addr[id - 1];
    gap_state->ble_gap_adv_buf_addr[id - 1] = nullptr;

    return ret;
}

void app_ble_gap_set_adv_data_set(uint8_t adv_handle, uint8_t *buf1, uint8_t *buf2)
{
    if (adv_handle == BLE_GAP_ADV_SET_HANDLE_NOT_SET)
    {
        return;
    }

    for (int i = 0; i < BLE_GAP_ADV_SET_COUNT_MAX; i++)
    {
        if (adv_set_data[i].adv_handle == adv_handle)
        {
            /* If adv_set is already configured replace old buffers with new one. */
            if (adv_set_data[i].buf1 != buf1)
            {
                app_ble_gap_adv_buf_addr_unregister(adv_set_data[i].buf1);
            }

            if (adv_set_data[i].buf2 != buf2)
            {
                app_ble_gap_adv_buf_addr_unregister(adv_set_data[i].buf2);
            }

            adv_set_data[i].buf1 = buf1;
            adv_set_data[i].buf2 = buf2;

            return;
        }
    }

    for (int i = 0; i < BLE_GAP_ADV_SET_COUNT_MAX; i++)
    {
        if (adv_set_data[i].adv_handle == BLE_GAP_ADV_SET_HANDLE_NOT_SET)
        {
            adv_set_data[i].adv_handle = adv_handle;
            adv_set_data[i].buf1       = buf1;
            adv_set_data[i].buf2       = buf2;
            return;
        }
    }
}

// Update the adapter gap state scan_data_id variable based on pointer received???
void app_ble_gap_scan_data_set(const uint8_t *p_scan_data)
{
    if (!app_ble_gap_check_current_adapter_set(REQUEST_REPLY_CODEC_CONTEXT))
    {
        return;
    }

    // Find location for scan_data
    const auto gap_state = adapters_gap_state.at(current_request_reply_context.adapter_id);

    auto id = 0;

    // Check if ptr to scan data is already registered???
    for (auto &addr : gap_state->ble_gap_adv_buf_addr)
    {
        if (addr == p_scan_data)
        {
            gap_state->scan_data_id = id + 1;
            return;
        }
        id++;
    }

    gap_state->scan_data_id = 0;
}

void app_ble_gap_scan_data_unset(bool free)
{
    if (!app_ble_gap_check_current_adapter_set(REQUEST_REPLY_CODEC_CONTEXT))
    {
        return;
    }

    const auto gap_state = adapters_gap_state.at(current_request_reply_context.adapter_id);

    if (gap_state->scan_data_id)
    {
        if (free)
        {
            app_ble_gap_adv_buf_unregister(gap_state->scan_data_id, false);
        }
        gap_state->scan_data_id = 0;
    }
}

#endif // NRF_SD_BLE_API_VERSION >= 6
