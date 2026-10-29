#pragma once

#include "bsky_cpp/result.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace bsky_cpp {

/// A syntactically valid DID. Method support is reported separately.
class Did {
public:
    static Result<Did> parse(std::string value);

    const std::string& method() const noexcept { return m_method; }
    const std::string& value() const noexcept { return m_value; }
    const std::string& to_string() const noexcept { return m_canonical; }
    bool is_supported_method() const noexcept { return m_supported; }

    friend bool operator==(const Did& left, const Did& right) noexcept {
        return left.m_canonical == right.m_canonical;
    }
    friend bool operator!=(const Did& left, const Did& right) noexcept {
        return !(left == right);
    }

private:
    Did(std::string canonical, std::string method, std::string value, bool supported);

    std::string m_canonical;
    std::string m_method;
    std::string m_value;
    bool m_supported;
};

/// A canonical, ASCII-lowercase AT Protocol handle.
class Handle {
public:
    static Result<Handle> parse(std::string value);

    const std::string& to_string() const noexcept { return m_canonical; }

    friend bool operator==(const Handle& left, const Handle& right) noexcept {
        return left.m_canonical == right.m_canonical;
    }
    friend bool operator!=(const Handle& left, const Handle& right) noexcept {
        return !(left == right);
    }

private:
    explicit Handle(std::string canonical) : m_canonical(std::move(canonical)) {}

    std::string m_canonical;
};

/// A validated NSID used as an AT Protocol collection name.
class Nsid {
public:
    static Result<Nsid> parse(std::string value);

    const std::string& to_string() const noexcept { return m_canonical; }

private:
    explicit Nsid(std::string canonical) : m_canonical(std::move(canonical)) {}

    std::string m_canonical;
};

/// A case-sensitive record key using the AT Protocol restricted character set.
class RecordKey {
public:
    static Result<RecordKey> parse(std::string value);

    const std::string& to_string() const noexcept { return m_canonical; }

private:
    explicit RecordKey(std::string canonical) : m_canonical(std::move(canonical)) {}

    std::string m_canonical;
};

/// A parsed at:// URI. A URI may be DID- or handle-authorized and may omit
/// the collection when it identifies an authority origin. Query and fragment
/// components are preserved verbatim.
class AtUri {
public:
    static Result<AtUri> parse(std::string value);

    const std::string& authority() const noexcept { return m_authority; }
    const std::string& collection() const noexcept { return m_collection; }
    const std::optional<std::string>& record_key() const noexcept { return m_record_key; }
    const std::optional<std::string>& query() const noexcept { return m_query; }
    const std::optional<std::string>& fragment() const noexcept { return m_fragment; }
    const std::string& to_string() const noexcept { return m_canonical; }
    bool is_record() const noexcept { return m_record_key.has_value(); }
    bool is_did_based() const noexcept { return m_did_based; }
    bool is_handle_based() const noexcept { return !m_did_based; }

    friend bool operator==(const AtUri& left, const AtUri& right) noexcept {
        return left.m_canonical == right.m_canonical;
    }
    friend bool operator!=(const AtUri& left, const AtUri& right) noexcept {
        return !(left == right);
    }

private:
    AtUri(std::string canonical, std::string authority, std::string collection,
          std::optional<std::string> record_key, std::optional<std::string> query,
          std::optional<std::string> fragment, bool did_based);

    std::string m_canonical;
    std::string m_authority;
    std::string m_collection;
    std::optional<std::string> m_record_key;
    std::optional<std::string> m_query;
    std::optional<std::string> m_fragment;
    bool m_did_based;
};

/// A structurally decoded CID. Parsing accepts general CIDv1 and CIDv0 forms;
/// is_atproto_record() identifies dag-cbor record CIDs and is_atproto_blob()
/// identifies raw blob CIDs. is_atproto() accepts either AT Protocol form.
class Cid {
public:
    static Result<Cid> parse(std::string value);

    std::uint64_t version() const noexcept { return m_version; }
    std::uint64_t codec() const noexcept { return m_codec; }
    std::uint64_t multihash_code() const noexcept { return m_multihash_code; }
    std::size_t digest_size() const noexcept { return m_digest_size; }
    bool is_atproto_record() const noexcept;
    bool is_atproto_blob() const noexcept;
    bool is_atproto() const noexcept;
    const std::string& to_string() const noexcept { return m_canonical; }
    const std::vector<std::uint8_t>& bytes() const noexcept { return m_bytes; }

    friend bool operator==(const Cid& left, const Cid& right) noexcept {
        return left.m_bytes == right.m_bytes;
    }
    friend bool operator!=(const Cid& left, const Cid& right) noexcept {
        return !(left == right);
    }

private:
    Cid(std::string canonical, std::vector<std::uint8_t> bytes, std::uint64_t version,
        std::uint64_t codec, std::uint64_t multihash_code, std::size_t digest_size);

    std::string m_canonical;
    std::vector<std::uint8_t> m_bytes;
    std::uint64_t m_version;
    std::uint64_t m_codec;
    std::uint64_t m_multihash_code;
    std::size_t m_digest_size;
};

/// A validated record URI/record-CID pair identifying one record version.
class StrongRef {
public:
    static Result<StrongRef> create(AtUri uri, Cid cid);

    const AtUri& uri() const noexcept { return m_uri; }
    const Cid& cid() const noexcept { return m_cid; }

private:
    StrongRef(AtUri uri, Cid cid) : m_uri(std::move(uri)), m_cid(std::move(cid)) {}

    AtUri m_uri;
    Cid m_cid;
};

}  // namespace bsky_cpp
