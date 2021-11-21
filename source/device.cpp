/**
 * @file device.cpp
 * @brief Contains code for device-related UI elements and logic.
 *
 * Copyright (C) 2021 Clyne Sullivan
 *
 * Distributed under the GNU GPL v3 or later. You should have received a copy of
 * the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

#include "stmdsp.hpp"

#include "imgui.h"
#include "wav.hpp"

#include <array>
#include <charconv>
#include <cmath>
#include <deque>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

extern std::string tempFileName;
extern void log(const std::string& str);
extern std::vector<stmdsp::dacsample_t> deviceGenLoadFormulaEval(const std::string_view);

std::shared_ptr<stmdsp::device> m_device;

static std::timed_mutex mutexDrawSamples;
static std::timed_mutex mutexDeviceLoad;
static std::ofstream logSamplesFile;
static wav::clip wavOutput;
static std::deque<stmdsp::dacsample_t> drawSamplesQueue;
static std::deque<stmdsp::dacsample_t> drawSamplesInputQueue;
static bool drawSamplesInput = false;
static unsigned int drawSamplesBufferSize = 1;

void deviceSetInputDrawing(bool enabled)
{
    drawSamplesInput = enabled;
}

static void measureCodeTask(std::shared_ptr<stmdsp::device> device)
{
    std::this_thread::sleep_for(std::chrono::seconds(1));

    if (device) {
        const auto cycles = device->continuous_start_get_measurement();
        log(std::string("Execution time: ") + std::to_string(cycles) + " cycles.");
    }
}

static std::vector<stmdsp::dacsample_t> tryReceiveChunk(
    std::shared_ptr<stmdsp::device> device,
    auto readFunc)
{
    int tries = -1;
    do {
        const auto chunk = readFunc(device.get());
        if (!chunk.empty())
            return chunk;
        else
            std::this_thread::sleep_for(std::chrono::microseconds(20));
    } while (++tries < 100 && device->is_running());

    return {};
}

static std::chrono::duration<double> getBufferPeriod(
    std::shared_ptr<stmdsp::device> device,
    const double factor = 0.975)
{
    if (device) {
        const double bufferSize = device->get_buffer_size();
        const double sampleRate = device->get_sample_rate();
        return std::chrono::duration<double>(bufferSize / sampleRate * factor);
    } else {
        return {};
    }
}

static void drawSamplesTask(std::shared_ptr<stmdsp::device> device)
{
    if (!device)
        return;

    const auto bufferTime = getBufferPeriod(device);

    std::unique_lock<std::timed_mutex> lockDraw (mutexDrawSamples, std::defer_lock);
    std::unique_lock<std::timed_mutex> lockDevice (mutexDeviceLoad, std::defer_lock);

    auto addToQueue = [&lockDraw](auto& queue, const auto& chunk) {
        lockDraw.lock();
        std::copy(chunk.cbegin(), chunk.cend(), std::back_inserter(queue));
        lockDraw.unlock();
    };

    while (device && device->is_running()) {
        const auto next = std::chrono::high_resolution_clock::now() + bufferTime;

        if (lockDevice.try_lock_until(next)) {
            const auto chunk = tryReceiveChunk(device,
                std::mem_fn(&stmdsp::device::continuous_read));
            lockDevice.unlock();

            addToQueue(drawSamplesQueue, chunk);
            if (logSamplesFile.good()) {
                for (const auto& s : chunk)
                    logSamplesFile << s << '\n';
            }
        } else {
            // Device must be busy, cooldown.
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }

        if (drawSamplesInput) {
            if (lockDevice.try_lock_for(std::chrono::milliseconds(1))) {
                const auto chunk2 = tryReceiveChunk(device,
                    std::mem_fn(&stmdsp::device::continuous_read_input));
                lockDevice.unlock();

                addToQueue(drawSamplesInputQueue, chunk2);
            }
        }

        std::this_thread::sleep_until(next);
    }
}

static void feedSigGenTask(std::shared_ptr<stmdsp::device> device)
{
    if (!device)
        return;

    const auto delay = getBufferPeriod(device);
    const auto uploadDelay = getBufferPeriod(device, 0.001);

    std::vector<stmdsp::dacsample_t> wavBuf (device->get_buffer_size() * 2, 2048);

    std::unique_lock<std::timed_mutex> lockDevice (mutexDeviceLoad, std::defer_lock);

    lockDevice.lock();
    device->siggen_upload(wavBuf.data(), wavBuf.size());
    wavBuf.resize(wavBuf.size() / 2);
    device->siggen_start();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    lockDevice.unlock();

    std::vector<int16_t> wavIntBuf (wavBuf.size());

    while (device->is_siggening()) {
        const auto next = std::chrono::high_resolution_clock::now() + delay;

        wavOutput.next(wavIntBuf.data(), wavIntBuf.size());
        auto src = wavIntBuf.cbegin();
        std::generate(wavBuf.begin(), wavBuf.end(),
            [&src] { return static_cast<stmdsp::dacsample_t>(*src++ / 16 + 2048); });

        lockDevice.lock();
        while (!device->siggen_upload(wavBuf.data(), wavBuf.size()))
            std::this_thread::sleep_for(uploadDelay);
        lockDevice.unlock();

        std::this_thread::sleep_until(next);
    }
}

static void statusTask(std::shared_ptr<stmdsp::device> device)
{
    if (!device)
        return;

    while (device->connected()) {
        std::unique_lock<std::timed_mutex> lockDevice (mutexDeviceLoad, std::defer_lock);
        lockDevice.lock();
        const auto [status, error] = device->get_status();
        lockDevice.unlock();

        if (error != stmdsp::Error::None) {
            switch (error) {
            case stmdsp::Error::NotIdle:
                log("Error: Device already running...");
                break;
            case stmdsp::Error::ConversionAborted:
                log("Error: Algorithm unloaded, a fault occurred!");
                break;
            default:
                log("Error: Device had an issue...");
                break;
            }
        }

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

void deviceLoadAudioFile(const std::string& file)
{
    wavOutput = wav::clip(file);
    if (wavOutput.valid())
        log("Audio file loaded.");
    else
        log("Error: Bad WAV audio file.");
}

void deviceLoadLogFile(const std::string& file)
{
    logSamplesFile = std::ofstream(file);
    if (logSamplesFile.good())
        log("Log file ready.");
    else
        log("Error: Could not open log file.");
}

bool deviceGenStartToggle()
{
    if (m_device) {
        bool running = m_device->is_siggening();
        if (!running) {
            if (wavOutput.valid())
                std::thread(feedSigGenTask, m_device).detach();
            else
                m_device->siggen_start();
            log("Generator started.");
        } else {
            m_device->siggen_stop();
            log("Generator stopped.");
        }

        return !running;
    }

    return false;
}

void deviceUpdateDrawBufferSize(double timeframe)
{
    drawSamplesBufferSize = std::round(
        m_device->get_sample_rate() * timeframe);
}

void deviceSetSampleRate(unsigned int rate)
{
    do {
        m_device->set_sample_rate(rate);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    } while (m_device->get_sample_rate() != rate);
}

bool deviceConnect()
{
    static std::thread statusThread;

    if (!m_device) {
        stmdsp::scanner scanner;
        if (auto devices = scanner.scan(); !devices.empty()) {
            try {
                m_device.reset(new stmdsp::device(devices.front()));
            } catch (...) {
                log("Failed to connect (check permissions?).");
                m_device.reset();
            }

            if (m_device) {
                if (m_device->connected()) {
                    log("Connected!");
                    statusThread = std::thread(statusTask, m_device);
                    statusThread.detach();
                    return true;
                } else {
                    m_device.reset();
                    log("Failed to connect.");
                }
            }
        } else {
            log("No devices found.");
        }
    } else {
        m_device->disconnect();
        if (statusThread.joinable())
            statusThread.join();
        m_device.reset();
        log("Disconnected.");
    }

    return false;
}

void deviceStart(bool measureCodeTime, bool logResults, bool drawSamples)
{
    if (!m_device) {
        log("No device connected.");
        return;
    }

    if (m_device->is_running()) {
        {
            std::scoped_lock lock (mutexDrawSamples, mutexDeviceLoad);
            std::this_thread::sleep_for(std::chrono::microseconds(150));
            m_device->continuous_stop();
        }
        if (logSamplesFile.good()) {
            logSamplesFile.close();
            log("Log file saved and closed.");
        }
        log("Ready.");
    } else {
        if (measureCodeTime) {
            m_device->continuous_start_measure();
            std::thread(measureCodeTask, m_device).detach();
        } else {
            m_device->continuous_start();
            if (drawSamples || logResults || wavOutput.valid())
                std::thread(drawSamplesTask, m_device).detach();
        }
        log("Running.");
    }
}

void deviceAlgorithmUpload()
{
    if (!m_device) {
        log("No device connected.");
        return;
    } else if (m_device->is_running()) {
        return;
    }

    if (std::ifstream algo (tempFileName + ".o"); algo.good()) {
        std::ostringstream sstr;
        sstr << algo.rdbuf();
        auto str = sstr.str();

        m_device->upload_filter(reinterpret_cast<unsigned char *>(&str[0]), str.size());
        log("Algorithm uploaded.");
    } else {
        log("Algorithm must be compiled first.");
    }
}

void deviceAlgorithmUnload()
{
    if (!m_device) {
        log("No device connected.");
    } else if (!m_device->is_running()) {
        m_device->unload_filter();
        log("Algorithm unloaded.");
    }
}

void deviceGenLoadList(const std::string_view list)
{
    std::vector<stmdsp::dacsample_t> samples;

    auto it = list.cbegin();
    while (it != list.cend() && samples.size() < stmdsp::SAMPLES_MAX * 2) {
        const auto end = list.find_first_not_of("0123456789",
            std::distance(list.cbegin(), it));
        const auto itend = end != std::string_view::npos ? list.cbegin() + end
                                                         : list.cend();
        unsigned long n;
        const auto [ptr, ec] = std::from_chars(it, itend, n);
        if (ec != std::errc())
            break;

        samples.push_back(n & 4095);
        it = itend;
    }

    if (samples.size() <= stmdsp::SAMPLES_MAX * 2) {
        // DAC buffer must be of even size
        if (samples.size() % 2 != 0)
            samples.push_back(samples.back());

        if (m_device)
            m_device->siggen_upload(samples.data(), samples.size());
        log("Generator ready.");
    } else {
        log("Error: Too many samples for signal generator.");
    }
}

void deviceGenLoadFormula(std::string_view formula)
{
    auto samples = deviceGenLoadFormulaEval(formula);

    if (!samples.empty()) {
        if (m_device)
            m_device->siggen_upload(samples.data(), samples.size());

        log("Generator ready.");
    } else {
        log("Error: Bad formula.");
    }
}

void pullFromQueue(
    std::deque<stmdsp::dacsample_t>& queue,
    std::vector<stmdsp::dacsample_t>& buffer,
    decltype(buffer.begin())& bufferCursor,
    double timeframe)
{
    if (buffer.size() != drawSamplesBufferSize) {
        buffer.resize(drawSamplesBufferSize);
        bufferCursor = buffer.begin();
    }

    std::scoped_lock lock (mutexDrawSamples);

    auto count = drawSamplesBufferSize / (60. * timeframe) * 1.025;
    count = std::min(drawSamplesInputQueue.size(),
        static_cast<std::size_t>(count));
    for (auto i = count; i; --i) {
        *bufferCursor = queue.front();
        queue.pop_front();

        if (++bufferCursor == buffer.end())
            bufferCursor = buffer.begin();
    }
}

void pullFromDrawQueue(
    std::vector<stmdsp::dacsample_t>& buffer,
    decltype(buffer.begin())& bufferCursor,
    double timeframe)
{
    pullFromQueue(drawSamplesQueue, buffer, bufferCursor, timeframe);
}

void pullFromInputDrawQueue(
    std::vector<stmdsp::dacsample_t>& buffer,
    decltype(buffer.begin())& bufferCursor,
    double timeframe)
{
    pullFromQueue(drawSamplesInputQueue, buffer, bufferCursor, timeframe);
}

