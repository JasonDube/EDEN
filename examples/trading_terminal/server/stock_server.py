"""
EDEN Trading Terminal - Stock Data Server

Fetches real stock data using Yahoo Finance (yfinance) for candles
and Finnhub for live quotes/search. Integrates Tastytrade broker for
sandbox/live trading and an algo engine for strategy backtesting + live execution.

Usage:
    cd examples/trading_terminal/server
    uv venv && source .venv/bin/activate
    uv pip install -r requirements.txt
    python stock_server.py
"""

import os
import sys
import time
import logging
from typing import Optional

import httpx
import yfinance as yf
from fastapi import FastAPI, Query, HTTPException
from fastapi.middleware.cors import CORSMiddleware
from pydantic import BaseModel
import uvicorn
from dotenv import load_dotenv

load_dotenv()

logging.basicConfig(level=logging.INFO)
logger = logging.getLogger("stock_server")

FINNHUB_API_KEY = os.getenv("FINNHUB_API_KEY", "")
FINNHUB_BASE = "https://finnhub.io/api/v1"

app = FastAPI(title="EDEN Trading Terminal Server")

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_methods=["*"],
    allow_headers=["*"],
)

# Simple in-memory cache: key -> (timestamp, data)
_cache: dict[str, tuple[float, any]] = {}
CACHE_TTL = 60  # seconds


def _cache_key(endpoint: str, **params) -> str:
    sorted_params = "&".join(f"{k}={v}" for k, v in sorted(params.items()))
    return f"{endpoint}?{sorted_params}"


def _get_cached(key: str):
    if key in _cache:
        ts, data = _cache[key]
        if time.time() - ts < CACHE_TTL:
            return data
        del _cache[key]
    return None


def _set_cached(key: str, data):
    _cache[key] = (time.time(), data)


async def _finnhub_get(path: str, params: dict) -> dict:
    if not FINNHUB_API_KEY:
        raise HTTPException(status_code=500, detail="FINNHUB_API_KEY not set in .env")
    params["token"] = FINNHUB_API_KEY
    cache_key = _cache_key(path, **{k: v for k, v in params.items() if k != "token"})
    cached = _get_cached(cache_key)
    if cached is not None:
        return cached
    async with httpx.AsyncClient() as client:
        resp = await client.get(f"{FINNHUB_BASE}{path}", params=params, timeout=10.0)
        if resp.status_code != 200:
            raise HTTPException(status_code=resp.status_code, detail=resp.text)
        data = resp.json()
        _set_cached(cache_key, data)
        return data


# Map our resolution codes to yfinance intervals and periods
YF_INTERVAL_MAP = {
    "1": "1m",
    "5": "5m",
    "15": "15m",
    "30": "30m",
    "60": "1h",
    "D": "1d",
    "W": "1wk",
    "M": "1mo",
}

YF_PERIOD_MAP = {
    "1": "5d",      # 1-min: max 7 days
    "5": "5d",
    "15": "5d",
    "30": "1mo",
    "60": "1mo",
    "D": "6mo",
    "W": "2y",
    "M": "5y",
}


# --- Broker + Algo Engine initialization ---
from broker import TastytradeBroker
from algo_engine import AlgoEngine

broker = TastytradeBroker()
algo_engine = AlgoEngine(broker=broker)


@app.on_event("startup")
async def startup_broker():
    await broker.initialize()


# ============================================================
# Existing endpoints (market data)
# ============================================================

@app.get("/health")
async def health():
    return {
        "status": "ok",
        "source": "yfinance+finnhub",
        "broker": "configured" if broker.configured else "mock",
        "sandbox": broker.sandbox,
    }


@app.get("/candles")
async def candles(
    symbol: str = Query(..., description="Ticker symbol, e.g. AAPL"),
    resolution: str = Query("D", description="1, 5, 15, 30, 60, D, W, M"),
):
    """Fetch OHLCV candle data via yfinance. Returns per-candle JSON array."""
    cache_key = _cache_key("candles", symbol=symbol.upper(), resolution=resolution)
    cached = _get_cached(cache_key)
    if cached is not None:
        return cached

    interval = YF_INTERVAL_MAP.get(resolution, "1d")
    period = YF_PERIOD_MAP.get(resolution, "6mo")

    try:
        ticker = yf.Ticker(symbol.upper())
        df = ticker.history(period=period, interval=interval)
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"yfinance error: {e}")

    if df.empty:
        result = {"symbol": symbol.upper(), "candles": [], "status": "no_data"}
        return result

    candle_list = []
    for ts, row in df.iterrows():
        candle_list.append({
            "t": int(ts.timestamp()),
            "o": round(float(row["Open"]), 4),
            "h": round(float(row["High"]), 4),
            "l": round(float(row["Low"]), 4),
            "c": round(float(row["Close"]), 4),
            "v": int(row["Volume"]),
        })

    result = {
        "symbol": symbol.upper(),
        "resolution": resolution,
        "candles": candle_list,
    }
    _set_cached(cache_key, result)
    return result


