"""
Algo Engine — backtest and live execution for trading strategies.

Backtests strategies on historical data and runs live execution loops
that periodically check for new signals and optionally place orders.
"""

import time
import logging
import threading
import uuid
from dataclasses import dataclass, field, asdict
from typing import Optional

import pandas as pd
import numpy as np
import yfinance as yf

from strategies import get_strategy, list_strategies
from strategies.base import Signal, BacktestResult

logger = logging.getLogger("algo_engine")

# yfinance resolution mapping (same as stock_server)
YF_INTERVAL_MAP = {
    "1": "1m", "5": "5m", "15": "15m", "30": "30m",
    "60": "1h", "D": "1d", "W": "1wk", "M": "1mo",
}
YF_PERIOD_MAP = {
    "1": "5d", "5": "5d", "15": "5d", "30": "1mo",
    "60": "1mo", "D": "6mo", "W": "2y", "M": "5y",
}


def _fetch_dataframe(symbol: str, resolution: str) -> pd.DataFrame:
    """Fetch OHLCV data as DataFrame for strategy computation."""
    interval = YF_INTERVAL_MAP.get(resolution, "1d")
    period = YF_PERIOD_MAP.get(resolution, "6mo")

    ticker = yf.Ticker(symbol.upper())
    df = ticker.history(period=period, interval=interval)

    if df.empty:
        return pd.DataFrame()

    df = df.reset_index()
    # Normalize column names
    rename = {}
    for col in df.columns:
        lower = col.lower()
        if lower in ("date", "datetime"):
            rename[col] = "datetime"
        elif lower == "open":
            rename[col] = "open"
        elif lower == "high":
            rename[col] = "high"
        elif lower == "low":
            rename[col] = "low"
        elif lower == "close":
            rename[col] = "close"
        elif lower == "volume":
            rename[col] = "volume"
    df = df.rename(columns=rename)

    # Add unix timestamp column
    if "datetime" in df.columns:
        df["timestamp"] = df["datetime"].apply(lambda x: int(x.timestamp()))
    else:
        df["timestamp"] = 0

    return df[["timestamp", "open", "high", "low", "close", "volume"]].copy()


def _compute_backtest(signals: list[Signal], initial_capital: float = 10000.0) -> BacktestResult:
    """Simulate trades from signals and compute P&L metrics."""
    trades = []
    open_trade = None
    equity_curve = [initial_capital]
    capital = initial_capital

    for sig in signals:
        if sig.side == "buy" and open_trade is None:
            open_trade = {"entry_time": sig.timestamp, "entry_price": sig.price}
        elif sig.side == "sell" and open_trade is not None:
            pnl = sig.price - open_trade["entry_price"]
            pnl_pct = (pnl / open_trade["entry_price"]) * 100
            capital += pnl * (initial_capital / open_trade["entry_price"])
            trades.append({
                "entry_time": open_trade["entry_time"],
                "exit_time": sig.timestamp,
                "entry_price": round(open_trade["entry_price"], 4),
                "exit_price": round(sig.price, 4),
                "pnl": round(pnl, 4),
                "pnl_pct": round(pnl_pct, 2),
            })
            equity_curve.append(capital)
            open_trade = None

    # Metrics
    total_trades = len(trades)
    winning = [t for t in trades if t["pnl"] > 0]
    losing = [t for t in trades if t["pnl"] <= 0]
    total_pnl = sum(t["pnl"] for t in trades)
    win_rate = (len(winning) / total_trades * 100) if total_trades > 0 else 0

    # Sharpe ratio (annualized, assuming daily)
    if len(equity_curve) > 1:
        returns = np.diff(equity_curve) / equity_curve[:-1]
        sharpe = (np.mean(returns) / np.std(returns) * np.sqrt(252)) if np.std(returns) > 0 else 0
    else:
        sharpe = 0

    # Max drawdown
    peak = equity_curve[0]
    max_dd = 0
    for val in equity_curve:
        if val > peak:
            peak = val
        dd = (peak - val) / peak * 100
        if dd > max_dd:
            max_dd = dd

    return BacktestResult(
        signals=[s.to_dict() for s in signals],
        trades=trades,
        total_pnl=round(total_pnl, 4),
        win_rate=round(win_rate, 1),
        sharpe_ratio=round(float(sharpe), 2),
        total_trades=total_trades,
        winning_trades=len(winning),
        losing_trades=len(losing),
        max_drawdown=round(max_dd, 2),
    )


