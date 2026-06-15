import json
import os
import subprocess
import sys
import time
import datetime
import threading

from flask import Flask, render_template, jsonify, request
import yfinance as yf

app = Flask(__name__)
WATCHLIST_FILE = "watchlist.json"
CONFIG_FILE = "config.json"
DEFAULT_TICKERS = ["AAPL", "MSFT", "NVDA", "TSLA"]
VALID_MODES = ("price", "change", "change_pct")

# Real-time playback: advance to the next data point once every this many seconds.
# 60 = update once per minute, just like the live ticker.
PLAYBACK_STEP_SECONDS = 60.0
LIVE_CACHE_TTL = 30.0       # seconds to cache live quotes (protects yfinance)

# Tracks the running ticker.py subprocess so we never spawn duplicates fighting over COM4
ticker_process = None

# --- live data cache -------------------------------------------------------
_live_cache = {"time": 0.0, "stocks": []}
_live_lock = threading.Lock()

# --- playback engine state -------------------------------------------------
_pb_lock = threading.Lock()
playback = {
    "active": False,
    "finished": False,
    "date": None,
    "ticker": None,      # playback is for ONE stock at a time
    "index": 0,
    "total": 0,
    "series": {},        # ticker -> [prices]
    "times": [],         # representative HH:MM labels
    "prev_close": {},    # ticker -> float
    "meta": {},          # ticker -> {name, market_cap}
}
_pb_thread = None


# ---------------------------------------------------------------------------
# Files
# ---------------------------------------------------------------------------
def load_watchlist():
    if os.path.exists(WATCHLIST_FILE):
        try:
            with open(WATCHLIST_FILE, "r") as f:
                data = json.load(f)
                if isinstance(data, list) and data:
                    return data
        except Exception:
            pass
    return DEFAULT_TICKERS.copy()


def save_watchlist(tickers):
    with open(WATCHLIST_FILE, "w") as f:
        json.dump(tickers, f)


def load_config():
    if os.path.exists(CONFIG_FILE):
        try:
            with open(CONFIG_FILE, "r") as f:
                cfg = json.load(f)
                if cfg.get("display_mode") in VALID_MODES:
                    return cfg
        except Exception:
            pass
    return {"display_mode": "price"}


def save_config(cfg):
    with open(CONFIG_FILE, "w") as f:
        json.dump(cfg, f)


# ---------------------------------------------------------------------------
# Data helpers
# ---------------------------------------------------------------------------
def _extract_close_series(hist):
    """yf.download can return multi-index columns. Return a flat Close series."""
    close = hist["Close"]
    if hasattr(close, "columns"):
        close = close.iloc[:, 0]
    return close


def _build_stock(ticker, price, prev_close, name, market_cap):
    if price <= 0 or prev_close <= 0:
        return None
    change = price - prev_close
    change_pct = (change / prev_close * 100) if prev_close else 0
    return {
        "ticker": ticker,
        "name": name or ticker,
        "price": round(price, 2),
        "change": round(change, 2),
        "change_pct": round(change_pct, 2),
        "market_cap": market_cap,
        "market_cap_formatted": format_market_cap(market_cap),
        "trend": "up" if change_pct > 0.05 else ("down" if change_pct < -0.05 else "flat"),
    }


def fetch_live_stock(ticker):
    try:
        quote = yf.Ticker(ticker)
        info = quote.info
        price = float(info.get("currentPrice") or info.get("regularMarketPrice") or 0)
        prev_close = float(info.get("previousClose") or price)
        return _build_stock(ticker, price, prev_close,
                            info.get("shortName", ticker), info.get("marketCap", 0))
    except Exception as e:
        print(f"Failed to fetch live {ticker}: {e}")
        return None


def fetch_static_close(ticker, playback_date):
    """A single past day's closing snapshot (when a date is picked but not playing)."""
    try:
        parse_date = datetime.datetime.strptime(playback_date, "%Y-%m-%d").date()
        start = parse_date - datetime.timedelta(days=10)
        end = parse_date + datetime.timedelta(days=1)
        hist = yf.download(ticker, start=str(start), end=str(end), progress=False)
        if hist is None or hist.empty:
            return None
        close = _extract_close_series(hist)
        close = close[close.index.date <= parse_date].dropna()
        if len(close) == 0:
            return None
        price = float(close.iloc[-1])
        prev_close = float(close.iloc[-2]) if len(close) >= 2 else price
        info = yf.Ticker(ticker).info
        return _build_stock(ticker, price, prev_close,
                            info.get("shortName", ticker), info.get("marketCap", 0))
    except Exception as e:
        print(f"Failed static {ticker}: {e}")
        return None


def format_market_cap(value):
    if not isinstance(value, (int, float)) or value <= 0:
        return "N/A"
    if value >= 1_000_000_000_000:
        return f"${value / 1_000_000_000_000:.2f}T"
    if value >= 1_000_000_000:
        return f"${value / 1_000_000_000:.2f}B"
    return f"${value / 1_000_000:.2f}M"


