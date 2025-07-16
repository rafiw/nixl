/*
 * SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

#include <iostream>
#include <signal.h>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <thread>
#include <filesystem>
#include <string>
#include <cstring>
#include <cstdlib>
#include <memory>
#include <random>
#include <gflags/gflags.h>

namespace fs = std::filesystem;

#include "common/cyclic_buffer.h"
#include "nixl_types.h"

// GFlags definitions
DEFINE_int32(num_events, 10, "Number of events to generate");
DEFINE_int32(interval_ms, 1000, "Interval between events in milliseconds");
DEFINE_bool(continuous, false, "Generate events continuously until interrupted");
DEFINE_bool(verbose, false, "Print generated events to console");

volatile bool g_running = true;

std::string
format_timestamp(uint64_t timestamp_us) {
    auto time_point =
        std::chrono::system_clock::time_point(std::chrono::microseconds(timestamp_us));
    auto time_t = std::chrono::system_clock::to_time_t(time_point);

    std::stringstream ss;
    ss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");

    auto microseconds = timestamp_us % 1000000;
    ss << "." << std::setfill('0') << std::setw(6) << microseconds;

    return ss.str();
}

std::string
format_bytes(uint64_t bytes) {
    const char *units[] = {"B", "KB", "MB", "GB", "TB"};
    int unit_index = 0;
    double value = static_cast<double>(bytes);

    while (value >= 1024.0 && unit_index < 4) {
        value /= 1024.0;
        unit_index++;
    }

    std::stringstream ss;
    ss << std::fixed << std::setprecision(2) << value << " " << units[unit_index];
    return ss.str();
}

void
print_telemetry_event(const nixlTelemetryEvent &event) {
    std::cout << "\n=== Generated NIXL Telemetry Event ===" << std::endl;
    std::cout << "Timestamp: " << format_timestamp(event.timestamp_us) << std::endl;
    std::cout << "Transfer Statistics:" << std::endl;
    std::cout << "  TX Bytes: " << format_bytes(event.tx_bytes) << std::endl;
    std::cout << "  RX Bytes: " << format_bytes(event.rx_bytes) << std::endl;
    std::cout << "  TX Requests: " << event.tx_requests_num << std::endl;
    std::cout << "  RX Requests: " << event.rx_requests_num << std::endl;
    std::cout << "  Memory Registered: " << format_bytes(event.memory_registered) << " bytes"
              << std::endl;
    std::cout << "  Total Transaction Time: " << event.total_transaction_time_us << " us"
              << std::endl;
    std::cout << "  Error Counts:" << std::endl;

    for (int i = 0; i < std::abs(NIXL_ERR_LAST); i++) {
        // skip success since not captured
        if (i != 1) {
            std::cout << "    "
                      << nixlEnumStrings::statusStr(static_cast<nixl_status_t>((i - 1) * -1))
                      << ": " << event.error_counts[i] << std::endl;
        }
    }

    if (event.num_backends > 0) {
        std::cout << "Plugin Telemetry (" << event.num_backends << " backends):" << std::endl;
        for (uint16_t i = 0; i < event.num_backends; i++) {
            const auto &backend = event.backend_telemetry[i];
            std::cout << "  Backend: " << backend.plugin_name << std::endl;
            std::cout << "    Metrics (" << backend.num_metrics << "):" << std::endl;
            for (uint32_t j = 0; j < backend.num_metrics; j++) {
                const auto &metric = backend.metrics[j];
                std::cout << "      " << metric.name << ": " << metric.value << std::endl;
            }
        }
    }
    std::cout << "=====================================" << std::endl;
}


nixlTelemetryEvent
generate_random_event(uint64_t event_id) {
    std::random_device rd;
    std::mt19937 gen(rd());

    // Create distributions for different types of data
    std::uniform_int_distribution<uint64_t> bytes_dist(1024, 1024 * 1024 * 100); // 1KB to 100MB
    std::uniform_int_distribution<uint32_t> requests_dist(1, 1000);
    std::uniform_int_distribution<uint64_t> memory_dist(1024 * 1024,
                                                        1024 * 1024 * 1024); // 1MB to 1GB
    std::uniform_int_distribution<uint64_t> time_dist(100, 10000); // 100us to 10ms
    std::uniform_int_distribution<uint32_t> error_dist(0, 5);
    std::uniform_int_distribution<uint8_t> backend_dist(0, 3); // 0-3 backends
    std::uniform_int_distribution<uint32_t> metric_dist(1, 5); // 1-5 metrics per backend

    nixlTelemetryEvent event = {};

    // Set timestamp
    event.timestamp_us = std::chrono::duration_cast<std::chrono::microseconds>(
                             std::chrono::system_clock::now().time_since_epoch())
                             .count();

    // Generate transfer statistics
    event.tx_bytes = bytes_dist(gen);
    event.rx_bytes = bytes_dist(gen);
    event.tx_requests_num = requests_dist(gen);
    event.rx_requests_num = requests_dist(gen);
    event.memory_registered = memory_dist(gen);
    event.total_transaction_time_us = time_dist(gen);

    // Generate last requests times
    for (size_t i = 0; i < LAST_REQUESTS_ARRAY_SIZE; i++) {
        event.last_requests_time_us[i] = time_dist(gen);
    }

    // Generate error counts (mostly zeros, some random errors)
    for (int i = 0; i < std::abs(NIXL_ERR_LAST); i++) {
        if (i != 1) { // skip success
            event.error_counts[i] = error_dist(gen);
        }
    }

    // Generate plugin telemetry
    event.num_backends = backend_dist(gen);

    const char *backend_names[] = {
        "gpu_backend", "network_backend", "storage_backend", "compute_backend"};
    const char *metric_names[] = {"gpu_memory_usage",
                                  "gpu_utilization",
                                  "network_bandwidth",
                                  "network_latency",
                                  "storage_throughput",
                                  "storage_latency",
                                  "cpu_usage",
                                  "memory_usage",
                                  "queue_depth",
                                  "error_rate",
                                  "cache_hit_rate",
                                  "connection_count"};

    for (uint8_t i = 0; i < event.num_backends; i++) {
        auto &backend = event.backend_telemetry[i];

        // Set backend name
        strncpy(backend.plugin_name, backend_names[i % 4], MAX_PLUGIN_NAME_LEN - 1);
        backend.plugin_name[MAX_PLUGIN_NAME_LEN - 1] = '\0';

        // Generate metrics
        backend.num_metrics = metric_dist(gen);
        for (uint32_t j = 0; j < backend.num_metrics; j++) {
            auto &metric = backend.metrics[j];

            // Set metric name
            strncpy(metric.name, metric_names[(i * 3 + j) % 12], MAX_METRIC_NAME_LEN - 1);
            metric.name[MAX_METRIC_NAME_LEN - 1] = '\0';

            // Generate metric value
            std::uniform_int_distribution<uint64_t> metric_value_dist(0, 1000000);
            metric.value = metric_value_dist(gen);
        }
    }

    return event;
}

nixlTelemetryEvent
update_event(nixlTelemetryEvent &event) {
    std::random_device rd;
    std::mt19937 gen(rd());

    // Create distributions for different types of data
    std::uniform_int_distribution<uint64_t> bytes_dist(1024, 1024 * 1024 * 100); // 1KB to 100MB
    std::uniform_int_distribution<uint32_t> requests_dist(1, 1000);
    std::uniform_int_distribution<uint64_t> memory_dist(1024 * 1024,
                                                        1024 * 1024 * 1024); // 1MB to 1GB
    std::uniform_int_distribution<uint64_t> time_dist(100, 10000); // 100us to 10ms
    std::uniform_int_distribution<uint32_t> error_dist(0, 5);
    std::uniform_int_distribution<uint8_t> backend_dist(0, 3); // 0-3 backends
    std::uniform_int_distribution<uint32_t> metric_dist(1, 5); // 1-5 metrics per backend

    // Set timestamp
    event.timestamp_us += std::chrono::duration_cast<std::chrono::microseconds>(
                              std::chrono::system_clock::now().time_since_epoch())
                              .count();

    // Generate transfer statistics
    event.tx_bytes += bytes_dist(gen);
    event.rx_bytes += bytes_dist(gen);
    event.tx_requests_num += requests_dist(gen);
    event.rx_requests_num += requests_dist(gen);
    event.memory_registered += memory_dist(gen);
    event.total_transaction_time_us += time_dist(gen);

    // Generate last requests times
    for (size_t i = 0; i < LAST_REQUESTS_ARRAY_SIZE; i++) {
        event.last_requests_time_us[i] += time_dist(gen);
    }

    // Generate error counts (mostly zeros, some random errors)
    for (int i = 0; i < std::abs(NIXL_ERR_LAST); i++) {
        if (i != 1) { // skip success
            event.error_counts[i] += error_dist(gen);
        }
    }

    // Generate plugin telemetry

    const char *backend_names[] = {"ucx", "gpunet_io"};
    const char *metric_names[] = {"tx_bytes",
                                  "rx_bytes",
                                  "tx_requests_num",
                                  "rx_requests_num",
                                  "memory_registered",
                                  "total_transaction_time_us"};

    for (uint8_t i = 0; i < event.num_backends; i++) {
        auto &backend = event.backend_telemetry[i];

        // Set backend name
        strncpy(backend.plugin_name, backend_names[i % 2], MAX_PLUGIN_NAME_LEN - 1);
        backend.plugin_name[MAX_PLUGIN_NAME_LEN - 1] = '\0';

        // Generate metrics
        for (uint32_t j = 0; j < backend.num_metrics; j++) {
            auto &metric = backend.metrics[j];

            // Set metric name
            strncpy(metric.name, metric_names[(i * 3 + j) % 6], MAX_METRIC_NAME_LEN - 1);
            metric.name[MAX_METRIC_NAME_LEN - 1] = '\0';

            // Generate metric value
            std::uniform_int_distribution<uint64_t> metric_value_dist(0, 1000000);
            metric.value += metric_value_dist(gen);
        }
    }

    return event;
}


struct Config {
    std::string telemetry_path;
    int num_events;
    int interval_ms;
    bool continuous;
    bool verbose;
};

Config
parse_arguments(int argc, char *argv[]) {
    // Parse gflags
    gflags::ParseCommandLineFlags(&argc, &argv, true);

    Config config;
    config.num_events = FLAGS_num_events;
    config.interval_ms = FLAGS_interval_ms;
    config.continuous = FLAGS_continuous;
    config.verbose = FLAGS_verbose;

    return config;
}

int
main(int argc, char *argv[]) {
    Config config = parse_arguments(argc, argv);

    std::cout << "NIXL Telemetry Generator" << std::endl;
    std::cout << "========================" << std::endl;
    std::cout << "Telemetry path: " << config.telemetry_path << std::endl;
    std::cout << "Number of events: "
              << (config.continuous ? "continuous" : std::to_string(config.num_events))
              << std::endl;
    std::cout << "Interval: " << config.interval_ms << " ms" << std::endl;
    std::cout << "Verbose: " << (config.verbose ? "true" : "false") << std::endl;


    // Get telemetry file path
    std::string telemetry_file_path = TELEMETRY_PREFIX + std::to_string(getpid());
    std::cout << "Telemetry file: " << telemetry_file_path << std::endl;

    try {
        std::cout << "\nCreating telemetry buffer..." << std::endl;

        // Create the shared memory buffer for writing
        SharedRingBuffer<nixlTelemetryEvent, TELEMETRY_BUFFER_SIZE> buffer(
            ("/tmp/" + telemetry_file_path).c_str(), true, TELEMETRY_VERSION);

        std::cout << "Successfully created telemetry buffer (version: " << buffer.get_version()
                  << ")" << std::endl;
        std::cout << "Buffer size: " << buffer.size() << " events" << std::endl;
        std::cout << "Press Ctrl+C to stop generating telemetry..." << std::endl;

        uint64_t event_count = 0;
        uint64_t total_events = config.continuous ? UINT64_MAX : config.num_events;

        nixlTelemetryEvent event = generate_random_event(event_count);

        while (g_running && event_count < total_events) {
            // Generate a random telemetry event

            // Push event to buffer
            if (buffer.push(event)) {
                event_count++;

                if (config.verbose) {
                    print_telemetry_event(event);
                } else {
                    std::cout << "Generated event " << event_count;
                    if (!config.continuous) {
                        std::cout << "/" << config.num_events;
                    }
                    std::cout << " (buffer size: " << buffer.size() << ")" << std::endl;
                }
            } else {
                std::cout << "Buffer full, waiting for space..." << std::endl;
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
            update_event(event);
            // Sleep for the specified interval
            if (config.interval_ms > 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(config.interval_ms));
            }
        }

        std::cout << "\nTelemetry generation completed!" << std::endl;
        std::cout << "Total events generated: " << event_count << std::endl;
        std::cout << "Final buffer size: " << buffer.size() << " events" << std::endl;
        std::cout << "Telemetry file: " << telemetry_file_path << std::endl;
        std::cout << "\nYou can now use the telemetry reader to view the generated data:"
                  << std::endl;
        std::cout << "  ./telemetry_reader_example " << config.telemetry_path << std::endl;
    }
    catch (const std::exception &e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    // Cleanup gflags
    gflags::ShutDownCommandLineFlags();

    return 0;
}