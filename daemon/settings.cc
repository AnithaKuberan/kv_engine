/* -*- Mode: C++; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 *     Copyright 2015 Couchbase, Inc.
 *
 *   Licensed under the Apache License, Version 2.0 (the "License");
 *   you may not use this file except in compliance with the License.
 *   You may obtain a copy of the License at
 *
 *       http://www.apache.org/licenses/LICENSE-2.0
 *
 *   Unless required by applicable law or agreed to in writing, software
 *   distributed under the License is distributed on an "AS IS" BASIS,
 *   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *   See the License for the specific language governing permissions and
 *   limitations under the License.
 */

#include "config.h"

#include <platform/dirutils.h>
#include <platform/strerror.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <gsl/gsl>
#include <system_error>

#include "log_macros.h"
#include "settings.h"
#include "ssl_utils.h"

#include <mcbp/mcbp.h>

// the global entry of the settings object
Settings settings;


/**
 * Initialize all members to "null" to preserve backwards
 * compatibility with the previous versions.
 */
Settings::Settings()
    : num_threads(0),
      bio_drain_buffer_sz(0),
      datatype_json(false),
      datatype_snappy(false),
      reqs_per_event_high_priority(0),
      reqs_per_event_med_priority(0),
      reqs_per_event_low_priority(0),
      default_reqs_per_event(00),
      max_packet_size(0),
      topkeys_size(0),
      maxconns(0) {

    verbose.store(0);
    connection_idle_time.reset();
    dedupe_nmvb_maps.store(false);
    xattr_enabled.store(false);
    privilege_debug.store(false);
    collections_prototype.store(false);

    memset(&has, 0, sizeof(has));
    memset(&extensions, 0, sizeof(extensions));
}

Settings::Settings(const unique_cJSON_ptr& json)
    : Settings() {
    reconfigure(json);
}

/**
 * Handle deprecated tags in the settings by simply ignoring them
 */
static void ignore_entry(Settings&, cJSON*) {
}

enum class FileError {
    Missing,
    Empty,
    Invalid
};

static void throw_file_exception(const std::string &key,
                                 const std::string& filename,
                                 FileError reason,
                                 const std::string& extra_reason = std::string()) {
    std::string message("'" + key + "': '" + filename + "'");
    if (reason == FileError::Missing) {
        throw std::system_error(
                std::make_error_code(std::errc::no_such_file_or_directory),
                message);
    } else if (reason == FileError::Empty) {
        throw std::invalid_argument(message + " is empty ");
    } else if (reason == FileError::Invalid) {
        std::string extra;
        if (!extra_reason.empty()) {
            extra = " (" + extra_reason + ")";
        }
        throw std::invalid_argument(message + " is badly formatted: " +
                                    extra_reason);
    } else {
        throw std::runtime_error(message);
    }
}

static void throw_missing_file_exception(const std::string &key,
                                         const cJSON *obj) {
    throw_file_exception(key,
                         obj->valuestring == nullptr ? "null" : obj->valuestring,
                         FileError::Missing);
}

static void throw_missing_file_exception(const std::string& key,
                                         const std::string& filename) {
    throw_file_exception(key, filename, FileError::Missing);
}

/**
 * Handle the "rbac_file" tag in the settings
 *
 *  The value must be a string that points to a file that must exist
 *
 * @param s the settings object to update
 * @param obj the object in the configuration
 */
static void handle_rbac_file(Settings& s, cJSON* obj) {
    if (obj->type != cJSON_String) {
        throw std::invalid_argument("\"rbac_file\" must be a string");
    }

    if (!cb::io::isFile(obj->valuestring)) {
        throw_missing_file_exception("rbac_file", obj);
    }

    s.setRbacFile(obj->valuestring);
}

/**
 * Handle the "privilege_debug" tag in the settings
 *
 *  The value must be a boolean value
 *
 * @param s the settings object to update
 * @param obj the object in the configuration
 */
static void handle_privilege_debug(Settings& s, cJSON* obj) {
    if (obj->type == cJSON_True) {
        s.setPrivilegeDebug(true);
    } else if (obj->type == cJSON_False) {
        s.setPrivilegeDebug(false);
    } else {
        throw std::invalid_argument(
            "\"privilege_debug\" must be a boolean value");
    }
}

/**
 * Handle the "audit_file" tag in the settings
 *
 *  The value must be a string that points to a file that must exist
 *
 * @param s the settings object to update
 * @param obj the object in the configuration
 */
static void handle_audit_file(Settings& s, cJSON* obj) {
    if (obj->type != cJSON_String) {
        throw std::invalid_argument("\"audit_file\" must be a string");
    }

    if (!cb::io::isFile(obj->valuestring)) {
        throw_missing_file_exception("audit_file", obj);
    }

    s.setAuditFile(obj->valuestring);
}

static void handle_error_maps_dir(Settings& s, cJSON* obj) {
    if (obj->type != cJSON_String) {
        throw std::invalid_argument("\"error_maps_dir\" must be a string");
    }
    s.setErrorMapsDir(obj->valuestring);
}

