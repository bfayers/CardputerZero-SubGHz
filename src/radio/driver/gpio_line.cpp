/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "gpio_line.h"

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <string>

#if !USE_DESKTOP && defined(__linux__)
#include <gpiod.h>
#endif

namespace {
std::string& chipPathStorage() {
    static std::string chip_path = "/dev/gpiochip0";
    return chip_path;
}

#if !USE_DESKTOP && defined(__linux__)
std::string err(const std::string& action) {
    return action + " failed: " + std::strerror(errno);
}
#endif
}  // namespace

GpioLine::~GpioLine() {
    releaseLine();
#if !USE_DESKTOP && defined(__linux__)
    if (chip_) {
        gpiod_chip_close(chip_);
        chip_ = nullptr;
    }
#endif
}

void GpioLine::setDefaultChip(const std::string& chip_path) {
    chipPathStorage() = chip_path;
}

const std::string& GpioLine::defaultChip() {
    return chipPathStorage();
}

const std::string& GpioLine::chipPath() const {
    return chip_path_.empty() ? chipPathStorage() : chip_path_;
}

void GpioLine::ensureLine() const {
#if !USE_DESKTOP && defined(__linux__)
    if (!valid()) return;
    if (!chip_) {
        chip_ = gpiod_chip_open(chipPath().c_str());
        if (!chip_) throw std::runtime_error(err("gpiod_chip_open " + chipPath()));
    }
#ifndef GPIOD_V2
    if (!line_) {
        line_ = gpiod_chip_get_line(chip_, static_cast<unsigned int>(gpio_));
        if (!line_) throw std::runtime_error(err("gpiod_chip_get_line " + std::to_string(gpio_)));
    }
#endif
#endif
}

void GpioLine::releaseLine() const {
#if !USE_DESKTOP && defined(__linux__)
#ifdef GPIOD_V2
    if (request_) {
        gpiod_line_request_release(request_);
        request_ = nullptr;
        requested_ = false;
    }
#else
    if (requested_ && line_) {
        gpiod_line_release(line_);
        requested_ = false;
    }
#endif
#endif
}

void GpioLine::requestInput() const {
#if !USE_DESKTOP && defined(__linux__)
    ensureLine();
    if (!valid()) return;
    releaseLine();
#ifdef GPIOD_V2
    gpiod_line_settings* settings = gpiod_line_settings_new();
    gpiod_line_config* line_config = gpiod_line_config_new();
    gpiod_request_config* request_config = gpiod_request_config_new();
    if (!settings || !line_config || !request_config) {
        gpiod_line_settings_free(settings);
        gpiod_line_config_free(line_config);
        gpiod_request_config_free(request_config);
        throw std::runtime_error(err("gpiod request config allocation"));
    }
    unsigned int offset = static_cast<unsigned int>(gpio_);
    int rc = gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_INPUT);
    if (rc == 0) rc = gpiod_line_config_add_line_settings(line_config, &offset, 1, settings);
    gpiod_request_config_set_consumer(request_config, "cc1101-subghz");
    if (rc == 0) request_ = gpiod_chip_request_lines(chip_, request_config, line_config);
    gpiod_line_settings_free(settings);
    gpiod_line_config_free(line_config);
    gpiod_request_config_free(request_config);
    if (rc < 0 || !request_) throw std::runtime_error(err("gpiod request input GPIO" + std::to_string(gpio_)));
#else
    if (gpiod_line_request_input(line_, "cc1101-subghz") < 0) {
        throw std::runtime_error(err("gpiod_line_request_input GPIO" + std::to_string(gpio_)));
    }
#endif
    requested_ = true;
    direction_ = Direction::In;
#endif
}

void GpioLine::requestOutput(bool value) const {
#if !USE_DESKTOP && defined(__linux__)
    ensureLine();
    if (!valid()) return;
    releaseLine();
#ifdef GPIOD_V2
    gpiod_line_settings* settings = gpiod_line_settings_new();
    gpiod_line_config* line_config = gpiod_line_config_new();
    gpiod_request_config* request_config = gpiod_request_config_new();
    if (!settings || !line_config || !request_config) {
        gpiod_line_settings_free(settings);
        gpiod_line_config_free(line_config);
        gpiod_request_config_free(request_config);
        throw std::runtime_error(err("gpiod request config allocation"));
    }
    unsigned int offset = static_cast<unsigned int>(gpio_);
    int rc = gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_OUTPUT);
    if (rc == 0) {
        rc = gpiod_line_settings_set_output_value(
            settings, value ? GPIOD_LINE_VALUE_ACTIVE : GPIOD_LINE_VALUE_INACTIVE);
    }
    if (rc == 0) rc = gpiod_line_config_add_line_settings(line_config, &offset, 1, settings);
    gpiod_request_config_set_consumer(request_config, "cc1101-subghz");
    if (rc == 0) request_ = gpiod_chip_request_lines(chip_, request_config, line_config);
    gpiod_line_settings_free(settings);
    gpiod_line_config_free(line_config);
    gpiod_request_config_free(request_config);
    if (rc < 0 || !request_) throw std::runtime_error(err("gpiod request output GPIO" + std::to_string(gpio_)));
