// Group 1: Order & Trade Protocol tests
#include "OrderMessage.hpp"
#include "SPSCQueue.hpp"
#include "TradingChannel.hpp"
#include "TradingEngineAPI.hpp"

#include <catch2/catch.hpp>

#include <thread>
#include <vector>
#include <chrono>

using namespace cmf;

// ── Helper: build a NewOrder ───────────────────────────────────────────────
static OrderMsg make_new_order(OrderId oid, SecurityId iid,
                                Side side, int64_t price, int64_t size,
                                uint64_t engine_id = 1) {
    OrderMsg msg{};
    msg.new_order.hdr.msg_type          = MsgType::NewOrder;
    msg.new_order.hdr.order_id          = oid;
    msg.new_order.hdr.instrument_id     = iid;
    msg.new_order.hdr.trading_engine_id = engine_id;
    msg.new_order.hdr.timestamp_ns      = 1000000;
    msg.new_order.side                  = side;
    msg.new_order.price                 = price;
    msg.new_order.size                  = size;
    return msg;
}

// ── SPSCQueue tests ────────────────────────────────────────────────────────
TEST_CASE("SPSCQueue: empty on construction", "[spsc]") {
    SPSCQueue<OrderMsg, 16> q;
    REQUIRE(q.empty());
    REQUIRE(q.size() == 0);
}

TEST_CASE("SPSCQueue: push and pop single item", "[spsc]") {
    SPSCQueue<OrderMsg, 16> q;
    auto msg = make_new_order(1, 100, Side::Buy, 1180000000, 1000);

    REQUIRE(q.push(msg));
    REQUIRE_FALSE(q.empty());
    REQUIRE(q.size() == 1);

    auto opt = q.pop();
    REQUIRE(opt.has_value());
    REQUIRE(opt->hdr.msg_type == MsgType::NewOrder);
    REQUIRE(opt->new_order.hdr.order_id == 1);
    REQUIRE(opt->new_order.price == 1180000000);
    REQUIRE(q.empty());
}

TEST_CASE("SPSCQueue: pop empty returns nullopt", "[spsc]") {
    SPSCQueue<OrderMsg, 16> q;
    auto opt = q.pop();
    REQUIRE_FALSE(opt.has_value());
}

TEST_CASE("SPSCQueue: push until full returns false", "[spsc]") {
    SPSCQueue<OrderMsg, 4> q; // capacity=4, max items=3
    auto msg = make_new_order(1, 100, Side::Buy, 1180000000, 1000);

    REQUIRE(q.push(msg));
    REQUIRE(q.push(msg));
    REQUIRE(q.push(msg));
    REQUIRE_FALSE(q.push(msg)); // full
}

TEST_CASE("SPSCQueue: FIFO order preserved", "[spsc]") {
    SPSCQueue<OrderMsg, 16> q;
    for (int i = 1; i <= 5; ++i) {
        auto msg = make_new_order(i, 100, Side::Buy, 1180000000, 1000);
        REQUIRE(q.push(msg));
    }
    for (int i = 1; i <= 5; ++i) {
        auto opt = q.pop();
        REQUIRE(opt.has_value());
        REQUIRE(opt->hdr.order_id == static_cast<OrderId>(i));
    }
    REQUIRE(q.empty());
}

TEST_CASE("SPSCQueue: concurrent producer/consumer", "[spsc]") {
    SPSCQueue<OrderMsg, 1024> q;
    constexpr int N = 500;
    std::vector<OrderId> received;
    received.reserve(N);

    std::thread producer([&]{
        for (int i = 1; i <= N; ++i) {
            auto msg = make_new_order(i, 100, Side::Buy, 1180000000, 1000);
            while (!q.push(msg)) {} // spin if full
        }
    });

    std::thread consumer([&]{
        int count = 0;
        while (count < N) {
            auto opt = q.pop();
            if (opt) {
                received.push_back(opt->hdr.order_id);
                ++count;
            }
        }
    });

    producer.join();
    consumer.join();

    REQUIRE(received.size() == N);
    for (int i = 0; i < N; ++i)
        REQUIRE(received[i] == static_cast<OrderId>(i + 1));
}

