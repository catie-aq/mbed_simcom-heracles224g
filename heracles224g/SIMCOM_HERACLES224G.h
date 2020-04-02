/*
 * Copyright (c) 2020, CATIE
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef CELLULAR_TARGETS_SIMCOM_HERACLES224G_SIMCOM_HERACLES224G_H_
#define CELLULAR_TARGETS_SIMCOM_HERACLES224G_SIMCOM_HERACLES224G_H_

#ifdef TARGET_FF_ARDUINO
#ifndef MBED_CONF_SIMCOM_HERACLES224G_TX
#define MBED_CONF_SIMCOM_HERACLES224G_TX D1
#endif
#ifndef MBED_CONF_SIMCOM_HERACLES224G_RX
#define MBED_CONF_SIMCOM_HERACLES224G_RX D0
#endif
#endif /* TARGET_FF_ARDUINO */

#include "mbed.h"
#include "DigitalOut.h"
#include <AT_CellularDevice.h>

namespace mbed {

class SIMCOM_HERACLES224G : public AT_CellularDevice {
public:
    /**
     * Constructs the SIMCOM Heracles224G series driver. It is mandatory to provide
     * a FileHandle object, the power pin and the polarity of the pin.
     */
    SIMCOM_HERACLES224G(FileHandle *fh, PinName pwr_key = NC, bool active_high = true, PinName rst = NC);

protected: // AT_CellularDevice
    virtual nsapi_error_t init();
    virtual nsapi_error_t soft_power_on();
    virtual nsapi_error_t soft_power_off();
    virtual AT_CellularNetwork *open_network_impl(ATHandler &at);
    virtual AT_CellularContext *create_context_impl(ATHandler &at, const char *apn, bool cp_req = false, bool nonip_req = false);
    virtual void set_ready_cb(Callback<void()> callback);

private:
    void press_button(DigitalOut &button, uint32_t timeout);
    bool wake_up(bool reset = false);
    bool _active_high;
    DigitalOut _pwr_key;
    DigitalOut _rst;
};
} // namespace mbed
#endif /* CELLULAR_TARGETS_SIMCOM_HERACLES224G_SIMCOM_HERACLES224G_H_ */
