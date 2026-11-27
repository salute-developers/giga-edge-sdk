#include "giga_edge/lib/common/scope_guard.h"
#include "giga_edge/modules/web/environment.h"
#include "giga_edge/modules/web/tests/web_test_utils/capturing_web_service_observer.h"
#include "giga_edge/modules/web/tests/web_test_utils/fake_web_transport.h"
#include "giga_edge/modules/web/tests/web_test_utils/passthrough_decoder.h"
#include "giga_edge/modules/web/web_coordinator.h"
#include "giga_edge/modules/web/web_service.h"

#include <catch.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

using namespace giga_edge;
using namespace giga_edge::web;
using namespace giga_edge::web::test_utils;

namespace {

std::vector<WebFrame> TextFrames(const std::string& text) {
  WebFrame frame;
  frame.bytes.assign(text.begin(), text.end());
  frame.end_of_stream = true;
  return {frame};
}

struct ServiceFixture {
  std::shared_ptr<IWebService> service = CreateWebService(WebServiceConfig{});
  std::shared_ptr<CapturingWebServiceObserver> observer = std::make_shared<CapturingWebServiceObserver>();
  std::shared_ptr<FakeWebTransport> transport = std::make_shared<FakeWebTransport>();

  ServiceFixture() {
    service->SetObserver(observer);
    service->AddTransport("p", transport);
  }

  bool Submit(WebRequestToken token, const std::string& body) {
    return service->SubmitAsync(
      token, "p", WebStreamDescriptor{}, TextFrames(body), std::make_shared<PassthroughDecoder>()
    );
  }
};

} // namespace

TEST_CASE("Service submit reaches the transport as open+send+close", "[web][service]") {
  ServiceFixture fx;
  REQUIRE(fx.Submit(42, "hello world"));

  REQUIRE(WaitFor([&]() {
    return fx.transport->OpenStreamCount() >= 1;
  }));
  const auto stream_id = fx.transport->OpenedStreams().front();

  REQUIRE(WaitFor([&]() {
    return !fx.transport->CloseSendCalls().empty();
  }));
  const auto sent = fx.transport->SentFrames(stream_id);
  REQUIRE(sent.size() == 1);
  REQUIRE(sent[0] == "hello world");
  REQUIRE(fx.transport->CloseSendCalls().front() == stream_id);
}

TEST_CASE("Service decodes down-frames into chunks and a final chunk", "[web][service]") {
  ServiceFixture fx;
  fx.Submit(7, "ignored");
  const auto stream_id = fx.transport->WaitSentStream();
  REQUIRE(stream_id != 0);

  fx.transport->Emit(stream_id, "streamed reply");
  REQUIRE(WaitFor([&]() {
    return fx.observer->ChunkCount(7) >= 1;
  }));

  fx.transport->EmitClose(stream_id, WebError{});
  REQUIRE(WaitFor([&]() {
    return fx.observer->HasFinal(7);
  }));

  REQUIRE(fx.observer->Text(7) == "streamed reply");
}

TEST_CASE("Service submit fails when no matching provider is registered", "[web][service]") {
  auto service = CreateWebService(WebServiceConfig{});
  REQUIRE_FALSE(
    service->SubmitAsync(1, "missing", WebStreamDescriptor{}, TextFrames("hi"), std::make_shared<PassthroughDecoder>())
  );
}

TEST_CASE("Service rejects a submit without a provider id", "[web][service]") {
  ServiceFixture fx;
  REQUIRE_FALSE(
    fx.service->SubmitAsync(9, "", WebStreamDescriptor{}, TextFrames("hi"), std::make_shared<PassthroughDecoder>())
  );
  REQUIRE(fx.transport->OpenStreamCount() == 0);
}

TEST_CASE("Service rejects a submit without a decoder", "[web][service]") {
  ServiceFixture fx;
  REQUIRE_FALSE(fx.service->SubmitAsync(9, "p", WebStreamDescriptor{}, TextFrames("hi"), nullptr));
}

TEST_CASE("Service binds a stream answered from inside OpenStream", "[web][service]") {
  ServiceFixture fx;
  fx.transport->SetEagerReply("instant");
  REQUIRE(fx.Submit(11, "q"));

  REQUIRE(WaitFor([&]() {
    return fx.observer->HasFinal(11);
  }));
  REQUIRE(fx.observer->Text(11) == "instant");
}

TEST_CASE("Service times out a stream that goes silent", "[web][service]") {
  ServiceFixture fx;
  WebStreamDescriptor descriptor;
  descriptor.idle_timeout_ms = 40;
  REQUIRE(fx.service->SubmitAsync(5, "p", descriptor, TextFrames("q"), std::make_shared<PassthroughDecoder>()));

  REQUIRE(WaitFor([&]() {
    return fx.observer->HasFinal(5);
  }));
  const auto chunks = fx.observer->Chunks(5);
  REQUIRE(chunks.size() == 1);
  REQUIRE(chunks.back().meta.is_error);
  REQUIRE(chunks.back().meta.finish_reason == WebFinishReason::ERROR);
  REQUIRE(chunks.back().meta.error == "request timed out");
  // The timeout is counted from the submission: under load it may expire before the pool opens the stream. Then
  // there is no stream at all, and an opened one is closed in any order of events.
  REQUIRE(WaitFor([&]() {
    return fx.transport->CloseStreamCalls().size() == fx.transport->OpenStreamCount();
  }));
}