def format_for_display(data, mode):
    """Single source of truth for what a stock looks like on the LCD / preview."""
    trend_char = "^" if data["trend"] == "up" else ("v" if data["trend"] == "down" else "=")
    if mode == "change":
        value = f"{data['change']:+.2f}"
    elif mode == "change_pct":
        value = f"{data['change_pct']:+.2f}%"
    else:
        value = f"${data['price']:.2f}"
    return f"{data['ticker']}: {value} {trend_char}"


def build_bottom_row(stocks, mode):
    if not stocks:
        return "No data"
    return " | ".join(format_for_display(s, mode) for s in stocks) + " |"


def playback_bottom_row(stocks, mode):
    """During playback the LCD shows ONE stock, frozen, with NO trend arrow so the
    full price always fits in 16 characters and is never cut off."""
    if not stocks:
        return "No data"
    s = stocks[0]
    if mode == "change":
        value = f"{s['change']:+.2f}"
    elif mode == "change_pct":
        value = f"{s['change_pct']:+.2f}%"
    else:
        value = f"${s['price']:.2f}"
    return f"{s['ticker']}: {value}"[:16]


# ---------------------------------------------------------------------------
# Live cache + current-stocks resolver
# ---------------------------------------------------------------------------
def get_live_stocks():
    now = time.time()
    with _live_lock:
        if now - _live_cache["time"] < LIVE_CACHE_TTL and _live_cache["stocks"]:
            return _live_cache["stocks"]
    stocks = [s for s in (fetch_live_stock(t) for t in load_watchlist()) if s]
    with _live_lock:
        _live_cache["time"] = now
        _live_cache["stocks"] = stocks
    return stocks


def get_playback_frame():
    """Build the current frame's stock list from in-memory intraday series.

    Returns the (single-stock) frame while playing AND after it finishes, so the
    closing values stay frozen on screen until the user goes Live / Stops.
    """
    with _pb_lock:
        if not (playback["active"] or playback["finished"]):
            return None
        idx = playback["index"]
        out = []
        for ticker, series in playback["series"].items():
            if not series:
                continue
            price = series[min(idx, len(series) - 1)]
            meta = playback["meta"].get(ticker, {})
            prev = playback["prev_close"].get(ticker, price)
            s = _build_stock(ticker, price, prev, meta.get("name"), meta.get("market_cap", 0))
            if s:
                out.append(s)
        return out


def live_or_static(date_param=None):
    """Watchlist data for the dashboard table: live (today) or a past-day snapshot.
    Deliberately ignores playback — the live/today view must always show the full
    watchlist regardless of any playback state."""
    if date_param:
        return [s for s in (fetch_static_close(t, date_param) for t in load_watchlist()) if s]
    return get_live_stocks()


def current_stocks(date_param=None):
    """What the Arduino LCD should show: the playback frame when playing, else live/static."""
    frame = get_playback_frame()
    if frame is not None:
        return frame
    return live_or_static(date_param)


# ---------------------------------------------------------------------------
# Playback engine
# ---------------------------------------------------------------------------
def fetch_intraday(ticker, date):
    """Return (prices, time_labels) of intraday bars for `date`, or ([],[])."""
    start = date
    end = date + datetime.timedelta(days=1)
    # Prefer 1-minute bars so playback steps minute-by-minute; fall back to coarser
    # intervals for older dates where 1m history isn't available.
    for interval in ("1m", "5m", "15m", "30m", "1h"):
        try:
            h = yf.download(ticker, start=str(start), end=str(end),
                            interval=interval, progress=False)
            if h is not None and not h.empty:
                c = _extract_close_series(h).dropna()
                if len(c) >= 2:
                    prices = [round(float(x), 2) for x in c.tolist()]
                    labels = [ts.strftime("%H:%M") for ts in c.index]
                    return prices, labels
        except Exception as e:
            print(f"intraday {ticker} {interval}: {e}")
    return [], []


def get_prev_close(ticker, date):
    try:
        start = date - datetime.timedelta(days=10)
        h = yf.download(ticker, start=str(start), end=str(date), progress=False)
        c = _extract_close_series(h).dropna()
        c = c[c.index.date < date]
        if len(c):
            return float(c.iloc[-1])
    except Exception as e:
        print(f"prev_close {ticker}: {e}")
    return None


def _playback_stepper():
    while True:
        with _pb_lock:
            if not playback["active"]:
                return
            total = playback["total"]
            step = PLAYBACK_STEP_SECONDS
            if playback["index"] >= total - 1:
                playback["index"] = total - 1
                playback["finished"] = True
                playback["active"] = False
                return
            playback["index"] += 1
        time.sleep(step)


def start_playback(date_str, ticker):
    """Play a single stock through one trading day (open -> close)."""
    global _pb_thread
    ticker = ticker.upper().strip()
    date = datetime.datetime.strptime(date_str, "%Y-%m-%d").date()

    prices, labels = fetch_intraday(ticker, date)
    if not prices:
        return False, f"No intraday data for {ticker} on {date_str} (intraday history is limited to ~60 days back)."

    prev_close = get_prev_close(ticker, date) or prices[0]
    try:
        info = yf.Ticker(ticker).info
        meta = {"name": info.get("shortName", ticker), "market_cap": info.get("marketCap", 0)}
    except Exception:
        meta = {"name": ticker, "market_cap": 0}

    with _pb_lock:
        playback.update({
            "active": True, "finished": False, "date": date_str, "ticker": ticker,
            "index": 0, "total": len(prices), "series": {ticker: prices},
            "times": labels, "prev_close": {ticker: prev_close}, "meta": {ticker: meta},
        })

    _pb_thread = threading.Thread(target=_playback_stepper, daemon=True)
    _pb_thread.start()
    return True, "playing"


