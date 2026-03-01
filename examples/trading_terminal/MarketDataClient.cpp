#include "MarketDataClient.hpp"

#define CPPHTTPLIB_NO_EXCEPTIONS
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
#undef CPPHTTPLIB_OPENSSL_SUPPORT
#endif
#include <httplib.h>
#include <nlohmann/json.hpp>

#include <iostream>
#include <sstream>

namespace trading {

MarketDataClient::MarketDataClient(const std::string& host, int port)
    : m_host(host), m_port(port) {}

MarketDataClient::~MarketDataClient() {
    stop();
}

void MarketDataClient::start() {
    if (m_running) return;
    m_running = true;
    m_workerThread = std::thread(&MarketDataClient::workerThread, this);
}

void MarketDataClient::stop() {
    m_running = false;
    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }
}

void MarketDataClient::workerThread() {
    while (m_running) {
        Request request;
        bool hasRequest = false;

        {
            std::lock_guard<std::mutex> lock(m_requestMutex);
            if (!m_requestQueue.empty()) {
                request = std::move(m_requestQueue.front());
                m_requestQueue.pop();
                hasRequest = true;
            }
        }

        if (hasRequest) {
            auto completed = executeRequest(request);
            {
                std::lock_guard<std::mutex> lock(m_responseMutex);
                m_responseQueue.push(std::move(completed));
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
}

MarketDataClient::CompletedRequest MarketDataClient::executeRequest(const Request& request) {
    CompletedRequest result;
    result.request = request;

    httplib::Client client(m_host, m_port);
    client.set_connection_timeout(5);
    client.set_read_timeout(10);

    httplib::Result httpResult;

    if (request.method == "POST") {
        httpResult = client.Post(request.path, request.body, "application/json");
    } else if (request.method == "DELETE") {
        httpResult = client.Delete(request.path);
    } else {
        httpResult = client.Get(request.path);
    }

    if (httpResult) {
        result.success = (httpResult->status >= 200 && httpResult->status < 300);
        result.statusCode = httpResult->status;
        result.body = httpResult->body;
        m_connected = true;
    } else {
        result.success = false;
        result.error = "Connection failed to " + m_host + ":" + std::to_string(m_port);
        m_connected = false;
    }

    return result;
}

void MarketDataClient::dispatchResponse(const CompletedRequest& completed) {
    const auto& req = completed.request;

    if (req.type == RequestType::Candles && req.candleCallback) {
        CandleResponse resp;
        if (completed.success) {
            try {
                auto j = nlohmann::json::parse(completed.body);
                resp.symbol = j.value("symbol", "");
                resp.resolution = j.value("resolution", "");
                resp.success = true;
                for (const auto& c : j["candles"]) {
                    Candle candle;
                    candle.timestamp = c.value("t", (int64_t)0);
                    candle.open = c.value("o", 0.0f);
                    candle.high = c.value("h", 0.0f);
                    candle.low = c.value("l", 0.0f);
                    candle.close = c.value("c", 0.0f);
                    candle.volume = c.value("v", (int64_t)0);
                    resp.candles.push_back(candle);
                }
            } catch (const std::exception& e) {
                resp.success = false;
                resp.error = std::string("JSON parse error: ") + e.what();
            }
        } else {
            resp.error = completed.error;
        }
        req.candleCallback(resp);

    } else if (req.type == RequestType::Quote && req.quoteCallback) {
        QuoteResponse resp;
        if (completed.success) {
            try {
                auto j = nlohmann::json::parse(completed.body);
                resp.quote.symbol = j.value("symbol", "");
                resp.quote.current = j.value("current", 0.0f);
                resp.quote.high = j.value("high", 0.0f);
                resp.quote.low = j.value("low", 0.0f);
                resp.quote.open = j.value("open", 0.0f);
                resp.quote.previousClose = j.value("previousClose", 0.0f);
                resp.quote.change = j.value("change", 0.0f);
                resp.quote.changePercent = j.value("changePercent", 0.0f);
                resp.success = true;
            } catch (const std::exception& e) {
                resp.error = std::string("JSON parse error: ") + e.what();
            }
        } else {
            resp.error = completed.error;
        }
        req.quoteCallback(resp);

    } else if (req.type == RequestType::Search && req.searchCallback) {
        SearchResponse resp;
        if (completed.success) {
            try {
                auto j = nlohmann::json::parse(completed.body);
                for (const auto& item : j["results"]) {
                    SearchResult sr;
                    sr.symbol = item.value("symbol", "");
                    sr.description = item.value("description", "");
                    sr.type = item.value("type", "");
                    resp.results.push_back(sr);
                }
                resp.success = true;
            } catch (const std::exception& e) {
                resp.error = std::string("JSON parse error: ") + e.what();
            }
        } else {
            resp.error = completed.error;
        }
        req.searchCallback(resp);

    } else if (req.type == RequestType::Account && req.accountCallback) {
        AccountResponse resp;
        if (completed.success) {
            try {
                auto j = nlohmann::json::parse(completed.body);
                resp.account.buyingPower = j.value("buying_power", 0.0f);
                resp.account.equity = j.value("equity", 0.0f);
                resp.account.cash = j.value("cash", 0.0f);
                resp.account.portfolioValue = j.value("portfolio_value", 0.0f);
                resp.account.paper = j.value("paper", true);
                resp.success = true;
            } catch (const std::exception& e) {
                resp.error = std::string("JSON parse error: ") + e.what();
            }
        } else {
            resp.error = completed.error;
        }
        req.accountCallback(resp);

    } else if (req.type == RequestType::Positions && req.positionsCallback) {
        PositionsResponse resp;
        if (completed.success) {
            try {
                auto j = nlohmann::json::parse(completed.body);
                for (const auto& item : j) {
                    Position pos;
                    pos.symbol = item.value("symbol", "");
                    pos.qty = item.value("qty", 0.0f);
                    pos.avgEntryPrice = item.value("avg_entry_price", 0.0f);
                    pos.currentPrice = item.value("current_price", 0.0f);
                    pos.unrealizedPl = item.value("unrealized_pl", 0.0f);
                    pos.unrealizedPlPct = item.value("unrealized_plpc", 0.0f);
                    pos.marketValue = item.value("market_value", 0.0f);
                    pos.side = item.value("side", "");
                    resp.positions.push_back(pos);
                }
                resp.success = true;
            } catch (const std::exception& e) {
                resp.error = std::string("JSON parse error: ") + e.what();
            }
        } else {
            resp.error = completed.error;
        }
        req.positionsCallback(resp);

    } else if (req.type == RequestType::SubmitOrder && req.orderCallback) {
        OrderSubmitResponse resp;
        if (completed.success) {
            try {
                auto j = nlohmann::json::parse(completed.body);
                if (j.contains("error")) {
                    resp.order.error = j.value("error", "");
                } else {
                    resp.order.id = j.value("id", "");
                    resp.order.symbol = j.value("symbol", "");
                    resp.order.qty = j.value("qty", "");
                    resp.order.side = j.value("side", "");
                    resp.order.type = j.value("type", "");
                    resp.order.status = j.value("status", "");
                    resp.order.success = true;
                }
                resp.success = true;
            } catch (const std::exception& e) {
                resp.error = std::string("JSON parse error: ") + e.what();
            }
        } else {
            resp.error = completed.error;
        }
        req.orderCallback(resp);

    } else if (req.type == RequestType::AlgoStrategies && req.algoStrategiesCallback) {
        AlgoStrategiesResponse resp;
        if (completed.success) {
            try {
                auto j = nlohmann::json::parse(completed.body);
                for (const auto& item : j) {
                    AlgoStrategy strat;
                    strat.name = item.value("name", "");
                    if (item.contains("default_params")) {
                        for (auto& [key, val] : item["default_params"].items()) {
                            strat.defaultParams.push_back({key, val.dump()});
                        }
                    }
                    if (item.contains("param_schema")) {
                        strat.paramSchemaJson = item["param_schema"].dump();
                    }
                    resp.strategies.push_back(strat);
                }
                resp.success = true;
            } catch (const std::exception& e) {
                resp.error = std::string("JSON parse error: ") + e.what();
            }
        } else {
            resp.error = completed.error;
        }
        req.algoStrategiesCallback(resp);

    } else if (req.type == RequestType::Backtest && req.backtestCallback) {
        BacktestResponse resp;
        if (completed.success) {
            try {
                auto j = nlohmann::json::parse(completed.body);
                resp.totalPnl = j.value("total_pnl", 0.0f);
                resp.winRate = j.value("win_rate", 0.0f);
                resp.sharpeRatio = j.value("sharpe_ratio", 0.0f);
                resp.totalTrades = j.value("total_trades", 0);
                resp.winningTrades = j.value("winning_trades", 0);
                resp.losingTrades = j.value("losing_trades", 0);
                resp.maxDrawdown = j.value("max_drawdown", 0.0f);
                if (j.contains("signals")) {
                    for (const auto& s : j["signals"]) {
                        TradeSignal sig;
                        sig.timestamp = s.value("timestamp", (int64_t)0);
                        sig.side = s.value("side", "");
                        sig.price = s.value("price", 0.0f);
                        sig.reason = s.value("reason", "");
                        sig.candleIndex = s.value("candle_index", -1);
                        resp.signals.push_back(sig);
                    }
                }
                resp.success = true;
            } catch (const std::exception& e) {
                resp.error = std::string("JSON parse error: ") + e.what();
            }
        } else {
            resp.error = completed.error;
        }
        req.backtestCallback(resp);

    } else if (req.type == RequestType::AlgoStart && req.algoStartCallback) {
        AlgoStartResponse resp;
        if (completed.success) {
            try {
                auto j = nlohmann::json::parse(completed.body);
                resp.id = j.value("id", "");
                resp.status = j.value("status", "");
                resp.success = true;
            } catch (const std::exception& e) {
                resp.error = std::string("JSON parse error: ") + e.what();
            }
        } else {
            resp.error = completed.error;
        }
        req.algoStartCallback(resp);

    } else if (req.type == RequestType::AlgoStop && req.algoStopCallback) {
        AlgoStopResponse resp;
        if (completed.success) {
            try {
                auto j = nlohmann::json::parse(completed.body);
                resp.id = j.value("id", "");
                resp.status = j.value("status", "");
                resp.success = true;
            } catch (const std::exception& e) {
                resp.error = std::string("JSON parse error: ") + e.what();
            }
        } else {
            resp.error = completed.error;
        }
        req.algoStopCallback(resp);

    } else if (req.type == RequestType::AlgoStatus && req.algoStatusCallback) {
        AlgoStatusResponse resp;
        if (completed.success) {
            try {
                auto j = nlohmann::json::parse(completed.body);
                for (const auto& item : j) {
                    AlgoStatus as;
                    as.id = item.value("id", "");
                    as.strategy = item.value("strategy", "");
                    as.symbol = item.value("symbol", "");
                    as.status = item.value("status", "");
                    as.pnl = item.value("pnl", 0.0f);
                    if (item.contains("signals")) {
                        for (const auto& s : item["signals"]) {
                            TradeSignal sig;
                            sig.timestamp = s.value("timestamp", (int64_t)0);
                            sig.side = s.value("side", "");
                            sig.price = s.value("price", 0.0f);
                            sig.reason = s.value("reason", "");
                            sig.candleIndex = s.value("candle_index", -1);
                            as.signals.push_back(sig);
                        }
                    }
                    resp.algos.push_back(as);
                }
                resp.success = true;
            } catch (const std::exception& e) {
                resp.error = std::string("JSON parse error: ") + e.what();
            }
        } else {
            resp.error = completed.error;
        }
        req.algoStatusCallback(resp);
    }
}

// ============================================================
// Market data fetch methods (existing)
// ============================================================

void MarketDataClient::fetchCandles(const std::string& symbol, const std::string& resolution,
                                     CandleCallback callback) {
    Request request;
    request.type = RequestType::Candles;
    request.path = "/candles?symbol=" + symbol + "&resolution=" + resolution;
    request.candleCallback = callback;

    std::lock_guard<std::mutex> lock(m_requestMutex);
    m_requestQueue.push(std::move(request));
}

void MarketDataClient::fetchQuote(const std::string& symbol, QuoteCallback callback) {
    Request request;
    request.type = RequestType::Quote;
    request.path = "/quote?symbol=" + symbol;
    request.quoteCallback = callback;

    std::lock_guard<std::mutex> lock(m_requestMutex);
    m_requestQueue.push(std::move(request));
}

void MarketDataClient::fetchSearch(const std::string& query, SearchCallback callback) {
    Request request;
    request.type = RequestType::Search;
    request.path = "/search?query=" + query;
    request.searchCallback = callback;

    std::lock_guard<std::mutex> lock(m_requestMutex);
    m_requestQueue.push(std::move(request));
}

// ============================================================
// Trading fetch methods (new)
// ============================================================

void MarketDataClient::fetchAccount(AccountCallback callback) {
    Request request;
    request.type = RequestType::Account;
    request.path = "/account";
    request.accountCallback = callback;

    std::lock_guard<std::mutex> lock(m_requestMutex);
    m_requestQueue.push(std::move(request));
}

void MarketDataClient::fetchPositions(PositionsCallback callback) {
    Request request;
    request.type = RequestType::Positions;
    request.path = "/positions";
    request.positionsCallback = callback;

    std::lock_guard<std::mutex> lock(m_requestMutex);
    m_requestQueue.push(std::move(request));
}

void MarketDataClient::submitOrder(const OrderRequest& order, OrderCallback callback) {
    Request request;
    request.type = RequestType::SubmitOrder;
    request.method = "POST";
    request.path = "/order";

    nlohmann::json body;
    body["symbol"] = order.symbol;
    body["qty"] = order.qty;
    body["side"] = order.side;
    body["type"] = order.type;
    if (order.limitPrice.has_value()) {
        body["limit_price"] = order.limitPrice.value();
    }
    request.body = body.dump();
    request.orderCallback = callback;

    std::lock_guard<std::mutex> lock(m_requestMutex);
    m_requestQueue.push(std::move(request));
}

// ============================================================
// Algo fetch methods (new)
// ============================================================

void MarketDataClient::fetchAlgoStrategies(AlgoStrategiesCallback callback) {
    Request request;
    request.type = RequestType::AlgoStrategies;
    request.path = "/algo/strategies";
    request.algoStrategiesCallback = callback;

    std::lock_guard<std::mutex> lock(m_requestMutex);
    m_requestQueue.push(std::move(request));
}

void MarketDataClient::runBacktest(const std::string& strategy, const std::string& symbol,
                                    const std::string& resolution, const std::string& paramsJson,
                                    BacktestCallback callback) {
    Request request;
    request.type = RequestType::Backtest;
    request.method = "POST";
    request.path = "/algo/backtest";

    nlohmann::json body;
    body["strategy"] = strategy;
    body["symbol"] = symbol;
    body["resolution"] = resolution;
    if (!paramsJson.empty()) {
        body["params"] = nlohmann::json::parse(paramsJson);
    }
    request.body = body.dump();
    request.backtestCallback = callback;

    std::lock_guard<std::mutex> lock(m_requestMutex);
    m_requestQueue.push(std::move(request));
}

void MarketDataClient::startAlgo(const std::string& strategy, const std::string& symbol,
                                  const std::string& resolution, const std::string& paramsJson,
                                  AlgoStartCallback callback) {
    Request request;
    request.type = RequestType::AlgoStart;
    request.method = "POST";
    request.path = "/algo/start";

    nlohmann::json body;
    body["strategy"] = strategy;
    body["symbol"] = symbol;
    body["resolution"] = resolution;
    if (!paramsJson.empty()) {
        body["params"] = nlohmann::json::parse(paramsJson);
    }
    request.body = body.dump();
    request.algoStartCallback = callback;

    std::lock_guard<std::mutex> lock(m_requestMutex);
    m_requestQueue.push(std::move(request));
}

void MarketDataClient::stopAlgo(const std::string& algoId, AlgoStopCallback callback) {
    Request request;
    request.type = RequestType::AlgoStop;
    request.method = "POST";
    request.path = "/algo/stop";

    nlohmann::json body;
    body["strategy_id"] = algoId;
    request.body = body.dump();
    request.algoStopCallback = callback;

    std::lock_guard<std::mutex> lock(m_requestMutex);
    m_requestQueue.push(std::move(request));
}

void MarketDataClient::fetchAlgoStatus(AlgoStatusCallback callback) {
    Request request;
    request.type = RequestType::AlgoStatus;
    request.path = "/algo/status";
    request.algoStatusCallback = callback;

    std::lock_guard<std::mutex> lock(m_requestMutex);
    m_requestQueue.push(std::move(request));
}

void MarketDataClient::pollResponses() {
    std::queue<CompletedRequest> toProcess;
    {
        std::lock_guard<std::mutex> lock(m_responseMutex);
        std::swap(toProcess, m_responseQueue);
    }

    while (!toProcess.empty()) {
        dispatchResponse(toProcess.front());
        toProcess.pop();
    }
}

} // namespace trading