TEST_CASE("Service never opens a stream for a request that timed out in the queue", "[web][service]") {
  auto service = CreateWebService(WebServiceConfig{1, 0, false});
  auto observer = std::make_shared<CapturingWebServiceObserver>();
  auto transport = std::make_shared<FakeWebTransport>();
  service->SetObserver(observer);
  service->AddTransport("p", transport);

  const auto submit = [&](WebRequestToken token, uint32_t idle_timeout_ms) {
    WebStreamDescriptor descriptor;
    descriptor.headers.emplace_back("x-token", std::to_string(token));
    descriptor.idle_timeout_ms = idle_timeout_ms;
    return service->SubmitAsync(token, "p", descriptor, TextFrames("q"), std::make_shared<PassthroughDecoder>());
  };
  const auto streams_of = [&](WebRequestToken token) {
    size_t count = 0;
    for (const auto id : transport->OpenedStreams()) {
      const auto descriptor = transport->DescriptorOf(id);
      const auto* value = HeaderValueOf(descriptor.headers, "x-token");
      count += value != nullptr && *value == std::to_string(token) ? 1 : 0;
    }
    return count;
  };

  // The only pool thread is busy with the first request; the second one times out in the queue.
  transport->BlockOpenStream();
  REQUIRE(submit(1, 0));
  REQUIRE(transport->WaitUntilInsideOpenStream());
  REQUIRE(submit(2, 40));
  REQUIRE(WaitFor([&]() {
    return observer->HasFinal(2);
  }));
  REQUIRE(observer->Chunks(2).back().meta.error == "request timed out");

  // The pool queue is FIFO: when the stream of the third request is open, the task of the second has already run.
  REQUIRE(submit(3, 0));
  transport->ReleaseOpenStream();
  REQUIRE(WaitFor([&]() {
    return streams_of(3) == 1;
  }));
  REQUIRE(streams_of(2) == 0);
  REQUIRE(transport->CloseStreamCalls().empty());
}

TEST_CASE("Service idle timeout falls back to the configured default", "[web][service]") {
  WebServiceConfig config;
  config.response_idle_timeout_ms = 40;
  auto service = CreateWebService(config);
  auto observer = std::make_shared<CapturingWebServiceObserver>();
  auto transport = std::make_shared<FakeWebTransport>();
  service->SetObserver(observer);
  service->AddTransport("p", transport);

  REQUIRE(service->SubmitAsync(3, "p", WebStreamDescriptor{}, TextFrames("q"), std::make_shared<PassthroughDecoder>()));
  REQUIRE(WaitFor([&]() {
    return observer->HasFinal(3);
  }));
  REQUIRE(observer->Chunks(3).back().meta.error == "request timed out");
}

TEST_CASE("Service does not time out a request that answers in time", "[web][service]") {
  RepeatWithLongerIdleTimeout([](uint32_t idle_timeout_ms) {
    ServiceFixture fx;
    fx.transport->SetEchoMode(true);
    WebStreamDescriptor descriptor;
    descriptor.idle_timeout_ms = idle_timeout_ms;
    REQUIRE(fx.service->SubmitAsync(4, "p", descriptor, TextFrames("done"), std::make_shared<PassthroughDecoder>()));

    REQUIRE(WaitFor([&]() {
      return fx.observer->HasFinal(4);
    }));
    // The echo arrives only when the pool opens the stream: under load it may have missed the timeout.
    if (fx.observer->Chunks(4).back().meta.error == "request timed out") {
      return false;
    }
    REQUIRE(fx.observer->Text(4) == "done");
    for (const auto& chunk : fx.observer->Chunks(4)) {
      REQUIRE_FALSE(chunk.meta.is_error);
    }
    return true;
  });
}

TEST_CASE("Service cancel closes the stream and yields a CANCELLED final chunk", "[web][service]") {
  ServiceFixture fx;
  fx.Submit(5, "hi");
  const auto stream_id = fx.transport->WaitSentStream();
  REQUIRE(stream_id != 0);

  REQUIRE(fx.service->Cancel(5));
  REQUIRE(WaitFor([&]() {
    return fx.observer->HasFinal(5);
  }));

  const auto chunks = fx.observer->Chunks(5);
  REQUIRE(chunks.back().meta.finish_reason == WebFinishReason::CANCELLED);
  REQUIRE(fx.transport->CloseStreamCalls() == std::vector<WebStreamId>{stream_id});
}

TEST_CASE("Service tears down cleanly while a request is in flight", "[web][service]") {
  auto observer = std::make_shared<CapturingWebServiceObserver>();
  auto transport = std::make_shared<FakeWebTransport>();
  {
    auto service = CreateWebService(WebServiceConfig{});
    service->SetObserver(observer);
    service->AddTransport("p", transport);
    service->SubmitAsync(1, "p", WebStreamDescriptor{}, TextFrames("hi"), std::make_shared<PassthroughDecoder>());
  }
  SUCCEED("service destroyed with in-flight request");
}

