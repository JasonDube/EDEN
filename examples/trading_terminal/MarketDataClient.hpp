#pragma once

#include <string>
#include <vector>
#include <functional>
#include <thread>
#include <mutex>
#include <queue>
#include <atomic>
#include <cstdint>
#include <optional>

namespace trading {

struct Candle {
    int64_t timestamp = 0;
    float open = 0, high = 0, low = 0, close = 0;
    int64_t volume = 0;

    bool bullish() const { return close >= open; }
};

struct Quote {
    std::string symbol;
    float current = 0, high = 0, low = 0, open = 0;
    float previousClose = 0, change = 0, changePercent = 0;
};

struct SearchResult {
    std::string symbol;
    std::string description;
    std::string type;
};

// --- Trading structs ---

struct AccountInfo {
    float buyingPower = 0;
    float equity = 0;
    float cash = 0;
    float portfolioValue = 0;
    bool paper = true;
};

struct Position {
    std::string symbol;
    float qty = 0;
    float avgEntryPrice = 0;
    float currentPrice = 0;
    float unrealizedPl = 0;
    float unrealizedPlPct = 0;
    float marketValue = 0;
    std::string side;
};

struct OrderRequest {
    std::string symbol;
    float qty = 0;
    std::string side;           // "buy" or "sell"
    std::string type = "market"; // "market" or "limit"
    std::optional<float> limitPrice;
};

struct OrderResponse {
    std::string id;
    std::string symbol;
    std::string qty;
    std::string side;
    std::string type;
    std::string status;
    std::string error;
    bool success = false;
};

// --- Algo structs ---

struct TradeSignal {
    int64_t timestamp = 0;
    std::string side;       // "buy" or "sell"
    float price = 0;
    std::string reason;
    int candleIndex = -1;
};

struct AlgoStrategy {
    std::string name;
    std::vector<std::pair<std::string, std::string>> defaultParams; // name -> value
    // param_schema stored as raw JSON string for simplicity
    std::string paramSchemaJson;
};

struct AlgoStatus {
    std::string id;
    std::string strategy;
    std::string symbol;
    std::string status;
    std::vector<TradeSignal> signals;
    float pnl = 0;
};

struct BacktestResponse {
    std::vector<TradeSignal> signals;
    float totalPnl = 0;
    float winRate = 0;
    float sharpeRatio = 0;
    int totalTrades = 0;
    int winningTrades = 0;
    int losingTrades = 0;
    float maxDrawdown = 0;
    bool success = false;
    std::string error;
};

// --- Response wrappers ---

struct CandleResponse {
    std::string symbol;
    std::string resolution;
    std::vector<Candle> candles;
    bool success = false;
    std::string error;
};

struct QuoteResponse {
    Quote quote;
    bool success = false;
    std::string error;
};

struct SearchResponse {
    std::vector<SearchResult> results;
    bool success = false;
    std::string error;
};

struct AccountResponse {
    AccountInfo account;
    bool success = false;
    std::string error;
};

struct PositionsResponse {
    std::vector<Position> positions;
    bool success = false;
    std::string error;
};

struct OrderSubmitResponse {
    OrderResponse order;
    bool success = false;
    std::string error;
};

struct AlgoStrategiesResponse {
    std::vector<AlgoStrategy> strategies;
    bool success = false;
    std::string error;
};

struct AlgoStatusResponse {
    std::vector<AlgoStatus> algos;
    bool success = false;
    std::string error;
};

struct AlgoStartResponse {
    std::string id;
    std::string status;
    bool success = false;
    std::string error;
};

struct AlgoStopResponse {
    std::string id;
    std::string status;
    bool success = false;
    std::string error;
};

enum class RequestType {
    Candles, Quote, Search,
    Account, Positions, SubmitOrder, AlgoStrategies,
    Backtest, AlgoStart, AlgoStop, AlgoStatus
};

/**
 * HTTP client for the trading terminal Python server.
 * Runs requests on a background thread to avoid blocking rendering.
 */
class MarketDataClient {
public:
    using CandleCallback = std::function<void(const CandleResponse&)>;
    using QuoteCallback = std::function<void(const QuoteResponse&)>;
    using SearchCallback = std::function<void(const SearchResponse&)>;
    using AccountCallback = std::function<void(const AccountResponse&)>;
    using PositionsCallback = std::function<void(const PositionsResponse&)>;
    using OrderCallback = std::function<void(const OrderSubmitResponse&)>;
    using AlgoStrategiesCallback = std::function<void(const AlgoStrategiesResponse&)>;
    using BacktestCallback = std::function<void(const BacktestResponse&)>;
    using AlgoStartCallback = std::function<void(const AlgoStartResponse&)>;
    using AlgoStopCallback = std::function<void(const AlgoStopResponse&)>;
    using AlgoStatusCallback = std::function<void(const AlgoStatusResponse&)>;

    MarketDataClient(const std::string& host, int port);
    ~MarketDataClient();

    void start();
    void stop();
    bool isConnected() const { return m_connected; }

    // Market data
    void fetchCandles(const std::string& symbol, const std::string& resolution,
                      CandleCallback callback);
    void fetchQuote(const std::string& symbol, QuoteCallback callback);
    void fetchSearch(const std::string& query, SearchCallback callback);

    // Trading
    void fetchAccount(AccountCallback callback);
    void fetchPositions(PositionsCallback callback);
    void submitOrder(const OrderRequest& order, OrderCallback callback);

    // Algo
    void fetchAlgoStrategies(AlgoStrategiesCallback callback);
    void runBacktest(const std::string& strategy, const std::string& symbol,
                     const std::string& resolution, const std::string& paramsJson,
                     BacktestCallback callback);
    void startAlgo(const std::string& strategy, const std::string& symbol,
                   const std::string& resolution, const std::string& paramsJson,
                   AlgoStartCallback callback);
    void stopAlgo(const std::string& algoId, AlgoStopCallback callback);
    void fetchAlgoStatus(AlgoStatusCallback callback);

    // Call from main thread each frame to dispatch callbacks
    void pollResponses();

private:
    struct Request {
        RequestType type;
        std::string method = "GET";   // "GET", "POST", "DELETE"
        std::string path;
        std::string body;             // JSON body for POST

        CandleCallback candleCallback;
        QuoteCallback quoteCallback;
        SearchCallback searchCallback;
        AccountCallback accountCallback;
        PositionsCallback positionsCallback;
        OrderCallback orderCallback;
        AlgoStrategiesCallback algoStrategiesCallback;
        BacktestCallback backtestCallback;
        AlgoStartCallback algoStartCallback;
        AlgoStopCallback algoStopCallback;
        AlgoStatusCallback algoStatusCallback;
    };

    struct CompletedRequest {
        Request request;
        bool success = false;
        int statusCode = 0;
        std::string body;
        std::string error;
    };

    void workerThread();
    CompletedRequest executeRequest(const Request& request);
    void dispatchResponse(const CompletedRequest& completed);

    std::string m_host;
    int m_port;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_connected{false};
    std::thread m_workerThread;

    std::mutex m_requestMutex;
    std::queue<Request> m_requestQueue;

    std::mutex m_responseMutex;
    std::queue<CompletedRequest> m_responseQueue;
};

} // namespace trading
