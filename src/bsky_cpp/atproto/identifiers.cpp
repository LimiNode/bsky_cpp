#include "bsky_cpp/atproto/identifiers.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace bsky_cpp {
namespace {

constexpr std::size_t kMaxDidLength = 2048U;
constexpr std::size_t kMaxHandleLength = 253U;
constexpr std::size_t kMaxNsidLength = 317U;
constexpr std::size_t kMaxRecordKeyLength = 512U;
constexpr std::size_t kMaxAtUriLength = 8192U;
constexpr std::size_t kMaxCidLength = 256U;
constexpr std::uint64_t kDagCborCodec = 0x71U;
constexpr std::uint64_t kRawCodec = 0x55U;
constexpr std::uint64_t kSha256Multihash = 0x12U;

Error invalid(std::string operation, std::string message) {
    return Error(ErrorCategory::InvalidArgument, std::move(message), std::move(operation));
}

bool is_alpha(char character) noexcept {
    return (character >= 'a' && character <= 'z') ||
           (character >= 'A' && character <= 'Z');
}

bool is_digit(char character) noexcept {
    return character >= '0' && character <= '9';
}

bool is_hex(char character) noexcept {
    return is_digit(character) || (character >= 'a' && character <= 'f') ||
           (character >= 'A' && character <= 'F');
}

int hex_value(char character) noexcept {
    if (character >= '0' && character <= '9') {
        return character - '0';
    }
    if (character >= 'a' && character <= 'f') {
        return character - 'a' + 10;
    }
    return character - 'A' + 10;
}

bool valid_percent(std::string_view value, std::size_t index) noexcept {
    return index + 2U < value.size() && value[index] == '%' &&
           is_hex(value[index + 1U]) && is_hex(value[index + 2U]);
}

bool is_uri_unreserved(char character) noexcept {
    return is_alpha(character) || is_digit(character) || character == '-' ||
           character == '.' || character == '_' || character == '~';
}

bool is_uri_subdelim(char character) noexcept {
    switch (character) {
    case '!': case '$': case '&': case '\'': case '(': case ')': case '*':
    case '+': case ',': case ';': case '=':
        return true;
    default:
        return false;
    }
}

bool is_valid_utf8(std::string_view value) noexcept {
    std::size_t index = 0U;
    while (index < value.size()) {
        const auto first = static_cast<unsigned char>(value[index]);
        if (first <= 0x7fU) {
            ++index;
            continue;
        }

        std::size_t continuation_count = 0U;
        std::uint32_t code_point = 0U;
        std::uint32_t minimum = 0U;
        if (first >= 0xc2U && first <= 0xdfU) {
            continuation_count = 1U;
            code_point = first & 0x1fU;
            minimum = 0x80U;
        } else if (first >= 0xe0U && first <= 0xefU) {
            continuation_count = 2U;
            code_point = first & 0x0fU;
            minimum = 0x800U;
        } else if (first >= 0xf0U && first <= 0xf4U) {
            continuation_count = 3U;
            code_point = first & 0x07U;
            minimum = 0x10000U;
        } else {
            return false;
        }
        if (continuation_count > value.size() - index - 1U) {
            return false;
        }
        for (std::size_t offset = 1U; offset <= continuation_count; ++offset) {
            const auto byte = static_cast<unsigned char>(value[index + offset]);
            if ((byte & 0xc0U) != 0x80U) {
                return false;
            }
            code_point = (code_point << 6U) | (byte & 0x3fU);
        }
        if (code_point < minimum || code_point > 0x10ffffU ||
            (code_point >= 0xd800U && code_point <= 0xdfffU)) {
            return false;
        }
        index += continuation_count + 1U;
    }
    return true;
}

std::optional<std::string> percent_decode(std::string_view value) {
    std::string decoded;
    decoded.reserve(value.size());
    for (std::size_t index = 0U; index < value.size(); ++index) {
        if (value[index] != '%') {
            decoded.push_back(value[index]);
            continue;
        }
        if (!valid_percent(value, index)) {
            return std::nullopt;
        }
        decoded.push_back(static_cast<char>((hex_value(value[index + 1U]) << 4) |
                                            hex_value(value[index + 2U])));
        index += 2U;
    }
    return decoded;
}

bool valid_json_pointer(std::string_view encoded) {
    auto decoded = percent_decode(encoded);
    if (!decoded.has_value() || !is_valid_utf8(*decoded) || decoded->empty() ||
        decoded->front() != '/') {
        return false;
    }
    for (std::size_t index = 0U; index < decoded->size(); ++index) {
        if ((*decoded)[index] == '~') {
            if (index + 1U >= decoded->size() ||
                ((*decoded)[index + 1U] != '0' && (*decoded)[index + 1U] != '1')) {
                return false;
            }
            ++index;
        }
    }
    return true;
}

bool valid_uri_component(std::string_view value, bool fragment) {
    if (fragment && (value.empty() || value.front() != '/')) {
        return false;
    }
    for (std::size_t index = 0U; index < value.size(); ++index) {
        const unsigned char byte = static_cast<unsigned char>(value[index]);
        if (value[index] == '%') {
            if (!valid_percent(value, index)) {
                return false;
            }
            index += 2U;
            continue;
        }
        if (byte < 0x21U || byte > 0x7eU) {
            return false;
        }
        const char character = static_cast<char>(byte);
        if (!(is_uri_unreserved(character) || is_uri_subdelim(character) ||
              character == ':' || character == '@' || character == '/' ||
              character == '?' || (fragment && (character == '[' || character == ']')))) {
            return false;
        }
    }
    return !fragment || valid_json_pointer(value);
}

bool valid_did_value(std::string_view value) noexcept {
    if (value.empty()) {
        return false;
    }
    for (std::size_t index = 0U; index < value.size(); ++index) {
        const char character = value[index];
        if (is_alpha(character) || is_digit(character) || character == '.' ||
            character == '-' || character == '_' || character == ':') {
            continue;
        }
        if (character == '%' && valid_percent(value, index)) {
            index += 2U;
            continue;
        }
        return false;
    }
    return true;
}

bool valid_dns_label(std::string_view label, bool allow_uppercase) noexcept {
    if (label.empty() || label.size() > 63U ||
        ((!is_alpha(label.front())) && (!is_digit(label.front()))) ||
        ((!is_alpha(label.back())) && (!is_digit(label.back())))) {
        return false;
    }
    for (const char character : label) {
        const bool alpha = is_alpha(character);
        if (alpha && allow_uppercase) {
            continue;
        }
        if (!(is_digit(character) || (character >= 'a' && character <= 'z') ||
              character == '-')) {
            return false;
        }
    }
    return true;
}

std::vector<std::string_view> split(std::string_view value, char separator) {
    std::vector<std::string_view> parts;
    std::size_t start = 0U;
    while (start <= value.size()) {
        const std::size_t end = value.find(separator, start);
        if (end == std::string_view::npos) {
            parts.push_back(value.substr(start));
            break;
        }
        parts.push_back(value.substr(start, end - start));
        start = end + 1U;
    }
    return parts;
}

int base32_value(char character) noexcept {
    if (character >= 'A' && character <= 'Z') {
        character = static_cast<char>(character - 'A' + 'a');
    }
    if (character >= 'a' && character <= 'z') {
        const int value = character - 'a';
        return value < 26 ? value : -1;
    }
    if (character >= '2' && character <= '7') {
        return character - '2' + 26;
    }
    return -1;
}

std::vector<std::uint8_t> decode_base32(std::string_view value) {
    const std::size_t remainder = value.size() % 8U;
    if (remainder == 1U || remainder == 3U || remainder == 6U) {
        return {};
    }
    std::vector<std::uint8_t> output;
    std::uint32_t buffer = 0U;
    int bits = 0;
    for (const char character : value) {
        const int digit = base32_value(character);
        if (digit < 0) {
            return {};
        }
        buffer = (buffer << 5U) | static_cast<std::uint32_t>(digit);
        bits += 5;
        if (bits >= 8) {
            bits -= 8;
            output.push_back(static_cast<std::uint8_t>((buffer >> bits) & 0xffU));
            if (bits == 0) {
                buffer = 0U;
            } else {
                buffer &= (1U << bits) - 1U;
            }
        }
    }
    if (bits > 0 && (buffer & ((1U << bits) - 1U)) != 0U) {
        return {};
    }
    return output;
}

std::string encode_base32(const std::vector<std::uint8_t>& value) {
    static constexpr char alphabet[] = "abcdefghijklmnopqrstuvwxyz234567";
    std::string output;
    std::uint32_t buffer = 0U;
    int bits = 0;
    for (const std::uint8_t byte : value) {
        buffer = (buffer << 8U) | byte;
        bits += 8;
        while (bits >= 5) {
            bits -= 5;
            output.push_back(alphabet[(buffer >> bits) & 31U]);
        }
    }
    if (bits > 0) {
        output.push_back(alphabet[(buffer << (5 - bits)) & 31U]);
    }
    return output;
}

const char* base58_alphabet() noexcept {
    return "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
}

std::vector<std::uint8_t> decode_base58(std::string_view value) {
    const char* alphabet = base58_alphabet();
    std::array<int, 128> indexes{};
    indexes.fill(-1);
    for (int index = 0; alphabet[index] != '\0'; ++index) {
        indexes[static_cast<unsigned char>(alphabet[index])] = index;
    }
    std::vector<std::uint8_t> bytes(1U, 0U);
    for (const char character : value) {
        if (static_cast<unsigned char>(character) >= indexes.size() ||
            indexes[static_cast<unsigned char>(character)] < 0) {
            return {};
        }
        int carry = indexes[static_cast<unsigned char>(character)];
        for (auto iterator = bytes.rbegin(); iterator != bytes.rend(); ++iterator) {
            carry += static_cast<int>(*iterator) * 58;
            *iterator = static_cast<std::uint8_t>(carry & 0xff);
            carry >>= 8;
        }
        while (carry != 0) {
            bytes.insert(bytes.begin(), static_cast<std::uint8_t>(carry & 0xff));
            carry >>= 8;
        }
    }
    std::size_t leading_zeroes = 0U;
    while (leading_zeroes < value.size() && value[leading_zeroes] == '1') {
        ++leading_zeroes;
    }
    if (leading_zeroes > 0U) {
        bytes.insert(bytes.begin(), leading_zeroes - (bytes.size() > 0U && bytes[0] == 0U), 0U);
    }
    while (bytes.size() > 1U && bytes.front() == 0U && leading_zeroes == 0U) {
        bytes.erase(bytes.begin());
    }
    return bytes;
}

std::string encode_base58(const std::vector<std::uint8_t>& value) {
    const char* alphabet = base58_alphabet();
    std::vector<std::uint8_t> digits(1U, 0U);
    for (const std::uint8_t byte : value) {
        int carry = byte;
        for (auto iterator = digits.rbegin(); iterator != digits.rend(); ++iterator) {
            carry += static_cast<int>(*iterator) * 256;
            *iterator = static_cast<std::uint8_t>(carry % 58);
            carry /= 58;
        }
        while (carry != 0) {
            digits.insert(digits.begin(), static_cast<std::uint8_t>(carry % 58));
            carry /= 58;
        }
    }
    std::string output;
    for (const std::uint8_t byte : value) {
        if (byte != 0U) {
            break;
        }
        output.push_back('1');
    }
    for (const std::uint8_t digit : digits) {
        output.push_back(alphabet[digit]);
    }
    return output;
}

std::vector<std::uint8_t> decode_base16(std::string_view value) {
    if (value.empty() || (value.size() % 2U) != 0U) {
        return {};
    }
    std::vector<std::uint8_t> output;
    output.reserve(value.size() / 2U);
    for (std::size_t index = 0U; index < value.size(); index += 2U) {
        if (!is_hex(value[index]) || !is_hex(value[index + 1U])) {
            return {};
        }
        output.push_back(static_cast<std::uint8_t>((hex_value(value[index]) << 4) |
                                                   hex_value(value[index + 1U])));
    }
    return output;
}

bool read_varint(const std::vector<std::uint8_t>& bytes, std::size_t& position,
                 std::uint64_t& value) noexcept {
    value = 0U;
    for (unsigned int shift = 0U; shift < 64U; shift += 7U) {
        if (position >= bytes.size()) {
            return false;
        }
        const std::uint8_t byte = bytes[position++];
        if (shift == 63U && byte > 1U) {
            return false;
        }
        value |= static_cast<std::uint64_t>(byte & 0x7fU) << shift;
        if ((byte & 0x80U) == 0U) {
            if (shift > 0U && byte == 0U) {
                return false;
            }
            return true;
        }
    }
    return false;
}

void append_varint(std::vector<std::uint8_t>& output, std::uint64_t value) {
    do {
        std::uint8_t byte = static_cast<std::uint8_t>(value & 0x7fU);
        value >>= 7U;
        if (value != 0U) {
            byte = static_cast<std::uint8_t>(byte | 0x80U);
        }
        output.push_back(byte);
    } while (value != 0U);
}

struct ParsedCid {
    std::vector<std::uint8_t> bytes;
    std::uint64_t version;
    std::uint64_t codec;
    std::uint64_t hash_code;
    std::size_t digest_size;
};

std::optional<ParsedCid> parse_cid_bytes(std::vector<std::uint8_t> bytes) {
    std::size_t position = 0U;
    std::uint64_t version = 0U;
    std::uint64_t codec = 0U;
    std::uint64_t hash_code = 0U;
    std::uint64_t digest_size = 0U;
    if (!read_varint(bytes, position, version) || version != 1U ||
        !read_varint(bytes, position, codec) || codec == 0U ||
        !read_varint(bytes, position, hash_code) || hash_code == 0U ||
        !read_varint(bytes, position, digest_size) || digest_size == 0U ||
        digest_size > 4096U || position > bytes.size() ||
        digest_size > bytes.size() - position ||
        position + digest_size != bytes.size()) {
        return std::nullopt;
    }
    std::vector<std::uint8_t> canonical;
    canonical.reserve(bytes.size());
    append_varint(canonical, version);
    append_varint(canonical, codec);
    append_varint(canonical, hash_code);
    append_varint(canonical, digest_size);
    canonical.insert(canonical.end(), bytes.begin() + static_cast<std::ptrdiff_t>(position),
                     bytes.end());
    return ParsedCid{std::move(canonical), version, codec, hash_code,
                     static_cast<std::size_t>(digest_size)};
}

}  // namespace