/**
 * Handle the "threads" tag in the settings
 *
 *  The value must be an integer value
 *
 * @param s the settings object to update
 * @param obj the object in the configuration
 */
static void handle_threads(Settings& s, cJSON* obj) {
    if (obj->type != cJSON_Number) {
        throw std::invalid_argument("\"threads\" must be an integer");
    }
    if (obj->valueint < 0) {
        throw std::invalid_argument("\"threads\" must be non-negative");
    } else {
        s.setNumWorkerThreads(gsl::narrow_cast<size_t>(obj->valueint));
    }
}

/**
 * Handle the "topkeys_enabled" tag in the settings
 *
 *  The value must be a  value
 *
 * @param s the settings object to update
 * @param obj the object in the configuration
 */
static void handle_topkeys_enabled(Settings& s, cJSON* obj) {
    if (obj->type == cJSON_True) {
        s.setTopkeysEnabled(true);
    } else if (obj->type == cJSON_False) {
        s.setTopkeysEnabled(false);
    } else {
        throw std::invalid_argument(
                "\"topkeys_enabled\" must be a boolean value");
    }
}

/**
 * Handle the "tracing_enabled" tag in the settings
 *
 *  The value must be a boolean value
 *
 * @param s the settings object to update
 * @param obj the object in the configuration
 */
static void handle_tracing_enabled(Settings& s, cJSON* obj) {
    if (obj->type == cJSON_True) {
        s.setTracingEnabled(true);
    } else if (obj->type == cJSON_False) {
        s.setTracingEnabled(false);
    } else {
        throw std::invalid_argument(
                "\"tracing_enabled\" must be a boolean value");
    }
}

/**
 * Handle the "stdin_listener" tag in the settings
 *
 *  The value must be a boolean value
 *
 * @param s the settings object to update
 * @param obj the object in the configuration
 */
static void handle_stdin_listener(Settings& s, cJSON* obj) {
    if (obj->type == cJSON_True) {
        s.setStdinListenerEnabled(true);
    } else if (obj->type == cJSON_False) {
        s.setStdinListenerEnabled(false);
    } else {
        throw std::invalid_argument(
                R"("stdin_listener" must be a boolean value)");
    }
}

/**
 * Handle "default_reqs_per_event", "reqs_per_event_high_priority",
 * "reqs_per_event_med_priority" and "reqs_per_event_low_priority" tag in
 * the settings
 *
 *  The value must be a integer value
 *
 * @param s the settings object to update
 * @param obj the object in the configuration
 */
static void handle_reqs_event(Settings& s, cJSON* obj) {
    std::string name(obj->string);

    if (obj->type != cJSON_Number) {
        throw std::invalid_argument("\"" + name + "\" must be an integer");
    }

    EventPriority priority;

    if (name == "default_reqs_per_event") {
        priority = EventPriority::Default;
    } else if (name == "reqs_per_event_high_priority") {
        priority = EventPriority::High;
    } else if (name == "reqs_per_event_med_priority") {
        priority = EventPriority::Medium;
    } else if (name == "reqs_per_event_low_priority") {
        priority = EventPriority::Low;
    } else {
        throw std::invalid_argument("Invalid key specified: " + name);
    }
    s.setRequestsPerEventNotification(gsl::narrow<int>(obj->valueint),
                                      priority);
}

/**
 * Handle the "verbosity" tag in the settings
 *
 *  The value must be a numeric value
 *
 * @param s the settings object to update
 * @param obj the object in the configuration
 */
static void handle_verbosity(Settings& s, cJSON* obj) {
    if (obj->type != cJSON_Number) {
        throw std::invalid_argument("\"verbosity\" must be an integer");
    }
    s.setVerbose(gsl::narrow<int>(obj->valueint));
}

/**
 * Handle the "connection_idle_time" tag in the settings
 *
 *  The value must be a numeric value
 *
 * @param s the settings object to update
 * @param obj the object in the configuration
 */
static void handle_connection_idle_time(Settings& s, cJSON* obj) {
    if (obj->type != cJSON_Number) {
        throw std::invalid_argument(
            "\"connection_idle_time\" must be an integer");
    }
    s.setConnectionIdleTime(obj->valueint);
}

/**
 * Handle the "bio_drain_buffer_sz" tag in the settings
 *
 *  The value must be a numeric value
 *
 * @param s the settings object to update
 * @param obj the object in the configuration
 */
static void handle_bio_drain_buffer_sz(Settings& s, cJSON* obj) {
    if (obj->type != cJSON_Number) {
        throw std::invalid_argument(
            "\"bio_drain_buffer_sz\" must be an integer");
    }
    s.setBioDrainBufferSize(gsl::narrow<unsigned int>(obj->valueint));
}

/**
 * Handle the "datatype_snappy" tag in the settings
 *
 *  The value must be a boolean value
 *
 * @param s the settings object to update
 * @param obj the object in the configuration
 */
