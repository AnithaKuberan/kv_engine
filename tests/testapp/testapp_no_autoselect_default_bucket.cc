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

#include "testapp.h"
#include "testapp_client_test.h"
#include <protocol/connection/client_mcbp_connection.h>

#include <algorithm>
#include <platform/compress.h>

static std::string env{"MEMCACHED_UNIT_TESTS_NO_DEFAULT_BUCKET=true"};

class NoAutoselectDefaultBucketTest : public TestappClientTest {
public:
    static void SetUpTestCase() {
        putenv(const_cast<char*>(env.c_str()));
        ::TestappClientTest::SetUpTestCase();
    }

    static void TearDownTestCase() {
        stop_memcached_server();
    }

    void SetUp() override {
    }

    void TearDown() override {
    }
};

INSTANTIATE_TEST_CASE_P(TransportProtocols,
                        NoAutoselectDefaultBucketTest,
                        ::testing::Values(TransportProtocols::McbpPlain,
                                          TransportProtocols::McbpIpv6Plain,
                                          TransportProtocols::McbpSsl,
                                          TransportProtocols::McbpIpv6Ssl
                                         ),
                        ::testing::PrintToStringParamName());

TEST_P(NoAutoselectDefaultBucketTest, NoAutoselect) {
    auto& c = getAdminConnection();
    auto& conn = dynamic_cast<MemcachedBinprotConnection&>(c);

    auto buckets = conn.listBuckets();
    for (auto& name : buckets) {
        if (name == "default") {
            conn.deleteBucket("default");
        }
    }
    conn.createBucket("default", "", BucketType::Memcached);

    // Reconnect (to drop the admin credentials)
    c = getConnection();
    conn = dynamic_cast<MemcachedBinprotConnection&>(c);

    BinprotGetCommand cmd;
    cmd.setKey(name);
    conn.sendCommand(cmd);

    BinprotResponse rsp;
    conn.recvResponse(rsp);

    EXPECT_FALSE(rsp.isSuccess());
    // You would have expected NO BUCKET, but we don't have access
    // to this bucket ;)
    EXPECT_EQ(PROTOCOL_BINARY_RESPONSE_EACCESS, rsp.getStatus());

    c = getAdminConnection();
    conn = dynamic_cast<MemcachedBinprotConnection&>(c);
    conn.deleteBucket("default");
}