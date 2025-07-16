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

#ifndef _DOCA_EXPORTER_H
#define _DOCA_EXPORTER_H

#include "telemetry_exporter.h"
#include "nixl_types.h"
#include <string>
#include <memory>

#ifndef NIXL_ENABLE_DOCA
#define NIXL_ENABLE_DOCA 1
#endif

#ifdef NIXL_ENABLE_DOCA

#include <doca_telemetry_exporter.h>

class DocaExporter : public TelemetryExporter {
public:
    DocaExporter(const std::string &schema_name = "nixl_telemetry_schema",
                 const std::string &source_id = "",
                 const std::string &source_tag = "nixl_telemetry",
                 size_t buffer_size = 1024);
    ~DocaExporter();

    // Export a telemetry event
    bool
    exportEvent(const nixlTelemetryEvent &event) override;

    // Check if exporter is properly initialized
    bool
    isInitialized() const override {
        return initialized_;
    }

private:
    void
    initializeSchema();
    void
    registerFields();
    void
    initializeSource();
    void
    serializeEvent(const nixlTelemetryEvent &event, void *buffer);

    std::string schema_name_;
    std::string source_id_;
    std::string source_tag_;
    size_t buffer_size_;
    bool initialized_;

    // DOCA telemetry components
    struct doca_telemetry_exporter_schema *schema_;
    struct doca_telemetry_exporter_type *type_;
    struct doca_telemetry_exporter_source *source_;
    uint8_t type_index_;
};

#endif // NIXL_ENABLE_DOCA

#endif // _DOCA_EXPORTER_H