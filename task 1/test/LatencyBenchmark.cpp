// Group 1: Round-trip latency benchmark
// Measures time from sendOrder() to onAck() callback.
//
// Run: build/bin/hft-market-making --benchmark
// Or:  build/bin/tests [latency]

#include "OrderMessage.hpp"
#include "TradingChannel.hpp"
#include "TradingEngineAPI.hpp"

#include <catch2/catch.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <numeric>
#include <thread>
#include <vector>

using namespace cmf;
using namespace std::chrono;

// ── Simple backtest simulator (runs in separate thread) ───────────────────
// Reads NewOrder from engine_to_backtest, immediately sends OrderAck back.
static void backtest_sim(TradingChannel& ch, std::atomic<bool>& stop)
{
    while (!stop.load(std::memory_order_relaxed))
    {
        auto opt = ch.engine_to_backtest.pop();
        if (!opt)
            continue;

        if (opt->hdr.msg_type == MsgType::NewOrder)
        {
            OrderMsg ack{};
            ack.ack.hdr.msg_type = MsgType::OrderAck;
            ack.ack.hdr.order_id = opt->hdr.order_id;
            ack.ack.hdr.instrument_id = opt->hdr.instrument_id;
            ack.ack.hdr.trading_engine_id = opt->hdr.trading_engine_id;
            ack.ack.hdr.timestamp_ns = opt->hdr.timestamp_ns;
            ack.ack.status = OrdStatus::Acked;
            while (!ch.backtest_to_engine.push(ack))
            {
            } // spin
        }
    }
}

// ── Engine that records round-trip timestamps ─────────────────────────────
struct BenchEngine : public TradingEngineAPI
{
    std::vector<int64_t> latencies_ns;

    int64_t send_ts = 0;

    BenchEngine(uint64_t id, TradingChannel& ch, int n)
        : TradingEngineAPI(id, ch)
    {
        latencies_ns.reserve(n);
    }

    void onAck(const OrderAck&) noexcept override
    {
        const int64_t recv_ts = now_ns();
        if (send_ts > 0)
            latencies_ns.push_back(recv_ts - send_ts);
    }

    OrderId sendAndRecord(SecurityId iid, Side side,
                          int64_t price, int64_t size) noexcept
    {
        send_ts = now_ns();
        return sendOrder(iid, side, price, size);
    }
};

TEST_CASE("Latency benchmark: round-trip sendOrder → onAck", "[latency]")
{
    constexpr int WARMUP = 100;
    constexpr int N = 1000;

    TradingChannel ch;
    BenchEngine engine(1, ch, N);

    std::atomic<bool> stop{false};
    std::thread sim([&]
                    { backtest_sim(ch, stop); });

    // Warmup
    for (int i = 0; i < WARMUP; ++i)
    {
        engine.sendAndRecord(436, Side::Buy, 1180000000, 1000);
        while (engine.latencies_ns.size() <= static_cast<size_t>(i))
            engine.processResponses();
    }
    engine.latencies_ns.clear();

    // Benchmark
    for (int i = 0; i < N; ++i)
    {
        engine.sendAndRecord(436, Side::Buy, 1180000000, 1000);
        while (static_cast<int>(engine.latencies_ns.size()) <= i)
            engine.processResponses();
    }

    stop = true;
    sim.join();

    REQUIRE(static_cast<int>(engine.latencies_ns.size()) == N);

    // Statistics
    auto lats = engine.latencies_ns;
    std::sort(lats.begin(), lats.end());

    const double mean = static_cast<double>(
                            std::accumulate(lats.begin(), lats.end(), int64_t(0))) /
                        N;

    printf("\n=== Round-trip Latency Benchmark (%d samples) ===\n", N);
    printf("  Mean   : %.0f ns\n", mean);
    printf("  Min    : %lld ns\n", (long long)lats.front());
    printf("  P50    : %lld ns\n", (long long)lats[N * 50 / 100]);
    printf("  P95    : %lld ns\n", (long long)lats[N * 95 / 100]);
    printf("  P99    : %lld ns\n", (long long)lats[N * 99 / 100]);
    printf("  Max    : %lld ns\n", (long long)lats.back());
    printf("=================================================\n\n");

    // Sanity: median should be under 100 microseconds in-process
    REQUIRE(lats[N / 2] < 100'000); // < 100 µs
}
