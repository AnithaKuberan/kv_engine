/* -*- Mode: C++; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 *     Copyright 2018 Couchbase, Inc.
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

#include <mcbp/protocol/request.h>
#include <memcached/dcp_stream_id.h>
#include <memcached/protocol_binary.h>
#include <nlohmann/json.hpp>
#include <cctype>

namespace cb {
namespace mcbp {

/**
 * Check to see if the specified opcode supports reordering
 *
 * @return true if the server supports reordering of that command
 */
static bool reorderSupported(ClientOpcode opcode) {
    switch (opcode) {
    case ClientOpcode::Get:
        return true;
    case ClientOpcode::Set:
    case ClientOpcode::Add:
    case ClientOpcode::Replace:
    case ClientOpcode::Delete:
    case ClientOpcode::Increment:
    case ClientOpcode::Decrement:
    case ClientOpcode::Quit:
    case ClientOpcode::Flush:
    case ClientOpcode::Getq:
    case ClientOpcode::Noop:
    case ClientOpcode::Version:
    case ClientOpcode::Getk:
    case ClientOpcode::Getkq:
    case ClientOpcode::Append:
    case ClientOpcode::Prepend:
    case ClientOpcode::Stat:
    case ClientOpcode::Setq:
    case ClientOpcode::Addq:
    case ClientOpcode::Replaceq:
    case ClientOpcode::Deleteq:
    case ClientOpcode::Incrementq:
    case ClientOpcode::Decrementq:
    case ClientOpcode::Quitq:
    case ClientOpcode::Flushq:
    case ClientOpcode::Appendq:
    case ClientOpcode::Prependq:
    case ClientOpcode::Verbosity:
    case ClientOpcode::Touch:
    case ClientOpcode::Gat:
    case ClientOpcode::Gatq:
    case ClientOpcode::Hello:
    case ClientOpcode::SaslListMechs:
    case ClientOpcode::SaslAuth:
    case ClientOpcode::SaslStep:
    case ClientOpcode::IoctlGet:
    case ClientOpcode::IoctlSet:
    case ClientOpcode::ConfigValidate:
    case ClientOpcode::ConfigReload:
    case ClientOpcode::AuditPut:
    case ClientOpcode::AuditConfigReload:
    case ClientOpcode::Shutdown:
    case ClientOpcode::Rget:
    case ClientOpcode::Rset:
    case ClientOpcode::Rsetq:
    case ClientOpcode::Rappend:
    case ClientOpcode::Rappendq:
    case ClientOpcode::Rprepend:
    case ClientOpcode::Rprependq:
    case ClientOpcode::Rdelete:
    case ClientOpcode::Rdeleteq:
    case ClientOpcode::Rincr:
    case ClientOpcode::Rincrq:
    case ClientOpcode::Rdecr:
    case ClientOpcode::Rdecrq:
    case ClientOpcode::SetVbucket:
    case ClientOpcode::GetVbucket:
    case ClientOpcode::DelVbucket:
    case ClientOpcode::TapConnect:
    case ClientOpcode::TapMutation:
    case ClientOpcode::TapDelete:
    case ClientOpcode::TapFlush:
    case ClientOpcode::TapOpaque:
    case ClientOpcode::TapVbucketSet:
    case ClientOpcode::TapCheckpointStart:
    case ClientOpcode::TapCheckpointEnd:
    case ClientOpcode::GetAllVbSeqnos:
    case ClientOpcode::DcpOpen:
    case ClientOpcode::DcpAddStream:
    case ClientOpcode::DcpCloseStream:
    case ClientOpcode::DcpStreamReq:
    case ClientOpcode::DcpGetFailoverLog:
    case ClientOpcode::DcpStreamEnd:
    case ClientOpcode::DcpSnapshotMarker:
    case ClientOpcode::DcpMutation:
    case ClientOpcode::DcpDeletion:
    case ClientOpcode::DcpExpiration:
    case ClientOpcode::DcpSetVbucketState:
    case ClientOpcode::DcpNoop:
    case ClientOpcode::DcpBufferAcknowledgement:
    case ClientOpcode::DcpControl:
    case ClientOpcode::DcpSystemEvent:
    case ClientOpcode::DcpPrepare:
    case ClientOpcode::DcpSeqnoAcknowledged:
    case ClientOpcode::DcpCommit:
    case ClientOpcode::DcpAbort:
    case ClientOpcode::StopPersistence:
    case ClientOpcode::StartPersistence:
    case ClientOpcode::SetParam:
    case ClientOpcode::GetReplica:
    case ClientOpcode::CreateBucket:
    case ClientOpcode::DeleteBucket:
    case ClientOpcode::ListBuckets:
    case ClientOpcode::SelectBucket:
    case ClientOpcode::ObserveSeqno:
    case ClientOpcode::Observe:
    case ClientOpcode::EvictKey:
    case ClientOpcode::GetLocked:
    case ClientOpcode::UnlockKey:
    case ClientOpcode::GetFailoverLog:
    case ClientOpcode::LastClosedCheckpoint:
    case ClientOpcode::ResetReplicationChain:
    case ClientOpcode::DeregisterTapClient:
    case ClientOpcode::GetMeta:
    case ClientOpcode::GetqMeta:
    case ClientOpcode::SetWithMeta:
    case ClientOpcode::SetqWithMeta:
    case ClientOpcode::AddWithMeta:
    case ClientOpcode::AddqWithMeta:
    case ClientOpcode::SnapshotVbStates:
    case ClientOpcode::VbucketBatchCount:
    case ClientOpcode::DelWithMeta:
    case ClientOpcode::DelqWithMeta:
    case ClientOpcode::CreateCheckpoint:
    case ClientOpcode::NotifyVbucketUpdate:
    case ClientOpcode::EnableTraffic:
    case ClientOpcode::DisableTraffic:
    case ClientOpcode::ChangeVbFilter:
    case ClientOpcode::CheckpointPersistence:
    case ClientOpcode::ReturnMeta:
    case ClientOpcode::CompactDb:
    case ClientOpcode::SetClusterConfig:
    case ClientOpcode::GetClusterConfig:
    case ClientOpcode::GetRandomKey:
    case ClientOpcode::SeqnoPersistence:
    case ClientOpcode::GetKeys:
    case ClientOpcode::CollectionsSetManifest:
    case ClientOpcode::CollectionsGetManifest:
    case ClientOpcode::CollectionsGetID:
    case ClientOpcode::SetDriftCounterState:
    case ClientOpcode::GetAdjustedTime:
    case ClientOpcode::SubdocGet:
    case ClientOpcode::SubdocExists:
    case ClientOpcode::SubdocDictAdd:
    case ClientOpcode::SubdocDictUpsert:
    case ClientOpcode::SubdocDelete:
    case ClientOpcode::SubdocReplace:
    case ClientOpcode::SubdocArrayPushLast:
    case ClientOpcode::SubdocArrayPushFirst:
    case ClientOpcode::SubdocArrayInsert:
    case ClientOpcode::SubdocArrayAddUnique:
    case ClientOpcode::SubdocCounter:
    case ClientOpcode::SubdocMultiLookup:
    case ClientOpcode::SubdocMultiMutation:
    case ClientOpcode::SubdocGetCount:
    case ClientOpcode::Scrub:
    case ClientOpcode::IsaslRefresh:
    case ClientOpcode::SslCertsRefresh:
    case ClientOpcode::GetCmdTimer:
    case ClientOpcode::SetCtrlToken:
    case ClientOpcode::GetCtrlToken:
    case ClientOpcode::UpdateExternalUserPermissions:
    case ClientOpcode::RbacRefresh:
    case ClientOpcode::AuthProvider:
    case ClientOpcode::DropPrivilege:
    case ClientOpcode::AdjustTimeofday:
    case ClientOpcode::EwouldblockCtl:
    case ClientOpcode::GetErrorMap:
    case ClientOpcode::Invalid:
        return false;
    }
    throw std::runtime_error("reorderSupported(): Unknown opcode: " +
                             std::to_string(int(opcode)));
}

