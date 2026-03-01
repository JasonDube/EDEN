"""SMA Crossover strategy — buy when fast SMA crosses above slow SMA."""

import pandas as pd
from .base import Strategy, Signal


class SMACrossover(Strategy):
    def name(self) -> str:
        return "SMA Crossover"

    def default_params(self) -> dict:
        return {"fast_period": 20, "slow_period": 50}

    def param_schema(self) -> list[dict]:
        return [
            {"name": "fast_period", "type": "int", "min": 2, "max": 200, "default": 20,
             "description": "Fast SMA period"},
            {"name": "slow_period", "type": "int", "min": 5, "max": 500, "default": 50,
             "description": "Slow SMA period"},
        ]

    def compute(self, df: pd.DataFrame, params: dict) -> tuple[list[Signal], dict]:
        fast = int(params.get("fast_period", 20))
        slow = int(params.get("slow_period", 50))

        df = df.copy()
        df["sma_fast"] = df["close"].rolling(window=fast).mean()
        df["sma_slow"] = df["close"].rolling(window=slow).mean()

        signals = []
        prev_fast_above = None

        for i in range(slow, len(df)):
            row = df.iloc[i]
            fast_above = row["sma_fast"] > row["sma_slow"]

            if prev_fast_above is not None:
                if fast_above and not prev_fast_above:
                    signals.append(Signal(
                        timestamp=int(row["timestamp"]),
                        side="buy",
                        price=float(row["close"]),
                        reason=f"SMA{fast} crossed above SMA{slow}",
                        candle_index=i,
                    ))
                elif not fast_above and prev_fast_above:
                    signals.append(Signal(
                        timestamp=int(row["timestamp"]),
                        side="sell",
                        price=float(row["close"]),
                        reason=f"SMA{fast} crossed below SMA{slow}",
                        candle_index=i,
                    ))

            prev_fast_above = fast_above

        indicators = {
            "sma_fast": df["sma_fast"].tolist(),
            "sma_slow": df["sma_slow"].tolist(),
        }
        return signals, indicators
