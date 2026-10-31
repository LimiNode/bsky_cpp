#include <bsky_cpp/atproto/identifiers.hpp>

#include <fstream>
#include <iostream>
#include <string>

#ifndef BSKY_FIXTURE_DIR
#error "BSKY_FIXTURE_DIR must point at the pinned upstream syntax fixtures"
#endif

namespace {

template <typename Parser>
bool run_fixture(const std::string& name, bool expect_valid, Parser parser,
                 std::size_t& passed, std::size_t& total) {
    std::ifstream input(std::string(BSKY_FIXTURE_DIR) + "/" + name);
    if (!input) {
        std::cerr << "cannot open fixture: " << name << '\n';
        return false;
    }
    bool ok = true;
    std::size_t file_passed = 0;
    std::size_t file_total = 0;
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty() || line.front() == '#') {
            continue;
        }
        ++total;
        ++file_total;
        const bool valid = static_cast<bool>(parser(line));
        if (valid == expect_valid) {
            ++passed;
            ++file_passed;
        } else {
            std::cerr << name << ": unexpected " << (valid ? "accept" : "reject")
                      << ": " << line << '\n';
            ok = false;
        }
    }
    std::cout << name << ": " << file_passed << '/' << file_total << " passed\n";
    return ok;
}

}  // namespace

int main() {
    std::size_t passed = 0;
    std::size_t total = 0;
    bool ok = true;

    ok = run_fixture("did_syntax_valid.txt", true,
                     [](const std::string& value) { return bsky_cpp::Did::parse(value); },
                     passed, total) && ok;
    ok = run_fixture("did_syntax_invalid.txt", false,
                     [](const std::string& value) { return bsky_cpp::Did::parse(value); },
                     passed, total) && ok;
    ok = run_fixture("handle_syntax_valid.txt", true,
                     [](const std::string& value) { return bsky_cpp::Handle::parse(value); },
                     passed, total) && ok;
    ok = run_fixture("handle_syntax_invalid.txt", false,
                     [](const std::string& value) { return bsky_cpp::Handle::parse(value); },
                     passed, total) && ok;
    ok = run_fixture("nsid_syntax_valid.txt", true,
                     [](const std::string& value) { return bsky_cpp::Nsid::parse(value); },
                     passed, total) && ok;
    ok = run_fixture("nsid_syntax_invalid.txt", false,
                     [](const std::string& value) { return bsky_cpp::Nsid::parse(value); },
                     passed, total) && ok;
    ok = run_fixture("recordkey_syntax_valid.txt", true,
                     [](const std::string& value) { return bsky_cpp::RecordKey::parse(value); },
                     passed, total) && ok;
    ok = run_fixture("recordkey_syntax_invalid.txt", false,
                     [](const std::string& value) { return bsky_cpp::RecordKey::parse(value); },
                     passed, total) && ok;
    ok = run_fixture("aturi_syntax_valid.txt", true,
                     [](const std::string& value) { return bsky_cpp::AtUri::parse(value); },
                     passed, total) && ok;
    ok = run_fixture("aturi_syntax_invalid.txt", false,
                     [](const std::string& value) { return bsky_cpp::AtUri::parse(value); },
                     passed, total) && ok;

    std::cout << "upstream syntax fixtures: " << passed << '/' << total << " passed\n";
    return ok ? 0 : 1;
}
