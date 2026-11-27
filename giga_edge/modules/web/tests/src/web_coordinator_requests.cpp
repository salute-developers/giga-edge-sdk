#include "giga_edge/modules/web/tests/web_test_utils/capturing_web_service_observer.h"
#include "giga_edge/modules/web/tests/web_test_utils/fake_web_transport.h"
#include "giga_edge/modules/web/tests/web_test_utils/passthrough_decoder.h"
#include "giga_edge/modules/web/web_coordinator.h"
#include "giga_edge/modules/web/web_service.h"

#include <catch.hpp>

#include <algorithm>
#include <memory>
#include <string>

using namespace giga_edge;
using namespace giga_edge::web;
using namespace giga_edge::web::test_utils;

namespace {

std::string ChunkText(const WebDecodedChunk& chunk) {
  const auto* text = ChunkPayloadAs<WebTextChunk>(chunk);
  return text == nullptr ? std::string() : text->text;
}

std::vector<WebFrame> TextFrames(const std::string& text) {
  WebFrame frame;
  frame.bytes.assign(text.begin(), text.end());
  frame.end_of_stream = true;
  return {frame};
}

WebConsumerConfig ConsumerNamed(const std::string& name, size_t capacity = 4096) {
  WebConsumerConfig config;
  config.consumer_name = name;
  config.chunk_queue_capacity = capacity;
  return config;
}

// Accumulates the request text until the terminal chunk. Returns true as soon as that chunk has arrived.
struct RequestDrain {
  std::string text;
  bool saw_final = false;
  bool final_is_error = false;
  std::string final_error;

  bool Pump(const std::shared_ptr<IWebCoordinator>& coordinator, WebConsumerId consumer, WebRequestToken token) {
    for (const auto& chunk : coordinator->TakeChunkResults(consumer, token, 200).items) {
      text += ChunkText(chunk);
      if (chunk.meta.is_final) {
        saw_final = true;
        final_is_error = chunk.meta.is_error;
        final_error = chunk.meta.error;
      }
    }
    return saw_final;
  }
};

struct CoordinatorFixture {
  std::shared_ptr<IWebService> service = CreateWebService(WebServiceConfig{});
  std::shared_ptr<IWebCoordinator> coordinator = CreateWebCoordinator(service);
  std::shared_ptr<FakeWebTransport> transport = std::make_shared<FakeWebTransport>();

  CoordinatorFixture() {
    transport->SetEchoMode(true);
    service->AddTransport("p", transport);
  }

  SubmitResult Submit(WebConsumerId consumer, const std::string& body) {
    return coordinator->Submit(
      consumer, "p", WebStreamDescriptor{}, TextFrames(body), std::make_shared<PassthroughDecoder>()
    );
  }

  bool DrainUntilFinal(WebConsumerId consumer, WebRequestToken token, RequestDrain& drain) {
    return WaitFor([&]() {
      return drain.Pump(coordinator, consumer, token);
    });
  }
};

} // namespace

TEST_CASE("Coordinator EnsureConsumer is idempotent by name", "[web][coordinator]") {
  CoordinatorFixture fx;
  const auto a = fx.coordinator->EnsureConsumer(ConsumerNamed("node"));
  const auto b = fx.coordinator->EnsureConsumer(ConsumerNamed("node"));
  REQUIRE(a == b);
  const auto c = fx.coordinator->RegisterConsumer(ConsumerNamed("other"));
  REQUIRE(c != a);
}

TEST_CASE("Coordinator EnsureConsumer keeps the record until the last owner leaves", "[web][coordinator]") {
  CoordinatorFixture fx;
  const auto a = fx.coordinator->EnsureConsumer(ConsumerNamed("shared"));
  const auto b = fx.coordinator->EnsureConsumer(ConsumerNamed("shared"));
  REQUIRE(a == b);

  fx.coordinator->UnregisterConsumer(a);
  REQUIRE(fx.Submit(b, "still alive").accepted);

  fx.coordinator->UnregisterConsumer(b);
  REQUIRE_FALSE(fx.Submit(b, "gone").accepted);
}