TEST_CASE("Service released during OpenStream stops on the releasing thread", "[web][service]") {
  auto transport = std::make_shared<FakeWebTransport>();
  auto service = CreateWebService(WebServiceConfig{});
  service->AddTransport("p", transport);
  transport->BlockOpenStream();
  REQUIRE(
    service->SubmitAsync(9, "p", WebStreamDescriptor{}, TextFrames("hi"), std::make_shared<PassthroughDecoder>())
  );
  REQUIRE(transport->WaitUntilInsideOpenStream());

  // OpenStream is released once Shutdown has removed the observer, which means the service stops inside reset.
  std::atomic<bool> done = false;
  std::thread releaser([&]() {
    while (transport->HasObserver() && !done.load()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    transport->ReleaseOpenStream();
  });
  service.reset();
  const auto closes = transport->CloseStreamCalls().size();
  done.store(true);
  transport->ReleaseOpenStream();
  releaser.join();

  REQUIRE(closes == 1);
}

TEST_CASE("Web environment shuts the service down while a consumer still holds the coordinator", "[web][service]") {
  auto transport = std::make_shared<FakeWebTransport>();
  auto env = std::make_shared<WebServiceEnvironment>(WebServiceConfig{});
  env->service->AddTransport("p", transport);
  const auto coordinator = env->coordinator;
  WebConsumerConfig consumer_config;
  consumer_config.consumer_name = "node";
  const auto consumer = coordinator->RegisterConsumer(consumer_config);
  const auto submit = [&]() {
    return coordinator
      ->Submit(consumer, "p", WebStreamDescriptor{}, TextFrames("q"), std::make_shared<PassthroughDecoder>())
      .accepted;
  };

  REQUIRE(submit());
  // CloseSend comes after the stream is bound to the request.
  REQUIRE(WaitFor([&]() {
    return !transport->CloseSendCalls().empty();
  }));
  const auto stream_id = transport->OpenedStreams().front();

  env.reset();

  REQUIRE(transport->CloseStreamCalls() == std::vector<WebStreamId>{stream_id});
  REQUIRE_FALSE(submit());
}

namespace {

// Holds the last reference to the service and releases it in the callback, on a thread of the service itself.
class ServiceHoldingObserver : public IWebServiceObserver {
public:
  void Hold(std::shared_ptr<IWebService> service) {
    std::lock_guard<std::mutex> lock(mutex_);
    service_ = std::move(service);
  }

  void AllowRelease() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      release_allowed_ = true;
    }
    cv_.notify_all();
  }

  void OnChunk(WebRequestToken /*token*/, WebDecodedChunk /*chunk*/) override {
    std::shared_ptr<IWebService> released;
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this]() {
      return release_allowed_;
    });
    released = std::move(service_);
    lock.unlock();
  }

private:
  std::mutex mutex_;
  std::condition_variable cv_;
  std::shared_ptr<IWebService> service_;
  bool release_allowed_ = false;
};

} // namespace

TEST_CASE("Service whose last reference goes in a transport callback on its pool stops cleanly", "[web][service]") {
  auto transport = std::make_shared<FakeWebTransport>();
  transport->SetEagerReply("instant");
  transport->BlockOpenStream();
  auto observer = std::make_shared<ServiceHoldingObserver>();
  {
    auto service = CreateWebService(WebServiceConfig{});
    service->SetObserver(observer);
    service->AddTransport("p", transport);
    observer->Hold(service);
    REQUIRE(
      service->SubmitAsync(1, "p", WebStreamDescriptor{}, TextFrames("q"), std::make_shared<PassthroughDecoder>())
    );
    REQUIRE(transport->WaitUntilInsideOpenStream());
  }

  // The reply comes from inside OpenStream: the transport callback on the pool thread holds a temporary reference,
  // and that one becomes the last.
  observer->AllowRelease();
  transport->ReleaseOpenStream();
  REQUIRE(WaitFor([&]() {
    return transport.use_count() == 1;
  }));
  REQUIRE_FALSE(transport->HasObserver());
}

TEST_CASE("Service whose last reference goes in a watchdog delivery stops cleanly", "[web][service]") {
  auto transport = std::make_shared<FakeWebTransport>();
  auto observer = std::make_shared<ServiceHoldingObserver>();
  {
    auto service = CreateWebService(WebServiceConfig{});
    service->SetObserver(observer);
    service->AddTransport("p", transport);
    observer->Hold(service);
    WebStreamDescriptor descriptor;
    descriptor.idle_timeout_ms = 40;
    REQUIRE(service->SubmitAsync(1, "p", descriptor, TextFrames("q"), std::make_shared<PassthroughDecoder>()));
  }

  // The timeout final is delivered by the watchdog: the last reference is released on its thread.
  observer->AllowRelease();
  REQUIRE(WaitFor([&]() {
    return transport.use_count() == 1;
  }));
  REQUIRE_FALSE(transport->HasObserver());
}

TEST_CASE("Service ignores a transport added after the shutdown", "[web][service]") {
  auto env = std::make_shared<WebServiceEnvironment>(WebServiceConfig{});
  const auto service = env->service;
  env.reset();

  auto transport = std::make_shared<FakeWebTransport>();
  service->AddTransport("p", transport);
  REQUIRE_FALSE(transport->HasObserver());
  REQUIRE_FALSE(
    service->SubmitAsync(1, "p", WebStreamDescriptor{}, TextFrames("q"), std::make_shared<PassthroughDecoder>())
  );
}

TEST_CASE("Service RemoveTransport detaches the provider", "[web][service]") {
  ServiceFixture fx;
  REQUIRE(fx.service->GetProviderIds().size() == 1);
  fx.service->RemoveTransport("p");
  REQUIRE(fx.service->GetProviderIds().empty());
  REQUIRE_FALSE(fx.Submit(1, "hi"));
}