static void handle_datatype_json(Settings& s, cJSON* obj) {
    if (obj->type == cJSON_True) {
        s.setDatatypeJsonEnabled(true);
    } else if (obj->type == cJSON_False) {
        s.setDatatypeJsonEnabled(false);
    } else {
        throw std::invalid_argument(
                "\"datatype_json\" must be a boolean value");
    }
}

/**
 * Handle the "datatype_snappy" tag in the settings
 *
 *  The value must be a boolean value
 *
 * @param s the settings object to update
 * @param obj the object in the configuration
 */
static void handle_datatype_snappy(Settings& s, cJSON* obj) {
    if (obj->type == cJSON_True) {
        s.setDatatypeSnappyEnabled(true);
    } else if (obj->type == cJSON_False) {
        s.setDatatypeSnappyEnabled(false);
    } else {
        throw std::invalid_argument(
                "\"datatype_snappy\" must be a boolean value");
    }
}

/**
 * Handle the "root" tag in the settings
 *
 * The value must be a string that points to a directory that must exist
 *
 * @param s the settings object to update
 * @param obj the object in the configuration
 */
static void handle_root(Settings& s, cJSON* obj) {
    if (obj->type != cJSON_String) {
        throw std::invalid_argument("\"root\" must be a string");
    }

    if (!cb::io::isDirectory(obj->valuestring)) {
        throw_missing_file_exception("root", obj);
    }

    s.setRoot(obj->valuestring);
}

/**
 * Handle the "ssl_cipher_list" tag in the settings
 *
 * The value must be a string
 *
 * @param s the settings object to update
 * @param obj the object in the configuration
 */
static void handle_ssl_cipher_list(Settings& s, cJSON* obj) {
    if (obj->type != cJSON_String) {
        throw std::invalid_argument("\"ssl_cipher_list\" must be a string");
    }
    s.setSslCipherList(obj->valuestring);
}

/**
 * Handle the "ssl_minimum_protocol" tag in the settings
 *
 * The value must be a string containing one of the following:
 *    tlsv1, tlsv1.1, tlsv1_1, tlsv1.2, tlsv1_2
 *
 * @param s the settings object to update
 * @param obj the object in the configuration
 */
static void handle_ssl_minimum_protocol(Settings& s, cJSON* obj) {
    if (obj->type != cJSON_String) {
        throw std::invalid_argument(
            "\"ssl_minimum_protocol\" must be a string");
    }

    try {
        decode_ssl_protocol(obj->valuestring);
    } catch (const std::exception& e) {
        throw std::invalid_argument(
            "\"ssl_minimum_protocol\"" + std::string(e.what()));
    }
    s.setSslMinimumProtocol(obj->valuestring);
}

/**
 * Handle the "get_max_packet_size" tag in the settings
 *
 *  The value must be a numeric value
 *
 * @param s the settings object to update
 * @param obj the object in the configuration
 */
static void handle_max_packet_size(Settings& s, cJSON* obj) {
    if (obj->type != cJSON_Number) {
        throw std::invalid_argument(
            "\"max_packet_size\" must be an integer");
    }
    s.setMaxPacketSize(gsl::narrow<uint32_t>(obj->valueint) * 1024 * 1024);
}

/**
 * Handle the "saslauthd_socketpath" tag in the settings
 *
 * The value must be a string
 *
 * @param s the settings object to update
 * @param obj the object in the configuration
 */
static void handle_saslauthd_socketpath(Settings& s, cJSON* obj) {
    if (obj->type != cJSON_String) {
        throw std::invalid_argument("\"saslauthd_socketpath\" must be a string");
    }

    // We allow non-existing files, because we want to be
    // able to have it start to work if the user end up installing the
    // package after the configuration is set (and it'll just start to
    // work).
    s.setSaslauthdSocketpath(obj->valuestring);
}

/**
 * Handle the "sasl_mechanisms" tag in the settings
 *
 * The value must be a string
 *
 * @param s the settings object to update
 * @param obj the object in the configuration
 */
static void handle_sasl_mechanisms(Settings& s, cJSON* obj) {
    if (obj->type != cJSON_String) {
        throw std::invalid_argument("\"sasl_mechanisms\" must be a string");
    }
    s.setSaslMechanisms(obj->valuestring);
}

/**
 * Handle the "ssl_sasl_mechanisms" tag in the settings
 *
 * The value must be a string
 *
 * @param s the settings object to update
 * @param obj the object in the configuration
 */
static void handle_ssl_sasl_mechanisms(Settings& s, cJSON* obj) {
    if (obj->type != cJSON_String) {
        throw std::invalid_argument("\"ssl_sasl_mechanisms\" must be a string");
    }
    s.setSslSaslMechanisms(obj->valuestring);
}


/**
 * Handle the "dedupe_nmvb_maps" tag in the settings
 *
 *  The value must be a boolean value
 *
 * @param s the settings object to update
 * @param obj the object in the configuration
 */
