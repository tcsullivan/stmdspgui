/**
 * @file stmdsp.cpp
 * @brief Interface for communication with stmdsp device over serial.
 *
 * Copyright (C) 2021 Clyne Sullivan
 *
 * Distributed under the GNU GPL v3 or later. You should have received a copy of
 * the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

#include "stmdsp.hpp"

#include <serial/serial.h>

extern void log(const std::string& str);

namespace stmdsp
{
    std::list<std::string>& scanner::scan()
    {
        auto devices = serial::list_ports();
        for (auto& device : devices) {
            if (device.hardware_id.find(STMDSP_USB_ID) != std::string::npos)
                m_available_devices.emplace_front(device.port);
        }

        return m_available_devices;
    }

    device::device(const std::string& file)
    {
        // This could throw!
        m_serial.reset(new serial::Serial(file, 8'000'000, serial::Timeout::simpleTimeout(50)));

        m_serial->flush();
        m_serial->write("i");
        auto id = m_serial->read(7);

        if (id.starts_with("stmdsp")) {
            if (id.back() == 'h')
                m_platform = platform::H7;
            else if (id.back() == 'l')
                m_platform = platform::L4;
            else
                m_serial.release();
        } else {
            m_serial.release();
        }
    }

    device::~device()
    {
        disconnect();
    }

    bool device::connected() {
        if (m_serial && !m_serial->isOpen())
            m_serial.release();

        return m_serial ? true : false;
    }

    void device::disconnect() {
        if (m_serial)
            m_serial.release();
    }

    void device::continuous_set_buffer_size(unsigned int size) {
        if (connected()) {
            m_buffer_size = size;

            uint8_t request[3] = {
                'B',
                static_cast<uint8_t>(size),
                static_cast<uint8_t>(size >> 8)
            };

            try {
                m_serial->write(request, 3);
            } catch (...) {
                m_serial.release();
                log("Lost connection!");
            }
        }
    }

    void device::set_sample_rate(unsigned int id) {
        if (connected()) {
            uint8_t request[2] = {
                'r',
                static_cast<uint8_t>(id)
            };

            try {
                m_serial->write(request, 2);
            } catch (...) {
                m_serial.release();
                log("Lost connection!");
            }
        }
    }

    unsigned int device::get_sample_rate() {
        if (connected() && !is_running()) {
            uint8_t request[2] = {
                'r', 0xFF
            };

            unsigned char result = 0xFF;
            try {
                m_serial->write(request, 2);
                m_serial->read(&result, 1);
            } catch (...) {
                m_serial.release();
                log("Lost connection!");
            }

            m_sample_rate = result;
        }

        return m_sample_rate;
    }

    void device::continuous_start() {
        if (connected()) {
            try {
                m_serial->write("R");
                m_is_running = true;
            } catch (...) {
                m_serial.release();
                log("Lost connection!");
            }
        }
    }

    void device::continuous_start_measure() {
        if (connected()) {
            try {
                m_serial->write("M");
                m_is_running = true;
            } catch (...) {
                m_serial.release();
                log("Lost connection!");
            }
        }
    }

    uint32_t device::continuous_start_get_measurement() {
        uint32_t count = 0;
        if (connected()) {
            try {
                m_serial->write("m");
                m_serial->read(reinterpret_cast<uint8_t *>(&count), sizeof(uint32_t));
            } catch (...) {
                m_serial.release();
                log("Lost connection!");
            }
        }

        return count / 2;
    }

    std::vector<adcsample_t> device::continuous_read() {
        if (connected()) {
            try {
                m_serial->write("s");
                unsigned char sizebytes[2];
                m_serial->read(sizebytes, 2);
                unsigned int size = sizebytes[0] | (sizebytes[1] << 8);
                if (size > 0) {
                    std::vector<adcsample_t> data (size);
                    unsigned int total = size * sizeof(adcsample_t);
                    unsigned int offset = 0;

                    while (total > 512) {
                        m_serial->read(reinterpret_cast<uint8_t *>(&data[0]) + offset, 512);
                        m_serial->write("n");
                        offset += 512;
                        total -= 512;
                    }
                    m_serial->read(reinterpret_cast<uint8_t *>(&data[0]) + offset, total);
                    m_serial->write("n");
                    return data;

                }
            } catch (...) {
                m_serial.release();
                log("Lost connection!");
            }
        }

        return {};
    }

    std::vector<adcsample_t> device::continuous_read_input() {
        if (connected()) {
            try {
                m_serial->write("t");
                unsigned char sizebytes[2];
                m_serial->read(sizebytes, 2);
                unsigned int size = sizebytes[0] | (sizebytes[1] << 8);
                if (size > 0) {
                    std::vector<adcsample_t> data (size);
                    unsigned int total = size * sizeof(adcsample_t);
                    unsigned int offset = 0;

                    while (total > 512) {
                        m_serial->read(reinterpret_cast<uint8_t *>(&data[0]) + offset, 512);
                        m_serial->write("n");
                        offset += 512;
                        total -= 512;
                    }
                    m_serial->read(reinterpret_cast<uint8_t *>(&data[0]) + offset, total);
                    m_serial->write("n");
                    return data;

                }
            } catch (...) {
                m_serial.release();
                log("Lost connection!");
            }
        }

        return {};
    }

    void device::continuous_stop() {
        if (connected()) {
            try {
                m_serial->write("S");
                m_is_running = false;
            } catch (...) {
                m_serial.release();
                log("Lost connection!");
            }
        }
    }

    void device::siggen_upload(dacsample_t *buffer, unsigned int size) {
        if (connected()) {
            uint8_t request[3] = {
                'D',
                static_cast<uint8_t>(size),
                static_cast<uint8_t>(size >> 8)
            };

            try {
                m_serial->write(request, 3);
                // TODO different write size if feeding audio?
                m_serial->write((uint8_t *)buffer, size * sizeof(dacsample_t));
            } catch (...) {
                m_serial.release();
                log("Lost connection!");
            }
        }
    }

    void device::siggen_start() {
        if (connected()) {
            try {
                m_serial->write("W");
                m_is_siggening = true;
            } catch (...) {
                m_serial.release();
                log("Lost connection!");
            }
        }
    }

    void device::siggen_stop() {
        if (connected()) {
            try {
                m_serial->write("w");
                m_is_siggening = false;
            } catch (...) {
                m_serial.release();
                log("Lost connection!");
            }
        }
    }

    void device::upload_filter(unsigned char *buffer, size_t size) {
        if (connected()) {
            uint8_t request[3] = {
                'E',
                static_cast<uint8_t>(size),
                static_cast<uint8_t>(size >> 8)
            };

            try {
                m_serial->write(request, 3);
                m_serial->write(buffer, size);
            } catch (...) {
                m_serial.release();
                log("Lost connection!");
            }
        }
    }

    void device::unload_filter() {
        if (connected()) {
            try {
                m_serial->write("e");
            } catch (...) {
                m_serial.release();
                log("Lost connection!");
            }
        }
    }

    std::pair<RunStatus, Error> device::get_status() {
        std::pair<RunStatus, Error> ret;

        if (connected()) {
            try {
                m_serial->write("I");
                auto result = m_serial->read(2);
                ret = {static_cast<RunStatus>(result[0]),
                       static_cast<Error>(result[1])};

                bool running = ret.first == RunStatus::Running;
                if (m_is_running != running)
                    m_is_running = running;
            } catch (...) {
                m_serial.release();
                log("Lost connection!");
            }
        }

        return ret;
    }
}

