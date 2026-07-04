let currentPlaybackDate = null;   // set when viewing a static past-day snapshot
let displayMode = 'price';
let playbackPoll = null;          // interval handle while a day is playing
let isPlaying = false;

document.addEventListener('DOMContentLoaded', async () => {
    const today = new Date().toISOString().split('T')[0];
    document.getElementById('playbackDate').value = today;
    document.getElementById('playbackDate').max = today;

    try {
        const cfg = await (await fetch('/api/config')).json();
        if (cfg.display_mode) {
            displayMode = cfg.display_mode;
            document.getElementById('displayMode').value = displayMode;
        }
    } catch (e) {
        console.error('Could not load config', e);
    }

    document.getElementById('displayMode').addEventListener('change', async (e) => {
        displayMode = e.target.value;
        try {
            await fetch('/api/set-display-mode', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ mode: displayMode })
            });
        } catch (err) {
            console.error('Failed to save display mode', err);
        }
        if (!isPlaying) loadStocks();
    });

    document.getElementById('playbackDate').addEventListener('change', () => {
        if (isPlaying) return; // ignore while a day is actively playing
        const selectedDate = document.getElementById('playbackDate').value;
        currentPlaybackDate = (selectedDate && selectedDate !== today) ? selectedDate : null;
        updatePlaybackBadge();
        loadStocks();
    });

    document.getElementById('playDayBtn').addEventListener('click', playDay);
    document.getElementById('stopBtn').addEventListener('click', stopPlayback);
    document.getElementById('viewLiveBtn').addEventListener('click', goLive);

    document.getElementById('addStockBtn').addEventListener('click', addStock);
    document.getElementById('newTickerInput').addEventListener('keypress', (e) => {
        if (e.key === 'Enter') addStock();
    });
    document.getElementById('runTickerBtn').addEventListener('click', runTicker);

    loadStocks();

    // Live auto-refresh (skipped while reviewing a past date or playing)
    setInterval(() => {
        if (!currentPlaybackDate && !isPlaying) {
            loadStocks();
        }
    }, 30000);
});

function updatePlaybackBadge() {
    document.getElementById('playbackBadge').style.display =
        (currentPlaybackDate || isPlaying) ? 'inline-block' : 'none';
}

function trendArrow(trend) {
    if (trend === 'up') return '↑';
    if (trend === 'down') return '↓';
    return '→';
}

function populatePlaybackStocks(stocks) {
    const sel = document.getElementById('playbackStock');
    const prev = sel.value;
    const tickers = stocks.map(s => s.ticker);
    sel.innerHTML = '';
    tickers.forEach(t => {
        const opt = document.createElement('option');
        opt.value = t;
        opt.textContent = t;
        sel.appendChild(opt);
    });
    if (tickers.includes(prev)) sel.value = prev;
}

function renderStocks(stocks) {
    const tbody = document.getElementById('stocksList');
    if (!Array.isArray(stocks) || stocks.length === 0) {
        tbody.innerHTML = '<tr><td colspan="6" class="loading">No data available</td></tr>';
        return;
    }
    tbody.innerHTML = '';
    stocks.forEach(stock => {
        const row = document.createElement('tr');
        const cls = stock.trend === 'up' ? 'value-up' : (stock.trend === 'down' ? 'value-down' : 'value-flat');
        const arrow = trendArrow(stock.trend);
        const priceActive = displayMode === 'price' ? cls : '';
        const changeActive = displayMode === 'change' ? cls : '';
        const pctActive = displayMode === 'change_pct' ? cls : '';
        row.innerHTML = `
            <td>
                <div class="symbol-col">${stock.ticker}</div>
                <div class="symbol-name">${stock.name}</div>
            </td>
            <td class="${cls}" style="font-size:18px;">${arrow}</td>
            <td class="${priceActive}">$${stock.price.toFixed(2)}</td>
            <td class="${changeActive || cls}">${stock.change >= 0 ? '+' : ''}${stock.change.toFixed(2)}</td>
            <td class="${pctActive || cls}">${stock.change_pct >= 0 ? '+' : ''}${stock.change_pct.toFixed(2)}%</td>
            <td>
                ${stock.market_cap_formatted}
                <button class="remove-btn" onclick="removeStock('${stock.ticker}')">Remove</button>
            </td>
        `;
        tbody.appendChild(row);
    });
}

async function loadStocks() {
    try {
        const url = `/api/stocks${currentPlaybackDate ? `?date=${currentPlaybackDate}` : ''}`;
        const stocks = await (await fetch(url)).json();
        if ((!Array.isArray(stocks) || stocks.length === 0) && currentPlaybackDate) {
            document.getElementById('stocksList').innerHTML =
                '<tr><td colspan="6" class="loading">No market data for this date</td></tr>';
            return;
        }
        renderStocks(stocks);
        if (!isPlaying) populatePlaybackStocks(stocks);
    } catch (error) {
        console.error('Error loading stocks:', error);
        const tbody = document.getElementById('stocksList');
        if (tbody.children.length === 0 || tbody.querySelector('.loading')) {
            tbody.innerHTML = '<tr><td colspan="6" class="loading">Error loading data</td></tr>';
        }
    }
}