// ── OrderMessage tests ─────────────────────────────────────────────────────
TEST_CASE("OrderMessage: NewOrder fields", "[msg]") {
    auto msg = make_new_order(42, 436, Side::Sell, 1200000000, 50000, 2);
    REQUIRE(msg.hdr.msg_type          == MsgType::NewOrder);
    REQUIRE(msg.hdr.order_id          == 42);
    REQUIRE(msg.hdr.instrument_id     == 436);
    REQUIRE(msg.hdr.trading_engine_id == 2);
    REQUIRE(msg.new_order.side        == Side::Sell);
    REQUIRE(msg.new_order.price       == 1200000000);
    REQUIRE(msg.new_order.size        == 50000);
}

TEST_CASE("OrderMessage: CancelOrder fields", "[msg]") {
    OrderMsg msg{};
    msg.cancel_order.hdr.msg_type          = MsgType::CancelOrder;
    msg.cancel_order.hdr.order_id          = 99;
    msg.cancel_order.hdr.instrument_id     = 436;
    msg.cancel_order.hdr.trading_engine_id = 1;
    msg.cancel_order.hdr.timestamp_ns      = 9999;

    REQUIRE(msg.hdr.msg_type      == MsgType::CancelOrder);
    REQUIRE(msg.hdr.order_id      == 99);
    REQUIRE(msg.hdr.timestamp_ns  == 9999);
}

TEST_CASE("OrderMessage: OrderFill fields", "[msg]") {
    OrderMsg msg{};
    msg.fill.hdr.msg_type      = MsgType::OrderFill;
    msg.fill.hdr.order_id      = 7;
    msg.fill.fill_price        = 1185000000;
    msg.fill.fill_size         = 10000;
    msg.fill.remaining_size    = 40000;
    msg.fill.status            = OrdStatus::PartialFill;

    REQUIRE(msg.hdr.msg_type       == MsgType::OrderFill);
    REQUIRE(msg.fill.fill_price    == 1185000000);
    REQUIRE(msg.fill.remaining_size == 40000);
    REQUIRE(msg.fill.status        == OrdStatus::PartialFill);
}

// ── TradingChannel tests ───────────────────────────────────────────────────
TEST_CASE("TradingChannel: engine sends order, backtest receives", "[channel]") {
    TradingChannel ch;

    auto order = make_new_order(1, 436, Side::Buy, 1180000000, 100000);
    REQUIRE(ch.engine_to_backtest.push(order));

    auto opt = ch.engine_to_backtest.pop();
    REQUIRE(opt.has_value());
    REQUIRE(opt->hdr.msg_type == MsgType::NewOrder);
    REQUIRE(opt->new_order.size == 100000);
}

TEST_CASE("TradingChannel: backtest sends ack, engine receives", "[channel]") {
    TradingChannel ch;

    OrderMsg ack{};
    ack.ack.hdr.msg_type  = MsgType::OrderAck;
    ack.ack.hdr.order_id  = 1;
    ack.ack.status        = OrdStatus::Acked;
    REQUIRE(ch.backtest_to_engine.push(ack));

    auto opt = ch.backtest_to_engine.pop();
    REQUIRE(opt.has_value());
    REQUIRE(opt->hdr.msg_type == MsgType::OrderAck);
    REQUIRE(opt->ack.status   == OrdStatus::Acked);
}

// ── TradingEngineAPI tests ─────────────────────────────────────────────────

// Concrete engine that records callbacks
struct TestEngine : public TradingEngineAPI {
    int acks = 0, fills = 0, rejects = 0;

    TestEngine(uint64_t id, TradingChannel& ch)
        : TradingEngineAPI(id, ch) {}

    void onAck   (const OrderAck&)    noexcept override { ++acks;    }
    void onFill  (const OrderFill&)   noexcept override { ++fills;   }
    void onReject(const OrderReject&) noexcept override { ++rejects; }
};

