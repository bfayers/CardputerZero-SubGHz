/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "subghz_types.h"

#include <filesystem>
#include <string>
#include <string_view>

namespace model {

bool parse_sub_string(std::string_view content, SubGhzSignal& out_signal, std::string& out_error);
bool parse_sub_file(const std::filesystem::path& path, SubGhzSignal& out_signal, std::string& out_error);

std::string serialize_sub_signal(const SubGhzSignal& signal);
bool save_sub_file(const std::filesystem::path& path, const SubGhzSignal& signal, std::string& out_error);

}  // namespace model