def stop_playback():
    with _pb_lock:
        playback["active"] = False
        playback["finished"] = False


# ---------------------------------------------------------------------------
# Routes
# ---------------------------------------------------------------------------
@app.route("/")
def index():
    return render_template("index.html")


@app.route("/api/config")
def get_config():
    return jsonify(load_config())


@app.route("/api/set-display-mode", methods=["POST"])
def set_display_mode():
    mode = request.json.get("mode", "price")
    if mode not in VALID_MODES:
        return jsonify({"error": "Invalid mode"}), 400
    cfg = load_config()
    cfg["display_mode"] = mode
    save_config(cfg)
    return jsonify({"success": True, "display_mode": mode})


@app.route("/api/stocks")
def get_stocks():
    # Dashboard table: always the full watchlist (live or static), never a playback frame.
    return jsonify(live_or_static(request.args.get("date")))


@app.route("/api/arduino-preview")
def arduino_preview():
    mode = load_config()["display_mode"]
    stocks = current_stocks(request.args.get("date"))
    with _pb_lock:
        is_pb = playback["active"] or playback["finished"]
    bottom = playback_bottom_row(stocks, mode) if is_pb else build_bottom_row(stocks, mode)
    return jsonify({
        "top_row": "Subscribe!      "[:16],
        "bottom_row": bottom,
        "is_playback": is_pb,
    })


@app.route("/api/play", methods=["POST"])
def api_play():
    date_str = request.json.get("date")
    ticker = request.json.get("ticker")
    if not date_str:
        return jsonify({"error": "No date given"}), 400
    if not ticker:
        return jsonify({"error": "Pick a stock to play"}), 400
    ok, msg = start_playback(date_str, ticker)
    if not ok:
        return jsonify({"error": msg}), 400
    return jsonify({"success": True})


@app.route("/api/stop-playback", methods=["POST"])
def api_stop_playback():
    stop_playback()
    return jsonify({"success": True})


@app.route("/api/playback-status")
def api_playback_status():
    mode = load_config()["display_mode"]
    with _pb_lock:
        active = playback["active"]
        finished = playback["finished"]
        idx = playback["index"]
        total = playback["total"]
        date = playback["date"]
        times = playback["times"]
    if not active and not finished:
        return jsonify({"active": False, "finished": False})

    stocks = get_playback_frame() or []
    time_label = times[min(idx, len(times) - 1)] if times else ""
    progress = round((idx + 1) / total * 100) if total else 0
    return jsonify({
        "active": active,
        "finished": finished,
        "date": date,
        "index": idx,
        "total": total,
        "time_label": time_label,
        "progress": progress,
        "stocks": stocks,
        "bottom_row": playback_bottom_row(stocks, mode),
    })


@app.route("/api/add-stock", methods=["POST"])
def add_stock():
    ticker = request.json.get("ticker", "").upper().strip()
    if not ticker or len(ticker) > 6:
        return jsonify({"error": "Invalid ticker"}), 400
    tickers = load_watchlist()
    if ticker in tickers:
        return jsonify({"error": f"{ticker} already in watchlist"}), 400
    data = fetch_live_stock(ticker)
    if not data:
        return jsonify({"error": f"Could not fetch data for {ticker}. Ticker may be invalid."}), 400
    tickers.append(ticker)
    save_watchlist(tickers)
    with _live_lock:  # bust cache so the new stock shows immediately
        _live_cache["time"] = 0.0
    return jsonify({"success": True, "stock": data})


@app.route("/api/remove-stock", methods=["POST"])
def remove_stock():
    ticker = request.json.get("ticker", "").upper().strip()
    tickers = load_watchlist()
    if ticker in tickers:
        tickers.remove(ticker)
        save_watchlist(tickers)
        with _live_lock:
            _live_cache["time"] = 0.0
        return jsonify({"success": True})
    return jsonify({"error": "Stock not found"}), 400


@app.route("/api/run-ticker", methods=["POST"])
def run_ticker():
    """(Re)start ticker.py. Kills any prior instance so COM4 is never double-opened."""
    global ticker_process
    try:
        if ticker_process and ticker_process.poll() is None:
            ticker_process.terminate()
            try:
                ticker_process.wait(timeout=5)
            except Exception:
                ticker_process.kill()
        ticker_process = subprocess.Popen([sys.executable, "ticker.py"])
        return jsonify({"success": True, "message": "ticker.py started"})
    except Exception as e:
        return jsonify({"error": str(e)}), 500


if __name__ == "__main__":
    app.run(debug=False, use_reloader=False, host="localhost", port=5000, threaded=True)
