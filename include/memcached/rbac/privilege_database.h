/* -*- Mode: C++; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 *     Copyright 2017 Couchbase, Inc.
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
#pragma once

/**
 * This file contains the definitions of the privilege system used
 * by the memcached core. For more information see rbac.md in the
 * docs directory.
 */
#include <boost/optional.hpp>
#include <cbsasl/domain.h>
#include <memcached/rbac/privileges.h>
#include <nlohmann/json_fwd.hpp>
#include <bitset>
#include <chrono>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace cb {
namespace rbac {

using Domain = cb::sasl::Domain;

/**
 * An array containing all of the possible privileges we've got. It is
 * tightly coupled with the Privilege enum class, and when entries is
 * added to the Privilege enum class the size of the mask needs to
 * be updated.
 */
using PrivilegeMask = std::bitset<size_t(Privilege::Impersonate) + 1>;

/**
 * The UserEntry object is in an in-memory representation of the per-user
 * privileges.
 */
class UserEntry {
public:
    UserEntry(const UserEntry&) = default;

    bool operator==(const UserEntry& other) const;

    /**
     * Create a new UserEntry from the provided JSON
     *
     * @param username The name of the user we're currently parse
     * @param json A JSON representation of the user.
     * @param domain The expected domain for this user
     * @throws std::invalid_argument if the provided JSON isn't according
     *         to the specification.
     * @throws std::bad_alloc if we run out of memory
     * @throws std::runtime_error if the domain found for the entry isn't
     *         the expected tomain
     */
    UserEntry(const std::string& username,
              const nlohmann::json& json,
              Domain domain);

    /**
     * Get a map containing all of the buckets and the privileges in those
     * buckets that the user have access to.
     */
    const std::unordered_map<std::string, PrivilegeMask>& getBuckets() const {
        return buckets;
    }

    /**
     * Get all of the "global" (not related to a bucket) privileges the user
     * have in its effective set.
     */
    const PrivilegeMask& getPrivileges() const {
        return privileges;
    }

    /**
     * Is this a system internal user or not? A system internal user is a
     * user one of the system components use.
     */
    bool isInternal() const {
        return internal;
    }

    nlohmann::json to_json(Domain domain) const;

    /**
     * Get the timestamp for the last time we updated the user entry
     */
    std::chrono::steady_clock::time_point getTimestamp() const {
        return timestamp;
    }

    /**
     * Set the timestamp for the user. It looks a bit weird that this method
     * is const and that the timestamp is marked mutable, but it has a reason.
     * The user database is using a copy on write schema, so we don't want
     * to update any entries in here. As part of moving LDAP authentication and
     * authorization to ns_server it push the external users at a fixed rate.
     * We don't want to copy the entire user database just to update the
     * timestamp. The timestamp is needed as ns_server wants to not have
     * to return the RBAC data as part of each authentication request. We
     * need to know that the entry is fresh (and not 1 month old) when we try
     * to log in.
     */
    void setTimestamp(std::chrono::steady_clock::time_point ts) const {
        timestamp = ts;
    }

protected:
    /**
     * Parse a JSON array containing a set of privileges.
     *
     * @param priv The JSON array to parse
     * @param buckets Set to true if this is for the bucket list (which
     *                will mask out some of the privileges you can't
     *                specify for a bucket)
     * @return A vector of all of the privileges found in the specified JSON
     */
    PrivilegeMask parsePrivileges(const nlohmann::json& privs, bool buckets);

    std::vector<std::string> mask2string(const PrivilegeMask& mask) const;
    mutable std::chrono::steady_clock::time_point timestamp;
    std::unordered_map<std::string, PrivilegeMask> buckets;
    PrivilegeMask privileges;
    bool internal;
};

/**
 * The PrivilegeContext is the current context (selected bucket).
 * The reason for this class is to provide a fast lookup for all
 * of the privileges. It is used (possibly multiple times) for every
 * command being executed.
 */
class PrivilegeContext {
public:
    PrivilegeContext() = delete;