TEST_CASE("Coordinator RegisterConsumer never shares a record with the same name", "[web][coordinator]") {
  CoordinatorFixture fx;
  const auto a = fx.coordinator->RegisterConsumer(ConsumerNamed("llm_chat"));
  auto scoped = ConsumerNamed("llm_chat");
  scoped.visible_provider_ids = {"elsewhere"};
  const auto b = fx.coordinator->RegisterConsumer(scoped);

  REQUIRE(a != b);
  // The config of the second record is its own, not inherited from the first one.
  REQUIRE(fx.Submit(a, "q").accepted);
  REQUIRE_FALSE(fx.Submit(b, "q").accepted);
}

TEST_CASE("Coordinator submit to unknown consumer is rejected", "[web][coordinator]") {
  CoordinatorFixture fx;
  REQUIRE_FALSE(fx.Submit(999, "q").accepted);
}

TEST_CASE("Coordinator submit with no provider is rejected and frees the token", "[web][coordinator]") {
  auto service = CreateWebService(WebServiceConfig{});
  auto coordinator = CreateWebCoordinator(service);
  const auto consumer = coordinator->RegisterConsumer(ConsumerNamed("node"));
  const auto result = coordinator->Submit(
    consumer, "p", WebStreamDescriptor{}, TextFrames("q"), std::make_shared<PassthroughDecoder>()
  );
  REQUIRE_FALSE(result.accepted);
  REQUIRE_FALSE(coordinator->HasRequest(consumer, 1));
}

TEST_CASE("Coordinator routes chunks to the owning request and drains them", "[web][coordinator]") {
  CoordinatorFixture fx;
  const auto consumer = fx.coordinator->RegisterConsumer(ConsumerNamed("node"));
  const auto submit = fx.Submit(consumer, "hello world");
  REQUIRE(submit.accepted);

  RequestDrain drain;
  REQUIRE(fx.DrainUntilFinal(consumer, submit.token, drain));
  REQUIRE(drain.text == "hello world");
}

TEST_CASE("Coordinator keeps two requests of one consumer apart", "[web][coordinator]") {
  CoordinatorFixture fx;
  fx.transport->SetEchoMode(false);
  // One registration, two concurrent requests: their chunks must not merge into a common queue.
  const auto consumer = fx.coordinator->RegisterConsumer(ConsumerNamed("shared"));

  const auto first = fx.Submit(consumer, "a");
  const auto first_stream = fx.transport->WaitSentStream(0);
  REQUIRE(first_stream != 0);

  const auto second = fx.Submit(consumer, "b");
  const auto second_stream = fx.transport->WaitSentStream(1);
  REQUIRE(second_stream != 0);
  REQUIRE(first.token != second.token);

  fx.transport->Emit(first_stream, "FIRST");
  fx.transport->Emit(second_stream, "SECOND");
  fx.transport->EmitClose(first_stream, WebError{});
  fx.transport->EmitClose(second_stream, WebError{});

  RequestDrain first_drain;
  RequestDrain second_drain;
  REQUIRE(fx.DrainUntilFinal(consumer, first.token, first_drain));
  REQUIRE(fx.DrainUntilFinal(consumer, second.token, second_drain));
  REQUIRE(first_drain.text == "FIRST");
  REQUIRE(second_drain.text == "SECOND");
}

TEST_CASE("Coordinator does not hand a request to a foreign consumer", "[web][coordinator]") {
  CoordinatorFixture fx;
  const auto owner = fx.coordinator->RegisterConsumer(ConsumerNamed("owner"));
  const auto other = fx.coordinator->RegisterConsumer(ConsumerNamed("other"));
  const auto submit = fx.Submit(owner, "mine");
  REQUIRE(submit.accepted);

  REQUIRE(WaitFor([&]() {
    return fx.coordinator->HasRequest(owner, submit.token);
  }));
  REQUIRE_FALSE(fx.coordinator->HasRequest(other, submit.token));
  // A foreign consumer can neither read nor consume the chunks of the owner.
  REQUIRE(fx.coordinator->TakeChunkResults(other, submit.token, 100).items.empty());

  RequestDrain drain;
  REQUIRE(fx.DrainUntilFinal(owner, submit.token, drain));
  REQUIRE(drain.text == "mine");
}