#else
    if (gpiod_line_request_output(line_, "cc1101-subghz", value ? 1 : 0) < 0) {
        throw std::runtime_error(err("gpiod_line_request_output GPIO" + std::to_string(gpio_)));
    }
#endif
    requested_ = true;
    direction_ = Direction::Out;
    output_value_ = value;
#else
    (void)value;
#endif
}

void GpioLine::requestEdge(bool both_edges) const {
#if !USE_DESKTOP && defined(__linux__)
    ensureLine();
    if (!valid()) return;
    releaseLine();
#ifdef GPIOD_V2
    gpiod_line_settings* settings = gpiod_line_settings_new();
    gpiod_line_config* line_config = gpiod_line_config_new();
    gpiod_request_config* request_config = gpiod_request_config_new();
    if (!settings || !line_config || !request_config) {
        gpiod_line_settings_free(settings);
        gpiod_line_config_free(line_config);
        gpiod_request_config_free(request_config);
        throw std::runtime_error(err("gpiod request config allocation"));
    }
    unsigned int offset = static_cast<unsigned int>(gpio_);
    int rc = gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_INPUT);
    if (rc == 0) {
        rc = gpiod_line_settings_set_edge_detection(
            settings, both_edges ? GPIOD_LINE_EDGE_BOTH : GPIOD_LINE_EDGE_RISING);
    }
    if (rc == 0) rc = gpiod_line_config_add_line_settings(line_config, &offset, 1, settings);
    gpiod_request_config_set_consumer(request_config, "cc1101-subghz");
    if (rc == 0) request_ = gpiod_chip_request_lines(chip_, request_config, line_config);
    gpiod_line_settings_free(settings);
    gpiod_line_config_free(line_config);
    gpiod_request_config_free(request_config);
    if (rc < 0 || !request_) throw std::runtime_error(err("gpiod request edge GPIO" + std::to_string(gpio_)));
#else
    const int rc = both_edges ? gpiod_line_request_both_edges_events(line_, "cc1101-subghz")
                              : gpiod_line_request_rising_edge_events(line_, "cc1101-subghz");
    if (rc < 0) {
        throw std::runtime_error(err("gpiod_line_request_edge_events GPIO" + std::to_string(gpio_)));
    }
#endif
    requested_ = true;
    direction_ = Direction::In;
#else
    (void)both_edges;
#endif
}

void GpioLine::exportLine() {
    ensureLine();
}

void GpioLine::unexportLine() {
    releaseLine();
}

void GpioLine::setDirection(Direction dir) {
    if (!valid()) return;
    if (dir == Direction::Out)
        requestOutput(output_value_);
    else
        requestInput();
}

void GpioLine::setValue(bool value) {
#if !USE_DESKTOP && defined(__linux__)
    if (!valid()) return;
    ensureLine();
    if (!requested_ || direction_ != Direction::Out) requestOutput(value);
#ifdef GPIOD_V2
    if (gpiod_line_request_set_value(request_,
                                     static_cast<unsigned int>(gpio_),
                                     value ? GPIOD_LINE_VALUE_ACTIVE : GPIOD_LINE_VALUE_INACTIVE) < 0) {
        throw std::runtime_error(err("gpiod_line_request_set_value GPIO" + std::to_string(gpio_)));
    }
#else
    if (gpiod_line_set_value(line_, value ? 1 : 0) < 0) {
        throw std::runtime_error(err("gpiod_line_set_value GPIO" + std::to_string(gpio_)));
    }
#endif
    output_value_ = value;
#else
    (void)value;
#endif
}