// --- Day playback ----------------------------------------------------------
async function playDay() {
    const date = document.getElementById('playbackDate').value;
    const ticker = document.getElementById('playbackStock').value;
    if (!date) { alert('Pick a date first'); return; }
    if (!ticker) { alert('Pick a stock to play'); return; }

    const btn = document.getElementById('playDayBtn');
    btn.disabled = true;
    btn.textContent = 'Loading...';
    try {
        const resp = await fetch('/api/play', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ date, ticker })
        });
        const data = await resp.json();
        if (!resp.ok) {
            alert(data.error || 'Could not start playback');
            return;
        }
        isPlaying = true;
        currentPlaybackDate = null;
        document.getElementById('stopBtn').style.display = 'inline-block';
        document.getElementById('playbackProgress').style.display = 'flex';
        updatePlaybackBadge();
        startPlaybackPolling();
    } catch (e) {
        console.error(e);
        alert('Error starting playback');
    } finally {
        btn.disabled = false;
        btn.textContent = 'Play Day';
    }
}

function startPlaybackPolling() {
    if (playbackPoll) clearInterval(playbackPoll);
    lastFrameIndex = -1;
    pollPlaybackFrame();
    playbackPoll = setInterval(pollPlaybackFrame, 3000);
}

let lastFrameIndex = -1;
async function pollPlaybackFrame() {
    try {
        const status = await (await fetch('/api/playback-status')).json();
        if (!status.active && !status.finished) {
            stopPlaybackUI();
            return;
        }
        // Only redraw when the minute (frame) actually advances — the price shows
        // up once for that minute and then stays put until the next minute.
        if (status.index !== lastFrameIndex) {
            lastFrameIndex = status.index;
            renderStocks(status.stocks || []);
            document.getElementById('playbackTime').textContent = status.time_label || '--:--';
            document.getElementById('playbackPct').textContent = (status.progress || 0) + '%';
            document.getElementById('progressFill').style.width = (status.progress || 0) + '%';
        }

        if (status.finished) {
            // Day finished playing — leave the closing frame on screen
            isPlaying = false;
            if (playbackPoll) { clearInterval(playbackPoll); playbackPoll = null; }
            document.getElementById('stopBtn').style.display = 'none';
        }
    } catch (e) {
        console.error('playback poll error', e);
    }
}

async function stopPlayback() {
    try { await fetch('/api/stop-playback', { method: 'POST' }); } catch (e) {}
    stopPlaybackUI();
    goLive();
}

function stopPlaybackUI() {
    isPlaying = false;
    if (playbackPoll) { clearInterval(playbackPoll); playbackPoll = null; }
    document.getElementById('stopBtn').style.display = 'none';
    document.getElementById('playbackProgress').style.display = 'none';
    document.getElementById('progressFill').style.width = '0%';
    updatePlaybackBadge();
}

async function goLive() {
    if (isPlaying) { try { await fetch('/api/stop-playback', { method: 'POST' }); } catch (e) {} }
    stopPlaybackUI();
    const today = new Date().toISOString().split('T')[0];
    document.getElementById('playbackDate').value = today;
    currentPlaybackDate = null;
    updatePlaybackBadge();
    loadStocks();
}

// --- Watchlist management --------------------------------------------------
async function addStock() {
    const input = document.getElementById('newTickerInput');
    const ticker = input.value.trim().toUpperCase();
    if (!ticker) { alert('Please enter a ticker'); return; }

    const addBtn = document.getElementById('addStockBtn');
    const originalText = addBtn.textContent;
    addBtn.disabled = true;
    addBtn.textContent = 'Adding...';
    try {
        const resp = await fetch('/api/add-stock', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ ticker })
        });
        const data = await resp.json();
        if (resp.ok) {
            input.value = '';
            if (!isPlaying) await loadStocks();
        } else {
            alert(data.error || 'Failed to add stock');
        }
    } catch (error) {
        console.error('Error:', error);
        alert('Error adding stock. Check console.');
    } finally {
        addBtn.disabled = false;
        addBtn.textContent = originalText;
    }
}

async function removeStock(ticker) {
    try {
        const resp = await fetch('/api/remove-stock', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ ticker })
        });
        if (resp.ok && !isPlaying) {
            await loadStocks();
        }
    } catch (error) {
        console.error('Error:', error);
    }
}

async function runTicker() {
    const btn = document.getElementById('runTickerBtn');
    const originalText = btn.textContent;
    btn.disabled = true;
    btn.textContent = 'Starting...';
    try {
        const resp = await fetch('/api/run-ticker', { method: 'POST' });
        const data = await resp.json();
        if (resp.ok) {
            btn.textContent = 'Arduino Updating';
            setTimeout(() => { btn.textContent = originalText; btn.disabled = false; }, 2500);
            return;
        }
        alert('Error: ' + (data.error || 'unknown'));
    } catch (error) {
        console.error('Error:', error);
        alert('Error running ticker.py');
    }
    btn.disabled = false;
    btn.textContent = originalText;
}