Did::Did(std::string canonical, std::string method, std::string value, bool supported)
    : m_canonical(std::move(canonical)),
      m_method(std::move(method)),
      m_value(std::move(value)),
      m_supported(supported) {}

Result<Did> Did::parse(std::string value) {
    if (value.size() > kMaxDidLength || value.rfind("did:", 0U) != 0U) {
        return Result<Did>::failure(invalid("did.parse", "invalid DID prefix or length"));
    }
    const std::size_t method_end = value.find(':', 4U);
    if (method_end == std::string::npos || method_end == 4U || method_end + 1U >= value.size()) {
        return Result<Did>::failure(invalid("did.parse", "DID requires method and value"));
    }
    const std::string method = value.substr(4U, method_end - 4U);
    for (const char character : method) {
        if (!(character >= 'a' && character <= 'z')) {
            return Result<Did>::failure(invalid("did.parse", "invalid DID method"));
        }
    }
    const std::string method_value = value.substr(method_end + 1U);
    if (value.back() == ':' || value.back() == '%' || !valid_did_value(method_value)) {
        return Result<Did>::failure(invalid("did.parse", "invalid DID method-specific id"));
    }
    const bool supported = method == "plc" || method == "web";
    return Result<Did>::success(
        Did(std::move(value), method, method_value, supported));
}

