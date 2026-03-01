"""RSI strategy — buy when oversold, sell when overbought."""

import pandas as pd
import numpy as np
from .base import Strategy, Signal


class RSIStrategy(Strategy):
    def name(self) -> str:
        return "RSI"

    def default_params(self) -> dict:
        return {"period": 14, "oversold": 30, "overbought": 70}

    def param_schema(self) -> list[dict]:
        return [
            {"name": "period", "type": "int", "min": 2, "max": 100, "default": 14,
             "description": "RSI lookback period"},
            {"name": "oversold", "type": "int", "min": 5, "max": 50, "default": 30,
             "description": "Oversold threshold (buy signal)"},
            {"name": "overbought", "type": "int", "min": 50, "max": 95, "default": 70,
             "description": "Overbought threshold (sell signal)"},
        ]

    def compute(self, df: pd.DataFrame, params: dict) -> tuple[list[Signal], dict]:
        period = int(params.get("period", 14))
        oversold = float(params.get("oversold", 30))
        overbought = float(params.get("overbought", 70))

        df = df.copy()

        # Calculate RSI
        delta = df["close"].diff()
        gain = delta.clip(lower=0)
        loss = (-delta).clip(lower=0)

        avg_gain = gain.rolling(window=period, min_periods=period).mean()
        avg_loss = loss.rolling(window=period, min_periods=period).mean()

        # Use Wilder's smoothing after initial SMA
        for i in range(period, len(df)):
            avg_gain.iloc[i] = (avg_gain.iloc[i - 1] * (period - 1) + gain.iloc[i]) / period
            avg_loss.iloc[i] = (avg_loss.iloc[i - 1] * (period - 1) + loss.iloc[i]) / period

        rs = avg_gain / avg_loss
        df["rsi"] = 100 - (100 / (1 + rs))

        signals = []
        in_position = False

        for i in range(period + 1, len(df)):
            rsi_val = df["rsi"].iloc[i]
            if pd.isna(rsi_val):
                continue

            row = df.iloc[i]

            if rsi_val < oversold and not in_position:
                signals.append(Signal(
                    timestamp=int(row["timestamp"]),
                    side="buy",
                    price=float(row["close"]),
                    reason=f"RSI={rsi_val:.1f} < {oversold} (oversold)",
                    candle_index=i,
                ))
                in_position = True
            elif rsi_val > overbought and in_position:
                signals.append(Signal(
                    timestamp=int(row["timestamp"]),
                    side="sell",
                    price=float(row["close"]),
                    reason=f"RSI={rsi_val:.1f} > {overbought} (overbought)",
                    candle_index=i,
                ))
                in_position = False

        indicators = {
            "rsi": df["rsi"].tolist(),
        }
        return signals, indicators
