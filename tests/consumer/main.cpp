#include <bsky_cpp.hpp>

#ifdef BSKY_CONSUMER_RUNTIME
#include <bsky_cpp/runtime/runtime.hpp>
#endif

#include <string>
#include <chrono>
#include <atomic>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

#ifdef BSKY_CONSUMER_RUNTIME
class ConsumerExecutor final : public bsky_cpp::runtime::IExecutor {
public:
    bool post(bsky_cpp::runtime::MoveOnlyFunction<void()> task) override {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_tasks.emplace_back(std::move(task));
        return true;
    }

    void drain() {
        for (;;) {
            bsky_cpp::runtime::MoveOnlyFunction<void()> task;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (m_tasks.empty()) return;
                task = std::move(m_tasks.front());
                m_tasks.erase(m_tasks.begin());
            }
            task();
        }
    }

private:
    std::mutex m_mutex;
    std::vector<bsky_cpp::runtime::MoveOnlyFunction<void()>> m_tasks;
};
#endif

int main() {
    bsky_cpp::Result<int> result(42);
    if (!result.has_value() || result.value() != 42) {
        return 1;
    }

    bsky_cpp::Error error(bsky_cpp::ErrorCategory::Internal, "consumer error");
    error.add_metadata("api_key", "must be redacted");
    if (error.metadata().size() != 1 ||
        error.metadata().front().value != "<redacted>") {
        return 1;
    }
    if (std::string(bsky_cpp::error_category_name(error.category())) != "internal") {
        return 1;
    }

    bsky_cpp::CancellationSource source;
    const bsky_cpp::CancellationToken token = source.token();
    if (token.is_cancellation_requested() || !source.request_cancellation() ||
        !token.is_cancellation_requested()) {
        return 1;
    }

    auto did = bsky_cpp::Did::parse("did:plc:z72i7hdynmk6r22z27s6tvur");
    auto handle = bsky_cpp::Handle::parse("Alice.Example.COM");
    auto uri = bsky_cpp::AtUri::parse(
        "at://alice.example.com/com.atproto.repo.create/3jfc");
    auto cid = bsky_cpp::Cid::parse(
        "bafyreiaaaebagbafaydqqcikbmga2dqpcaireeyuculbogazdinryhi6d4");
    auto blob_cid = bsky_cpp::Cid::parse(
        "bafkreiaaaebagbafaydqqcikbmga2dqpcaireeyuculbogazdinryhi6d4");
    if (!did || !handle || !uri || !cid || !cid.value().is_atproto_record() ||
        !blob_cid || !blob_cid.value().is_atproto_blob()) {
        return 1;
    }
    auto strong_ref = bsky_cpp::StrongRef::create(uri.value(), cid.value());
    if (!strong_ref) {
        return 1;
    }
    auto request = bsky_cpp::TransportRequest::create(
        bsky_cpp::HttpMethod::Get, "https://example.com/health");
    auto response = bsky_cpp::TransportResponse::create(204U);
    if (!request || !response) {
        return 1;
    }
#ifdef BSKY_CONSUMER_RUNTIME
    if (std::string(bsky_cpp::error_category_name(
            bsky_cpp::ErrorCategory::QuiescencePending)) != "quiescence_pending" ||
        std::string(bsky_cpp::error_category_name(
            bsky_cpp::ErrorCategory::ShutdownTimedOut)) != "shutdown_timed_out") return 1;
    bsky_cpp::runtime::Runtime runtime;
    if (!runtime.start()) return 1;
    std::atomic<bool> delivered{false};
    auto operation = runtime.submit([](bsky_cpp::CancellationToken) {
        return bsky_cpp::Result<void>::success();
    }, [&](bsky_cpp::Result<void> completion) { delivered = completion.has_value(); });
    if (!operation || !operation.value().wait_for_completion(std::chrono::seconds(1)) ||
        !operation.value().await_callback_quiescence(std::chrono::seconds(1)) ||
        !delivered || !runtime.shutdown(std::chrono::seconds(1))) {
        return 1;
    }

    bsky_cpp::runtime::Runtime typed_runtime;
    if (!typed_runtime.start()) return 1;
    std::atomic<bool> typed_delivered{false};
    auto typed_operation = typed_runtime.submit_typed<std::unique_ptr<int>>(
        [](bsky_cpp::CancellationToken) {
            return bsky_cpp::Result<std::unique_ptr<int>>::success(
                std::make_unique<int>(42));
        },
        [&typed_delivered](bsky_cpp::Result<std::unique_ptr<int>> completion) {
            typed_delivered = completion && completion.value() &&
                              *completion.value() == 42;
        });
    if (!typed_operation ||
        !typed_operation.value().wait_for_completion(std::chrono::seconds(1)) ||
        !typed_operation.value().await_callback_quiescence(std::chrono::seconds(1)) ||
        !typed_delivered || !typed_runtime.shutdown(std::chrono::seconds(1))) {
        return 1;
    }

    auto injected_executor = std::make_shared<ConsumerExecutor>();
    bsky_cpp::runtime::Runtime injected_runtime(
        {bsky_cpp::runtime::RuntimeMode::External, 2U, {}, injected_executor});
    if (!injected_runtime.start()) return 1;
    std::atomic<bool> injected_delivered{false};
    auto injected_operation = injected_runtime.submit(
        [](bsky_cpp::CancellationToken) {
            return bsky_cpp::Result<void>::success();
        },
        [&injected_delivered](bsky_cpp::Result<void> completion) {
            injected_delivered = completion.has_value();
        });
    if (!injected_operation || !injected_runtime.process_once()) return 1;
    injected_executor->drain();
    if (!injected_operation.value().await_callback_quiescence(std::chrono::seconds(1)) ||
        !injected_delivered || !injected_runtime.shutdown(std::chrono::seconds(1))) {
        return 1;
    }

    auto bridge_executor = std::make_shared<ConsumerExecutor>();
    bsky_cpp::runtime::Runtime bridge_runtime(
        {bsky_cpp::runtime::RuntimeMode::Managed, 4U, {}, bridge_executor, 2U});
    if (!bridge_runtime.start()) return 1;
    auto bridge_transport = std::make_shared<bsky_cpp::FakeTransport>(
        [](const bsky_cpp::TransportRequest&, bsky_cpp::CancellationToken) {
            return bsky_cpp::TransportResponse::create(200U);
        });
    auto bridge_request = bsky_cpp::TransportRequest::create(
        bsky_cpp::HttpMethod::Get, "https://example.com/bridge");
    if (!bridge_request) return 1;
    std::atomic<bool> bridge_delivered{false};
    auto bridge_operation = bridge_runtime.submit_blocking(
        bridge_transport,
        std::move(bridge_request.value()),
        [&bridge_delivered](bsky_cpp::Result<bsky_cpp::TransportResponse> response) {
            bridge_delivered = response && response.value().status() == 200U;
        });
    if (!bridge_operation ||
        !bridge_operation.value().wait_for_completion(std::chrono::seconds(1))) {
        return 1;
    }
    bridge_executor->drain();
    if (!bridge_operation.value().await_callback_quiescence(std::chrono::seconds(1)) ||
        !bridge_delivered || !bridge_runtime.shutdown(std::chrono::seconds(1))) {
        return 1;
    }
#endif
    return 0;
}