Result<Handle> Handle::parse(std::string value) {
    if (value.size() > kMaxHandleLength || value.empty() || value.front() == '@') {
        return Result<Handle>::failure(invalid("handle.parse", "invalid handle length or UI prefix"));
    }
    const auto labels = split(value, '.');
    if (labels.size() < 2U || value.back() == '.') {
        return Result<Handle>::failure(invalid("handle.parse", "handle requires DNS labels"));
    }
    std::string canonical;
    canonical.reserve(value.size());
    for (std::size_t index = 0U; index < labels.size(); ++index) {
        if (!valid_dns_label(labels[index], true)) {
            return Result<Handle>::failure(invalid("handle.parse", "invalid handle label"));
        }
        if (index != 0U) {
            canonical.push_back('.');
        }
        for (const char character : labels[index]) {
            canonical.push_back((character >= 'A' && character <= 'Z')
                                    ? static_cast<char>(character - 'A' + 'a')
                                    : character);
        }
    }
    const std::string_view suffix = labels.back();
    if (std::all_of(suffix.begin(), suffix.end(), is_digit) ||
        !is_alpha(suffix.front())) {
        return Result<Handle>::failure(invalid("handle.parse", "numeric final label"));
    }
    return Result<Handle>::success(Handle(std::move(canonical)));
}

