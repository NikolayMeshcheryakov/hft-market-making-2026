#pragma once
#include "TradingChannel.hpp"
#include <ctime>

namespace cmf {

// ── TradingEngineAPI ──────────────────────────────────────────────────────
//
// C++ API for a single TradingEngine instance.
//
// Public interface:
//   sendOrder()    — submit new limit order, returns order_id
//   cancelOrder()  — cancel resting order
//   modifyOrder()  — modify price/size of resting order
//   onAck()        — callback: order acknowledged
//   onFill()       — callback: order filled (partial or full)
//   onReject()     — callback: order rejected
//   processResponses() — drain backtest→engine queue, fires callbacks

class TradingEngineAPI {
public:
    TradingEngineAPI(uint64_t engine_id, TradingChannel& channel)
        : engine_id_(engine_id)
        , ch_(channel)
        , next_oid_(1)
    {}

    // ── Outbound (Engine → Backtest) ──────────────────────────────────────

    // Submit new limit order. Returns assigned order_id.
    OrderId sendOrder(SecurityId instrument_id,
                      Side      side,
                      int64_t   price,
                      int64_t   size) noexcept {
        const OrderId oid = next_oid_++;
        OrderMsg msg{};
        msg.new_order.hdr.msg_type          = MsgType::NewOrder;
        msg.new_order.hdr.order_id          = oid;
        msg.new_order.hdr.instrument_id     = instrument_id;
        msg.new_order.hdr.trading_engine_id = engine_id_;
        msg.new_order.hdr.timestamp_ns      = now_ns();
        msg.new_order.side                  = side;
        msg.new_order.price                 = price;
        msg.new_order.size                  = size;
        ch_.engine_to_backtest.push(msg);
        return oid;
    }

    // Cancel resting order by order_id.
    void cancelOrder(SecurityId instrument_id,
                     OrderId    order_id) noexcept {
        OrderMsg msg{};
        msg.cancel_order.hdr.msg_type          = MsgType::CancelOrder;
        msg.cancel_order.hdr.order_id          = order_id;
        msg.cancel_order.hdr.instrument_id     = instrument_id;
        msg.cancel_order.hdr.trading_engine_id = engine_id_;
        msg.cancel_order.hdr.timestamp_ns      = now_ns();
        ch_.engine_to_backtest.push(msg);
    }

    // Modify price and/or size of resting order.
    void modifyOrder(SecurityId instrument_id,
                     OrderId    order_id,
                     int64_t    new_price,
                     int64_t    new_size) noexcept {
        OrderMsg msg{};
        msg.modify_order.hdr.msg_type          = MsgType::ModifyOrder;
        msg.modify_order.hdr.order_id          = order_id;
        msg.modify_order.hdr.instrument_id     = instrument_id;
        msg.modify_order.hdr.trading_engine_id = engine_id_;
        msg.modify_order.hdr.timestamp_ns      = now_ns();
        msg.modify_order.new_price             = new_price;
        msg.modify_order.new_size              = new_size;
        ch_.engine_to_backtest.push(msg);
    }

    // ── Inbound (Backtest → Engine) ───────────────────────────────────────

    // Drain response queue and fire callbacks. Call from engine thread.
    void processResponses() noexcept {
        while (true) {
            auto opt = ch_.backtest_to_engine.pop();
            if (!opt) break;
            const OrderMsg& m = *opt;
            switch (m.hdr.msg_type) {
                case MsgType::OrderAck:    onAck(m.ack);       break;
                case MsgType::OrderFill:   onFill(m.fill);     break;
                case MsgType::OrderReject: onReject(m.reject); break;
                default: break;
            }
        }
    }

    // ── Callbacks (override in subclass) ──────────────────────────────────
    virtual void onAck   (const OrderAck&    /*msg*/) noexcept {}
    virtual void onFill  (const OrderFill&   /*msg*/) noexcept {}
    virtual void onReject(const OrderReject& /*msg*/) noexcept {}

    virtual ~TradingEngineAPI() = default;

    uint64_t engineId() const noexcept { return engine_id_; }

protected:
    static NanoTime now_ns() noexcept {
        struct timespec ts{};
        clock_gettime(CLOCK_REALTIME, &ts);
        return static_cast<NanoTime>(ts.tv_sec) * 1'000'000'000LL
             + static_cast<NanoTime>(ts.tv_nsec);
    }

    uint64_t        engine_id_;
    TradingChannel& ch_;
    OrderId         next_oid_;
};

} // namespace cmf
