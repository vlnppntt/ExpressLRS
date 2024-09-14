#include "targets.h"

#if defined(PLATFORM_ESP32) && defined(TARGET_TX)

#include "AutoDetect.h"
#include "CRSFHandset.h"
#include "PPMHandset.h"
#include "logging.h"

#include <driver/rmt.h>

// we need a level of accuracy to measure a single pulse at 5.25MBaud
constexpr auto RMT_TICKS_PER_US = 32;

void AutoDetect::Begin()
{
    constexpr auto divisor = 80 / RMT_TICKS_PER_US;

    rmt_config_t rmt_rx_config = RMT_DEFAULT_CONFIG_RX(static_cast<gpio_num_t>(GPIO_PIN_RCSIGNAL_RX), PPM_RMT_CHANNEL);
    rmt_rx_config.clk_div = divisor;
    rmt_rx_config.rx_config.filter_ticks_thresh = 0;
    rmt_rx_config.rx_config.idle_threshold = RMT_TICKS_PER_US * 2000; // 2ms
    rmt_config(&rmt_rx_config);
    rmt_driver_install(PPM_RMT_CHANNEL, 1000, 0);

    rmt_get_ringbuf_handle(PPM_RMT_CHANNEL, &rb);
    rmt_rx_start(PPM_RMT_CHANNEL, true);
    input_detect = 0;
}

void AutoDetect::End()
{
    rmt_driver_uninstall(PPM_RMT_CHANNEL);
}

bool AutoDetect::IsArmed()
{
    return false;
}

void AutoDetect::migrateTo(Handset *that) const
{
    that->setRCDataCallback(RCdataCallback);
    that->registerParameterUpdateCallback(RecvParameterUpdate);
    that->registerCallbacks(connected, disconnected, RecvModelUpdate, OnBindingCommand);
    that->Begin();
    that->setPacketInterval(RequestedRCpacketInterval);
    delete this;
    handset = that;
}

void AutoDetect::startPPM() const
{
    migrateTo(new PPMHandset());
}

void AutoDetect::startCRSF() const
{
    migrateTo(new CRSFHandset());
}

void AutoDetect::handleInput()
{
    size_t length = 0;
    const auto now = millis();

    const auto items = static_cast<rmt_item32_t *>(xRingbufferReceive(rb, &length, 0));
    if (items)
    {
        vRingbufferReturnItem(rb, static_cast<void *>(items));
        lastDetect = now;
        length /= 4; // one RMT = 4 Bytes
        for (int i = 0; i < length; i++)
        {
            const auto item = items[i];
            if (item.duration0 > (400 * RMT_TICKS_PER_US) || item.duration1 > (400 * RMT_TICKS_PER_US))
            {
                input_detect++;
            }
            // Depending on the baud rate pulse will be somewhere between 0.19 and 86 us,
            // so use 100 as an upper bound
            else if ((item.duration0 < (100 * RMT_TICKS_PER_US) && item.duration0 != 0) ||
                     (item.duration1 < (100 * RMT_TICKS_PER_US) && item.duration1 != 0))
            {
                input_detect--;
            }
        }
        if (input_detect < -100)
        {
            DBGLN("Serial signal detected");
            rmt_driver_uninstall(PPM_RMT_CHANNEL);
            startCRSF();
        }
        else if (input_detect > 100)
        {
            DBGLN("PPM signal detected");
            rmt_driver_uninstall(PPM_RMT_CHANNEL);
            startPPM();
        }
    }
    else
    {
        if (now - 1000 > lastDetect && input_detect != 0)
        {
            DBGLN("No signal detected");
            input_detect = 0;
        }
    }
}

#endif