TEST_CASE("Coordinator isolates chunks between consumers", "[web][coordinator]") {
  CoordinatorFixture fx;
  const auto a = fx.coordinator->RegisterConsumer(ConsumerNamed("a"));
  const auto b = fx.coordinator->RegisterConsumer(ConsumerNamed("b"));

  const auto submit = fx.Submit(a, "alpha");
  RequestDrain drain;
  REQUIRE(fx.DrainUntilFinal(a, submit.token, drain));
  REQUIRE(drain.text == "alpha");
  REQUIRE(fx.coordinator->TakeChunkResults(b, submit.token, 100).items.empty());
}

TEST_CASE("Coordinator releases a request once its final chunk is drained", "[web][coordinator]") {
  CoordinatorFixture fx;
  const auto consumer = fx.coordinator->RegisterConsumer(ConsumerNamed("node"));
  const auto submit = fx.Submit(consumer, "bye");

  RequestDrain drain;
  REQUIRE(fx.DrainUntilFinal(consumer, submit.token, drain));

  REQUIRE_FALSE(fx.coordinator->HasRequest(consumer, submit.token));
  REQUIRE_FALSE(fx.coordinator->CancelRequest(consumer, submit.token));
  REQUIRE(fx.coordinator->TakeChunkResults(consumer, submit.token, 100).items.empty());
}

TEST_CASE("Coordinator drops chunks that arrive after the terminal one", "[web][coordinator]") {
  CoordinatorFixture fx;
  fx.transport->SetEchoMode(false);
  const auto consumer = fx.coordinator->RegisterConsumer(ConsumerNamed("node"));
  const auto submit = fx.Submit(consumer, "q");
  const auto stream_id = fx.transport->WaitSentStream();
  REQUIRE(stream_id != 0);

  fx.transport->Emit(stream_id, "before");
  fx.transport->EmitClose(stream_id, WebError{});
  // A late frame after the close: the decoder could still be finishing on another thread.
  fx.transport->Emit(stream_id, "after");

  RequestDrain drain;
  REQUIRE(fx.DrainUntilFinal(consumer, submit.token, drain));
  REQUIRE(drain.text == "before");
}

TEST_CASE("Coordinator cancel only succeeds for the owning consumer", "[web][coordinator]") {
  CoordinatorFixture fx;
  const auto owner = fx.coordinator->RegisterConsumer(ConsumerNamed("owner"));
  const auto other = fx.coordinator->RegisterConsumer(ConsumerNamed("other"));

  fx.transport->SetEchoMode(false);
  const auto submit = fx.coordinator->Submit(
    owner, "p", WebStreamDescriptor{}, TextFrames("q"), std::make_shared<PassthroughDecoder>()
  );
  REQUIRE(WaitFor([&]() {
    return fx.transport->OpenStreamCount() >= 1;
  }));

  REQUIRE_FALSE(fx.coordinator->CancelRequest(other, submit.token));
  REQUIRE(fx.coordinator->CancelRequest(owner, submit.token));
  REQUIRE_FALSE(fx.coordinator->HasRequest(owner, submit.token));
}

TEST_CASE("Coordinator rejects a submit without a provider id", "[web][coordinator]") {
  CoordinatorFixture fx;
  const auto consumer = fx.coordinator->RegisterConsumer(ConsumerNamed("node"));
  const auto result = fx.coordinator->Submit(
    consumer, "", WebStreamDescriptor{}, TextFrames("q"), std::make_shared<PassthroughDecoder>()
  );
  REQUIRE_FALSE(result.accepted);
  REQUIRE(result.error_message == "provider_id is required");
  REQUIRE(fx.transport->OpenStreamCount() == 0);
}

