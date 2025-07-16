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

#include "doca_exporter.h"
#include <iostream>
#include <cstring>
#include <unistd.h>
#include <netdb.h>
#include <arpa/inet.h>

#ifndef NIXL_ENABLE_DOCA
#define NIXL_ENABLE_DOCA 1
#endif

#ifdef NIXL_ENABLE_DOCA

// Define the telemetry event structure for DOCA exporter, at the end use the one in nixl_types.h
struct nixl_doca_event {
    uint64_t timestamp_us;
    uint64_t tx_bytes;
    uint64_t rx_bytes;
    uint32_t tx_requests_num;
    uint32_t rx_requests_num;
    uint64_t memory_registered;
    uint8_t num_backends;
    uint64_t total_transaction_time_us;
    uint32_t last_requests_time_us[LAST_REQUESTS_ARRAY_SIZE];
    uint32_t error_counts[std::abs(NIXL_ERR_LAST)];
    // nixlPluginTelemetryEvent backend_telemetry[MAX_BACKENDS_PER_EVENT];
} __attribute__((packed));

// Field definitions for DOCA telemetry
static const struct {
    const char *name;
    const char *desc;
    const char *type_name;
    uint16_t len;
} field_mapping[] = {
    {"timestamp_us",
     "Timestamp (microseconds)",
     DOCA_TELEMETRY_EXPORTER_FIELD_TYPE_UINT64,
     sizeof(uint64_t)},
    {"tx_bytes", "Transmitted Bytes", DOCA_TELEMETRY_EXPORTER_FIELD_TYPE_UINT64, sizeof(uint64_t)},
    {"rx_bytes", "Received Bytes", DOCA_TELEMETRY_EXPORTER_FIELD_TYPE_UINT64, sizeof(uint64_t)},
    {"tx_requests_num",
     "Transmit Requests Count",
     DOCA_TELEMETRY_EXPORTER_FIELD_TYPE_UINT,
     sizeof(uint32_t)},
    {"rx_requests_num",
     "Receive Requests Count",
     DOCA_TELEMETRY_EXPORTER_FIELD_TYPE_UINT,
     sizeof(uint32_t)},
    {"memory_registered",
     "Memory Registered (bytes)",
     DOCA_TELEMETRY_EXPORTER_FIELD_TYPE_UINT64,
     sizeof(uint64_t)},
    {"num_backends",
     "Number of Backends",
     DOCA_TELEMETRY_EXPORTER_FIELD_TYPE_UINT8,
     sizeof(uint8_t)},
    {"total_transaction_time_us",
     "Total Transaction Time (microseconds)",
     DOCA_TELEMETRY_EXPORTER_FIELD_TYPE_UINT64,
     sizeof(uint64_t)},
    {"last_requests_time_us",
     "Last Requests Time Array",
     DOCA_TELEMETRY_EXPORTER_FIELD_TYPE_UINT,
     LAST_REQUESTS_ARRAY_SIZE * sizeof(uint32_t)},
    {"error_counts",
     "Error Counts Array",
     DOCA_TELEMETRY_EXPORTER_FIELD_TYPE_UINT,
     std::abs(NIXL_ERR_LAST) * sizeof(uint32_t)},
    // {"backend_telemetry", "Backend Telemetry Data", DOCA_TELEMETRY_EXPORTER_FIELD_TYPE_CHAR,
    // MAX_BACKENDS_PER_EVENT * sizeof(nixlPluginTelemetryEvent)}
};

static const size_t NB_EXPORT_FIELDS = sizeof(field_mapping) / sizeof(field_mapping[0]);

DocaExporter::DocaExporter(const std::string &schema_name,
                           const std::string &source_id,
                           const std::string &source_tag,
                           size_t buffer_size)
    : schema_name_(schema_name),
      source_id_(source_id),
      source_tag_(source_tag),
      buffer_size_(buffer_size),
      initialized_(false),
      schema_(nullptr),
      type_(nullptr),
      source_(nullptr),
      type_index_(0) {

    try {
        initializeSchema();
        registerFields();
        initializeSource();
        initialized_ = true;
        std::cout << "DOCA Exporter initialized successfully" << std::endl;
        std::cout << "  Schema: " << schema_name_ << std::endl;
        std::cout << "  Source ID: " << source_id_ << std::endl;
        std::cout << "  Source Tag: " << source_tag_ << std::endl;
    }
    catch (const std::exception &e) {
        std::cerr << "Failed to initialize DOCA exporter: " << e.what() << std::endl;
        initialized_ = false;
    }
}

DocaExporter::~DocaExporter() {
    if (initialized_) {
        if (source_) {
            doca_telemetry_exporter_source_destroy(source_);
        }
        if (schema_) {
            doca_telemetry_exporter_schema_destroy(schema_);
        }
    }
}