Result<Nsid> Nsid::parse(std::string value) {
    if (value.empty() || value.size() > kMaxNsidLength) {
        return Result<Nsid>::failure(invalid("nsid.parse", "invalid NSID length"));
    }
    const auto labels = split(value, '.');
    if (labels.size() < 3U) {
        return Result<Nsid>::failure(invalid("nsid.parse", "NSID requires at least three parts"));
    }
    if (labels.front().empty() || !is_alpha(labels.front().front())) {
        return Result<Nsid>::failure(invalid("nsid.parse", "NSID authority must start with a letter"));
    }
    for (std::size_t index = 0U; index < labels.size(); ++index) {
        const auto label = labels[index];
        if (!valid_dns_label(label, true)) {
            return Result<Nsid>::failure(invalid("nsid.parse", "invalid NSID part"));
        }
        if (index + 1U == labels.size() &&
            (!is_alpha(label.front()) ||
             std::any_of(label.begin(), label.end(), [](char character) {
                 return !(is_alpha(character) || is_digit(character));
             }))) {
            return Result<Nsid>::failure(invalid("nsid.parse", "invalid NSID name"));
        }
    }
    return Result<Nsid>::success(Nsid(std::move(value)));
}

Result<RecordKey> RecordKey::parse(std::string value) {
    if (value.empty() || value.size() > kMaxRecordKeyLength || value.front() == '/' ||
        value.back() == '/') {
        return Result<RecordKey>::failure(invalid("record_key.parse", "invalid record key length"));
    }
    for (const char character : value) {
        if (is_alpha(character) || is_digit(character) || character == '.' ||
            character == '-' || character == '_' || character == '~' || character == ':') {
            continue;
        }
        return Result<RecordKey>::failure(invalid("record_key.parse", "invalid record key character"));
    }
    if (value == "." || value == "..") {
        return Result<RecordKey>::failure(invalid("record_key.parse", "dot record keys are not allowed"));
    }
    return Result<RecordKey>::success(RecordKey(std::move(value)));
}