TEST_CASE("Coordinator hides providers outside the consumer visibility list", "[web][coordinator]") {
  CoordinatorFixture fx;
  auto config = ConsumerNamed("scoped");
  config.visible_provider_ids = {"other"};
  const auto consumer = fx.coordinator->RegisterConsumer(config);

  const auto result = fx.Submit(consumer, "q");
  REQUIRE_FALSE(result.accepted);
  REQUIRE(result.error_message.find("not visible") != std::string::npos);
  REQUIRE(fx.transport->OpenStreamCount() == 0);
}

TEST_CASE("Coordinator allows a provider listed as visible", "[web][coordinator]") {
  CoordinatorFixture fx;
  auto config = ConsumerNamed("scoped");
  config.visible_provider_ids = {"p"};
  const auto consumer = fx.coordinator->RegisterConsumer(config);

  REQUIRE(fx.Submit(consumer, "q").accepted);
}

TEST_CASE("Coordinator without a visibility list accepts any provider", "[web][coordinator]") {
  CoordinatorFixture fx;
  const auto consumer = fx.coordinator->RegisterConsumer(ConsumerNamed("open"));
  REQUIRE(fx.Submit(consumer, "q").accepted);
}

TEST_CASE("Coordinator drops an overflowing request with a terminal error", "[web][coordinator]") {
  CoordinatorFixture fx;
  fx.transport->SetEchoMode(false);
  const auto consumer = fx.coordinator->RegisterConsumer(ConsumerNamed("small", 4));
  const auto submit = fx.coordinator->Submit(
    consumer, "p", WebStreamDescriptor{}, TextFrames("q"), std::make_shared<PassthroughDecoder>()
  );
  REQUIRE(submit.accepted);
  const auto stream_id = fx.transport->WaitSentStream();
  REQUIRE(stream_id != 0);

  for (int i = 0; i < 16; ++i) {
    fx.transport->Emit(stream_id, "chunk");
  }

  RequestDrain drain;
  REQUIRE(fx.DrainUntilFinal(consumer, submit.token, drain));
  REQUIRE(drain.final_is_error);
  REQUIRE(drain.final_error.find("overflow") != std::string::npos);

  REQUIRE(WaitFor([&]() {
    return !fx.transport->CloseStreamCalls().empty();
  }));
}

TEST_CASE("Coordinator overflow keeps the queue of another request", "[web][coordinator]") {
  CoordinatorFixture fx;
  fx.transport->SetEchoMode(false);
  const auto consumer = fx.coordinator->RegisterConsumer(ConsumerNamed("shared", 6));

  const auto slow = fx.coordinator->Submit(
    consumer, "p", WebStreamDescriptor{}, TextFrames("a"), std::make_shared<PassthroughDecoder>()
  );
  const auto slow_stream = fx.transport->WaitSentStream(0);
  REQUIRE(slow_stream != 0);

  const auto other = fx.coordinator->Submit(
    consumer, "p", WebStreamDescriptor{}, TextFrames("b"), std::make_shared<PassthroughDecoder>()
  );
  const auto other_stream = fx.transport->WaitSentStream(1);
  REQUIRE(other_stream != 0);

  for (int i = 0; i < 32; ++i) {
    fx.transport->Emit(slow_stream, "flood");
  }
  fx.transport->Emit(other_stream, "keep");
  fx.transport->EmitClose(other_stream, WebError{});

  RequestDrain slow_drain;
  RequestDrain other_drain;
  REQUIRE(fx.DrainUntilFinal(consumer, slow.token, slow_drain));
  REQUIRE(slow_drain.final_is_error);

  REQUIRE(fx.DrainUntilFinal(consumer, other.token, other_drain));
  REQUIRE(other_drain.text == "keep");
  REQUIRE_FALSE(other_drain.final_is_error);
}