    /**
     * Create a new (empty) instance of the privilege context.
     *
     * The generation is set to "max" which will cause the the access
     * check to return stale if being used. This is the initial
     * context being used.
     */
    explicit PrivilegeContext(Domain domain)
        : generation(std::numeric_limits<uint32_t>::max()),
          domain(domain),
          mask() {
    }

    /**
     * Create a new instance of the privilege context from the
     * given generation and assign it the given mask.
     *
     * @param gen the generation of the privilege database
     * @param m the mask to set it to.
     */
    PrivilegeContext(uint32_t gen, Domain domain, const PrivilegeMask& m)
        : generation(gen), domain(domain), mask(m) {
        // empty
    }

    /**
     * Check if the given privilege is part of the context
     *
     * @param privilege the privilege to check
     * @return if access is granted or not.
     */
    PrivilegeAccess check(Privilege privilege) const;

    /**
     * Get the generation of the Privilege Database this context maps
     * to. If there is a mismatch with this number and the current number
     * of the privilege database this context is no longer valid.
     */
    uint32_t getGeneration() const {
        return generation;
    }

    /**
     * Get a textual representation of this object in the format:
     *
     *   [privilege,privilege,privilege]
     *
     * An empty set is written as [none], and a full set is written
     * as [all].
     */
    std::string to_string() const;

    /**
     * Clear all of the privileges in this context which contains
     * bucket privileges.
     */
    void clearBucketPrivileges();

    /**
     * Set all of the privileges in this context which contains
     * bucket privileges.
     */
    void setBucketPrivileges();

    /**
     * Drop the named privilege from the privilege mask
     *
     * @param privilege the privilege to drop
     * @return true if the privilege was dropped
     *         false if the requested privilege wasn't set in the mask
     */
    bool dropPrivilege(Privilege privilege);

protected:
    void setBucketPrivilegeBits(bool value);

    /// The Database version this mask belongs to
    uint32_t generation;
    /// The Domain the mask belongs to
    Domain domain;
    /// The mask of effective privileges
    PrivilegeMask mask;
};

/**
 * Base class for exceptions thrown by the cb::rbac module in
 * case you want to handle all of them with the same catch block.
 */
class Exception : public std::runtime_error {
protected:
    explicit Exception(const char* msg) : std::runtime_error(msg) {
    }
};

/**
 * An exception class representing that the user doesn't exist in the
 * PrivilegeDatabase.
 */
class NoSuchUserException : public Exception {
public:
    explicit NoSuchUserException(const char* msg) : Exception(msg) {
    }
};

/**
 * An exception class representing that the bucket doesn't exist in the
 * PrivilegeDatabase.
 */
class NoSuchBucketException : public Exception {
public:
    explicit NoSuchBucketException(const char* msg) : Exception(msg) {
    }
};

/**
 * The PrivilegeDatabase is a container for all of the RBAC configuration
 * of the system.
 */
class PrivilegeDatabase {
public:
    /**
     * Create a new instance of the PrivilegeDatabase and initialize
     * it to the provided JSON
     *
     * @param json A JSON representation of the privilege database as
     *             specified above (or null to create an empty database)
     * @throws std::invalid_argument for invalid syntax
     * @throws std::bad_alloc if we run out of memory
     */
    PrivilegeDatabase(const nlohmann::json& json, Domain domain);

    /**
     * Try to look up a user in the privilege database
     *
     * @param user The name of the user to look up
     * @param domain The domain where the user is defined (not used)
     * @return The user entry for that user
     * @throws cb::rbac::NoSuchUserException if the user doesn't exist
     */
    const UserEntry& lookup(const std::string& user) const;