TEST_CASE("Service keeps a long stream alive while frames keep arriving", "[web][service]") {
  RepeatWithLongerIdleTimeout([](uint32_t idle_timeout_ms) {
    ServiceFixture fx;
    WebStreamDescriptor descriptor;
    descriptor.idle_timeout_ms = idle_timeout_ms;
    descriptor.keep_send_open = true;
    const auto idle = std::chrono::milliseconds(idle_timeout_ms);
    // The service marks activity inside SubmitAsync and Emit; the pauses between the marks are measured with a
    // margin: from the start of the previous call to the return of the next one. If the test thread overslept the
    // window, the run does not check the extension.
    auto activity = std::chrono::steady_clock::now();
    REQUIRE(fx.service->SubmitAsync(21, "p", descriptor, TextFrames("start"), std::make_shared<PassthroughDecoder>()));
    REQUIRE(WaitFor([&]() {
      return fx.transport->SentStream() != 0 || fx.observer->HasFinal(21);
    }));
    const auto stream_id = fx.transport->SentStream();

    // The stream lives longer than the silence window, but the pauses between frames are shorter than it.
    bool in_time = stream_id != 0;
    for (int i = 0; i < 15 && in_time; ++i) {
      std::this_thread::sleep_for(idle / 10);
      const auto before = std::chrono::steady_clock::now();
      fx.transport->Emit(stream_id, "tick");
      in_time = std::chrono::steady_clock::now() - activity < idle;
      activity = before;
    }

    const bool finished = fx.observer->HasFinal(21);
    const bool closed = !fx.transport->CloseStreamCalls().empty();
    const auto text = fx.observer->Text(21);
    if (!in_time || std::chrono::steady_clock::now() - activity >= idle) {
      return false;
    }
    REQUIRE_FALSE(finished);
    REQUIRE_FALSE(closed);
    REQUIRE(text.size() == 60);

    REQUIRE(WaitFor([&]() {
      return fx.observer->HasFinal(21);
    }));
    REQUIRE(fx.observer->Chunks(21).back().meta.error == "request timed out");
    return true;
  });
}

TEST_CASE("Service arms no watchdog when the timeout is disabled", "[web][service]") {
  WebServiceConfig config;
  config.response_idle_timeout_ms = 0;
  auto service = CreateWebService(config);
  auto observer = std::make_shared<CapturingWebServiceObserver>();
  auto transport = std::make_shared<FakeWebTransport>();
  service->SetObserver(observer);
  service->AddTransport("p", transport);

  REQUIRE(
    service->SubmitAsync(31, "p", WebStreamDescriptor{}, TextFrames("q"), std::make_shared<PassthroughDecoder>())
  );
  REQUIRE(WaitFor([&]() {
    return transport->OpenStreamCount() >= 1;
  }));

  std::this_thread::sleep_for(std::chrono::milliseconds(120));
  REQUIRE_FALSE(observer->HasFinal(31));
  REQUIRE(transport->CloseStreamCalls().empty());
}

TEST_CASE("Service completes many simultaneous requests to one provider", "[web][service]") {
  ServiceFixture fx;
  auto service = CreateWebService(WebServiceConfig{4, 0, false});
  auto observer = std::make_shared<CapturingWebServiceObserver>();
  auto transport = std::make_shared<FakeWebTransport>();
  service->SetObserver(observer);
  service->AddTransport("p", transport);
  transport->SetEchoMode(true);

  for (WebRequestToken token = 1; token <= 8; ++token) {
    REQUIRE(service->SubmitAsync(
      token, "p", WebStreamDescriptor{}, TextFrames("body"), std::make_shared<PassthroughDecoder>()
    ));
  }

  REQUIRE(WaitFor([&]() {
    for (WebRequestToken token = 1; token <= 8; ++token) {
      if (!observer->HasFinal(token)) {
        return false;
      }
    }
    return true;
  }));
  for (WebRequestToken token = 1; token <= 8; ++token) {
    REQUIRE(observer->Text(token) == "body");
  }
}

TEST_CASE("Service finishes a request whose transport hangs in OpenStream", "[web][service]") {
  RepeatWithLongerIdleTimeout([](uint32_t idle_timeout_ms) {
    ServiceFixture fx;
    fx.transport->BlockOpenStream();
    // A failed check must not leave the pool inside OpenStream: otherwise the destruction of the service would wait
    // for it forever.
    const ScopeGuard release([&fx]() {
      fx.transport->ReleaseOpenStream();
    });

    WebStreamDescriptor descriptor;
    descriptor.idle_timeout_ms = idle_timeout_ms;
    REQUIRE(fx.service->SubmitAsync(77, "p", descriptor, TextFrames("q"), std::make_shared<PassthroughDecoder>()));
    // The timeout is counted from the submission: if the pool did not enter OpenStream before it, the request timed
    // out in the queue.
    REQUIRE(WaitFor([&]() {
      return fx.transport->OpenEnteredCount() > 0 || fx.observer->HasFinal(77);
    }));
    if (fx.transport->OpenEnteredCount() == 0) {
      return false;
    }

    REQUIRE(WaitFor([&]() {
      return fx.observer->HasFinal(77);
    }));
    REQUIRE(fx.observer->Chunks(77).back().meta.error == "request timed out");

    fx.transport->ReleaseOpenStream();
    REQUIRE(WaitFor([&]() {
      return !fx.transport->CloseStreamCalls().empty();
    }));
    REQUIRE(fx.observer->ChunkCount(77) == 1);
    return true;
  });
}

