/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "sub_file_parser.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
        std::exit(1);
    }
}

}  // namespace

int main() {
    using namespace model;

    // 1. Parse Princeton Key File
    {
        const char* text =
            "Filetype: Flipper SubGhz Key File\n"
            "Version: 1\n"
            "Frequency: 433920000\n"
            "Preset: FuriHalSubGhzPresetOok650Async\n"
            "Protocol: Princeton\n"
            "Bit: 24\n"
            "Key: 00 00 00 00 00 55 AA 3C\n"
            "TE: 400\n"
            "Guard_time: 31\n";

        SubGhzSignal sig;
        std::string err;
        require(parse_sub_string(text, sig, err), "Princeton parse failed: " + err);
        require(sig.file_type == SubGhzFileType::KeyFile, "Expected KeyFile");
        require(sig.frequency_hz == 433920000U, "Frequency mismatch");
        require(sig.preset == SubGhzPreset::Ook650Async, "Preset mismatch");
        require(sig.protocol == "Princeton", "Protocol mismatch");
        require(sig.bit_count == 24U, "Bit count mismatch");
        require(sig.key == 0x55AA3CULL, "Key mismatch");
        require(sig.te_us == 400U, "TE mismatch");
        require(!sig.raw_timings.empty(), "Expected synthesized timings");
        require(sig.raw_timings.front() > 0, "Timings must begin with positive pulse");

        // Round-trip serialization
        const std::string serialized = serialize_sub_signal(sig);
        SubGhzSignal roundtrip;
        require(parse_sub_string(serialized, roundtrip, err), "Round-trip parse failed: " + err);
        require(roundtrip.key == sig.key && roundtrip.bit_count == sig.bit_count, "Round-trip key mismatch");
    }

    // 2. Parse Multi-line RAW File
    {
        const char* raw_text =
            "Filetype: Flipper SubGhz RAW File\n"
            "Version: 1\n"
            "Frequency: 315000000\n"
            "Preset: FuriHalSubGhzPresetOok270Async\n"
            "Protocol: RAW\n"
            "RAW_Data: 500 -500 1000 -1000\n"
            "RAW_Data: 350 -700 700 -350\n";

        SubGhzSignal sig;
        std::string err;
        require(parse_sub_string(raw_text, sig, err), "RAW parse failed: " + err);
        require(sig.file_type == SubGhzFileType::RawFile, "Expected RawFile");
        require(sig.frequency_hz == 315000000U, "RAW frequency mismatch");
        require(sig.preset == SubGhzPreset::Ook270Async, "RAW preset mismatch");
        require(sig.raw_timings.size() == 8U, "Expected 8 RAW timings");
        require(sig.raw_timings[0] == 500 && sig.raw_timings[1] == -500, "RAW timing values mismatch");
    }

    // 3. Parse BinRAW File
    {
        const char* binraw_text =
            "Filetype: Flipper SubGhz Key File\n"
            "Version: 1\n"
            "Frequency: 433920000\n"
            "Preset: FuriHalSubGhzPresetOok650Async\n"
            "Protocol: BinRAW\n"
            "Bit: 16\n"
            "TE: 350\n"
            "Bit_RAW: 16\n"
            "Data_RAW: EE 88\n";

        SubGhzSignal sig;
        std::string err;
        require(parse_sub_string(binraw_text, sig, err), "BinRAW parse failed: " + err);
        require(sig.protocol == "BinRAW", "Expected BinRAW");
        require(sig.bit_raw == 16U, "Bit_RAW mismatch");
        require(sig.data_raw.size() == 2U, "Data_RAW byte count mismatch");
        require(!sig.raw_timings.empty(), "BinRAW did not expand to timings");
        // 0xEE = 11101110 -> +3*350=1050, -350, +3*350=1050, -350
        require(sig.raw_timings[0] == 1050 && sig.raw_timings[1] == -350, "BinRAW timing coalescence mismatch");
    }

    // 4. Parse Custom Preset File
    {
        const char* custom_text =
            "Filetype: Flipper SubGhz Key File\n"
            "Version: 1\n"
            "Frequency: 868350000\n"
            "Preset: FuriHalSubGhzPresetCustom\n"
            "Custom_preset_module: CC1101\n"
            "Custom_preset_data: 02 0D 08 32 00 00 C0 00 00 00 00 00 00 00\n"
            "Protocol: CAME\n"
            "Bit: 12\n"
            "Key: 00 00 00 00 00 00 0A 5B\n";

        SubGhzSignal sig;
        std::string err;
        require(parse_sub_string(custom_text, sig, err), "Custom preset parse failed: " + err);
        require(sig.preset == SubGhzPreset::Custom, "Expected Custom preset");
        require(sig.custom_preset_module == "CC1101", "Custom module mismatch");
        require(sig.custom_preset_data.size() == 14U, "Custom preset data length mismatch");
    }

    // 5. Reject out-of-band frequency
    {
        const char* bad_freq =
            "Filetype: Flipper SubGhz Key File\n"
            "Version: 1\n"
            "Frequency: 100000000\n"
            "Preset: FuriHalSubGhzPresetOok650Async\n"
            "Protocol: Princeton\n"
            "Bit: 24\n"
            "Key: 00 00 00 00 00 11 22 33\n";
        SubGhzSignal sig;
        std::string err;
        require(!parse_sub_string(bad_freq, sig, err), "Out-of-band frequency should be rejected");
    }

    std::cout << "sub_file_parser_test passed\n";
    return 0;
}