TEST_CASE("Coordinator cancels in-flight requests of an unregistered consumer", "[web][coordinator]") {
  CoordinatorFixture fx;
  fx.transport->SetEchoMode(false);
  const auto consumer = fx.coordinator->RegisterConsumer(ConsumerNamed("leaving"));
  const auto submit = fx.coordinator->Submit(
    consumer, "p", WebStreamDescriptor{}, TextFrames("q"), std::make_shared<PassthroughDecoder>()
  );
  REQUIRE(submit.accepted);
  REQUIRE(WaitFor([&]() {
    return fx.transport->OpenStreamCount() >= 1;
  }));
  const auto stream_id = fx.transport->OpenedStreams().front();

  fx.coordinator->UnregisterConsumer(consumer);

  REQUIRE(WaitFor([&]() {
    const auto calls = fx.transport->CloseStreamCalls();
    return std::find(calls.begin(), calls.end(), stream_id) != calls.end();
  }));
  REQUIRE_FALSE(fx.coordinator->HasRequest(consumer, submit.token));
  REQUIRE(fx.coordinator->TakeChunkResults(consumer, submit.token, 100).items.empty());
}

TEST_CASE("Coordinator unregister leaves other consumers running", "[web][coordinator]") {
  CoordinatorFixture fx;
  const auto leaving = fx.coordinator->RegisterConsumer(ConsumerNamed("leaving"));
  const auto staying = fx.coordinator->RegisterConsumer(ConsumerNamed("staying"));

  fx.transport->SetEchoMode(false);
  const auto abandoned = fx.coordinator->Submit(
    leaving, "p", WebStreamDescriptor{}, TextFrames("q"), std::make_shared<PassthroughDecoder>()
  );
  REQUIRE(WaitFor([&]() {
    return fx.transport->OpenStreamCount() >= 1;
  }));
  fx.coordinator->UnregisterConsumer(leaving);

  fx.transport->SetEchoMode(true);
  const auto alive = fx.Submit(staying, "still here");
  REQUIRE(alive.accepted);
  REQUIRE(alive.token != abandoned.token);

  RequestDrain drain;
  REQUIRE(fx.DrainUntilFinal(staying, alive.token, drain));
  REQUIRE(drain.text == "still here");
}

namespace {

class CompletedUnitsDecoder : public IWebResponseDecoder {
public:
  std::vector<WebDecodedChunk> DecodeFrame(WebStreamId, const WebFrame& frame) override {
    return {MakeTextChunk(std::string(frame.bytes.begin(), frame.bytes.end()), /*is_complete=*/true)};
  }
  std::vector<WebDecodedChunk> Finalize(WebStreamId, const WebError& error) override {
    return {MakeTerminalChunk(error)};
  }
};

} // namespace

TEST_CASE("Coordinator keeps a duplex stream alive across completed units", "[web][coordinator]") {
  CoordinatorFixture fx;
  fx.transport->SetEchoMode(false);
  const auto consumer = fx.coordinator->RegisterConsumer(ConsumerNamed("asr"));

  WebStreamDescriptor descriptor;
  descriptor.keep_send_open = true;
  const auto submit = fx.coordinator->Submit(
    consumer, "p", descriptor, TextFrames("start"), std::make_shared<CompletedUnitsDecoder>()
  );
  REQUIRE(submit.accepted);
  const auto stream_id = fx.transport->WaitSentStream();
  REQUIRE(stream_id != 0);

  std::vector<std::string> units;
  for (const auto& utterance : {"первая фраза", "вторая фраза", "третья фраза"}) {
    fx.transport->Emit(stream_id, utterance);
    REQUIRE(WaitFor([&]() {
      for (const auto& chunk : fx.coordinator->TakeChunkResults(consumer, submit.token, 10).items) {
        REQUIRE_FALSE(chunk.meta.is_final);
        const auto* text = ChunkPayloadAs<WebTextChunk>(chunk);
        REQUIRE(text != nullptr);
        REQUIRE(text->is_complete);
        units.push_back(text->text);
      }
      return !units.empty() && units.back() == utterance;
    }));
  }
  REQUIRE(units.size() == 3);
  REQUIRE(fx.coordinator->HasRequest(consumer, submit.token));

  fx.transport->EmitClose(stream_id, WebError{});
  RequestDrain drain;
  REQUIRE(fx.DrainUntilFinal(consumer, submit.token, drain));
}