static void handle_dedupe_nmvb_maps(Settings& s, cJSON* obj) {
    if (obj->type == cJSON_True) {
        s.setDedupeNmvbMaps(true);
    } else if (obj->type == cJSON_False) {
        s.setDedupeNmvbMaps(false);
    } else {
        throw std::invalid_argument(
            "\"dedupe_nmvb_maps\" must be a boolean value");
    }
}

/**
 * Handle the "xattr_enabled" tag in the settings
 *
 *  The value must be a boolean value
 *
 * @param s the settings object to update
 * @param obj the object in the configuration
 */
static void handle_xattr_enabled(Settings& s, cJSON* obj) {
    if (obj->type == cJSON_True) {
        s.setXattrEnabled(true);
    } else if (obj->type == cJSON_False) {
        s.setXattrEnabled(false);
    } else {
        throw std::invalid_argument(
            "\"xattr_enabled\" must be a boolean value");
    }
}

/**
 * Handle the "client_cert_auth" tag in the settings
 *
 *  The value must be a string value
 *
 * @param s the settings object to update
 * @param obj the object in the configuration
 */
static void handle_client_cert_auth(Settings& s, cJSON* obj) {
    if (obj->type == cJSON_Object && obj->child != nullptr) {
        auto config = cb::x509::ClientCertConfig::create(*obj);
        s.reconfigureClientCertAuth(config);
    } else {
        throw std::invalid_argument(R"("client_cert_auth" must be an object)");
    }
}

/**
 * Handle the "collections_prototype" tag in the settings
 *
 *  The value must be a boolean value
 *
 * @param s the settings object to update
 * @param obj the object in the configuration
 */
static void handle_collections_prototype(Settings& s, cJSON* obj) {
    if (obj->type == cJSON_True) {
        s.setCollectionsPrototype(true);
    } else if (obj->type == cJSON_False) {
        s.setCollectionsPrototype(false);
    } else {
        throw std::invalid_argument(
                "\"collections_prototype\" must be a boolean value");
    }
}

static void handle_opcode_attributes_override(Settings& s, cJSON* obj) {
    if (obj->type == cJSON_NULL) {
        s.setOpcodeAttributesOverride("");
        return;
    }

    if (obj->type != cJSON_Object) {
        throw std::invalid_argument(
                R"("opcode_attributes_override" must be an object)");
    }
    s.setOpcodeAttributesOverride(to_string(obj));
}

static void handle_extensions(Settings& s, cJSON* obj) {
    cb::logger::get()->info("Extensions ignored");
}

static void handle_logger(Settings& s, cJSON* obj) {
    if (obj->type != cJSON_Object) {
        throw std::invalid_argument("\"logger\" must be an object");
    }
    cb::logger::Config config(*obj);
    s.setLoggerConfig(config);
}

/**
 * Handle the "interfaces" tag in the settings
 *
 *  The value must be an array
 *
 * @param s the settings object to update
 * @param obj the object in the configuration
 */
static void handle_interfaces(Settings& s, cJSON* obj) {
    if (obj->type != cJSON_Array) {
        throw std::invalid_argument("\"interfaces\" must be an array");
    }

    for (auto* child = obj->child; child != nullptr; child = child->next) {
        if (child->type != cJSON_Object) {
            throw std::invalid_argument(
                "Elements in the \"interfaces\" array myst be objects");
        }
        NetworkInterface ifc(child);
        s.addInterface(ifc);
    }
}

static void handle_breakpad(Settings& s, cJSON* obj) {
    if (obj->type != cJSON_Object) {
        throw std::invalid_argument(R"("breakpad" must be an object)");
    }

    cb::breakpad::Settings breakpad(obj);
    s.setBreakpadSettings(breakpad);
}