void Request::setKeylen(uint16_t value) {
    if (is_alternative_encoding(getMagic())) {
        reinterpret_cast<uint8_t*>(this)[3] = gsl::narrow<uint8_t>(value);
    } else {
        keylen = htons(value);
    }
}

void Request::setFramingExtraslen(uint8_t len) {
    setMagic(cb::mcbp::Magic::AltClientRequest);
    // @todo Split the member once we know all the tests pass with the
    //       current layout (aka: noone tries to set it htons()
    reinterpret_cast<uint8_t*>(this)[2] = len;
}

std::string Request::getPrintableKey() const {
    const auto key = getKey();

    std::string buffer{reinterpret_cast<const char*>(key.data()), key.size()};
    for (auto& ii : buffer) {
        if (!std::isgraph(ii)) {
            ii = '.';
        }
    }

    return buffer;
}

void Request::parseFrameExtras(FrameInfoCallback callback) const {
    auto fe = getFramingExtras();
    if (fe.empty()) {
        return;
    }
    size_t offset = 0;
    while (offset < fe.size()) {
        using cb::mcbp::request::FrameInfoId;
        const auto id = FrameInfoId(fe[offset] >> 4);
        size_t size = size_t(fe[offset] & 0x0f);

        if ((offset + 1 + size) > fe.size()) {
            throw std::overflow_error("parseFrameExtras: outside frame extras");
        }

        cb::const_byte_buffer content{fe.data() + offset + 1, size};
        offset += 1 + size;
        switch (id) {
        case FrameInfoId::Reorder:
            if (!content.empty()) {
                throw std::runtime_error(
                        "parseFrameExtras: Invalid size for Reorder, size:" +
                        std::to_string(content.size()));
            }
            if (!callback(FrameInfoId::Reorder, content)) {
                return;
            }
            continue;
        case FrameInfoId::DurabilityRequirement:
            if (content.size() != 1 && content.size() != 3) {
                throw std::runtime_error(
                        "parseFrameExtras: Invalid size for "
                        "DurabilityRequirement, size:" +
                        std::to_string(content.size()));
            }
            if (!callback(FrameInfoId::DurabilityRequirement, content)) {
                return;
            }
            continue;
        case FrameInfoId::DcpStreamId:
            if (content.size() != sizeof(DcpStreamId)) {
                throw std::runtime_error(
                        "parseFrameExtras: Invalid size for "
                        "DcpStreamId, size:" +
                        std::to_string(content.size()));
            }
            if (!callback(FrameInfoId::DcpStreamId, content)) {
                return;
            }
            continue;
        }
        throw std::runtime_error(
                "cb::mcbp::Request::parseFrameExtras: Unknown id: " +
                std::to_string(int(id)));
    }
}

