/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

struct gpiod_chip;
#ifdef GPIOD_V2
struct gpiod_line_request;
#else
struct gpiod_line;
#endif

class GpioLine {
public:
    enum class Direction { In, Out };

    struct EdgeEvent {
        bool rising{false};
        uint64_t timestamp_ns{0};
    };

    GpioLine() = default;
    explicit GpioLine(int gpio) : gpio_(gpio) {}
    GpioLine(std::string chip_path, int gpio) : gpio_(gpio), chip_path_(std::move(chip_path)) {}
    ~GpioLine();

    GpioLine(const GpioLine&)            = delete;
    GpioLine& operator=(const GpioLine&) = delete;
    GpioLine(GpioLine&&)                 = delete;
    GpioLine& operator=(GpioLine&&)      = delete;

    void setNumber(int gpio) {
        gpio_ = gpio;
    }
    int number() const {
        return gpio_;
    }
    bool valid() const {
        return gpio_ >= 0;
    }

    void exportLine();
    void unexportLine();
    void setDirection(Direction dir);
    void setValue(bool value);
    bool getValue() const;
    // Supported edge strings: "none", "rising", "both"
    void setEdge(const std::string& edge);
    bool waitForEdge(int timeout_ms) const;
    size_t readEdgeEvents(std::vector<EdgeEvent>& out_events, int timeout_ms, size_t max_events = 64) const;

    static void setDefaultChip(const std::string& chip_path);
    static const std::string& defaultChip();
    static void setLine(int gpio, bool value);

private:
    int gpio_ = -1;
    std::string chip_path_;
    mutable gpiod_chip* chip_ = nullptr;
#ifdef GPIOD_V2
    mutable gpiod_line_request* request_ = nullptr;
#else
    mutable gpiod_line* line_ = nullptr;
#endif
    mutable bool requested_      = false;
    mutable Direction direction_ = Direction::In;
    mutable bool output_value_   = false;

    void ensureLine() const;
    const std::string& chipPath() const;
    void releaseLine() const;
    void requestInput() const;
    void requestOutput(bool value) const;
    void requestEdge(bool both_edges) const;
};