void Settings::reconfigure(const unique_cJSON_ptr& json) {
    // Nuke the default interface added to the system in settings_init and
    // use the ones in the configuration file.. (this is a bit messy)
    interfaces.clear();

    struct settings_config_tokens {
        /**
         * The key in the configuration
         */
        std::string key;

        /**
         * A callback method used by the Settings object when we're parsing
         * the config attributes.
         *
         * @param settings the Settings object to update
         * @param obj the current object in the configuration we're looking at
         * @throws std::invalid_argument if it something is wrong with the
         *         entry
         */
        void (* handler)(Settings& settings, cJSON* obj);
    };

    std::vector<settings_config_tokens> handlers = {
            {"admin", ignore_entry},
            {"rbac_file", handle_rbac_file},
            {"privilege_debug", handle_privilege_debug},
            {"audit_file", handle_audit_file},
            {"error_maps_dir", handle_error_maps_dir},
            {"threads", handle_threads},
            {"interfaces", handle_interfaces},
            {"extensions", handle_extensions},
            {"logger", handle_logger},
            {"default_reqs_per_event", handle_reqs_event},
            {"reqs_per_event_high_priority", handle_reqs_event},
            {"reqs_per_event_med_priority", handle_reqs_event},
            {"reqs_per_event_low_priority", handle_reqs_event},
            {"verbosity", handle_verbosity},
            {"connection_idle_time", handle_connection_idle_time},
            {"bio_drain_buffer_sz", handle_bio_drain_buffer_sz},
            {"datatype_json", handle_datatype_json},
            {"datatype_snappy", handle_datatype_snappy},
            {"root", handle_root},
            {"ssl_cipher_list", handle_ssl_cipher_list},
            {"ssl_minimum_protocol", handle_ssl_minimum_protocol},
            {"breakpad", handle_breakpad},
            {"max_packet_size", handle_max_packet_size},
            {"saslauthd_socketpath", handle_saslauthd_socketpath},
            {"sasl_mechanisms", handle_sasl_mechanisms},
            {"ssl_sasl_mechanisms", handle_ssl_sasl_mechanisms},
            {"stdin_listener", handle_stdin_listener},
            {"dedupe_nmvb_maps", handle_dedupe_nmvb_maps},
            {"xattr_enabled", handle_xattr_enabled},
            {"client_cert_auth", handle_client_cert_auth},
            {"collections_prototype", handle_collections_prototype},
            {"opcode_attributes_override", handle_opcode_attributes_override},
            {"topkeys_enabled", handle_topkeys_enabled},
            {"tracing_enabled", handle_tracing_enabled}};

    cJSON* obj = json->child;
    while (obj != nullptr) {
        std::string key(obj->string);
        bool found = false;
        for (auto& handler : handlers) {
            if (handler.key == key) {
                handler.handler(*this, obj);
                found = true;
                break;
            }
        }

        if (!found) {
            LOG_WARNING(R"(Unknown token "{}" in config ignored.)",
                        obj->string);
        }

        obj = obj->next;
    }
}

void Settings::setOpcodeAttributesOverride(
        const std::string& opcode_attributes_override) {
    if (!opcode_attributes_override.empty()) {
        unique_cJSON_ptr json(cJSON_Parse(opcode_attributes_override.c_str()));
        if (!json) {
            throw std::invalid_argument(
                    "Settings::setOpcodeAttributesOverride: Invalid JSON "
                    "provided");
        }

        // Verify the content...
        cb::mcbp::sla::reconfigure(*json, false);
    }

    {
        std::lock_guard<std::mutex> guard(
                Settings::opcode_attributes_override.mutex);
        Settings::opcode_attributes_override.value = opcode_attributes_override;
        has.opcode_attributes_override = true;
    }
    notify_changed("opcode_attributes_override");
}