bool Request::isQuiet() const {
    if ((getMagic() == Magic::ClientRequest) ||
        (getMagic() == Magic::AltClientRequest)) {
        switch (getClientOpcode()) {
        case ClientOpcode::Get:
        case ClientOpcode::Set:
        case ClientOpcode::Add:
        case ClientOpcode::Replace:
        case ClientOpcode::Delete:
        case ClientOpcode::Increment:
        case ClientOpcode::Decrement:
        case ClientOpcode::Quit:
        case ClientOpcode::Flush:
        case ClientOpcode::Noop:
        case ClientOpcode::Version:
        case ClientOpcode::Getk:
        case ClientOpcode::Append:
        case ClientOpcode::Prepend:
        case ClientOpcode::Stat:
        case ClientOpcode::Verbosity:
        case ClientOpcode::Touch:
        case ClientOpcode::Gat:
        case ClientOpcode::Hello:
        case ClientOpcode::SaslListMechs:
        case ClientOpcode::SaslAuth:
        case ClientOpcode::SaslStep:
        case ClientOpcode::IoctlGet:
        case ClientOpcode::IoctlSet:
        case ClientOpcode::ConfigValidate:
        case ClientOpcode::ConfigReload:
        case ClientOpcode::AuditPut:
        case ClientOpcode::AuditConfigReload:
        case ClientOpcode::Shutdown:
        case ClientOpcode::Rget:
        case ClientOpcode::Rset:
        case ClientOpcode::Rappend:
        case ClientOpcode::Rprepend:
        case ClientOpcode::Rdelete:
        case ClientOpcode::Rincr:
        case ClientOpcode::Rdecr:
        case ClientOpcode::SetVbucket:
        case ClientOpcode::GetVbucket:
        case ClientOpcode::DelVbucket:
        case ClientOpcode::TapConnect:
        case ClientOpcode::TapMutation:
        case ClientOpcode::TapDelete:
        case ClientOpcode::TapFlush:
        case ClientOpcode::TapOpaque:
        case ClientOpcode::TapVbucketSet:
        case ClientOpcode::TapCheckpointStart:
        case ClientOpcode::TapCheckpointEnd:
        case ClientOpcode::GetAllVbSeqnos:
        case ClientOpcode::DcpOpen:
        case ClientOpcode::DcpAddStream:
        case ClientOpcode::DcpCloseStream:
        case ClientOpcode::DcpStreamReq:
        case ClientOpcode::DcpGetFailoverLog:
        case ClientOpcode::DcpStreamEnd:
        case ClientOpcode::DcpSnapshotMarker:
        case ClientOpcode::DcpMutation:
        case ClientOpcode::DcpDeletion:
        case ClientOpcode::DcpExpiration:
        case ClientOpcode::DcpSetVbucketState:
        case ClientOpcode::DcpNoop:
        case ClientOpcode::DcpBufferAcknowledgement:
        case ClientOpcode::DcpControl:
        case ClientOpcode::DcpSystemEvent:
        case ClientOpcode::DcpPrepare:
        case ClientOpcode::DcpSeqnoAcknowledged:
        case ClientOpcode::DcpCommit:
        case ClientOpcode::DcpAbort:
        case ClientOpcode::StopPersistence:
        case ClientOpcode::StartPersistence:
        case ClientOpcode::SetParam:
        case ClientOpcode::GetReplica:
        case ClientOpcode::CreateBucket:
        case ClientOpcode::DeleteBucket:
        case ClientOpcode::ListBuckets:
        case ClientOpcode::SelectBucket:
        case ClientOpcode::ObserveSeqno:
        case ClientOpcode::Observe:
        case ClientOpcode::EvictKey:
        case ClientOpcode::GetLocked:
        case ClientOpcode::UnlockKey:
        case ClientOpcode::GetFailoverLog:
        case ClientOpcode::LastClosedCheckpoint:
        case ClientOpcode::ResetReplicationChain:
        case ClientOpcode::DeregisterTapClient:
        case ClientOpcode::GetMeta:
        case ClientOpcode::SetWithMeta:
        case ClientOpcode::AddWithMeta:
        case ClientOpcode::SnapshotVbStates:
        case ClientOpcode::VbucketBatchCount:
        case ClientOpcode::DelWithMeta:
        case ClientOpcode::CreateCheckpoint:
        case ClientOpcode::NotifyVbucketUpdate:
        case ClientOpcode::EnableTraffic:
        case ClientOpcode::DisableTraffic:
        case ClientOpcode::ChangeVbFilter:
        case ClientOpcode::CheckpointPersistence:
        case ClientOpcode::ReturnMeta:
        case ClientOpcode::CompactDb:
        case ClientOpcode::SetClusterConfig:
        case ClientOpcode::GetClusterConfig:
        case ClientOpcode::GetRandomKey:
        case ClientOpcode::SeqnoPersistence:
        case ClientOpcode::GetKeys:
        case ClientOpcode::CollectionsSetManifest:
        case ClientOpcode::CollectionsGetManifest:
        case ClientOpcode::CollectionsGetID:
        case ClientOpcode::SetDriftCounterState:
        case ClientOpcode::GetAdjustedTime:
        case ClientOpcode::SubdocGet:
        case ClientOpcode::SubdocExists:
        case ClientOpcode::SubdocDictAdd:
        case ClientOpcode::SubdocDictUpsert:
        case ClientOpcode::SubdocDelete:
        case ClientOpcode::SubdocReplace:
        case ClientOpcode::SubdocArrayPushLast:
        case ClientOpcode::SubdocArrayPushFirst:
        case ClientOpcode::SubdocArrayInsert:
        case ClientOpcode::SubdocArrayAddUnique:
        case ClientOpcode::SubdocCounter:
        case ClientOpcode::SubdocMultiLookup:
        case ClientOpcode::SubdocMultiMutation:
        case ClientOpcode::SubdocGetCount:
        case ClientOpcode::Scrub:
        case ClientOpcode::IsaslRefresh:
        case ClientOpcode::SslCertsRefresh:
        case ClientOpcode::GetCmdTimer:
        case ClientOpcode::SetCtrlToken:
        case ClientOpcode::GetCtrlToken:
        case ClientOpcode::UpdateExternalUserPermissions:
        case ClientOpcode::RbacRefresh:
        case ClientOpcode::AuthProvider:
        case ClientOpcode::DropPrivilege:
        case ClientOpcode::AdjustTimeofday:
        case ClientOpcode::EwouldblockCtl:
        case ClientOpcode::GetErrorMap:
        case ClientOpcode::Invalid:
            return false;

        case ClientOpcode::Getq:
        case ClientOpcode::Getkq:
        case ClientOpcode::Setq:
        case ClientOpcode::Addq:
        case ClientOpcode::Replaceq:
        case ClientOpcode::Deleteq:
        case ClientOpcode::Incrementq:
        case ClientOpcode::Decrementq:
        case ClientOpcode::Quitq:
        case ClientOpcode::Flushq:
        case ClientOpcode::Appendq:
        case ClientOpcode::Prependq:
        case ClientOpcode::Gatq:
        case ClientOpcode::Rsetq:
        case ClientOpcode::Rappendq:
        case ClientOpcode::Rprependq:
        case ClientOpcode::Rdeleteq:
        case ClientOpcode::Rincrq:
        case ClientOpcode::Rdecrq:
        case ClientOpcode::GetqMeta:
        case ClientOpcode::SetqWithMeta:
        case ClientOpcode::AddqWithMeta:
        case ClientOpcode::DelqWithMeta:
            return true;
        }
    } else {
        switch (getServerOpcode()) {
        case ServerOpcode::ClustermapChangeNotification:
            return false;
        case ServerOpcode::Authenticate:
            return false;
        case ServerOpcode::ActiveExternalUsers:
            return false;
        }
    }

    throw std::invalid_argument("Request::isQuiet: Uknown opcode");
}

