"""
Tastytrade broker integration for EDEN Trading Terminal.

Wraps the tastytrade SDK (async) for account info, positions, orders, and equity trading.
Supports sandbox (cert) and live trading via .env config.
"""

import os
import logging
from dataclasses import dataclass, asdict
from decimal import Decimal
from typing import Optional

from tastytrade import Session, Account
from tastytrade.instruments import Equity
from tastytrade.order import (
    NewOrder,
    OrderAction,
    OrderType,
    OrderTimeInForce,
)

logger = logging.getLogger("broker")


@dataclass
class AccountInfo:
    buying_power: float
    equity: float
    cash: float
    portfolio_value: float
    sandbox: bool


@dataclass
class Position:
    symbol: str
    qty: float
    avg_entry_price: float
    current_price: float
    unrealized_pl: float
    unrealized_plpc: float
    market_value: float
    side: str


@dataclass
class OrderInfo:
    id: str
    symbol: str
    qty: float
    filled_qty: float
    side: str
    type: str
    status: str
    submitted_at: str
    limit_price: Optional[float] = None


class TastytradeBroker:
    """Wraps Tastytrade API for sandbox/live trading."""

    def __init__(self):
        self._session: Optional[Session] = None
        self._account: Optional[Account] = None
        self.sandbox = os.getenv("TASTYTRADE_SANDBOX", "true").lower() == "true"
        self._client_secret = os.getenv("TASTYTRADE_CLIENT_SECRET", "")
        self._refresh_token = os.getenv("TASTYTRADE_REFRESH_TOKEN", "")

    async def initialize(self):
        """Create session and fetch account. Call once at startup."""
        if not self._client_secret or not self._refresh_token:
            logger.warning("Tastytrade credentials not set — broker will return mock data")
            return

        try:
            self._session = Session(
                self._client_secret,
                self._refresh_token,
                is_test=self.sandbox,
            )
            accounts = await Account.get(self._session)
            if not accounts:
                logger.error("No accounts found for this Tastytrade session")
                self._session = None
                return
            self._account = accounts[0]
            mode = "sandbox" if self.sandbox else "LIVE"
            logger.info(f"Tastytrade broker initialized ({mode} trading, account {self._account.account_number})")
        except Exception as e:
            logger.error(f"Failed to initialize Tastytrade session: {e}")
            self._session = None
            self._account = None

    @property
    def configured(self) -> bool:
        return self._session is not None and self._account is not None

    async def get_account(self) -> dict:
        if not self.configured:
            return asdict(AccountInfo(
                buying_power=100000.0,
                equity=100000.0,
                cash=100000.0,
                portfolio_value=100000.0,
                sandbox=True,
            ))

        balance = await self._account.get_balances(self._session)
        return asdict(AccountInfo(
            buying_power=float(balance.equity_buying_power),
            equity=float(balance.margin_equity),
            cash=float(balance.cash_balance),
            portfolio_value=float(balance.net_liquidating_value),
            sandbox=self.sandbox,
        ))

    async def get_positions(self) -> list[dict]:
        if not self.configured:
            return []

        positions = await self._account.get_positions(self._session)
        result = []
        for p in positions:
            qty = float(p.quantity)
            avg_price = float(p.average_open_price) if p.average_open_price else 0.0
            close_price = float(p.close_price) if p.close_price else avg_price
            market_val = qty * close_price
            unrealized = (close_price - avg_price) * qty if avg_price else 0.0
            unrealized_pct = ((close_price - avg_price) / avg_price * 100) if avg_price else 0.0

            result.append(asdict(Position(
                symbol=p.symbol,
                qty=qty,
                avg_entry_price=avg_price,
                current_price=close_price,
                unrealized_pl=round(unrealized, 2),
                unrealized_plpc=round(unrealized_pct, 2),
                market_value=round(market_val, 2),
                side="long" if qty > 0 else "short",
            )))
        return result

    async def submit_order(self, symbol: str, qty: float, side: str,
                           order_type: str = "market", limit_price: Optional[float] = None) -> dict:
        if not self.configured:
            return {"error": "Broker not configured — set TASTYTRADE_CLIENT_SECRET in .env"}

        action = OrderAction.BUY if side.lower() == "buy" else OrderAction.SELL
        equity = await Equity.get(self._session, symbol.upper())
        leg = equity.build_leg(Decimal(str(qty)), action)

        if order_type == "limit" and limit_price is not None:
            # Negative price = debit (buying), positive = credit (selling)
            price = Decimal(str(-abs(limit_price))) if side.lower() == "buy" else Decimal(str(abs(limit_price)))
            order = NewOrder(
                time_in_force=OrderTimeInForce.GTC,
                order_type=OrderType.LIMIT,
                legs=[leg],
                price=price,
            )
        else:
            order = NewOrder(
                time_in_force=OrderTimeInForce.DAY,
                order_type=OrderType.MARKET,
                legs=[leg],
            )

        response = await self._account.place_order(self._session, order)
        placed = response.order
        return {
            "id": str(placed.id),
            "symbol": symbol.upper(),
            "qty": str(qty),
            "side": side,
            "type": order_type,
            "status": str(placed.status),
        }

    async def cancel_order(self, order_id: str) -> dict:
        if not self.configured:
            return {"error": "Broker not configured"}

        await self._account.delete_order(self._session, order_id)
        return {"status": "cancelled", "order_id": order_id}

    async def get_orders(self, status: str = "open") -> list[dict]:
        if not self.configured:
            return []

        orders = await self._account.get_live_orders(self._session)
        result = []
        for o in orders:
            # Filter to open orders if requested
            order_status = str(o.status)
            if status == "open" and order_status not in ("Received", "Live", "RECEIVED", "LIVE"):
                continue

            # Extract symbol from first leg
            order_symbol = o.legs[0].symbol if o.legs else ""
            # Sum qty across legs
            order_qty = sum(float(leg.quantity) for leg in o.legs) if o.legs else 0
            order_side = str(o.legs[0].action) if o.legs else ""

            result.append(asdict(OrderInfo(
                id=str(o.id),
                symbol=order_symbol,
                qty=order_qty,
                filled_qty=0,  # live orders are not yet filled
                side=order_side,
                type=str(o.order_type),
                status=order_status,
                submitted_at=str(o.updated_at) if hasattr(o, 'updated_at') else "",
                limit_price=float(o.price) if o.price else None,
            )))
        return result