TEST_CASE("Service cancel of a request still opening closes the stream once it appears", "[web][service]") {
  ServiceFixture fx;
  fx.transport->BlockOpenStream();

  REQUIRE(fx.Submit(78, "q"));
  REQUIRE(fx.transport->WaitUntilInsideOpenStream());
  REQUIRE(fx.service->Cancel(78));

  fx.transport->ReleaseOpenStream();
  REQUIRE(WaitFor([&]() {
    return !fx.transport->CloseStreamCalls().empty();
  }));
  REQUIRE(fx.transport->SentFrames(fx.transport->OpenedStreams().front()).empty());
}

TEST_CASE("Service fails in-flight requests when the transport is removed", "[web][service]") {
  ServiceFixture fx;
  REQUIRE(fx.Submit(91, "q"));
  // CloseSend comes after the stream is bound to the request.
  REQUIRE(WaitFor([&]() {
    return !fx.transport->CloseSendCalls().empty();
  }));
  const auto stream_id = fx.transport->OpenedStreams().front();

  fx.service->RemoveTransport("p");

  REQUIRE(WaitFor([&]() {
    return fx.observer->HasFinal(91);
  }));
  const auto chunks = fx.observer->Chunks(91);
  REQUIRE(chunks.back().meta.is_error);
  REQUIRE(chunks.back().meta.error == "provider removed");
  REQUIRE(fx.transport->CloseStreamCalls() == std::vector<WebStreamId>{stream_id});
}

TEST_CASE("Service fails a request whose frame the transport rejects", "[web][service]") {
  ServiceFixture fx;
  fx.transport->SetSendFrameResult(false);

  REQUIRE(fx.Submit(92, "q"));

  REQUIRE(WaitFor([&]() {
    return fx.observer->HasFinal(92);
  }));
  const auto chunks = fx.observer->Chunks(92);
  REQUIRE(chunks.back().meta.is_error);
  REQUIRE(chunks.back().meta.error == "SendFrame failed");
  REQUIRE(WaitFor([&]() {
    return !fx.transport->CloseStreamCalls().empty();
  }));
  REQUIRE(fx.transport->CloseSendCalls().empty());
}

namespace {

struct RetiredStreamFixture {
  std::shared_ptr<IWebService> service = CreateWebService(WebServiceConfig{/*threads=*/2, /*idle_ms=*/0, false});
  std::shared_ptr<CapturingWebServiceObserver> observer = std::make_shared<CapturingWebServiceObserver>();
  std::shared_ptr<FakeWebTransport> transport = std::make_shared<FakeWebTransport>();
  WebStreamId retired_id = 0;

  RetiredStreamFixture() {
    service->SetObserver(observer);
    service->AddTransport("p", transport);

    REQUIRE(
      service->SubmitAsync(1, "p", WebStreamDescriptor{}, TextFrames("first"), std::make_shared<PassthroughDecoder>())
    );
    REQUIRE(transport->WaitUntilInsideOpenStream(1));
    REQUIRE(WaitFor([&]() {
      return transport->OpenStreamCount() >= 1;
    }));
    retired_id = transport->OpenedStreams().front();

    service->Cancel(1);
    REQUIRE(WaitFor([&]() {
      return observer->HasFinal(1);
    }));

    transport->BlockOpenStream();
    REQUIRE(
      service->SubmitAsync(2, "p", WebStreamDescriptor{}, TextFrames("second"), std::make_shared<PassthroughDecoder>())
    );
    REQUIRE(transport->WaitUntilInsideOpenStream(2));
  }

  void RequireSecondRequestIsIntact() {
    transport->ReleaseOpenStream();
    REQUIRE(WaitFor([&]() {
      return transport->OpenStreamCount() >= 2;
    }));
    const auto own_id = transport->OpenedStreams().back();
    REQUIRE(own_id != retired_id);
    REQUIRE(WaitFor([&]() {
      return !transport->CloseSendCalls().empty();
    }));

    transport->Emit(own_id, "OWN-ANSWER", /*end_of_stream=*/true);
    transport->EmitClose(own_id, WebError{});

    REQUIRE(WaitFor([&]() {
      return observer->HasFinal(2);
    }));
    REQUIRE(observer->Text(2) == "OWN-ANSWER");
  }
};

} // namespace

TEST_CASE("Service ignores a frame of a retired stream while another request is opening", "[web][service]") {
  RetiredStreamFixture fx;

  fx.transport->Emit(fx.retired_id, "STALE");

  fx.RequireSecondRequestIsIntact();
  REQUIRE(fx.observer->Text(2).find("STALE") == std::string::npos);
}

TEST_CASE("Service ignores a close of a retired stream while another request is opening", "[web][service]") {
  RetiredStreamFixture fx;

  fx.transport->EmitClose(fx.retired_id, WebError{WebErrorCode::CANCELLED, "cancelled"});

  fx.RequireSecondRequestIsIntact();
  REQUIRE(fx.observer->Chunks(2).back().meta.finish_reason == WebFinishReason::COMPLETED);
}

TEST_CASE("Service keeps binding a stream answered from inside OpenStream", "[web][service]") {
  ServiceFixture fx;
  fx.transport->SetEagerReply("eager answer");

  REQUIRE(fx.Submit(5, "q"));
  REQUIRE(WaitFor([&]() {
    return fx.observer->HasFinal(5);
  }));
  REQUIRE(fx.observer->Text(5) == "eager answer");
}

