#pragma once
#include "OrderMessage.hpp"
#include "SPSCQueue.hpp"

namespace cmf {

// ── TradingChannel ────────────────────────────────────────────────────────
//
// Bidirectional lock-free channel between one TradingEngine and the
// BacktestEngine. Uses two SPSC queues — one per direction.
//
// One TradingChannel instance per TradingEngine.
//
// Thread safety:
//   engine_to_backtest: TradingEngine is producer, BacktestEngine is consumer
//   backtest_to_engine: BacktestEngine is producer, TradingEngine is consumer

struct TradingChannel {
    // TradingEngine → BacktestEngine: NewOrder, CancelOrder, ModifyOrder
    SPSCQueue<OrderMsg, 4096> engine_to_backtest;

    // BacktestEngine → TradingEngine: OrderAck, OrderFill, OrderReject
    SPSCQueue<OrderMsg, 4096> backtest_to_engine;
};

} // namespace cmf
