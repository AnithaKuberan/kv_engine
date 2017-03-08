/* -*- Mode: C++; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 *     Copyright 2015 Couchbase, Inc
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

#include <algorithm>
#include <iostream>
#include <limits>
#include <sstream>
#include <vector>

#include <platform/cb_malloc.h>

#include "configuration.h"

#ifdef AUTOCONF_BUILD
#include "generated_configuration.cc"
#endif

#define STATWRITER_NAMESPACE config
#include "statwriter.h"
#undef STATWRITER_NAMESPACE

Configuration::Configuration() {
    initialize();
}

std::string Configuration::getString(const std::string &key) const {
    LockHolder lh(mutex);

    const auto iter = attributes.find(key);
    if (iter == attributes.end()) {
        return std::string();
    }
    if (iter->second->datatype != DT_STRING) {
        throw std::invalid_argument("Configuration::getString: key (which is " +
                                    std::to_string(iter->second->datatype) +
                                    ") is not DT_STRING");
    }

    if (iter->second->val.v_string) {
        return std::string(iter->second->val.v_string);
    }
    return std::string();
}

bool Configuration::getBool(const std::string &key) const {
    LockHolder lh(mutex);

    const auto iter = attributes.find(key);
    if (iter == attributes.end()) {
        return false;
    }
    if (iter->second->datatype != DT_BOOL) {
        throw std::invalid_argument("Configuration::getBool: key (which is " +
                                    std::to_string(iter->second->datatype) +
                                    ") is not DT_BOOL");
    }
    return iter->second->val.v_bool;
}

float Configuration::getFloat(const std::string &key) const {
    LockHolder lh(mutex);

    const auto iter = attributes.find(key);
    if (iter == attributes.end()) {
        return 0;
    }
    if (iter->second->datatype != DT_FLOAT) {
        throw std::invalid_argument("Configuration::getFloat: key (which is " +
                                    std::to_string(iter->second->datatype) +
                                    ") is not DT_FLOAT");
    }
    return iter->second->val.v_float;
}

size_t Configuration::getInteger(const std::string &key) const {
    LockHolder lh(mutex);

    const auto iter = attributes.find(key);
    if (iter == attributes.end()) {
        return 0;
    }
    if (iter->second->datatype != DT_SIZE) {
        throw std::invalid_argument(
                "Configuration::getInteger: key (which is " +
                std::to_string(iter->second->datatype) + ") is not DT_SIZE");
    }
    return iter->second->val.v_size;
}

ssize_t Configuration::getSignedInteger(const std::string &key) const {
    LockHolder lh(mutex);

    const auto iter = attributes.find(key);
    if (iter == attributes.end()) {
        return 0;
    }
    if (iter->second->datatype != DT_SSIZE) {
        throw std::invalid_argument(
                "Configuration::getSignedInteger: key "
                "(which is " +
                std::to_string(iter->second->datatype) + ") is not DT_SSIZE");
    }
    return iter->second->val.v_ssize;
}

std::ostream& operator <<(std::ostream &out, const Configuration &config) {
    LockHolder lh(config.mutex);
    for (const auto& attribute : config.attributes) {
        std::stringstream line;
        line << attribute.first.c_str();
        line << " = [";
        switch (attribute.second->datatype) {
        case DT_BOOL:
            if (attribute.second->val.v_bool) {
                line << "true";
            } else {
                line << "false";
            }
            break;
        case DT_STRING:
            line << attribute.second->val.v_string;
            break;
        case DT_SIZE:
            line << attribute.second->val.v_size;
            break;
        case DT_SSIZE:
            line << attribute.second->val.v_ssize;
            break;
        case DT_FLOAT:
            line << attribute.second->val.v_float;
            break;
        case DT_CONFIGFILE:
            continue;
        default:
            // ignore
            ;
        }
        line << "]" << std::endl;
        out << line.str();
    }

    return out;
}

void Configuration::setParameter(const std::string &key, bool value) {
    std::vector<ValueChangedListener*> copy;
    {
        std::lock_guard<std::mutex> lh(mutex);
        auto validator = attributes.find(key);
        if (validator != attributes.end()) {
            if (validator->second->validator) {
                validator->second->validator->validateBool(key, value);
            }
        } else {
            attributes[key] = std::make_shared<value_t>();
        }
        attributes[key]->datatype = DT_BOOL;
        attributes[key]->val.v_bool = value;
        copy = attributes[key]->copyListeners();
    }

    for (auto* listener : copy) {
        listener->booleanValueChanged(key, value);
    }
}

void Configuration::setParameter(const std::string &key, size_t value) {
    std::vector<ValueChangedListener*> copy;
    {
        std::lock_guard<std::mutex> lh(mutex);
        auto validator = attributes.find(key);
        if (validator != attributes.end()) {
            if (validator->second->validator) {
                validator->second->validator->validateSize(key, value);
            }
        } else {
            attributes[key] = std::make_shared<value_t>();
        }
        attributes[key]->datatype = DT_SIZE;
        attributes[key]->val.v_size = value;

        copy = attributes[key]->copyListeners();
    }

    for (auto* listener : copy) {
        listener->sizeValueChanged(key, value);
    }
}

void Configuration::setParameter(const std::string &key, ssize_t value) {
    std::vector<ValueChangedListener*> copy;
    {
        std::lock_guard<std::mutex> lh(mutex);
        auto validator = attributes.find(key);
        if (validator != attributes.end()) {
            if (validator->second->validator) {
                validator->second->validator->validateSSize(key, value);
            }
        } else {
            attributes[key] = std::make_shared<value_t>();
        }
        attributes[key]->datatype = DT_SSIZE;
        attributes[key]->val.v_ssize = value;

        copy = attributes[key]->copyListeners();
    }

    for (auto* listener : copy) {
        listener->sizeValueChanged(key, value);
    }
}

void Configuration::setParameter(const std::string &key, float value) {
    std::vector<ValueChangedListener*> copy;
    {
        std::lock_guard<std::mutex> lh(mutex);

        auto validator = attributes.find(key);
        if (validator != attributes.end()) {
            if (validator->second->validator) {
                validator->second->validator->validateFloat(key, value);
            }
        } else {
            attributes[key] = std::make_shared<value_t>();
        }

        attributes[key]->datatype = DT_FLOAT;
        attributes[key]->val.v_float = value;
        copy = attributes[key]->copyListeners();
    }

    for (auto* listener : copy) {
        listener->floatValueChanged(key, value);
    }
}

void Configuration::setParameter(const std::string &key,
                                 const std::string &value) {
    if (value.length() == 0) {
        setParameter(key, (const char *)NULL);
    } else {
        setParameter(key, value.c_str());
    }
}

void Configuration::setParameter(const std::string &key, const char *value) {
    std::vector<ValueChangedListener*> copy;
    {
        std::lock_guard<std::mutex> lh(mutex);
        auto attr = attributes.find(key);
        if (attr != attributes.end()) {
            if (attr->second->validator) {
                attr->second->validator->validateString(key, value);
            }
            if (attr->second->datatype == DT_STRING) {
                cb_free((void*)attr->second->val.v_string);
            }
        } else {
            attributes[key] = std::make_shared<value_t>();
        }

        attributes[key]->datatype = DT_STRING;
        attributes[key]->val.v_string = NULL;
        if (value != NULL) {
            attributes[key]->val.v_string = cb_strdup(value);
        }

        copy = attributes[key]->copyListeners();
    }

    for (auto* listener : copy) {
        listener->stringValueChanged(key, value);
    }
}

void Configuration::addAlias(const std::string& key, const std::string& alias) {
    attributes[alias] = attributes[key];
}

void Configuration::addValueChangedListener(const std::string &key,
                                            ValueChangedListener *val) {
    LockHolder lh(mutex);
    if (attributes.find(key) != attributes.end()) {
        attributes[key]->changeListener.emplace_back(val);
    }
}

ValueChangedValidator *Configuration::setValueValidator(const std::string &key,
                                            ValueChangedValidator *validator) {
    ValueChangedValidator *ret = nullptr;
    LockHolder lh(mutex);
    if (attributes.find(key) != attributes.end()) {
        ret = attributes[key]->validator.release();
        attributes[key]->validator.reset(validator);
    }

    return ret;
}

void Configuration::addStats(ADD_STAT add_stat, const void *c) const {
    LockHolder lh(mutex);
    for (const auto& attribute :  attributes) {
        std::stringstream value;
        switch (attribute.second->datatype) {
        case DT_BOOL:
            value << std::boolalpha << attribute.second->val.v_bool
                  << std::noboolalpha;
            break;
        case DT_STRING:
            value << attribute.second->val.v_string;
            break;
        case DT_SIZE:
            value << attribute.second->val.v_size;
            break;
        case DT_SSIZE:
            value << attribute.second->val.v_ssize;
            break;
        case DT_FLOAT:
            value << attribute.second->val.v_float;
            break;
        case DT_CONFIGFILE:
        default:
            // ignore
            ;
        }

        std::stringstream key;
        key << "ep_" << attribute.first;
        std::string k = key.str();
        add_casted_stat(k.c_str(), value.str().data(), add_stat, c);
    }
}

/**
 * Internal container of an engine parameter.
 */