namespace {

class RecordingDecoder : public IWebResponseDecoder {
public:
  std::vector<WebDecodedChunk> DecodeFrame(WebStreamId /*id*/, const WebFrame& frame) override {
    Enter();
    ++decode_calls_;
    decoded_after_finalize_ = decoded_after_finalize_ || finalize_calls_ > 0;
    WaitWhileHeld();
    std::vector<WebDecodedChunk> chunks{MakeTextChunk(std::string(frame.bytes.begin(), frame.bytes.end()))};
    Leave();
    return chunks;
  }

  std::vector<WebDecodedChunk> Finalize(WebStreamId /*id*/, const WebError& error) override {
    Enter();
    ++finalize_calls_;
    Leave();
    return {MakeTerminalChunk(error)};
  }

  void HoldDecode() {
    std::lock_guard<std::mutex> lock(mutex_);
    held_ = true;
  }

  void Release() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      held_ = false;
    }
    gate_.notify_all();
  }

  int DecodeCalls() const {
    return decode_calls_;
  }
  int FinalizeCalls() const {
    return finalize_calls_;
  }
  bool DecodedAfterFinalize() const {
    return decoded_after_finalize_;
  }
  bool SawOverlap() const {
    return overlap_;
  }

private:
  void WaitWhileHeld() {
    std::unique_lock<std::mutex> lock(mutex_);
    gate_.wait_for(lock, kWaitHangGuard, [this]() {
      return !held_;
    });
  }

  void Enter() {
    overlap_ = overlap_ || inside_.exchange(true);
  }
  void Leave() {
    inside_.store(false);
  }

  std::mutex mutex_;
  std::condition_variable gate_;
  bool held_ = false;
  std::atomic<bool> inside_{false};
  std::atomic<bool> overlap_{false};
  std::atomic<bool> decoded_after_finalize_{false};
  std::atomic<int> decode_calls_{0};
  std::atomic<int> finalize_calls_{0};
};

} // namespace

TEST_CASE("Service finalizes a request exactly once when close races the timeout", "[web][service]") {
  RepeatWithLongerIdleTimeout([](uint32_t idle_timeout_ms) {
    auto service = CreateWebService(WebServiceConfig{});
    auto observer = std::make_shared<CapturingWebServiceObserver>();
    auto transport = std::make_shared<FakeWebTransport>();
    auto decoder = std::make_shared<RecordingDecoder>();
    service->SetObserver(observer);
    service->AddTransport("p", transport);

    WebStreamDescriptor descriptor;
    descriptor.idle_timeout_ms = idle_timeout_ms;
    REQUIRE(service->SubmitAsync(1, "p", descriptor, TextFrames("q"), decoder));
    REQUIRE(WaitFor([&]() {
      return observer->HasFinal(1);
    }));
    // The timeout is counted from the submission: the close races the timeout only if the stream was bound before
    // it.
    const auto stream_id = transport->SentStream();
    if (stream_id == 0) {
      return false;
    }
    transport->EmitClose(stream_id, WebError{});
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    REQUIRE(decoder->FinalizeCalls() == 1);
    REQUIRE(observer->Chunks(1).size() == 1);
    return true;
  });
}

TEST_CASE("Service does not let the watchdog finalize a decoder that is still decoding", "[web][service]") {
  RepeatWithLongerIdleTimeout([](uint32_t idle_timeout_ms) {
    auto service = CreateWebService(WebServiceConfig{});
    auto observer = std::make_shared<CapturingWebServiceObserver>();
    auto transport = std::make_shared<FakeWebTransport>();
    auto decoder = std::make_shared<RecordingDecoder>();
    service->SetObserver(observer);
    service->AddTransport("p", transport);

    WebStreamDescriptor descriptor;
    descriptor.idle_timeout_ms = idle_timeout_ms;
    REQUIRE(service->SubmitAsync(2, "p", descriptor, TextFrames("q"), decoder));
    REQUIRE(WaitFor([&]() {
      return transport->SentStream() != 0 || observer->HasFinal(2);
    }));
    const auto stream_id = transport->SentStream();
    if (stream_id == 0) {
      return false;
    }

    // The watchdog counts idle time from the submission of the request: the frame may have missed the timeout, and
    // then the service dropped it. Otherwise there is no final while decoding holds the session, and decoding is
    // held longer than the watchdog timeout.
    decoder->HoldDecode();
    std::thread emitter([&]() {
      transport->Emit(stream_id, "slow");
    });
    REQUIRE(WaitFor([&]() {
      return decoder->DecodeCalls() > 0 || observer->HasFinal(2);
    }));
    const bool inside_decode = decoder->DecodeCalls() > 0;
    if (inside_decode) {
      std::this_thread::sleep_for(2 * std::chrono::milliseconds(idle_timeout_ms));
    }
    decoder->Release();
    emitter.join();
    if (!inside_decode) {
      return false;
    }

    REQUIRE(WaitFor([&]() {
      return observer->HasFinal(2);
    }));
    REQUIRE(decoder->FinalizeCalls() == 1);
    REQUIRE_FALSE(decoder->SawOverlap());
    REQUIRE_FALSE(decoder->DecodedAfterFinalize());
    return true;
  });
}

