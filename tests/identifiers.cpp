#include "test_support.hpp"

#include <bsky_cpp/identifiers.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace {

std::string base32(const std::vector<std::uint8_t>& bytes) {
    static constexpr char alphabet[] = "abcdefghijklmnopqrstuvwxyz234567";
    std::string result;
    std::uint32_t buffer = 0;
    int bits = 0;
    for (const auto byte : bytes) {
        buffer = (buffer << 8U) | byte;
        bits += 8;
        while (bits >= 5) {
            bits -= 5;
            result.push_back(alphabet[(buffer >> bits) & 31U]);
        }
    }
    if (bits != 0) {
        result.push_back(alphabet[(buffer << (5 - bits)) & 31U]);
    }
    return result;
}

std::string cid_v1(std::uint8_t codec, std::uint8_t hash_code = 0x12U) {
    std::vector<std::uint8_t> bytes{1U, codec, hash_code, 32U};
    for (std::uint8_t index = 0; index < 32U; ++index) {
        bytes.push_back(index);
    }
    return "b" + base32(bytes);
}

bool check_error(const bsky_cpp::Result<bsky_cpp::Did>& result) {
    return !result && result.error().category() == bsky_cpp::ErrorCategory::InvalidArgument;
}

}  // namespace

int main() {
    using bsky_cpp::AtUri;
    using bsky_cpp::Cid;
    using bsky_cpp::Did;
    using bsky_cpp::ErrorCategory;
    using bsky_cpp::Handle;
    using bsky_cpp::Nsid;
    using bsky_cpp::RecordKey;
    using bsky_cpp::StrongRef;

    auto did = Did::parse("did:plc:z72i7hdynmk6r22z27s6tvur");
    if (!BSKY_TEST_CHECK(did && did.value().is_supported_method() &&
                         did.value().method() == "plc")) return 1;
    auto unsupported = Did::parse("did:example:abc_123");
    if (!BSKY_TEST_CHECK(unsupported && !unsupported.value().is_supported_method())) return 1;
    if (!BSKY_TEST_CHECK(check_error(Did::parse("did:PLc:value")))) return 1;
    if (!BSKY_TEST_CHECK(check_error(Did::parse("did:m123:val")))) return 1;
    if (!BSKY_TEST_CHECK(check_error(Did::parse("did:plc:")))) return 1;
    if (!BSKY_TEST_CHECK(check_error(Did::parse("did:method:val:")))) return 1;
    if (!BSKY_TEST_CHECK(check_error(Did::parse("did:plc:bad%2")))) return 1;

    auto handle = Handle::parse("Alice.Example.COM");
    if (!BSKY_TEST_CHECK(handle && handle.value().to_string() == "alice.example.com")) return 1;
    if (!BSKY_TEST_CHECK(!Handle::parse("@alice.example"))) return 1;
    if (!BSKY_TEST_CHECK(!Handle::parse("alice.-example"))) return 1;
    if (!BSKY_TEST_CHECK(!Handle::parse("alice.123"))) return 1;
    if (!BSKY_TEST_CHECK(!Handle::parse("thing.0aa"))) return 1;
    if (!BSKY_TEST_CHECK(!Handle::parse("alice..example"))) return 1;

    auto nsid = Nsid::parse("com.atproto.repo.createRecord");
    if (!BSKY_TEST_CHECK(nsid && nsid.value().to_string() == "com.atproto.repo.createRecord")) return 1;
    if (!BSKY_TEST_CHECK(!Nsid::parse("com.atproto"))) return 1;
    if (!BSKY_TEST_CHECK(!Nsid::parse("com.atproto.bad_underscore"))) return 1;

    auto key = RecordKey::parse("3jfc-safe");
    if (!BSKY_TEST_CHECK(key && key.value().to_string() == "3jfc-safe")) return 1;
    if (!BSKY_TEST_CHECK(!RecordKey::parse("bad%2fkey"))) return 1;
    if (!BSKY_TEST_CHECK(!RecordKey::parse("."))) return 1;
    if (!BSKY_TEST_CHECK(!RecordKey::parse(".."))) return 1;

    auto handle_uri = AtUri::parse("at://Alice.Example.COM/com.atproto.repo.create/3jfc");
    if (!BSKY_TEST_CHECK(handle_uri && handle_uri.value().is_handle_based() &&
                         handle_uri.value().is_record() &&
                         handle_uri.value().authority() == "alice.example.com" &&
                         handle_uri.value().to_string() ==
                             "at://alice.example.com/com.atproto.repo.create/3jfc")) return 1;
    auto did_uri = AtUri::parse("at://did:plc:z72i7hdynmk6r22z27s6tvur/com.atproto.repo");
    if (!BSKY_TEST_CHECK(did_uri && did_uri.value().is_did_based() && !did_uri.value().is_record())) return 1;
    auto authority_uri = AtUri::parse("at://did:plc:z72i7hdynmk6r22z27s6tvur#/profile");
    if (!BSKY_TEST_CHECK(authority_uri && authority_uri.value().collection().empty() &&
                         authority_uri.value().fragment().value() == "/profile" &&
                         authority_uri.value().to_string() ==
                             "at://did:plc:z72i7hdynmk6r22z27s6tvur#/profile")) return 1;
    auto query_uri = AtUri::parse("at://did:plc:z72i7hdynmk6r22z27s6tvur?foo=bar");
    if (!BSKY_TEST_CHECK(query_uri && query_uri.value().query().value() == "foo=bar")) return 1;
    if (!BSKY_TEST_CHECK(!AtUri::parse("at://alice.example.com#profile"))) return 1;
    if (!BSKY_TEST_CHECK(!AtUri::parse("at://alice.example.com?x=%ZZ"))) return 1;
    if (!BSKY_TEST_CHECK(!AtUri::parse("at://alice.example.com?x=\xF0\x9F\x92\xA9"))) return 1;
    if (!BSKY_TEST_CHECK(static_cast<bool>(AtUri::parse(
            "at://alice.example.com?x=%F0%9F%92%A9#/profile")))) return 1;
    auto web_authority = AtUri::parse(
        "at://did:web:localhost%3A3000/app.bsky.actor.profile/self");
    if (!BSKY_TEST_CHECK(web_authority && web_authority.value().is_did_based())) return 1;
    if (!BSKY_TEST_CHECK(!AtUri::parse(
            "at://did:web:localhost%3/app.bsky.actor.profile/self"))) return 1;
    if (!BSKY_TEST_CHECK(!AtUri::parse("at://alice.example.com/com.atproto.repo/x/y"))) return 1;
    if (!BSKY_TEST_CHECK(!AtUri::parse("at://user:pass@alice.example.com/com.atproto.repo/x"))) return 1;
    if (!BSKY_TEST_CHECK(!AtUri::parse("at://alice.example.com/com.atproto.repo/x?"))) return 1;
    const std::string long_did = "did:example:" + std::string(2030U, 'a');
    const std::string long_uri = "at://" + long_did + "/com.example.foo/self";
    if (!BSKY_TEST_CHECK(long_uri.size() > 2048U && long_uri.size() <= 8192U &&
                         AtUri::parse(long_uri))) return 1;
    const std::string oversized_uri = "at://alice.example.com?x=" + std::string(8200U, 'a');
    if (!BSKY_TEST_CHECK(oversized_uri.size() > 8192U && !AtUri::parse(oversized_uri))) return 1;

    const std::string at_cid_text = cid_v1(0x71U);
    auto at_cid = Cid::parse(at_cid_text);
    if (!BSKY_TEST_CHECK(at_cid && at_cid.value().version() == 1U &&
                         at_cid.value().codec() == 0x71U && at_cid.value().is_atproto() &&
                         at_cid.value().to_string() == at_cid_text)) return 1;
    auto general_cid = Cid::parse(cid_v1(0x55U));
    if (!BSKY_TEST_CHECK(general_cid && general_cid.value().is_atproto_blob() &&
                         general_cid.value().is_atproto() &&
                         !general_cid.value().is_atproto_record())) return 1;
    auto wrong_hash = Cid::parse(cid_v1(0x71U, 0x13U));
    if (!BSKY_TEST_CHECK(wrong_hash && !wrong_hash.value().is_atproto())) return 1;
    auto uppercase_base32 = Cid::parse("B" + at_cid_text.substr(1));
    if (!BSKY_TEST_CHECK(uppercase_base32 && uppercase_base32.value().to_string() == at_cid_text)) return 1;
    if (!BSKY_TEST_CHECK(!Cid::parse(at_cid_text + "a"))) return 1;
    if (!BSKY_TEST_CHECK(!Cid::parse("bafyreia"))) return 1;
    if (!BSKY_TEST_CHECK(!Cid::parse("b!invalid"))) return 1;
    if (!BSKY_TEST_CHECK(!Cid::parse("b" + base32({1U, 0x71U, 0x12U, 0x80U})))) return 1;

    auto cid_v0 = Cid::parse("QmNLfbof5rLekrACjeuLk9JmGZD2HDBHCU4z16iYKmx5SE");
    if (!BSKY_TEST_CHECK(cid_v0 && cid_v0.value().version() == 0U &&
                         !cid_v0.value().is_atproto())) return 1;

    auto strong = StrongRef::create(handle_uri.value(), at_cid.value());
    if (!BSKY_TEST_CHECK(strong && strong.value().uri() == handle_uri.value())) return 1;
    if (!BSKY_TEST_CHECK(!StrongRef::create(did_uri.value(), at_cid.value()))) return 1;
    auto query_record = AtUri::parse("at://alice.example.com/com.atproto.repo.create/3jfc?view=1");
    if (!BSKY_TEST_CHECK(query_record &&
                         !StrongRef::create(query_record.value(), at_cid.value()))) return 1;
    if (!BSKY_TEST_CHECK(!StrongRef::create(handle_uri.value(), general_cid.value()))) return 1;
    return 0;
}
