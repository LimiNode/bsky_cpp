/// \file identifiers.hpp
/// \brief Strict AT Protocol identifier value types.
#pragma once

#include "bsky_cpp/core/result.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace bsky_cpp {

/// A syntactically valid DID. Method support is reported separately.
class Did {
public:
    /// Parses and validates a DID string.
    /// \param value Candidate DID text.
    /// \return A valid DID or an InvalidArgument error.
    static Result<Did> parse(std::string value);

    /// Returns the lowercase method name.
    /// \return The DID method.
    const std::string& method() const noexcept { return m_method; }
    /// Returns the method-specific DID value.
    /// \return The method-specific value.
    const std::string& value() const noexcept { return m_value; }
    /// Returns the canonical DID spelling.
    /// \return The canonical DID.
    const std::string& to_string() const noexcept { return m_canonical; }
    /// Reports whether the method has a built-in local interpretation.
    /// This does not perform DID resolution or network I/O.
    /// \return `true` for methods recognized by the current implementation.
    bool is_supported_method() const noexcept { return m_supported; }

    /// Compares canonical DID spellings.
    friend bool operator==(const Did& left, const Did& right) noexcept {
        return left.m_canonical == right.m_canonical;
    }
    /// Returns the inverse of operator==.
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
    /// Parses and canonicalizes an AT Protocol handle.
    /// \param value Candidate handle text.
    /// \return A valid handle or an InvalidArgument error.
    static Result<Handle> parse(std::string value);

    /// Returns the canonical lowercase handle.
    /// \return The canonical handle.
    const std::string& to_string() const noexcept { return m_canonical; }

    /// Compares canonical handle spellings.
    friend bool operator==(const Handle& left, const Handle& right) noexcept {
        return left.m_canonical == right.m_canonical;
    }
    /// Returns the inverse of operator==.
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
    /// Parses and validates an NSID.
    /// \param value Candidate NSID text.
    /// \return A valid NSID or an InvalidArgument error.
    static Result<Nsid> parse(std::string value);

    /// Returns the canonical NSID spelling.
    /// \return The NSID.
    const std::string& to_string() const noexcept { return m_canonical; }

private:
    explicit Nsid(std::string canonical) : m_canonical(std::move(canonical)) {}

    std::string m_canonical;
};

/// A case-sensitive record key using the AT Protocol restricted character set.
class RecordKey {
public:
    /// Parses and validates a record key.
    /// \param value Candidate record key.
    /// \return A valid record key or an InvalidArgument error.
    static Result<RecordKey> parse(std::string value);

    /// Returns the original case-sensitive key.
    /// \return The record key.
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
    /// Parses and validates an at:// URI.
    /// \param value Candidate URI text.
    /// \return A valid URI or an InvalidArgument error.
    static Result<AtUri> parse(std::string value);

    /// Returns the authority component.
    /// \return The DID or handle authority.
    const std::string& authority() const noexcept { return m_authority; }
    /// Returns the collection, or an empty string for an origin URI.
    /// \return The collection component.
    const std::string& collection() const noexcept { return m_collection; }
    /// Returns the optional record key.
    /// \return The record key when present.
    const std::optional<std::string>& record_key() const noexcept { return m_record_key; }
    /// Returns the optional query component.
    /// \return The query when present.
    const std::optional<std::string>& query() const noexcept { return m_query; }
    /// Returns the optional fragment component.
    /// \return The fragment when present.
    const std::optional<std::string>& fragment() const noexcept { return m_fragment; }
    /// Returns the canonical URI spelling.
    /// \return The canonical URI.
    const std::string& to_string() const noexcept { return m_canonical; }
    /// Reports whether a record key is present.
    /// \return `true` for a record URI.
    bool is_record() const noexcept { return m_record_key.has_value(); }
    /// Reports whether the authority is a DID.
    /// \return `true` for DID-based URIs.
    bool is_did_based() const noexcept { return m_did_based; }
    /// Reports whether the authority is a handle.
    /// \return `true` for handle-based URIs.
    bool is_handle_based() const noexcept { return !m_did_based; }

    /// Compares canonical URI spellings.
    friend bool operator==(const AtUri& left, const AtUri& right) noexcept {
        return left.m_canonical == right.m_canonical;
    }
    /// Returns the inverse of operator==.
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
    /// Parses a CIDv0 or CIDv1 string.
    /// \param value Candidate CID text.
    /// \return A valid CID or an InvalidArgument error.
    static Result<Cid> parse(std::string value);

    /// Returns the CID version.
    /// \return The CID version number.
    std::uint64_t version() const noexcept { return m_version; }
    /// Returns the multicodec code.
    /// \return The multicodec code.
    std::uint64_t codec() const noexcept { return m_codec; }
    /// Returns the multihash code.
    /// \return The multihash code.
    std::uint64_t multihash_code() const noexcept { return m_multihash_code; }
    /// Returns the digest length in bytes.
    /// \return The digest length.
    std::size_t digest_size() const noexcept { return m_digest_size; }
    /// Reports whether this is a dag-cbor record CID.
    /// \return `true` for an AT Protocol record CID.
    bool is_atproto_record() const noexcept;
    /// Reports whether this is a raw blob CID.
    /// \return `true` for an AT Protocol blob CID.
    bool is_atproto_blob() const noexcept;
    /// Reports whether this is any supported AT Protocol CID.
    /// \return `true` for a record or blob CID.
    bool is_atproto() const noexcept;
    /// Returns the canonical CID spelling.
    /// \return The canonical CID.
    const std::string& to_string() const noexcept { return m_canonical; }
    /// Returns the canonical binary CID bytes.
    /// \return The binary CID representation.
    const std::vector<std::uint8_t>& bytes() const noexcept { return m_bytes; }

    /// Compares canonical binary CID representations.
    friend bool operator==(const Cid& left, const Cid& right) noexcept {
        return left.m_bytes == right.m_bytes;
    }
    /// Returns the inverse of operator==.
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
    /// Creates a strong reference for a record URI and AT Protocol CID.
    /// \param uri Record URI, which must identify a record.
    /// \param cid Record CID, which must be an AT Protocol record CID.
    /// \return A strong reference or an InvalidArgument error.
    static Result<StrongRef> create(AtUri uri, Cid cid);

    /// Returns the referenced record URI.
    /// \return The record URI.
    const AtUri& uri() const noexcept { return m_uri; }
    /// Returns the referenced record CID.
    /// \return The record CID.
    const Cid& cid() const noexcept { return m_cid; }

private:
    StrongRef(AtUri uri, Cid cid) : m_uri(std::move(uri)), m_cid(std::move(cid)) {}

    AtUri m_uri;
    Cid m_cid;
};

}  // namespace bsky_cpp