bool GpioLine::getValue() const {
#if !USE_DESKTOP && defined(__linux__)
    if (!valid()) return false;
    ensureLine();
    if (!requested_) requestInput();
#ifdef GPIOD_V2
    gpiod_line_value value = gpiod_line_request_get_value(request_, static_cast<unsigned int>(gpio_));
    if (value < 0) throw std::runtime_error(err("gpiod_line_request_get_value GPIO" + std::to_string(gpio_)));
    return value == GPIOD_LINE_VALUE_ACTIVE;
#else
    int value = gpiod_line_get_value(line_);
    if (value < 0) throw std::runtime_error(err("gpiod_line_get_value GPIO" + std::to_string(gpio_)));
    return value != 0;
#endif
#else
    return false;
#endif
}

void GpioLine::setEdge(const std::string& edge) {
    if (!valid()) return;
    if (edge == "rising")
        requestEdge(false);
    else if (edge == "both")
        requestEdge(true);
    else if (edge == "none")
        requestInput();
    else
        throw std::runtime_error("libgpiod backend supports edge 'none', 'rising', or 'both'");
}

bool GpioLine::waitForEdge(int timeout_ms) const {
    std::vector<EdgeEvent> events;
    return readEdgeEvents(events, timeout_ms, 1) > 0;
}

size_t GpioLine::readEdgeEvents(std::vector<EdgeEvent>& out_events, int timeout_ms, size_t max_events) const {
#if !USE_DESKTOP && defined(__linux__)
    if (!valid() || max_events == 0) return 0;
    ensureLine();
    if (!requested_ || direction_ != Direction::In) requestEdge(true);
#ifdef GPIOD_V2
    int64_t timeout_ns = -1;
    if (timeout_ms >= 0) timeout_ns = static_cast<int64_t>(timeout_ms) * 1000000LL;
    int rc = gpiod_line_request_wait_edge_events(request_, timeout_ns);
    if (rc < 0) throw std::runtime_error(err("gpiod_line_request_wait_edge_events GPIO" + std::to_string(gpio_)));
    if (rc == 0) return 0;

    gpiod_edge_event_buffer* buffer = gpiod_edge_event_buffer_new(max_events);
    if (!buffer) throw std::runtime_error(err("gpiod_edge_event_buffer_new"));
    rc = gpiod_line_request_read_edge_events(request_, buffer, max_events);
    if (rc < 0) {
        gpiod_edge_event_buffer_free(buffer);
        throw std::runtime_error(err("gpiod_line_request_read_edge_events GPIO" + std::to_string(gpio_)));
    }
    const size_t count = static_cast<size_t>(rc);
    for (size_t i = 0; i < count; ++i) {
        gpiod_edge_event* ev = gpiod_edge_event_buffer_get_event(buffer, i);
        if (!ev) continue;
        EdgeEvent item;
        item.rising = gpiod_edge_event_get_event_type(ev) == GPIOD_EDGE_EVENT_RISING_EDGE;
        item.timestamp_ns = gpiod_edge_event_get_timestamp_ns(ev);
        out_events.push_back(item);
    }
    gpiod_edge_event_buffer_free(buffer);
    return count;
#else
    timespec ts{};
    timespec* tsp = nullptr;
    if (timeout_ms >= 0) {
        ts.tv_sec = timeout_ms / 1000;
        ts.tv_nsec = (timeout_ms % 1000) * 1000000L;
        tsp = &ts;
    }
    int rc = gpiod_line_event_wait(line_, tsp);
    if (rc < 0) throw std::runtime_error(err("gpiod_line_event_wait GPIO" + std::to_string(gpio_)));
    if (rc == 0) return 0;

    size_t count = 0;
    while (count < max_events) {
        gpiod_line_event event{};
        if (gpiod_line_event_read(line_, &event) < 0) {
            break;
        }
        EdgeEvent item;
        item.rising = event.event_type == GPIOD_LINE_EVENT_RISING_EDGE;
        item.timestamp_ns =
            static_cast<uint64_t>(event.ts.tv_sec) * 1000000000ULL + static_cast<uint64_t>(event.ts.tv_nsec);
        out_events.push_back(item);
        ++count;

        timespec zero_ts{0, 0};
        if (gpiod_line_event_wait(line_, &zero_ts) <= 0) {
            break;
        }
    }
    return count;
#endif
#else
    (void)out_events;
    (void)timeout_ms;
    (void)max_events;
    return 0;
#endif
}

void GpioLine::setLine(int gpio, bool value) {
    GpioLine line(gpio);
    line.exportLine();
    line.setDirection(Direction::Out);
    line.setValue(value);
}
