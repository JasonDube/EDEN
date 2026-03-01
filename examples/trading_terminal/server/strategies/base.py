"""
Strategy base class and common dataclasses for the algo engine.
"""

from abc import ABC, abstractmethod
from dataclasses import dataclass, asdict
from typing import Optional
import pandas as pd


@dataclass
class Signal:
    """A buy or sell signal at a specific timestamp."""
    timestamp: int          # Unix timestamp
    side: str               # "buy" or "sell"
    price: float            # Price at signal
    reason: str = ""        # Human-readable reason
    candle_index: int = -1  # Index into candle array (set by engine)

    def to_dict(self) -> dict:
        return asdict(self)


@dataclass
class BacktestResult:
    """Results from running a backtest."""
    signals: list[dict]
    trades: list[dict]      # Paired buy/sell entries
    total_pnl: float
    win_rate: float         # 0-100
    sharpe_ratio: float
    total_trades: int
    winning_trades: int
    losing_trades: int
    max_drawdown: float

    def to_dict(self) -> dict:
        return asdict(self)


class Strategy(ABC):
    """Abstract base class for trading strategies."""

    @abstractmethod
    def name(self) -> str:
        """Human-readable strategy name."""
        ...

    @abstractmethod
    def default_params(self) -> dict:
        """Default parameter values."""
        ...

    @abstractmethod
    def param_schema(self) -> list[dict]:
        """Parameter schema: [{name, type, min, max, default, description}]"""
        ...

    @abstractmethod
    def compute(self, df: pd.DataFrame, params: dict) -> tuple[list[Signal], dict]:
        """
        Run strategy on OHLCV DataFrame.

        Args:
            df: DataFrame with columns [open, high, low, close, volume, timestamp]
            params: Strategy parameters

        Returns:
            (signals, indicator_data) where indicator_data contains
            computed indicators for optional visualization.
        """
        ...