void Settings::updateSettings(const Settings& other, bool apply) {
    if (other.has.rbac_file) {
        if (other.rbac_file != rbac_file) {
            throw std::invalid_argument("rbac_file can't be changed dynamically");
        }
    }
    if (other.has.threads) {
        if (other.num_threads != num_threads) {
            throw std::invalid_argument("threads can't be changed dynamically");
        }
    }

    if (other.has.audit) {
        if (other.audit_file != audit_file) {
            throw std::invalid_argument("audit can't be changed dynamically");
        }
    }
    if (other.has.bio_drain_buffer_sz) {
        if (other.bio_drain_buffer_sz != bio_drain_buffer_sz) {
            throw std::invalid_argument(
                "bio_drain_buffer_sz can't be changed dynamically");
        }
    }
    if (other.has.datatype_json) {
        if (other.datatype_json != datatype_json) {
            throw std::invalid_argument(
                    "datatype_json can't be changed dynamically");
        }
    }
    if (other.has.root) {
        if (other.root != root) {
            throw std::invalid_argument("root can't be changed dynamically");
        }
    }
    if (other.has.topkeys_size) {
        if (other.topkeys_size != topkeys_size) {
            throw std::invalid_argument(
                "topkeys_size can't be changed dynamically");
        }
    }
    if (other.has.sasl_mechanisms) {
        if (other.sasl_mechanisms != sasl_mechanisms) {
            throw std::invalid_argument(
                "sasl_mechanisms can't be changed dynamically");
        }
    }
    if (other.has.ssl_sasl_mechanisms) {
        if (other.ssl_sasl_mechanisms != ssl_sasl_mechanisms) {
            throw std::invalid_argument(
                "ssl_sasl_mechanisms can't be changed dynamically");
        }
    }

    if (other.has.interfaces) {
        if (other.interfaces.size() != interfaces.size()) {
            throw std::invalid_argument(
                "interfaces can't be changed dynamically");
        }

        // validate that we haven't changed stuff in the entries
        auto total = interfaces.size();
        for (std::vector<NetworkInterface>::size_type ii = 0; ii < total;
             ++ii) {
            const auto& i1 = interfaces[ii];
            const auto& i2 = other.interfaces[ii];

            if (i1.port == 0 || i2.port == 0) {
                // we can't look at dynamic ports...
                continue;
            }

            // the following fields can't change
            if ((i1.host != i2.host) || (i1.port != i2.port) ||
                (i1.ipv4 != i2.ipv4) || (i1.ipv6 != i2.ipv6) ||
                (i1.management != i2.management)) {
                throw std::invalid_argument(
                    "interfaces can't be changed dynamically");
            }
        }
    }

    if (other.has.stdin_listener) {
        if (other.stdin_listener.load() != stdin_listener.load()) {
            throw std::invalid_argument(
                    "stdin_listener can't be changed dynamically");
        }
    }

    if (other.has.logger) {
        if (other.logger_settings != logger_settings)
            throw std::invalid_argument(
                    "logger configuration can't be changed dynamically");
    }

    if (other.has.error_maps) {
        if (other.error_maps_dir != error_maps_dir) {
            throw std::invalid_argument(
                    "error_maps_dir can't be changed dynamically");
        }
    }

    // All non-dynamic settings has been validated. If we're not supposed
    // to update anything we can bail out.
    if (!apply) {
        return;
    }

    // Ok, go ahead and update the settings!!
    if (other.has.datatype_snappy) {
        if (other.datatype_snappy != datatype_snappy) {
            std::string curr_val_str = datatype_snappy ? "true" : "false";
            std::string other_val_str = other.datatype_snappy ? "true" : "false";
            LOG_INFO("Change datatype_snappy from {} to {}",
                     curr_val_str,
                     other_val_str);
            setDatatypeSnappyEnabled(other.datatype_snappy);
        }
    }

    if (other.has.verbose) {
        if (other.verbose != verbose) {
            LOG_INFO("Change verbosity level from {} to {}",
                     verbose.load(),
                     other.verbose.load());
            setVerbose(other.verbose.load());
        }
    }

    if (other.has.reqs_per_event_high_priority) {
        if (other.reqs_per_event_high_priority !=
            reqs_per_event_high_priority) {
            LOG_INFO("Change high priority iterations per event from {} to {}",
                     reqs_per_event_high_priority,
                     other.reqs_per_event_high_priority);
            setRequestsPerEventNotification(other.reqs_per_event_high_priority,
                                            EventPriority::High);
        }
    }
    if (other.has.reqs_per_event_med_priority) {
        if (other.reqs_per_event_med_priority != reqs_per_event_med_priority) {
            LOG_INFO(
                    "Change medium priority iterations per event from {} to {}",
                    reqs_per_event_med_priority,
                    other.reqs_per_event_med_priority);
            setRequestsPerEventNotification(other.reqs_per_event_med_priority,
                                            EventPriority::Medium);
        }
    }
    if (other.has.reqs_per_event_low_priority) {
        if (other.reqs_per_event_low_priority != reqs_per_event_low_priority) {
            LOG_INFO("Change low priority iterations per event from {} to {}",
                     reqs_per_event_low_priority,
                     other.reqs_per_event_low_priority);
            setRequestsPerEventNotification(other.reqs_per_event_low_priority,
                                            EventPriority::Low);
        }
    }
    if (other.has.default_reqs_per_event) {
        if (other.default_reqs_per_event != default_reqs_per_event) {
            LOG_INFO("Change default iterations per event from {} to {}",
                     default_reqs_per_event,
                     other.default_reqs_per_event);
            setRequestsPerEventNotification(other.default_reqs_per_event,
                                            EventPriority::Default);
        }
    }
    if (other.has.connection_idle_time) {
        if (other.connection_idle_time != connection_idle_time) {
            LOG_INFO("Change connection idle time from {} to {}",
                     connection_idle_time.load(),
                     other.connection_idle_time.load());
            setConnectionIdleTime(other.connection_idle_time);
        }
    }
    if (other.has.max_packet_size) {
        if (other.max_packet_size != max_packet_size) {
            LOG_INFO("Change max packet size from {} to {}",
                     max_packet_size,
                     other.max_packet_size);
            setMaxPacketSize(other.max_packet_size);
        }
    }
    if (other.has.ssl_cipher_list) {
        if (other.ssl_cipher_list != ssl_cipher_list) {
            // this isn't safe!! an other thread could call stats settings
            // which would cause this to crash...
            LOG_INFO(
                    R"(Change SSL Cipher list from "{}" to "{}")",
                    ssl_cipher_list,
                    other.ssl_cipher_list);
            setSslCipherList(other.ssl_cipher_list);
        }
    }
    if (other.has.client_cert_auth) {
        const auto m = client_cert_mapper.to_string();
        const auto o = other.client_cert_mapper.to_string();

        if (m != o) {
            LOG_INFO(
                    R"(Change SSL client auth from "{}" to "{}")", m, o);
            unique_cJSON_ptr json(cJSON_Parse(o.c_str()));
            auto config = cb::x509::ClientCertConfig::create(*json);
            reconfigureClientCertAuth(config);
        }
    }
    if (other.has.ssl_minimum_protocol) {
        if (other.ssl_minimum_protocol != ssl_minimum_protocol) {
            // this isn't safe!! an other thread could call stats settings
            // which would cause this to crash...
            LOG_INFO(
                    R"(Change SSL minimum protocol from "{}" to "{}")",
                    ssl_minimum_protocol,
                    other.ssl_minimum_protocol);
            setSslMinimumProtocol(other.ssl_minimum_protocol);
        }
    }
    if (other.has.dedupe_nmvb_maps) {
        if (other.dedupe_nmvb_maps != dedupe_nmvb_maps) {
            LOG_INFO("{} deduplication of NMVB maps",
                     other.dedupe_nmvb_maps.load() ? "Enable" : "Disable");
            setDedupeNmvbMaps(other.dedupe_nmvb_maps.load());
        }
    }

    if (other.has.xattr_enabled) {
        if (other.xattr_enabled != xattr_enabled) {
            LOG_INFO("{} XATTR",
                     other.xattr_enabled.load() ? "Enable" : "Disable");
            setXattrEnabled(other.xattr_enabled.load());
        }
    }

    if (other.has.collections_prototype) {
        if (other.collections_prototype != collections_prototype) {
            LOG_INFO("{} collections_prototype",
                     other.collections_prototype.load() ? "Enable" : "Disable");
            setCollectionsPrototype(other.collections_prototype.load());
        }
    }

    if (other.has.interfaces) {
        // validate that we haven't changed stuff in the entries
        auto total = interfaces.size();
        bool changed = false;
        for (std::vector<NetworkInterface>::size_type ii = 0; ii < total;
             ++ii) {
            auto& i1 = interfaces[ii];
            const auto& i2 = other.interfaces[ii];

            if (i1.port == 0 || i2.port == 0) {
                // we can't look at dynamic ports...
                continue;
            }

            if (i2.maxconn != i1.maxconn) {
                LOG_INFO("Change max connections for {}:{} from {} to {}",
                         i1.host,
                         i1.port,
                         i1.maxconn,
                         i2.maxconn);
                i1.maxconn = i2.maxconn;
                changed = true;
            }

            if (i2.backlog != i1.backlog) {
                LOG_INFO("Change backlog for {}:{} from {} to {}",
                         i1.host,
                         i1.port,
                         i1.backlog,
                         i2.backlog);
                i1.backlog = i2.backlog;
                changed = true;
            }

            if (i2.tcp_nodelay != i1.tcp_nodelay) {
                LOG_INFO("{} TCP NODELAY for {}:{}",
                         i2.tcp_nodelay ? "Enable" : "Disable",
                         i1.host,
                         i1.port);
                i1.tcp_nodelay = i2.tcp_nodelay;
                changed = true;
            }

            if (i2.ssl.cert != i1.ssl.cert) {
                LOG_INFO("Change SSL Certificiate for {}:{} from {} to {}",
                         i1.host,
                         i1.port,
                         i1.ssl.cert,
                         i2.ssl.cert);
                i1.ssl.cert.assign(i2.ssl.cert);
                changed = true;
            }

            if (i2.ssl.key != i1.ssl.key) {
                LOG_INFO("Change SSL Key for {}:{} from {} to {}",
                         i1.host,
                         i1.port,
                         i1.ssl.key,
                         i2.ssl.key);
                i1.ssl.key.assign(i2.ssl.key);
                changed = true;
            }
        }

        if (changed) {
            notify_changed("interfaces");
        }
    }

    if (other.has.breakpad) {
        bool changed = false;
        auto& b1 = breakpad;
        const auto& b2 = other.breakpad;

        if (b2.enabled != b1.enabled) {
            LOG_INFO("{} breakpad", b2.enabled ? "Enable" : "Disable");
            b1.enabled = b2.enabled;
            changed = true;
        }

        if (b2.minidump_dir != b1.minidump_dir) {
            LOG_INFO(
                    R"(Change minidump directory from "{}" to "{}")",
                    b1.minidump_dir,
                    b2.minidump_dir);
            b1.minidump_dir = b2.minidump_dir;
            changed = true;
        }

        if (b2.content != b1.content) {
            LOG_INFO("Change minidump content from {} to {}",
                     to_string(b1.content),
                     to_string(b2.content));
            b1.content = b2.content;
            changed = true;
        }

        if (changed) {
            notify_changed("breakpad");
        }
    }

    if (other.has.privilege_debug) {
        if (other.privilege_debug != privilege_debug) {
            bool value = other.isPrivilegeDebug();
            LOG_INFO("{} privilege debug", value ? "Enable" : "Disable");
            setPrivilegeDebug(value);
        }
    }

    if (other.has.saslauthd_socketpath) {
        // @todo Once ns_server allows for changing the path we need to
        //       make sure we properly populate this value all the way
        //       down to cbsasl
        auto path = other.getSaslauthdSocketpath();
        if (path != saslauthd_socketpath.path) {
            LOG_INFO(
                    R"(Change saslauthd socket path from "{}" to "{}")",
                    saslauthd_socketpath.path,
                    path);
            setSaslauthdSocketpath(path);
        }
    }

    if (other.has.opcode_attributes_override) {
        auto current = getOpcodeAttributesOverride();
        auto proposed = other.getOpcodeAttributesOverride();

        if (proposed != current) {
            LOG_INFO(
                    R"(Change opcode attributes from "{}" to "{}")",
                    current,
                    proposed);
            setOpcodeAttributesOverride(proposed);
        }
    }

    if (other.has.topkeys_enabled) {
        if (other.isTopkeysEnabled() != isTopkeysEnabled()) {
            LOG_INFO("{} topkeys support",
                     other.isTopkeysEnabled() ? "Enable" : "Disable");
        }
        setTopkeysEnabled(other.isTopkeysEnabled());
    }

    if (other.has.tracing_enabled) {
        if (other.isTracingEnabled() != isTracingEnabled()) {
            LOG_INFO("{} tracing support",
                     other.isTracingEnabled() ? "Enable" : "Disable");
        }
        setTracingEnabled(other.isTracingEnabled());
    }
}

