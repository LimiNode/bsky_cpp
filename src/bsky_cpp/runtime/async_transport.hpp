/// \file async_transport.hpp
/// \brief Native asynchronous transport boundary and deterministic fake.
#pragma once

#include "bsky_cpp/runtime/runtime.hpp"

#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace bsky_cpp {

/// Transport-neutral native asynchronous request boundary.
///
/// `start()` is an admission call, not a blocking send. On success the
/// adapter owns the moved request and completion callback until it invokes the
/// callback once. The callback may run inline or later on an adapter-owned
/// execution context; Runtime always dispatches the user callback through its
/// configured executor. Implementations must retain owned response values and
/// must not expose views into temporary adapter buffers. Runtime may call
/// `start()` concurrently for different operations on one adapter instance;
/// implementations must synchronize adapter state. A pre-cancelled token must
/// be observed before initiating a network side effect, while cancellation
/// racing with admission remains cooperative.
/// \par Thread safety
/// `start()` may be called concurrently. A completion may be invoked inline or
/// from any adapter-owned thread, including concurrently with other
/// completions. Implementations must make their own state and callback
/// ownership safe for those calls and must not hold internal locks while
/// invoking completion.
class AsyncTransport {
public:
    /// One-shot owned response completion delivered by an adapter.
    using Completion = runtime::TypedCompletionCallback<TransportResponse>;

    virtual ~AsyncTransport() = default;

    /// Admits one request for native asynchronous execution.
    ///
    /// A failure is synchronous and means the request was not admitted; the
    /// completion callback must not be invoked in that case. An accepted
    /// request has exactly one terminal completion, including cancellation or
    /// transport failure. Implementations must not block this call waiting for
    /// network completion.
    ///
    /// \param request Owned request transferred to the adapter.
    /// \param cancellation Cooperative cancellation token.
    /// \param completion One-shot owned completion callback.
    /// \return Success when admitted, or a typed admission/transport error.
    /// \par Cancellation
    /// If `cancellation` is already requested, implementations must reject
    /// admission with `Cancelled` and must not initiate a new request. A race
    /// after this observation is cooperative; the adapter reports its one
    /// terminal completion and the runtime applies first-winner semantics.
    virtual Result<void> start(TransportRequest request,
                               CancellationToken cancellation,
                               Completion completion) = 0;
};

/// Deterministic native asynchronous transport fake.
///
/// The handler controls when and where the completion is invoked. It may move
/// the completion into a test fixture and invoke it later, which makes
/// cancellation and duplicate-completion tests deterministic without creating
/// a scheduler or a worker thread. The handler may be called concurrently for
/// different requests and must be safe for concurrent invocation. It runs
/// outside the request-history mutex and may invoke completion inline.
class FakeAsyncTransport final : public AsyncTransport {
public:
    /// Handler used to admit a request and retain or invoke its completion.
    using Handler = runtime::MoveOnlyFunction<Result<void>(
        TransportRequest, CancellationToken, Completion)>;

    /// Creates a fake with an optional admission handler.
    ///
    /// Handler exceptions are contained and mapped to a generic transport
    /// error without copying exception text into diagnostics.
    /// \param handler Deterministic admission/completion handler.
    explicit FakeAsyncTransport(Handler handler = {});

    Result<void> start(TransportRequest request,
                       CancellationToken cancellation,
                       Completion completion) override;

    /// Returns an owned snapshot of requests presented to the handler. The
    /// history records admission attempts before the handler result is known,
    /// including attempts that the handler rejects.
    /// \return Requests in handler-call order.
    std::vector<TransportRequest> requests() const;

    /// Removes recorded request history.
    void clear_requests();

private:
    Handler m_handler;
    std::vector<TransportRequest> m_requests;
    mutable std::mutex m_mutex;
};

}  // namespace bsky_cpp
