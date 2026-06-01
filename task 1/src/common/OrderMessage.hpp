#pragma once
#include "BasicTypes.hpp"

// ── Group 1: FIX-like Order & Trade Protocol ──────────────────────────────
//
// Message schema for communication between TradingEngine and BacktestEngine.
// All messages carry a common header with fields agreed across all groups.
//
// Types agreed in interface document:
//   instrument_id     → cmf::SecurityId (uint16_t, matches HW1)
//   timestamp_ns      → cmf::NanoTime   (int64_t, nanoseconds since epoch)
//   price             → int64_t, scaled × 1e9 (e.g. 1.18 → 1180000000)
//   order_id          → cmf::OrderId    (uint64_t)
//   trading_engine_id → uint64_t

namespace cmf
{

// ── Order status ──────────────────────────────────────────────────────────
enum class OrdStatus : uint8_t
{
    New = 0,
    Acked = 1,
    PartialFill = 2,
    Filled = 3,
    Cancelled = 4,
    Rejected = 5
};

// ── Message type tag ──────────────────────────────────────────────────────
enum class MsgType : uint8_t
{
    NewOrder = 0,
    CancelOrder = 1,
    ModifyOrder = 2,
    OrderAck = 3,
    OrderFill = 4,
    OrderReject = 5
};

// ── Reject reason codes ───────────────────────────────────────────────────
enum class RejectReason : uint8_t
{
    None = 0,
    UnknownSymbol = 1,
    InvalidPrice = 2,
    InvalidQuantity = 3,
    DuplicateOrder = 4,
    RiskLimitExceeded = 5
};

// ── Common header (every message starts with this) ────────────────────────
struct MsgHeader
{
    MsgType msg_type;
    OrderId order_id;           // uint64_t
    SecurityId instrument_id;   // uint16_t — matches HW1 instrument_id
    uint64_t trading_engine_id; // identifies which TradingEngine sent this
    NanoTime timestamp_ns;      // int64_t, nanoseconds since Unix epoch
};

// ── Engine → Backtest ─────────────────────────────────────────────────────

// Submit a new limit order
struct NewOrder
{
    MsgHeader hdr;
    Side side;     // Buy or Sell
    int64_t price; // scaled × 1e9
    int64_t size;  // quantity
};

// Cancel an existing resting order
struct CancelOrder
{
    MsgHeader hdr;
};

// Modify price or size of existing resting order
struct ModifyOrder
{
    MsgHeader hdr;
    int64_t new_price; // scaled × 1e9
    int64_t new_size;
};

// ── Backtest → Engine ─────────────────────────────────────────────────────

// Order accepted and resting in book
struct OrderAck
{
    MsgHeader hdr;
    OrdStatus status; // New or Acked
};

// Order partially or fully filled
struct OrderFill
{
    MsgHeader hdr;
    int64_t fill_price;     // scaled × 1e9
    int64_t fill_size;      // how much was filled this time
    int64_t remaining_size; // how much still resting
    OrdStatus status;       // PartialFill or Filled
};

// Order rejected by backtest engine
struct OrderReject
{
    MsgHeader hdr;
    RejectReason reason;
};

// ── Union for queue transport ─────────────────────────────────────────────
// All message types fit in one union — single queue element type.
union OrderMsg
{
    MsgHeader hdr; // always valid — read msg_type first
    NewOrder new_order;
    CancelOrder cancel_order;
    ModifyOrder modify_order;
    OrderAck ack;
    OrderFill fill;
    OrderReject reject;
};

} // namespace cmf