    /**
     * Check if the provided context contains the requested privilege
     *
     * @param context The privilege context for the user
     * @param privilege The privilege to check
     * @return PrivilegeAccess::Stale If the context was created by a
     *                                different generation of the database
     *         PrivilegeAccess::Ok If the context contains the privilege
     *         PrivilegeAccess::Fail If the context lacks the privilege
     */
    PrivilegeAccess check(const PrivilegeContext& context,
                          Privilege privilege) const;

    /**
     * Create a new PrivilegeContext for the specified user in the specified
     * bucket.
     *
     * @param user The name of the user
     * @param bucket The name of the bucket (may be "" if you're not
     *               connecting to a bucket (aka the no bucket)).
     * @return The privilege context representing the user in that bucket
     * @throws cb::rbac::NoSuchUserException if the user doesn't exist
     * @throws cb::rbac::NoSuchBucketException if the user doesn't have access
     *                                         to that bucket.
     */
    PrivilegeContext createContext(const std::string& user,
                                   Domain domain,
                                   const std::string& bucket) const;

    /**
     * Create the initial context for a given user
     *
     * @param user The username to look up
     * @return A pair with a privilege context as the first element, and
     *         a boolean indicating if this is a system user as the second
     *         element.
     * @throws cb::rbac::NoSuchUserException if the user doesn't exist
     */
    std::pair<PrivilegeContext, bool> createInitialContext(
            const std::string& user, Domain domain) const;

    std::unique_ptr<PrivilegeDatabase> updateUser(const std::string& user,
                                                  Domain domain,
                                                  UserEntry& entry) const;

    nlohmann::json to_json(Domain domain) const;

    /**
     * The generation for this PrivilegeDatabase (a privilege context must
     * match this generation in order to be valid)
     */
    const uint32_t generation;

protected:
    std::unordered_map<std::string, UserEntry> userdb;
};

/**
 * Create a new PrivilegeContext for the specified user in the specified
 * bucket.
 *
 * @todo this might starve the writers?
 *
 * @param user The name of the user
 * @param bucket The name of the bucket (may be "" if you're not
 *               connecting to a bucket (aka the no bucket)).
 * @return The privilege context representing the user in that bucket
 * @throws cb::rbac::NoSuchUserException if the user doesn't exist
 * @throws cb::rbac::NoSuchBucketException if the user doesn't have access
 *                                         to that bucket.
 */
PrivilegeContext createContext(const std::string& user,
                               Domain domain,
                               const std::string& bucket);

/**
 * Create the initial context for a given user
 *
 * @param user The username to look up
 * @param domain The domain where the user exists
 * @return A pair with a privilege context as the first element, and
 *         a boolean indicating if this is a system user as the second
 *         element.
 * @throws cb::rbac::NoSuchUserException if the user doesn't exist
 */
std::pair<PrivilegeContext, bool> createInitialContext(const std::string& user,
                                                       Domain domain);

/**
 * Load the named file and install it as the current privilege database
 *
 * @param filename the name of the new file
 * @throws std::runtime_error
 */
void loadPrivilegeDatabase(const std::string& filename);

/**
 * Check if the specified user have access to the specified bucket
 */
bool mayAccessBucket(const std::string& user,
                     Domain domain,
                     const std::string& bucket);

/**
 * Update the user entry with the supplied new configuration
 *
 * @param json the new definition for the user
 */
void updateExternalUser(const std::string& json);

/**
 * Initialize the RBAC module
 */
void initialize();

/**
 * Destroy the RBAC module
 */
void destroy();

/**
 * Get the modification timestamp for an external user (if found)
 *
 * @param user the name of the user to search for
 * @return The modification timestamp for the user if found
 */
boost::optional<std::chrono::steady_clock::time_point> getExternalUserTimestamp(
        const std::string& user);

/**
 * Dump the user database to JSON
 *
 * This should only be used for testing as it holds a read lock for the
 * database while generating the dump.
 */
nlohmann::json to_json(Domain domain);
}
}