@dataclass
class LiveAlgo:
    """A running live algo instance."""
    id: str
    strategy_name: str
    symbol: str
    resolution: str
    params: dict
    signals: list[dict] = field(default_factory=list)
    pnl: float = 0.0
    status: str = "running"  # running, stopped
    started_at: float = 0.0
    _stop_event: threading.Event = field(default_factory=threading.Event)


class AlgoEngine:
    """Manages backtesting and live algo execution."""

    def __init__(self, broker=None):
        self._broker = broker
        self._live_algos: dict[str, LiveAlgo] = {}
        self._threads: dict[str, threading.Thread] = {}
        logger.info("Algo engine initialized")

    def backtest(self, strategy_name: str, symbol: str, resolution: str = "D",
                 params: Optional[dict] = None) -> dict:
        """Run a backtest and return results."""
        strategy = get_strategy(strategy_name)
        if not strategy:
            return {"error": f"Unknown strategy: {strategy_name}"}

        merged_params = {**strategy.default_params(), **(params or {})}

        df = _fetch_dataframe(symbol, resolution)
        if df.empty:
            return {"error": f"No data for {symbol} at resolution {resolution}"}

        signals, indicators = strategy.compute(df, merged_params)
        result = _compute_backtest(signals)
        return result.to_dict()

    def start_live(self, strategy_name: str, symbol: str, resolution: str = "D",
                   params: Optional[dict] = None) -> dict:
        """Start a live algo that periodically checks for new signals."""
        strategy = get_strategy(strategy_name)
        if not strategy:
            return {"error": f"Unknown strategy: {strategy_name}"}

        merged_params = {**strategy.default_params(), **(params or {})}
        algo_id = str(uuid.uuid4())[:8]

        algo = LiveAlgo(
            id=algo_id,
            strategy_name=strategy_name,
            symbol=symbol,
            resolution=resolution,
            params=merged_params,
            started_at=time.time(),
        )
        self._live_algos[algo_id] = algo

        thread = threading.Thread(target=self._live_loop, args=(algo,), daemon=True)
        self._threads[algo_id] = thread
        thread.start()

        logger.info(f"Started live algo {algo_id}: {strategy_name} on {symbol}")
        return {"id": algo_id, "status": "running", "strategy": strategy_name, "symbol": symbol}

    def stop_live(self, algo_id: str) -> dict:
        """Stop a running live algo."""
        algo = self._live_algos.get(algo_id)
        if not algo:
            return {"error": f"No algo with id {algo_id}"}

        algo._stop_event.set()
        algo.status = "stopped"
        logger.info(f"Stopped live algo {algo_id}")
        return {"id": algo_id, "status": "stopped"}

    def get_status(self) -> list[dict]:
        """Return status of all algos (running and recently stopped)."""
        result = []
        for algo in self._live_algos.values():
            result.append({
                "id": algo.id,
                "strategy": algo.strategy_name,
                "symbol": algo.symbol,
                "resolution": algo.resolution,
                "params": algo.params,
                "status": algo.status,
                "signals": algo.signals[-20:],  # Last 20 signals
                "pnl": algo.pnl,
                "started_at": algo.started_at,
            })
        return result

    def _live_loop(self, algo: LiveAlgo):
        """Background thread that periodically computes signals."""
        strategy = get_strategy(algo.strategy_name)
        if not strategy:
            algo.status = "error"
            return

        # Polling interval based on resolution
        poll_seconds = {
            "1": 60, "5": 300, "15": 900, "30": 1800,
            "60": 3600, "D": 300, "W": 3600, "M": 3600,
        }.get(algo.resolution, 300)

        while not algo._stop_event.is_set():
            try:
                df = _fetch_dataframe(algo.symbol, algo.resolution)
                if not df.empty:
                    signals, _ = strategy.compute(df, algo.params)
                    algo.signals = [s.to_dict() for s in signals]

                    # Compute running P&L from signals
                    result = _compute_backtest(signals)
                    algo.pnl = result.total_pnl

            except Exception as e:
                logger.error(f"Live algo {algo.id} error: {e}")

            algo._stop_event.wait(timeout=poll_seconds)

        algo.status = "stopped"