/**
 * Loads a single error map
 * @param filename The location of the error map
 * @param[out] contents The JSON-encoded contents of the error map
 * @return The version of the error map
 */
static size_t parseErrorMap(const std::string& filename,
                            std::string& contents) {
    const std::string errkey(
            "parseErrorMap: error_maps_dir (" + filename + ")");
    if (!cb::io::isFile(filename)) {
        throw_missing_file_exception(errkey, filename);
    }

    std::ifstream ifs(filename);
    if (ifs.good()) {
        // Read into buffer
        contents.assign(std::istreambuf_iterator<char>{ifs},
                        std::istreambuf_iterator<char>());
        if (contents.empty()) {
            throw_file_exception(errkey, filename, FileError::Empty);
        }
    } else if (ifs.fail()) {
        // TODO: make this into std::system_error
        throw std::runtime_error(errkey + ": " + "Couldn't read");
    }

    unique_cJSON_ptr json(cJSON_Parse(contents.c_str()));
    if (json.get() == nullptr) {
        throw_file_exception(errkey, filename, FileError::Invalid,
                             "Invalid JSON");
    }

    if (json->type != cJSON_Object) {
        throw_file_exception(errkey, filename, FileError::Invalid,
                             "Top-level contents must be objects");
    }
    // Find the 'version' field
    const cJSON *verobj = cJSON_GetObjectItem(json.get(), "version");
    if (verobj == nullptr) {
        throw_file_exception(errkey, filename, FileError::Invalid,
                             "Cannot find 'version' field");
    }
    if (verobj->type != cJSON_Number) {
        throw_file_exception(errkey, filename, FileError::Invalid,
                             "'version' must be numeric");
    }

    static const size_t max_version = 200;
    size_t version = verobj->valueint;

    if (version > max_version) {
        throw_file_exception(errkey, filename, FileError::Invalid,
                             "'version' too big. Maximum supported is " +
                             std::to_string(max_version));
    }

    return version;
}

