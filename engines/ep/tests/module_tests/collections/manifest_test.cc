/* -*- Mode: C++; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 *     Copyright 2017 Couchbase, Inc
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

#include "collections/manifest.h"
#include "tests/module_tests/collections/test_manifest.h"

#include <gtest/gtest.h>
#include <memcached/engine_error.h>

#include <cctype>
#include <limits>
#include <unordered_set>

TEST(ManifestTest, validation) {
    std::vector<std::string> invalidManifests = {
            "", // empty
            "not json", // definitely not json
            R"({"uid"})", // illegal json

            // valid uid, no scopes object
            R"({"uid" : "0"})",

            // valid uid, invalid scopes type
            R"({"uid":"0"
                "scopes" : 0})",

            // valid uid, no scopes
            R"({"uid" : "0",
                "scopes" : []})",

            // valid uid, no default scope
            R"({"uid" : "0",
                "scopes":[{"name":"not_the_default", "uid":"8",
                "collections":[]}]})",

            // default collection not in default scope
            R"({"uid" : "0",
                "scopes":[{"name":"_default", "uid":"0",
                                "collections":[]},
                          {"name":"brewerA", "uid":"8",
                                "collections":[
                                    {"name":"_default","uid":"0"}]}]})",

            // valid uid, invalid collections type
            R"({"uid" : "0",
                "scopes" : [{"name":"_default", "uid":"0","
                "collections":[0]}]})",

            // valid uid, valid name, no collection uid
            R"({"uid" : "0",
                "scopes" : [{"name":"_default", "uid":"0","
                "collections":[{"name":"beer"}]}]})",

            // valid uid, valid name, no scope uid
            R"({"uid" : "0",
                "scopes":[{"name":"_default", "uid":"0",
                                "collections":[]},
                          {"name":"scope1",
                                "collections":[]}]})",

            // valid uid, valid collection uid, no collection name
            R"({"uid":"0",
                "scopes" : [{"name":"_default", "uid":"0","
                "collections":[{"uid":"8"}]}]})",

            // valid uid, valid scope uid, no scope name
            R"({"uid" : "0",
                "scopes":[{"name":"_default", "uid":"0",
                                "collections":[]},
                          {"uid":"8",
                                "collections":[]}]})",

            // valid name, invalid collection uid (wrong type)
            R"({"uid":"0",
                "scopes" : [{"name":"_default", "uid":"0","
                "collections":[{"name":"beer", "uid":8}]}]})",

            // valid name, invalid scope uid (wrong type)
            R"({"uid" : "0",
                "scopes":[{"name":"_default", "uid":"0",
                                "collections":[]},
                          {"name":"1", "uid":8,
                                "collections":[]}]})",

            // valid name, invalid collection uid (not hex)
            R"({"uid":"0",
                "scopes" : [{"name":"_default", "uid":"0","
                "collections":[{"name":"beer", "uid":"turkey"}]}]})",

            // valid name, invalid scope uid (not hex)
            R"({"uid" : "0",
                "scopes":[{"name":"_default", "uid":"0",
                                "collections":[]},
                          {"name":"1", "uid":"turkey",
                                "collections":[]}]})",

            // invalid collection name (wrong type), valid uid
            R"({"uid" : "0",
                "scopes" : [{"name":"_default", "uid":"0","
                "collections":[{"name":1, "uid":"8"}]}]})",

            // invalid scope name (wrong type), valid uid
            R"({"uid" : "0",
                "scopes":[{"name":"_default", "uid":"0",
                                "collections":[]},
                          {"name":1, "uid":"8",
                                "collections":[]}]})",

            // duplicate CID
            R"({"uid" : "0",
                "scopes" : [{"name":"_default", "uid":"0","
                "collections":[{"name":"beer", "uid":"8"},
                               {"name":"lager", "uid":"8"}]}]})",

            // duplicate scope id
            R"({"uid" : "0",
                "scopes":[
                    {"name":"_default", "uid":"0", "collections":[]},
                    {"name":"brewerA", "uid":"8","collections":[]},
                    {"name":"brewerB", "uid":"8","collections":[]}]})",

            // duplicate cid across scopes
            R"({"uid" : "0",
                "scopes":[{"name":"_default", "uid":"0",
                                "collections":[
                                    {"name":"brewery", "uid":"8"},
                          {"name":"brewerA", "uid":"8",
                                "collections":[
                                    {"name":"brewery", "uid":"8"}]}]})",

            // Invalid manifest UIDs
            // Missing UID
            R"({"scopes":[{"name":"_default", "uid":"0"}]})",

            // UID wrong type
            R"({"uid" : 0,
                "scopes":[{"name":"_default", "uid":"0"}]})",

            // UID cannot be converted to a value
            R"({"uid" : "thisiswrong",
                "scopes":[{"name":"_default", "uid":"0"}]})",

            // UID cannot be converted to a value
            R"({"uid" : "12345678901234567890112111",
                "scopes":[{"name":"_default", "uid":"0}]})",

            // UID cannot be 0x prefixed
            R"({"uid" : "0x101",
                "scopes":[{"name":"_default", "uid":"0"}]})",

            // collection cid cannot be 1
            R"({"uid" : "101",
                "scopes":[{"name":"_default", "uid":"0",
                "collections":[{"name":"beer", "uid":"1"}]}]})",

            // collection cid cannot be 7 (1-7 reserved)
            R"({"uid" : "101",
                "scopes":[{"name":"_default", "uid":"0",
                "collections":[{"name":"beer", "uid":"7"}]}]})",

            // scope uid cannot be 1
            R"({"uid" : "0",
                "scopes":[
                    {"name":"_default", "uid":"0", "collections":[]},
                    {"name":"brewerA", "uid":"1","collections":[]}]})",

            // scope uid cannot be 7 (1-7 reserved)
            R"({"uid" : "0",
                "scopes":[
                    {"name":"_default", "uid":"0", "collections":[]},
                    {"name":"brewerA", "uid":"7","collections":[]}]})",

            // scope uid too long
            R"({"uid" : "0",
                "scopes":[
                    {"name":"_default", "uid":"0", "collections":[]},
                    {"name":"brewerA", "uid":"1234567890","collections":[]}]})",

            // collection cid too long
            R"({"uid" : "101",
                "scopes":[{"name":"_default", "uid":"0",
                "collections":[{"name":"beer", "uid":"1234567890"}]}]})",

            // scope uid too long
            R"({"uid" : "0",
                "scopes":[
                    {"name":"_default", "uid":"0", "collections":[]},
                    {"name":"brewerA", "uid":"1234567890","collections":[]}]})",

            // Invalid collection names, no $ prefix allowed yet and empty
            // also denied
            R"({"uid" : "0",
                "scopes":[{"name":"_default", "uid":"0",
                "collections":[{"name":"$beer", "uid":"8"}]}]})",
            R"({"uid" : "0",
                "scopes":[{"name":"_default", "uid":"0",
                "collections":[{"name":"", "uid":"8"}]}]})",
            R"({"uid" : "0",
                "scopes":[{"name":"_default", "uid":"0",
                "collections":[{"name":"name_is_far_too_long_for_collections",
                "uid":"8"}]}]})",
            R"({"uid" : "0",
                "scopes":[{"name":"_default", "uid":"0",
                "collections":[{"name":"collection.name",
                "uid":"8"}]}]})",

            // Invalid scope names, no $ prefix allowed yet and empty denies
            R"({"uid" : "0",
                "scopes":[
                    {"name":"_default", "uid":"0", "collections":[]},
                    {"name":"$beer", "uid":"8", "collections":[]}]})",
            R"({"uid" : "0",
                "scopes":[
                    {"name":"_default", "uid":"0", "collections":[]},
                    {"name":"", "uid":"8", "collections":[]}]})",
            R"({"uid" : "0",
                "scopes":[
                    {"name":"_default", "uid":"0", "collections":[]},
                    {"name":"name_is_far_too_long_for_collections", "uid":"8",
                        "collections":[]}]})",
            R"({"uid" : "0",
                "scopes":[
                    {"name":"scope.name", "uid":"8", "collections":[]}]})",

            // max_ttl invalid cases
            // wrong type
            R"({"uid" : "0",
                "scopes":[{"name":"_default", "uid":"0",
                "collections":[{"name":"_default","uid":"0"},
                               {"name":"brewery","uid":"9","max_ttl":"string"}]}]})",
            // negative (doesn't make sense)
            R"({"uid" : "0",
                "scopes":[{"name":"_default", "uid":"0",
                "collections":[{"name":"_default","uid":"0"},
                               {"name":"brewery","uid":"9","max_ttl":-700}]}]})",
            // too big for 32-bit
            R"({"uid" : "0",
                "scopes":[{"name":"_default", "uid":"0",
                "collections":[{"name":"_default","uid":"0"},
                               {"name":"brewery","uid":"9","max_ttl":4294967296}]}]})",
            // Test duplicate scope names
            R"({"uid" : "0",
                "scopes":[{"name":"_default", "uid":"0",
                                "collections":[
                                    {"name":"_default","uid":"0"},
                                    {"name":"beer", "uid":"8"},
                                    {"name":"brewery","uid":"9"}]},
                          {"name":"brewerA", "uid":"8",
                                "collections":[
                                    {"name":"beer", "uid":"a"},
                                    {"name":"brewery", "uid":"b"}]},
                          {"name":"brewerA", "uid":"9",
                                "collections":[
                                    {"name":"beer", "uid":"c"},
                                    {"name":"brewery", "uid":"d"}]}]})",
            // Test duplicate collection names within the same scope
            R"({"uid" : "0",
                "scopes":[{"name":"_default", "uid":"0",
                                "collections":[
                                    {"name":"_default","uid":"0"},
                                    {"name":"brewery", "uid":"8"},
                                    {"name":"brewery","uid":"9"}]},
                          {"name":"brewerA", "uid":"8",
                                "collections":[
                                    {"name":"beer", "uid":"a"},
                                    {"name":"beer", "uid":"b"}]}]})",
    };

    std::vector<std::string> validManifests = {
            R"({"uid" : "0",
                "scopes":[{"name":"_default", "uid":"0",
                "collections":[]}]})",

            R"({"uid" : "0",
                "scopes":[
                    {"name":"_default", "uid":"0", "collections":[]},
                    {"name":"brewerA", "uid":"8", "collections":[]}]})",

            R"({"uid" : "0",
                "scopes":[{"name":"_default", "uid":"0",
                "collections":[{"name":"_default","uid":"0"},
                               {"name":"beer", "uid":"8"},
                               {"name":"brewery","uid":"9"}]}]})",

            R"({"uid" : "0",
                "scopes":[{"name":"_default", "uid":"0",
                                "collections":[
                                    {"name":"_default","uid":"0"},
                                    {"name":"beer", "uid":"8"},
                                    {"name":"brewery","uid":"9"}]},
                          {"name":"brewerA", "uid":"8",
                                "collections":[
                                    {"name":"beer", "uid":"a"},
                                    {"name":"brewery", "uid":"b"}]}]})",

            R"({"uid" : "0",
                "scopes":[{"name":"_default", "uid":"0",
                "collections":[{"name":"_default","uid":"0"},
                               {"name":"beer", "uid":"8"},
                               {"name":"brewery","uid":"9"}]}]})",

            R"({"uid" : "0",
                "scopes":[{"name":"_default", "uid":"0",
                "collections":[{"name":"beer", "uid":"8"},
                               {"name":"brewery","uid":"9"}]}]})",

            R"({"uid" : "0",
                "scopes":[{"name":"_default", "uid":"0",
                                "collections":[
                                    {"name":"beer", "uid":"8"},
                                    {"name":"brewery","uid":"9"}]},
                          {"name":"brewerA", "uid":"8",
                                "collections":[
                                    {"name":"beer", "uid":"a"},
                                    {"name":"brewery", "uid":"b"}]}]})",

            // Extra keys ignored at the moment
            R"({"extra":"key",
                "uid" : "0",
                "scopes":[{"name":"_default", "uid":"0",
                "collections":[{"name":"beer", "uid":"af"},
                               {"name":"brewery","uid":"8"}]}]})",

            // lower-case uid is fine
            R"({"uid" : "abcd1",
                "scopes":[{"name":"_default", "uid":"0",
                "collections":[]}]})",
            // upper-case uid is fine
            R"({"uid" : "ABCD1",
                "scopes":[{"name":"_default", "uid":"0",
                "collections":[]}]})",
            // mix-case uid is fine
            R"({"uid" : "AbCd1",
                "scopes":[{"name":"_default", "uid":"0",
                "collections":[]}]})",

            // max_ttl valid cases
            R"({"uid" : "0",
                "scopes":[{"name":"_default", "uid":"0",
                "collections":[{"name":"_default","uid":"0"},
                               {"name":"brewery","uid":"9","max_ttl":0}]}]})",
            R"({"uid" : "0",
                "scopes":[{"name":"_default", "uid":"0",
                "collections":[{"name":"_default","uid":"0"},
                               {"name":"brewery","uid":"9","max_ttl":1}]}]})",
            // max u32int
            R"({"uid" : "0",
                "scopes":[{"name":"_default", "uid":"0",
                "collections":[{"name":"_default","uid":"0"},
                               {"name":"brewery","uid":"9","max_ttl":4294967295}]}]})",
    };

    for (auto& manifest : invalidManifests) {
        try {
            Collections::Manifest m(manifest);
            EXPECT_TRUE(false)
                    << "No exception thrown for invalid manifest:" << manifest
                    << std::endl;
        } catch (std::exception&) {
        }
    }

    for (auto& manifest : validManifests) {
        try {
            Collections::Manifest m(manifest);
        } catch (std::exception& e) {
            EXPECT_TRUE(false)
                    << "Exception thrown for valid manifest:" << manifest
                    << std::endl
                    << " what:" << e.what();
        }
    }
}

TEST(ManifestTest, getUid) {
    std::vector<std::pair<Collections::ManifestUid, std::string>>
            validManifests = {{0,
                               R"({"uid" : "0",
                "scopes":[{"name":"_default", "uid":"0",
                "collections":[{"name":"beer", "uid":"8"},
                               {"name":"brewery","uid":"9"}]}]})"},
                              {0xabcd,
                               R"({"uid" : "ABCD",
                "scopes":[{"name":"_default", "uid":"0",
                "collections":[{"name":"beer", "uid":"8"},
                               {"name":"brewery","uid":"9"}]}]})"},
                              {0xabcd,
                               R"({"uid" : "abcd",
                "scopes":[{"name":"_default", "uid":"0",
                "collections":[{"name":"beer", "uid":"8"},
                               {"name":"brewery","uid":"9"}]}]})"},
                              {0xabcd,
                               R"({"uid" : "aBcD",
                "scopes":[{"name":"_default", "uid":"0",
                "collections":[{"name":"beer", "uid":"8"},
                               {"name":"brewery","uid":"9"}]}]})"}};

    for (auto& manifest : validManifests) {
        Collections::Manifest m(manifest.second);
        EXPECT_EQ(manifest.first, m.getUid());
    }
}

TEST(ManifestTest, findCollection) {
    std::string manifest =
            R"({"uid" : "0",
                "scopes":[{"name":"_default", "uid":"0",
                "collections":[{"name":"beer", "uid":"8"},
                               {"name":"brewery","uid":"9"},
                               {"name":"_default","uid":"0"}]}]})";
    std::vector<CollectionID> collectionT = {0, 8, 9};
    std::vector<CollectionID> collectionF = {0xa, 0xb, 0xc};

    Collections::Manifest m(manifest);

    for (auto& collection : collectionT) {
        EXPECT_NE(m.end(), m.findCollection(collection));
    }

    for (auto& collection : collectionF) {
        EXPECT_EQ(m.end(), m.findCollection(collection));
    }
}

// MB-30547: Initialization of `input` below fails on Clang 7 - temporarily
// skip to fix build breakage.
#if !defined(__clang_major__) || __clang_major__ > 7
// validate we can construct from JSON, call toJSON and get back valid JSON
// containing what went in.
TEST(ManifestTest, toJson) {
    struct TestInput {
        CollectionEntry::Entry collection;
        ScopeEntry::Entry scope;
        cb::ExpiryLimit maxTtl;
    };
    std::vector<std::pair<std::string, std::vector<TestInput>>> input = {
            {"abc0", {}},
            {"abc1",
             {{CollectionEntry::defaultC,
               ScopeEntry::defaultS,
               cb::ExpiryLimit{}},
              {CollectionEntry::fruit, ScopeEntry::defaultS, cb::ExpiryLimit{}},
              {CollectionEntry::vegetable,
               ScopeEntry::defaultS,
               cb::ExpiryLimit{}}}},
            {"abc2",
             {{CollectionEntry::fruit, ScopeEntry::defaultS, cb::ExpiryLimit{}},
              {CollectionEntry::vegetable,
               ScopeEntry::defaultS,
               cb::ExpiryLimit{}}}},
            {"abc3",
             {{CollectionEntry::fruit, ScopeEntry::shop1, cb::ExpiryLimit{}},
              {CollectionEntry::vegetable,
               ScopeEntry::defaultS,
               cb::ExpiryLimit{}}}},
            {"abc4",
             {{CollectionEntry::dairy, ScopeEntry::shop1, cb::ExpiryLimit{}},
              {CollectionEntry::dairy2, ScopeEntry::shop2, cb::ExpiryLimit{}}}},
            {"abc5",
             {{{CollectionEntry::dairy,
                ScopeEntry::shop1,
                std::chrono::seconds(100)},
               {CollectionEntry::dairy2,
                ScopeEntry::shop2,
                std::chrono::seconds(0)}}}}};

    for (auto& manifest : input) {
        CollectionsManifest cm(NoDefault{});
        std::unordered_set<ScopeID> scopesAdded;
        scopesAdded.insert(ScopeID::Default); // always the default scope
        for (auto& collection : manifest.second) {
            if (scopesAdded.count(collection.scope.uid) == 0) {
                cm.add(collection.scope);
                scopesAdded.insert(collection.scope.uid);
            }
            cm.add(collection.collection, collection.maxTtl, collection.scope);
        }
        cm.setUid(manifest.first);

        Collections::Manifest m(cm);

        nlohmann::json output, input;
        try {
            output = nlohmann::json::parse(m.toJson());
        } catch (const nlohmann::json::exception& e) {
            FAIL() << "Cannot nlohmann::json::parse output " << m.toJson()
                   << " " << e.what();
        }
        std::string s(cm);
        try {
            input = nlohmann::json::parse(s);
        } catch (const nlohmann::json::exception& e) {
            FAIL() << "Cannot nlohmann::json::parse input " << s << " "
                   << e.what();
        }

        EXPECT_EQ(input.size(), output.size());
        EXPECT_EQ(input["uid"].dump(), output["uid"].dump());
        EXPECT_EQ(input["scopes"].size(), output["scopes"].size());
        for (const auto& scope1 : output["scopes"]) {
            auto scope2 = std::find_if(
                    input["scopes"].begin(),
                    input["scopes"].end(),
                    [scope1](const nlohmann::json& scopeEntry) {
                        return scopeEntry["name"] == scope1["name"] &&
                               scopeEntry["uid"] == scope1["uid"];
                    });
            ASSERT_NE(scope2, input["scopes"].end());
            // If we are here we know scope1 and scope2 have name, uid fields
            // and they match, check the overall size, should be 3 fields
            // name, uid and collections
            EXPECT_EQ(3, scope1.size());
            EXPECT_EQ(scope1.size(), scope2->size());

            for (const auto& collection1 : scope1["collections"]) {
                // Find the collection from scope in the output
                auto collection2 = std::find_if(
                        (*scope2)["collections"].begin(),
                        (*scope2)["collections"].end(),
                        [collection1](const nlohmann::json& collectionEntry) {
                            return collectionEntry["name"] ==
                                           collection1["name"] &&
                                   collectionEntry["uid"] == collection1["uid"];
                        });
                ASSERT_NE(collection2, (*scope2)["collections"].end());
                // If we are here we know collection1 and collection2 have
                // matching name and uid fields, check the other fields.
                // max_ttl is optional

                EXPECT_EQ(collection1.size(), collection2->size());

                auto ttl1 = collection1.find("max_ttl");
                if (ttl1 != collection1.end()) {
                    ASSERT_EQ(3, collection1.size());
                    auto ttl2 = collection2->find("max_ttl");
                    ASSERT_NE(ttl2, collection2->end());
                    EXPECT_EQ(*ttl1, *ttl2);
                } else {
                    EXPECT_EQ(2, collection1.size());
                }
            }
        }
    }
}
#endif // !defined(__clang_major__) || __clang_major__ > 7

TEST(ManifestTest, badNames) {
    for (char c = 127; c >= 0; c--) {
        std::string name(1, c);
        CollectionsManifest cm({name, 8});

        if (!(std::isdigit(c) || std::isalpha(c) || c == '_' || c == '-' ||
              c == '%')) {
            try {
                Collections::Manifest m(cm);
                EXPECT_TRUE(false)
                        << "No exception thrown for invalid manifest:" << m
                        << std::endl;
            } catch (std::exception&) {
            }
        } else {
            try {
                Collections::Manifest m(cm);
            } catch (std::exception& e) {
                EXPECT_TRUE(false) << "Exception thrown for valid manifest"
                                   << std::endl
                                   << " what:" << e.what();
            }
        }
    }
}

TEST(ManifestTest, tooManyCollections) {
    std::vector<std::string> invalidManifests = {
            // Too many collections in the default scope
            R"({"uid" : "0",
                "scopes":[{"name":"_default", "uid":"0",
                "collections":[{"name":"beer", "uid":"8"},
                               {"name":"brewery","uid":"9"}]}]})",

            // Too many collections in a non-default scope
            R"({"uid" : "0",
                "scopes":[{"name":"_default", "uid":"0",
                                "collections":[]},
                          {"name":"brewerA", "uid":"2",
                                "collections":[
                                    {"name":"beer", "uid":"8"},
                                    {"name":"brewery", "uid":"9"}]}]})",

            // Too many collections across all scopes
            R"({"uid" : "0",
                "scopes":[{"name":"_default", "uid":"0",
                                "collections":[
                                    {"name":"beer", "uid":"8"}]},
                          {"name":"brewerA", "uid":"2",
                                "collections":[
                                    {"name":"beer", "uid":"9"}]}]})",
    };

    for (auto& manifest : invalidManifests) {
        EXPECT_THROW(Collections::Manifest cm(manifest, 2, 1),
                     std::invalid_argument)
                << "No exception thrown for manifest "
                   "with too many collections. "
                   "Manifest: "
                << manifest << std::endl;
    }
}

TEST(ManifestTest, tooManyScopes) {
    std::vector<std::string> invalidManifests = {
            // Too many scopes
            R"({"uid" : "0",
                "scopes":[{"name":"_default", "uid":"0",
                                "collections":[]},
                          {"name":"brewerA", "uid":"2",
                                "collections":[
                                    {"name":"beer", "uid":"8"},
                                    {"name":"brewery", "uid":"9"}]}]})",
    };

    for (auto& manifest : invalidManifests) {
        EXPECT_THROW(Collections::Manifest cm(manifest, 1),
                     std::invalid_argument)
                << "No exception thrown for manifest "
                   "with too many collections. "
                   "Manifest: "
                << manifest << std::endl;
    }
}

TEST(ManifestTest, findCollectionByName) {
    std::string manifest = R"({"uid" : "0",
                "scopes":[{"name":"_default", "uid":"0",
                                "collections":[
                                    {"name":"_default", "uid":"0"},
                                    {"name":"meat", "uid":"8"}]},
                          {"name":"brewerA", "uid":"8",
                                "collections":[
                                    {"name":"beer", "uid":"9"}]}]})";
    Collections::Manifest cm(manifest);

    // We expect to find the collections in the default scope, when we do not
    // specify the scope
    // Test that the uid matches the collection that we are searching for
    EXPECT_EQ(cm.findCollection("_default")->first, 0);
    EXPECT_EQ(cm.findCollection("meat")->first, 8);

    // We do not expect to find collections not in the default scope, when we
    // do not specify the scope
    EXPECT_EQ(cm.findCollection("beer"), cm.end());

    // We expect to find collections when searching by collection and scope name
    // Test that the uid matches the collection that we are searching for
    EXPECT_EQ(cm.findCollection("_default", "_default")->first, 0);
    EXPECT_EQ(cm.findCollection("meat", "_default")->first, 8);
    EXPECT_EQ(cm.findCollection("beer", "brewerA")->first, 9);

    // We do not expect to find collections with incorrect scope that does exist
    EXPECT_EQ(cm.findCollection("_default", "brewerA"), cm.end());
    EXPECT_EQ(cm.findCollection("meat", "brewerA"), cm.end());
    EXPECT_EQ(cm.findCollection("beer", "_default"), cm.end());

    // We do not expect to find collections when we give a scope that does
    // not exist
    EXPECT_EQ(cm.findCollection("_default", "a_scope_name"), cm.end());
    EXPECT_EQ(cm.findCollection("meat", "a_scope_name"), cm.end());
    EXPECT_EQ(cm.findCollection("beer", "a_scope_name"), cm.end());

    // We do not expect to find collections that do not exist in a scope that
    // does
    EXPECT_EQ(cm.findCollection("fruit", "_default"), cm.end());
    EXPECT_EQ(cm.findCollection("fruit", "brewerA"), cm.end());

    // We do not expect to find collections that do not exist in scopes that
    // do not exist
    EXPECT_EQ(cm.findCollection("fruit", "a_scope_name"), cm.end());
}

TEST(ManifestTest, getCollectionID) {
    std::string manifest = R"({"uid" : "0",
                "scopes":[{"name":"_default", "uid":"0",
                                "collections":[
                                    {"name":"_default", "uid":"0"},
                                    {"name":"meat", "uid":"8"}]},
                          {"name":"brewerA", "uid":"8",
                                "collections":[
                                    {"name":"beer", "uid":"9"},
                                    {"name":"meat", "uid":"a"}]}]})";
    Collections::Manifest cm(manifest);

    EXPECT_EQ(CollectionID::Default, cm.getCollectionID(".").get());
    EXPECT_EQ(CollectionID::Default, cm.getCollectionID("_default.").get());
    EXPECT_EQ(8, cm.getCollectionID(".meat").get());
    EXPECT_EQ(8, cm.getCollectionID("_default.meat").get());
    EXPECT_EQ(9, cm.getCollectionID("brewerA.beer").get());
    EXPECT_EQ(0xa, cm.getCollectionID("brewerA.meat").get());

    try {
        cm.getCollectionID("bogus");
    } catch (const cb::engine_error& e) {
        EXPECT_EQ(cb::engine_errc::invalid_arguments,
                  cb::engine_errc(e.code().value()));
    }
    try {
        cm.getCollectionID("");
    } catch (const cb::engine_error& e) {
        EXPECT_EQ(cb::engine_errc::invalid_arguments,
                  cb::engine_errc(e.code().value()));
    }
    try {
        cm.getCollectionID("..");
    } catch (const cb::engine_error& e) {
        EXPECT_EQ(cb::engine_errc::invalid_arguments,
                  cb::engine_errc(e.code().value()));
    }
    try {
        cm.getCollectionID("a.b.c");
    } catch (const cb::engine_error& e) {
        EXPECT_EQ(cb::engine_errc::invalid_arguments,
                  cb::engine_errc(e.code().value()));
    }

    try {
        // Illegal names
        cm.getCollectionID("invalid***.collection&");
    } catch (const cb::engine_error& e) {
        EXPECT_EQ(cb::engine_errc::invalid_arguments,
                  cb::engine_errc(e.code().value()));
    }

    // Unknown names
    EXPECT_FALSE(cm.getCollectionID("unknown.collection"));

    // Unknown scope
    EXPECT_FALSE(cm.getCollectionID("unknown.beer"));

    // Unknown collection
    EXPECT_FALSE(cm.getCollectionID("brewerA.ale"));
}