AtUri::AtUri(std::string canonical, std::string authority, std::string collection,
             std::optional<std::string> record_key, std::optional<std::string> query,
             std::optional<std::string> fragment, bool did_based)
    : m_canonical(std::move(canonical)),
      m_authority(std::move(authority)),
      m_collection(std::move(collection)),
      m_record_key(std::move(record_key)),
      m_query(std::move(query)),
      m_fragment(std::move(fragment)),
      m_did_based(did_based) {}

Result<AtUri> AtUri::parse(std::string value) {
    if (value.size() > kMaxAtUriLength || value.rfind("at://", 0U) != 0U) {
        return Result<AtUri>::failure(invalid("aturi.parse", "invalid AT URI prefix or delimiters"));
    }

    const std::size_t fragment_pos = value.find('#', 5U);
    const std::size_t query_pos = value.find('?', 5U);
    if (query_pos != std::string::npos && fragment_pos != std::string::npos &&
        query_pos > fragment_pos) {
        return Result<AtUri>::failure(invalid("aturi.parse", "query must precede fragment"));
    }
    const std::size_t path_end = std::min(
        query_pos == std::string::npos ? value.size() : query_pos,
        fragment_pos == std::string::npos ? value.size() : fragment_pos);
    const std::size_t authority_end = value.find('/', 5U);
    const bool has_path = authority_end != std::string::npos && authority_end <= path_end;
    const std::size_t authority_limit = has_path ? authority_end : path_end;
    if (authority_limit == 5U) {
        return Result<AtUri>::failure(invalid("aturi.parse", "AT URI requires authority"));
    }
    const std::string raw_authority = value.substr(5U, authority_limit - 5U);
    if (raw_authority.find('@') != std::string::npos) {
        return Result<AtUri>::failure(invalid("aturi.parse", "userinfo is not allowed"));
    }

    std::string collection_string;
    std::optional<std::string> record_key;
    if (has_path) {
        const std::string path = value.substr(authority_end + 1U, path_end - authority_end - 1U);
        if (path.empty() || path.back() == '/') {
            return Result<AtUri>::failure(invalid("aturi.parse", "invalid AT URI path"));
        }
        const std::size_t collection_end = path.find('/');
        const std::string raw_collection = path.substr(0U, collection_end);
        auto collection = Nsid::parse(raw_collection);
        if (!collection) {
            return Result<AtUri>::failure(std::move(collection.error()));
        }
        collection_string = collection.value().to_string();
        if (collection_end != std::string::npos) {
            if (path.find('/', collection_end + 1U) != std::string::npos) {
                return Result<AtUri>::failure(invalid("aturi.parse", "invalid AT URI path"));
            }
            auto parsed_key = RecordKey::parse(path.substr(collection_end + 1U));
            if (!parsed_key) {
                return Result<AtUri>::failure(std::move(parsed_key.error()));
            }
            record_key = parsed_key.value().to_string();
        }
    }

    std::optional<std::string> query;
    if (query_pos != std::string::npos) {
        const std::size_t query_end = fragment_pos == std::string::npos ? value.size() : fragment_pos;
        if (query_pos + 1U == query_end ||
            !valid_uri_component(value.substr(query_pos + 1U,
                                              query_end - query_pos - 1U), false)) {
            return Result<AtUri>::failure(invalid("aturi.parse", "invalid AT URI query"));
        }
        query = value.substr(query_pos + 1U, query_end - query_pos - 1U);
    }
    std::optional<std::string> fragment;
    if (fragment_pos != std::string::npos) {
        if (!valid_uri_component(value.substr(fragment_pos + 1U), true)) {
            return Result<AtUri>::failure(invalid("aturi.parse", "invalid AT URI fragment"));
        }
        fragment = value.substr(fragment_pos + 1U);
    }

    bool did_based = false;
    std::string authority;
    if (raw_authority.rfind("did:", 0U) == 0U) {
        auto did = Did::parse(raw_authority);
        if (!did) {
            return Result<AtUri>::failure(std::move(did.error()));
        }
        authority = did.value().to_string();
        did_based = true;
    } else {
        auto handle = Handle::parse(raw_authority);
        if (!handle) {
            return Result<AtUri>::failure(std::move(handle.error()));
        }
        authority = handle.value().to_string();
    }
    std::string canonical = "at://" + authority;
    if (!collection_string.empty()) {
        canonical += "/" + collection_string;
    }
    if (record_key.has_value()) {
        canonical += "/" + *record_key;
    }
    if (query.has_value()) {
        canonical += "?" + *query;
    }
    if (fragment.has_value()) {
        canonical += "#" + *fragment;
    }
    return Result<AtUri>::success(AtUri(std::move(canonical), std::move(authority),
                                        std::move(collection_string), std::move(record_key),
                                        std::move(query), std::move(fragment), did_based));
}