TEST_CASE("Service never touches the decoder after finalization", "[web][service]") {
  auto service = CreateWebService(WebServiceConfig{});
  auto observer = std::make_shared<CapturingWebServiceObserver>();
  auto transport = std::make_shared<FakeWebTransport>();
  auto decoder = std::make_shared<RecordingDecoder>();
  service->SetObserver(observer);
  service->AddTransport("p", transport);

  REQUIRE(service->SubmitAsync(3, "p", WebStreamDescriptor{}, TextFrames("q"), decoder));
  const auto stream_id = transport->WaitSentStream();
  REQUIRE(stream_id != 0);

  transport->Emit(stream_id, "before");
  REQUIRE(WaitFor([&]() {
    return decoder->DecodeCalls() == 1;
  }));

  REQUIRE(service->Cancel(3));
  transport->Emit(stream_id, "after");
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  REQUIRE(decoder->FinalizeCalls() == 1);
  REQUIRE(decoder->DecodeCalls() == 1);
  REQUIRE_FALSE(decoder->DecodedAfterFinalize());
}

TEST_CASE("Service finishes a cancelled request whose transport reports nothing", "[web][service]") {
  ServiceFixture fx;
  fx.transport->SetSilentClose(true);

  REQUIRE(fx.Submit(31, "q"));
  const auto stream_id = fx.transport->WaitSentStream();
  REQUIRE(stream_id != 0);

  REQUIRE(fx.service->Cancel(31));

  REQUIRE(WaitFor([&]() {
    return fx.observer->HasFinal(31);
  }));
  REQUIRE(fx.observer->Chunks(31).back().meta.finish_reason == WebFinishReason::CANCELLED);
  REQUIRE(fx.transport->CloseStreamCalls() == std::vector<WebStreamId>{stream_id});
  REQUIRE_FALSE(fx.service->Cancel(31));
}

TEST_CASE("Service fails in-flight requests when the provider is replaced", "[web][service]") {
  ServiceFixture fx;
  REQUIRE(fx.Submit(41, "q"));
  // CloseSend comes after the stream is bound to the request.
  REQUIRE(WaitFor([&]() {
    return !fx.transport->CloseSendCalls().empty();
  }));
  const auto stream_id = fx.transport->OpenedStreams().front();

  auto replacement = std::make_shared<FakeWebTransport>();
  fx.service->AddTransport("p", replacement);

  REQUIRE(WaitFor([&]() {
    return fx.observer->HasFinal(41);
  }));
  REQUIRE(fx.observer->Chunks(41).back().meta.is_error);
  REQUIRE(fx.observer->Chunks(41).back().meta.error == "provider replaced");
  REQUIRE(fx.transport->CloseStreamCalls() == std::vector<WebStreamId>{stream_id});

  replacement->SetEchoMode(true);
  REQUIRE(
    fx.service->SubmitAsync(42, "p", WebStreamDescriptor{}, TextFrames("hi"), std::make_shared<PassthroughDecoder>())
  );
  REQUIRE(WaitFor([&]() {
    return fx.observer->HasFinal(42);
  }));
  REQUIRE(fx.observer->Text(42) == "hi");
}

TEST_CASE("Service keeps the order of frames sent while the stream is opening", "[web][service]") {
  ServiceFixture fx;
  fx.transport->BlockOpenStream();

  WebStreamDescriptor descriptor;
  descriptor.keep_send_open = true;
  REQUIRE(fx.service->SubmitAsync(51, "p", descriptor, TextFrames("first"), std::make_shared<PassthroughDecoder>()));
  REQUIRE(fx.transport->WaitUntilInsideOpenStream());

  WebFrame second;
  second.bytes = {'s', 'e', 'c', 'o', 'n', 'd'};
  WebFrame third;
  third.bytes = {'t', 'h', 'i', 'r', 'd'};
  REQUIRE(fx.service->SendFrame(51, second));
  REQUIRE(fx.service->SendFrame(51, third));

  fx.transport->ReleaseOpenStream();
  REQUIRE(WaitFor([&]() {
    return fx.transport->OpenStreamCount() >= 1;
  }));
  const auto stream_id = fx.transport->OpenedStreams().front();
  REQUIRE(WaitFor([&]() {
    return fx.transport->SentFrames(stream_id).size() == 3;
  }));

  const auto sent = fx.transport->SentFrames(stream_id);
  REQUIRE(sent[0] == "first");
  REQUIRE(sent[1] == "second");
  REQUIRE(sent[2] == "third");
  REQUIRE(fx.transport->CloseSendCalls().empty());
}

TEST_CASE("Service closes send once the queued frames are flushed", "[web][service]") {
  ServiceFixture fx;
  fx.transport->BlockOpenStream();

  WebStreamDescriptor descriptor;
  descriptor.keep_send_open = true;
  REQUIRE(fx.service->SubmitAsync(52, "p", descriptor, TextFrames("start"), std::make_shared<PassthroughDecoder>()));
  REQUIRE(fx.transport->WaitUntilInsideOpenStream());

  WebFrame tail;
  tail.bytes = {'t', 'a', 'i', 'l'};
  REQUIRE(fx.service->SendFrame(52, tail));
  REQUIRE(fx.service->CloseSend(52));
  REQUIRE_FALSE(fx.service->SendFrame(52, tail));

  fx.transport->ReleaseOpenStream();
  REQUIRE(WaitFor([&]() {
    return !fx.transport->CloseSendCalls().empty();
  }));
  const auto sent = fx.transport->SentFrames(fx.transport->OpenedStreams().front());
  REQUIRE(sent.size() == 2);
  REQUIRE(sent[1] == "tail");
}