boost::optional<cb::durability::Requirements>
Request::getDurabilityRequirements() const {
    using cb::durability::Level;
    using cb::durability::Requirements;
    Requirements ret;
    bool found = false;

    parseFrameExtras([&ret, &found](cb::mcbp::request::FrameInfoId id,
                                    cb::const_byte_buffer data) -> bool {
        if (id == cb::mcbp::request::FrameInfoId::DurabilityRequirement) {
            ret = Requirements{data};
            found = true;
            // stop parsing
            return false;
        }
        // Continue parsing
        return true;
    });
    if (found) {
        return {ret};
    }
    return {};
}

bool Request::mayReorder(const Request& other) const {
    if (!reorderSupported(getClientOpcode()) ||
        !reorderSupported(other.getClientOpcode())) {
        return false;
    }

    bool allowReorder = false;
    parseFrameExtras([&allowReorder](cb::mcbp::request::FrameInfoId id,
                                     cb::const_byte_buffer data) -> bool {
        if (id == cb::mcbp::request::FrameInfoId::Reorder) {
            allowReorder = true;
            // stop parsing
            return false;
        }
        // Continue parsing
        return true;
    });

    if (!allowReorder) {
        // This command don't allow for reordering of commands
        return false;
    }

    allowReorder = false;
    other.parseFrameExtras([&allowReorder](cb::mcbp::request::FrameInfoId id,
                                           cb::const_byte_buffer data) -> bool {
        if (id == cb::mcbp::request::FrameInfoId::Reorder) {
            allowReorder = true;
            // stop parsing
            return false;
        }
        // Continue parsing
        return true;
    });

    return allowReorder;
}