@app.get("/quote")
async def quote(symbol: str = Query(..., description="Ticker symbol")):
    """Fetch latest quote. Uses Finnhub if key available, otherwise yfinance."""
    if FINNHUB_API_KEY:
        try:
            raw = await _finnhub_get("/quote", {"symbol": symbol.upper()})
            return {
                "symbol": symbol.upper(),
                "current": raw.get("c", 0),
                "high": raw.get("h", 0),
                "low": raw.get("l", 0),
                "open": raw.get("o", 0),
                "previousClose": raw.get("pc", 0),
                "change": raw.get("d", 0),
                "changePercent": raw.get("dp", 0),
                "timestamp": raw.get("t", 0),
            }
        except Exception:
            pass  # Fall through to yfinance

    # yfinance fallback
    try:
        ticker = yf.Ticker(symbol.upper())
        info = ticker.fast_info
        current = float(info.last_price)
        prev = float(info.previous_close)
        change = current - prev
        change_pct = (change / prev * 100) if prev else 0
        return {
            "symbol": symbol.upper(),
            "current": round(current, 2),
            "high": round(float(info.day_high), 2) if info.day_high else 0,
            "low": round(float(info.day_low), 2) if info.day_low else 0,
            "open": round(float(info.open), 2) if info.open else 0,
            "previousClose": round(prev, 2),
            "change": round(change, 2),
            "changePercent": round(change_pct, 2),
            "timestamp": int(time.time()),
        }
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"Quote error: {e}")


@app.get("/search")
async def search(query: str = Query(..., description="Search query, e.g. 'apple'")):
    """Search for ticker symbols via Finnhub."""
    if FINNHUB_API_KEY:
        raw = await _finnhub_get("/search", {"q": query})
        results = []
        for item in raw.get("result", [])[:20]:
            results.append({
                "symbol": item.get("symbol", ""),
                "description": item.get("description", ""),
                "type": item.get("type", ""),
            })
        return {"query": query, "results": results}
    else:
        return {"query": query, "results": []}


# ============================================================
# Trading endpoints (Tastytrade broker)
# ============================================================

@app.get("/account")
async def get_account():
    """Get broker account info: buying power, equity, cash."""
    try:
        return await broker.get_account()
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"Account error: {e}")


@app.get("/positions")
async def get_positions():
    """Get open positions."""
    try:
        return await broker.get_positions()
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"Positions error: {e}")


class OrderRequest(BaseModel):
    symbol: str
    qty: float
    side: str               # "buy" or "sell"
    type: str = "market"    # "market" or "limit"
    limit_price: Optional[float] = None


@app.post("/order")
async def submit_order(req: OrderRequest):
    """Submit a buy/sell order."""
    try:
        return await broker.submit_order(
            symbol=req.symbol,
            qty=req.qty,
            side=req.side,
            order_type=req.type,
            limit_price=req.limit_price,
        )
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"Order error: {e}")


@app.delete("/order/{order_id}")
async def cancel_order(order_id: str):
    """Cancel an open order."""
    try:
        return await broker.cancel_order(order_id)
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"Cancel error: {e}")


@app.get("/orders")
async def get_orders(status: str = Query("open", description="open or all")):
    """List open or recent orders."""
    try:
        return await broker.get_orders(status=status)
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"Orders error: {e}")


# ============================================================
# Algo engine endpoints
# ============================================================

@app.get("/algo/strategies")
async def get_strategies():
    """List all available algo strategies with their parameters."""
    from strategies import list_strategies
    return list_strategies()


class BacktestRequest(BaseModel):
    strategy: str
    symbol: str
    resolution: str = "D"
    params: Optional[dict] = None


@app.post("/algo/backtest")
async def run_backtest(req: BacktestRequest):
    """Run a strategy backtest on historical data."""
    try:
        result = algo_engine.backtest(
            strategy_name=req.strategy,
            symbol=req.symbol,
            resolution=req.resolution,
            params=req.params,
        )
        if "error" in result:
            raise HTTPException(status_code=400, detail=result["error"])
        return result
    except HTTPException:
        raise
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"Backtest error: {e}")


class AlgoStartRequest(BaseModel):
    strategy: str
    symbol: str
    resolution: str = "D"
    params: Optional[dict] = None


@app.post("/algo/start")
async def start_algo(req: AlgoStartRequest):
    """Start a live algo execution."""
    try:
        result = algo_engine.start_live(
            strategy_name=req.strategy,
            symbol=req.symbol,
            resolution=req.resolution,
            params=req.params,
        )
        if "error" in result:
            raise HTTPException(status_code=400, detail=result["error"])
        return result
    except HTTPException:
        raise
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"Start algo error: {e}")


class AlgoStopRequest(BaseModel):
    strategy_id: str


@app.post("/algo/stop")
async def stop_algo(req: AlgoStopRequest):
    """Stop a running algo."""
    try:
        result = algo_engine.stop_live(req.strategy_id)
        if "error" in result:
            raise HTTPException(status_code=400, detail=result["error"])
        return result
    except HTTPException:
        raise
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"Stop algo error: {e}")


@app.get("/algo/status")
async def get_algo_status():
    """Get status of all running algos."""
    try:
        return algo_engine.get_status()
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"Algo status error: {e}")


if __name__ == "__main__":
    port = int(os.getenv("PORT", "8090"))
    print(f"EDEN Trading Terminal Server starting on port {port}")
    print(f"Finnhub API key: {'configured' if FINNHUB_API_KEY else 'not set (quotes via yfinance)'}")
    print(f"Tastytrade broker: {'configured' if broker.configured else 'mock mode (set TASTYTRADE_CLIENT_SECRET)'}")
    print(f"Sandbox: {broker.sandbox}")
    uvicorn.run(app, host="0.0.0.0", port=port)