TEST_CASE("Service lets a request switch the idle timeout off", "[web][service]") {
  auto service = CreateWebService(WebServiceConfig{/*threads=*/1, /*idle_ms=*/40, false});
  auto observer = std::make_shared<CapturingWebServiceObserver>();
  auto transport = std::make_shared<FakeWebTransport>();
  service->SetObserver(observer);
  service->AddTransport("p", transport);

  WebStreamDescriptor descriptor;
  descriptor.idle_timeout_ms = 0;
  REQUIRE(service->SubmitAsync(61, "p", descriptor, TextFrames("q"), std::make_shared<PassthroughDecoder>()));

  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  REQUIRE_FALSE(observer->HasFinal(61));
  REQUIRE(service->Cancel(61));
}

namespace {

struct CapturingTransportObserver : public IWebTransportObserver {
  void OnResponseHead(WebStreamId id, WebResponseHead head) override {
    std::lock_guard<std::mutex> lock(mutex);
    heads[id] = std::move(head);
    events.emplace_back(id, "head");
  }

  void OnFrame(WebStreamId id, WebFrame) override {
    std::lock_guard<std::mutex> lock(mutex);
    events.emplace_back(id, "frame");
  }

  void OnStreamClosed(WebStreamId id, WebError error) override {
    std::lock_guard<std::mutex> lock(mutex);
    closed.emplace(id, error);
    events.emplace_back(id, "closed");
  }

  bool Closed(WebStreamId id) {
    std::lock_guard<std::mutex> lock(mutex);
    return closed.count(id) != 0;
  }

  WebError CloseError(WebStreamId id) {
    std::lock_guard<std::mutex> lock(mutex);
    const auto it = closed.find(id);
    return it == closed.end() ? WebError{} : it->second;
  }

  size_t ClosedCount() {
    std::lock_guard<std::mutex> lock(mutex);
    return closed.size();
  }

  std::optional<WebResponseHead> HeadOf(WebStreamId id) {
    std::lock_guard<std::mutex> lock(mutex);
    const auto it = heads.find(id);
    return it == heads.end() ? std::optional<WebResponseHead>{} : it->second;
  }

  std::vector<std::string> EventsOf(WebStreamId id) {
    std::lock_guard<std::mutex> lock(mutex);
    std::vector<std::string> stream_events;
    for (const auto& entry : events) {
      if (entry.first == id) {
        stream_events.push_back(entry.second);
      }
    }
    return stream_events;
  }

  std::mutex mutex;
  std::map<WebStreamId, WebError> closed;
  std::map<WebStreamId, WebResponseHead> heads;
  std::vector<std::pair<WebStreamId, std::string>> events;
};

} // namespace

// The IWebTransportWithShutdown contract: Shutdown cancels open streams with OnStreamClosed(CANCELLED), is
// idempotent, and there are no callbacks after it returns.
TEST_CASE("FakeWebTransport Shutdown cancels open streams once", "[web][transport]") {
  auto transport = std::make_shared<FakeWebTransport>();
  auto observer = std::make_shared<CapturingTransportObserver>();
  transport->SetObserver(observer);

  const WebStreamId first = transport->OpenStream(WebStreamDescriptor{});
  const WebStreamId second = transport->OpenStream(WebStreamDescriptor{});
  REQUIRE(first != 0);
  REQUIRE(second != 0);
  transport->CloseStream(first); // already closed: there must be no second CANCELLED

  transport->Shutdown();
  REQUIRE(observer->Closed(second));
  REQUIRE(observer->CloseError(second).code == WebErrorCode::CANCELLED);
  REQUIRE(observer->ClosedCount() == 2); // the first was closed by CloseStream, the second by Shutdown

  transport->Shutdown(); // idempotent: no new closes
  REQUIRE(observer->ClosedCount() == 2);
}

// The synthesized response of the fake is a complete one: the head is configurable and comes before the frame, as
// in the curl transport (code running on the fake and on curl needs the same contract).
TEST_CASE("FakeWebTransport eager response delivers the configured head", "[web][transport]") {
  auto transport = std::make_shared<FakeWebTransport>();
  auto observer = std::make_shared<CapturingTransportObserver>();
  transport->SetObserver(observer);
  WebResponseHead configured;
  configured.status = 201;
  configured.headers.emplace_back("Mcp-Session-Id", "sess-7");
  transport->SetResponseHead(configured);
  transport->SetEagerReply("hello");

  const WebStreamId id = transport->OpenStream(WebStreamDescriptor{});
  REQUIRE(id != 0);

  const auto head = observer->HeadOf(id);
  REQUIRE(head.has_value());
  REQUIRE(head->status == 201);
  REQUIRE(head->headers == configured.headers);
  REQUIRE(observer->EventsOf(id) == (std::vector<std::string>{"head", "frame", "closed"}));
}

TEST_CASE("FakeWebTransport echo response delivers the head", "[web][transport]") {
  auto transport = std::make_shared<FakeWebTransport>();
  auto observer = std::make_shared<CapturingTransportObserver>();
  transport->SetObserver(observer);
  transport->SetEchoMode(true);

  const WebStreamId id = transport->OpenStream(WebStreamDescriptor{});
  REQUIRE(id != 0);
  REQUIRE(transport->SendFrame(id, WebFrame{}));
  transport->CloseSend(id);

  // An unconfigured head is the default success 200.
  const auto head = observer->HeadOf(id);
  REQUIRE(head.has_value());
  REQUIRE(head->status == 200);
  REQUIRE(observer->EventsOf(id) == (std::vector<std::string>{"head", "frame", "closed"}));
}