void Settings::loadErrorMaps(const std::string& dir) {
    static const std::string errkey("Settings::loadErrorMaps");
    if (!cb::io::isDirectory(dir)) {
        throw_missing_file_exception(errkey, dir);
    }

    size_t max_version = 1;
    static const std::string prefix("error_map");
    static const std::string suffix(".json");

    for (auto const& filename : cb::io::findFilesWithPrefix(dir, prefix)) {
        // Ensure the filename matches "error_map*.json", so we ignore editor
        // generated files or "hidden" files.
        if (filename.size() < suffix.size()) {
            continue;
        }
        if (!std::equal(suffix.rbegin(), suffix.rend(), filename.rbegin())) {
            continue;
        }

        std::string contents;
        size_t version = parseErrorMap(filename, contents);
        error_maps.resize(std::max(error_maps.size(), version + 1));
        error_maps[version] = contents;
        max_version = std::max(max_version, version);
    }

    // Ensure we have at least one error map.
    if (error_maps.empty()) {
        throw std::invalid_argument(errkey +": No valid files found in " + dir);
    }

    // Validate that there are no 'holes' in our versions
    for (size_t ii = 1; ii < max_version; ++ii) {
        if (getErrorMap(ii).empty()) {
            throw std::runtime_error(errkey + ": Missing error map version " +
                                     std::to_string(ii));
        }
    }
}

const std::string& Settings::getErrorMap(size_t version) const {
    const static std::string empty("");
    if (error_maps.empty()) {
        return empty;
    }

    version = std::min(version, error_maps.size()-1);
    return error_maps[version];
}


void Settings::notify_changed(const std::string& key) {
    auto iter = change_listeners.find(key);
    if (iter != change_listeners.end()) {
        for (auto& listener : iter->second) {
            listener(key, *this);
        }
    }
}