void
DocaExporter::initializeSchema() {
    doca_error_t result = doca_telemetry_exporter_schema_init(schema_name_.c_str(), &schema_);
    if (result != DOCA_SUCCESS) {
        throw std::runtime_error("Failed to initialize DOCA telemetry schema");
    }

    // Set buffer size and enable IPC
    // doca_telemetry_exporter_schema_set_buf_size(schema_, sizeof(nixl_doca_event) * 20);
    doca_telemetry_exporter_schema_set_buf_size(schema_, 10000);
    doca_telemetry_exporter_schema_set_ipc_enabled(schema_);
}

void
DocaExporter::registerFields() {
    doca_error_t result = doca_telemetry_exporter_type_create(&type_);
    if (result != DOCA_SUCCESS) {
        throw std::runtime_error("Failed to create DOCA telemetry type");
    }

    // Register all fields
    for (size_t i = 0; i < NB_EXPORT_FIELDS; i++) {
        struct doca_telemetry_exporter_field *field;
        result = doca_telemetry_exporter_field_create(&field);
        if (result != DOCA_SUCCESS) {
            throw std::runtime_error("Failed to create DOCA telemetry field");
        }

        doca_telemetry_exporter_field_set_name(field, field_mapping[i].name);
        doca_telemetry_exporter_field_set_description(field, field_mapping[i].desc);
        doca_telemetry_exporter_field_set_type_name(field, field_mapping[i].type_name);
        doca_telemetry_exporter_field_set_array_len(field, field_mapping[i].len);

        result = doca_telemetry_exporter_type_add_field(type_, field);
        if (result != DOCA_SUCCESS) {
            throw std::runtime_error("Failed to add field to DOCA telemetry type");
        }
    }

    // Add type to schema
    result =
        doca_telemetry_exporter_schema_add_type(schema_, schema_name_.c_str(), type_, &type_index_);
    if (result != DOCA_SUCCESS) {
        throw std::runtime_error("Failed to add type to DOCA telemetry schema");
    }

    // Start the schema
    result = doca_telemetry_exporter_schema_start(schema_);
    if (result != DOCA_SUCCESS) {
        throw std::runtime_error("Failed to start DOCA telemetry schema");
    }
}

void
DocaExporter::initializeSource() {
    // Generate source ID if not provided
    if (source_id_.empty()) {
        char hostname[256];
        if (gethostname(hostname, sizeof(hostname)) == 0) {
            source_id_ = std::string(hostname) + "_" + std::to_string(getpid());
        } else {
            source_id_ = "nixl_" + std::to_string(getpid());
        }
    }

    doca_error_t result = doca_telemetry_exporter_source_create(schema_, &source_);
    if (result != DOCA_SUCCESS) {
        throw std::runtime_error("Failed to create DOCA telemetry source");
    }

    doca_telemetry_exporter_source_set_id(source_, source_id_.c_str());
    doca_telemetry_exporter_source_set_tag(source_, source_tag_.c_str());

    result = doca_telemetry_exporter_source_start(source_);
    if (result != DOCA_SUCCESS) {
        throw std::runtime_error("Failed to start DOCA telemetry source");
    }
}

bool
DocaExporter::exportEvent(const nixlTelemetryEvent &event) {
    if (!initialized_) {
        std::cerr << "DOCA exporter not initialized" << std::endl;
        return false;
    }

    try {
        nixl_doca_event doca_event;
        serializeEvent(event, &doca_event);

        doca_error_t result =
            doca_telemetry_exporter_source_report(source_, type_index_, &doca_event, 1);
        if (result != DOCA_SUCCESS) {
            std::cerr << "Failed to report event to DOCA telemetry: " << result << std::endl;
            return false;
        }

        return true;
    }
    catch (const std::exception &e) {
        std::cerr << "Failed to export telemetry event to DOCA: " << e.what() << std::endl;
        return false;
    }
}

void
DocaExporter::serializeEvent(const nixlTelemetryEvent &event, void *buffer) {
    nixl_doca_event *doca_event = static_cast<nixl_doca_event *>(buffer);

    // Copy all fields from the original event
    doca_event->timestamp_us = event.timestamp_us;
    doca_event->tx_bytes = event.tx_bytes;
    doca_event->rx_bytes = event.rx_bytes;
    doca_event->tx_requests_num = event.tx_requests_num;
    doca_event->rx_requests_num = event.rx_requests_num;
    doca_event->memory_registered = event.memory_registered;
    doca_event->num_backends = event.num_backends;
    doca_event->total_transaction_time_us = event.total_transaction_time_us;

    // Copy arrays
    std::memcpy(doca_event->last_requests_time_us,
                event.last_requests_time_us,
                sizeof(event.last_requests_time_us));
    std::memcpy(doca_event->error_counts, event.error_counts, sizeof(event.error_counts));
    // std::memcpy(doca_event->backend_telemetry, event.backend_telemetry,
    //             sizeof(event.backend_telemetry));
}

#endif // NIXL_ENABLE_DOCA