Cid::Cid(std::string canonical, std::vector<std::uint8_t> bytes, std::uint64_t version,
         std::uint64_t codec, std::uint64_t multihash_code, std::size_t digest_size)
    : m_canonical(std::move(canonical)),
      m_bytes(std::move(bytes)),
      m_version(version),
      m_codec(codec),
      m_multihash_code(multihash_code),
      m_digest_size(digest_size) {}

Result<Cid> Cid::parse(std::string value) {
    if (value.empty() || value.size() > kMaxCidLength) {
        return Result<Cid>::failure(invalid("cid.parse", "invalid CID length"));
    }
    if (value.rfind("Qm", 0U) == 0U) {
        const auto bytes = decode_base58(value);
        if (bytes.size() != 34U || bytes[0] != kSha256Multihash || bytes[1] != 32U) {
            return Result<Cid>::failure(invalid("cid.parse", "invalid CIDv0 multihash"));
        }
        return Result<Cid>::success(Cid(encode_base58(bytes), bytes, 0U, 0U,
                                       kSha256Multihash, 32U));
    }
    if (value.size() < 2U) {
        return Result<Cid>::failure(invalid("cid.parse", "missing CID multibase prefix"));
    }
    std::vector<std::uint8_t> bytes;
    const char prefix = value.front();
    if (prefix == 'b' || prefix == 'B') {
        bytes = decode_base32(value.substr(1U));
    } else if (prefix == 'z') {
        bytes = decode_base58(value.substr(1U));
    } else if (prefix == 'f' || prefix == 'F') {
        bytes = decode_base16(value.substr(1U));
    } else {
        return Result<Cid>::failure(invalid("cid.parse", "unsupported CID multibase"));
    }
    if (bytes.empty() || bytes.size() > 4096U) {
        return Result<Cid>::failure(invalid("cid.parse", "invalid CID byte payload"));
    }
    auto parsed = parse_cid_bytes(std::move(bytes));
    if (!parsed.has_value()) {
        return Result<Cid>::failure(invalid("cid.parse", "invalid CIDv1 structure"));
    }
    const std::string canonical = "b" + encode_base32(parsed->bytes);
    return Result<Cid>::success(Cid(canonical,
                                    std::move(parsed->bytes), parsed->version,
                                    parsed->codec, parsed->hash_code,
                                    parsed->digest_size));
}

bool Cid::is_atproto_record() const noexcept {
    return m_version == 1U && m_codec == kDagCborCodec &&
           m_multihash_code == kSha256Multihash && m_digest_size == 32U;
}

bool Cid::is_atproto_blob() const noexcept {
    return m_version == 1U && m_codec == kRawCodec &&
           m_multihash_code == kSha256Multihash && m_digest_size == 32U;
}

bool Cid::is_atproto() const noexcept {
    return is_atproto_record() || is_atproto_blob();
}

Result<StrongRef> StrongRef::create(AtUri uri, Cid cid) {
    if (!uri.is_record() || uri.query().has_value() || uri.fragment().has_value()) {
        return Result<StrongRef>::failure(
            invalid("strong_ref.create", "StrongRef requires a query-free record AT URI"));
    }
    if (!cid.is_atproto_record()) {
        return Result<StrongRef>::failure(
            invalid("strong_ref.create", "StrongRef requires an AT Protocol CID"));
    }
    return Result<StrongRef>::success(StrongRef(std::move(uri), std::move(cid)));
}

}  // namespace bsky_cpp
