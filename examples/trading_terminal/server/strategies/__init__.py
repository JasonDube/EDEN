"""Strategy registry — imports and registers all available strategies."""

from .sma_crossover import SMACrossover
from .rsi import RSIStrategy
from .bollinger import BollingerBands
from .base import Strategy

# All available strategies, keyed by name
STRATEGIES: dict[str, Strategy] = {}


def _register(strategy_cls):
    s = strategy_cls()
    STRATEGIES[s.name()] = s


_register(SMACrossover)
_register(RSIStrategy)
_register(BollingerBands)


def get_strategy(name: str) -> Strategy | None:
    return STRATEGIES.get(name)


def list_strategies() -> list[dict]:
    result = []
    for s in STRATEGIES.values():
        result.append({
            "name": s.name(),
            "default_params": s.default_params(),
            "param_schema": s.param_schema(),
        })
    return result