nlohmann::json Request::toJSON() const {
    if (!isValid()) {
        throw std::logic_error("Request::toJSON(): Invalid packet");
    }

    nlohmann::json ret;
    auto m = Magic(magic);
    ret["magic"] = ::to_string(m);

    if (is_client_magic(m)) {
        ret["opcode"] = ::to_string(getClientOpcode());
    } else {
        ret["opcode"] = ::to_string(getServerOpcode());
    }

    ret["keylen"] = getKeylen();
    ret["extlen"] = getExtlen();
    ret["datatype"] = ::toJSON(getDatatype());
    ret["vbucket"] = getVBucket().get();
    ret["bodylen"] = getBodylen();
    ret["opaque"] = getOpaque();
    ret["cas"] = getCas();

    return ret;
}

bool Request::isValid() const {
    auto m = Magic(magic);
    if (!is_legal(m) || !is_request(m)) {
        return false;
    }

    return (size_t(extlen) + size_t(getKeylen()) <= size_t(getBodylen()));
}

} // namespace mcbp
} // namespace cb

std::string to_string(cb::mcbp::request::FrameInfoId id) {
    using cb::mcbp::request::FrameInfoId;

    switch (id) {
    case FrameInfoId::Reorder:
        return "Reorder";
    case FrameInfoId::DurabilityRequirement:
        return "DurabilityRequirement";
    case FrameInfoId::DcpStreamId:
        return "DcpStreamId";
    }

    throw std::invalid_argument("to_string(): Invalid frame id: " +
                                std::to_string(int(id)));
}