class ConfigItem: public config_item {
public:
    ConfigItem(const char *theKey, config_datatype theDatatype) :
                                                                holder(NULL) {
        key = theKey;
        datatype = theDatatype;
        value.dt_string = &holder;
    }

private:
    char *holder;
};

bool Configuration::parseConfiguration(const char *str,
                                       SERVER_HANDLE_V1* sapi) {
    std::vector<std::unique_ptr<ConfigItem> > config;

    for (const auto& attribute : attributes) {
        config.push_back(std::make_unique<ConfigItem>(
                attribute.first.c_str(), attribute.second->datatype));
    }

    // And add support for config files...
    config.push_back(std::make_unique<ConfigItem>("config_file", DT_CONFIGFILE));

    const int nelem = config.size();
    std::vector<config_item> items(nelem + 1);
    for (int ii = 0; ii < nelem; ++ii) {
        items[ii].key = config[ii]->key;
        items[ii].datatype = config[ii]->datatype;
        items[ii].value.dt_string = config[ii]->value.dt_string;
    }

    bool ret = sapi->core->parse_config(str, items.data(), stderr) == 0;
    for (int ii = 0; ii < nelem; ++ii) {
        if (items[ii].found) {
            if (ret) {
                switch (items[ii].datatype) {
                case DT_STRING:
                    setParameter(items[ii].key, *(items[ii].value.dt_string));
                    break;
                case DT_SIZE:
                    setParameter(items[ii].key, *items[ii].value.dt_size);
                    break;
                case DT_SSIZE:
                    setParameter(items[ii].key,
                                 (ssize_t)*items[ii].value.dt_ssize);
                    break;
                case DT_BOOL:
                    setParameter(items[ii].key, *items[ii].value.dt_bool);
                    break;
                case DT_FLOAT:
                    setParameter(items[ii].key, *items[ii].value.dt_float);
                    break;
                case DT_CONFIGFILE:
                    throw std::logic_error("Configuration::parseConfiguration: "
                            "Unexpected DT_CONFIGFILE element after parse_config");
                    break;
                }
            }

            if (items[ii].datatype == DT_STRING) {
                cb_free(*items[ii].value.dt_string);
            }
        }
    }

    return ret;
}

Configuration::~Configuration() = default;
