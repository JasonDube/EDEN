"""Bollinger Bands strategy — buy at lower band, sell at upper band."""

import pandas as pd
import numpy as np
from .base import Strategy, Signal


class BollingerBands(Strategy):
    def name(self) -> str:
        return "Bollinger Bands"

    def default_params(self) -> dict:
        return {"period": 20, "std_dev": 2.0}

    def param_schema(self) -> list[dict]:
        return [
            {"name": "period", "type": "int", "min": 5, "max": 200, "default": 20,
             "description": "Moving average period"},
            {"name": "std_dev", "type": "float", "min": 0.5, "max": 5.0, "default": 2.0,
             "description": "Standard deviation multiplier"},
        ]

    def compute(self, df: pd.DataFrame, params: dict) -> tuple[list[Signal], dict]:
        period = int(params.get("period", 20))
        std_mult = float(params.get("std_dev", 2.0))

        df = df.copy()
        df["bb_mid"] = df["close"].rolling(window=period).mean()
        df["bb_std"] = df["close"].rolling(window=period).std()
        df["bb_upper"] = df["bb_mid"] + std_mult * df["bb_std"]
        df["bb_lower"] = df["bb_mid"] - std_mult * df["bb_std"]

        signals = []
        in_position = False

        for i in range(period, len(df)):
            row = df.iloc[i]
            if pd.isna(row["bb_lower"]):
                continue

            close = float(row["close"])
            lower = float(row["bb_lower"])
            upper = float(row["bb_upper"])

            if close <= lower and not in_position:
                signals.append(Signal(
                    timestamp=int(row["timestamp"]),
                    side="buy",
                    price=close,
                    reason=f"Price ${close:.2f} hit lower band ${lower:.2f}",
                    candle_index=i,
                ))
                in_position = True
            elif close >= upper and in_position:
                signals.append(Signal(
                    timestamp=int(row["timestamp"]),
                    side="sell",
                    price=close,
                    reason=f"Price ${close:.2f} hit upper band ${upper:.2f}",
                    candle_index=i,
                ))
                in_position = False

        indicators = {
            "bb_upper": df["bb_upper"].tolist(),
            "bb_mid": df["bb_mid"].tolist(),
            "bb_lower": df["bb_lower"].tolist(),
        }
        return signals, indicators
