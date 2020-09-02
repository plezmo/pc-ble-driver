/*
 * Copyright (c) 2016 Nordic Semiconductor ASA
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 *   1. Redistributions of source code must retain the above copyright notice, this
 *   list of conditions and the following disclaimer.
 *
 *   2. Redistributions in binary form must reproduce the above copyright notice, this
 *   list of conditions and the following disclaimer in the documentation and/or
 *   other materials provided with the distribution.
 *
 *   3. Neither the name of Nordic Semiconductor ASA nor the names of other
 *   contributors to this software may be used to endorse or promote products
 *   derived from this software without specific prior written permission.
 *
 *   4. This software must only be used in or with a processor manufactured by Nordic
 *   Semiconductor ASA, or in or with a processor manufactured by a third party that
 *   is used in combination with a processor manufactured by Nordic Semiconductor.
 *
 *   5. Any software provided in binary or object form under this license must not be
 *   reverse engineered, decompiled, modified and/or disassembled.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef TRANSPORT_H
#define TRANSPORT_H

#include "sd_rpc_types.h"

#include <functional>
#include <string>
#include <vector>

#include <stdint.h>

typedef std::function<void(const sd_rpc_app_status_t code, const std::string &message)> status_cb_t;
typedef std::function<void(const uint8_t *data, const size_t length)> data_cb_t;
typedef std::function<void(const sd_rpc_log_severity_t severity, const std::string &message)>
    log_cb_t;

class Transport
{
  public:
    virtual ~Transport();
    virtual uint32_t open(const status_cb_t &status_callback, const data_cb_t &data_callback,
                          const log_cb_t &log_callback);
    virtual uint32_t close() = 0;

    virtual uint32_t send(const std::vector<uint8_t> &data) = 0;

    void log(const sd_rpc_log_severity_t severity, const std::string &message) const;
    void status(const sd_rpc_app_status_t code, const std::string &message) const;

  protected:
    Transport();

    status_cb_t upperStatusCallback;
    data_cb_t upperDataCallback;
    log_cb_t upperLogCallback;
};

#endif // TRANSPORT_H