TEST_CASE("TradingEngineAPI: sendOrder puts message in channel", "[api]") {
    TradingChannel ch;
    TestEngine engine(1, ch);

    auto oid = engine.sendOrder(436, Side::Buy, 1180000000, 50000);
    REQUIRE(oid == 1);

    auto opt = ch.engine_to_backtest.pop();
    REQUIRE(opt.has_value());
    REQUIRE(opt->hdr.msg_type          == MsgType::NewOrder);
    REQUIRE(opt->hdr.order_id          == 1);
    REQUIRE(opt->hdr.trading_engine_id == 1);
    REQUIRE(opt->new_order.side        == Side::Buy);
    REQUIRE(opt->new_order.price       == 1180000000);
}

TEST_CASE("TradingEngineAPI: order_id increments", "[api]") {
    TradingChannel ch;
    TestEngine engine(1, ch);

    auto oid1 = engine.sendOrder(436, Side::Buy,  1180000000, 10000);
    auto oid2 = engine.sendOrder(436, Side::Sell, 1190000000, 10000);
    REQUIRE(oid1 == 1);
    REQUIRE(oid2 == 2);
}

TEST_CASE("TradingEngineAPI: cancelOrder puts message in channel", "[api]") {
    TradingChannel ch;
    TestEngine engine(1, ch);

    engine.cancelOrder(436, 42);

    auto opt = ch.engine_to_backtest.pop();
    REQUIRE(opt.has_value());
    REQUIRE(opt->hdr.msg_type == MsgType::CancelOrder);
    REQUIRE(opt->hdr.order_id == 42);
}

TEST_CASE("TradingEngineAPI: processResponses fires onAck", "[api]") {
    TradingChannel ch;
    TestEngine engine(1, ch);

    OrderMsg ack{};
    ack.ack.hdr.msg_type = MsgType::OrderAck;
    ack.ack.hdr.order_id = 1;
    ack.ack.status       = OrdStatus::Acked;
    ch.backtest_to_engine.push(ack);

    engine.processResponses();
    REQUIRE(engine.acks == 1);
    REQUIRE(engine.fills == 0);
}

TEST_CASE("TradingEngineAPI: processResponses fires onFill", "[api]") {
    TradingChannel ch;
    TestEngine engine(1, ch);

    OrderMsg fill{};
    fill.fill.hdr.msg_type   = MsgType::OrderFill;
    fill.fill.hdr.order_id   = 1;
    fill.fill.fill_price     = 1180000000;
    fill.fill.fill_size      = 50000;
    fill.fill.remaining_size = 0;
    fill.fill.status         = OrdStatus::Filled;
    ch.backtest_to_engine.push(fill);

    engine.processResponses();
    REQUIRE(engine.fills == 1);
}

TEST_CASE("TradingEngineAPI: processResponses fires onReject", "[api]") {
    TradingChannel ch;
    TestEngine engine(1, ch);

    OrderMsg rej{};
    rej.reject.hdr.msg_type = MsgType::OrderReject;
    rej.reject.hdr.order_id = 1;
    rej.reject.reason       = RejectReason::InvalidPrice;
    ch.backtest_to_engine.push(rej);

    engine.processResponses();
    REQUIRE(engine.rejects == 1);
}

TEST_CASE("TradingEngineAPI: multiple engines on separate channels", "[api]") {
    TradingChannel ch1, ch2;
    TestEngine e1(1, ch1);
    TestEngine e2(2, ch2);

    e1.sendOrder(436, Side::Buy,  1180000000, 10000);
    e2.sendOrder(436, Side::Sell, 1190000000, 10000);

    auto m1 = ch1.engine_to_backtest.pop();
    auto m2 = ch2.engine_to_backtest.pop();

    REQUIRE(m1.has_value());
    REQUIRE(m2.has_value());
    REQUIRE(m1->hdr.trading_engine_id == 1);
    REQUIRE(m2->hdr.trading_engine_id == 2);
    REQUIRE(m1->new_order.side == Side::Buy);
    REQUIRE(m2->new_order.side == Side::Sell);
}
