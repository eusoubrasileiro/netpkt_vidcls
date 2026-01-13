#!/usr/bin/env python3
"""StreamGuard Web Interface - Query client quota states."""

import json
import os
from datetime import datetime
from flask import Flask, jsonify, render_template_string

app = Flask(__name__)

STATE_FILE = os.environ.get('STREAMGUARD_STATE_FILE', '/var/lib/streamguard/state.json')
QUOTA_SECONDS = int(os.environ.get('STREAMGUARD_QUOTA', '3600'))

HTML_TEMPLATE = '''
<!DOCTYPE html>
<html>
<head>
    <title>StreamGuard</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        * { box-sizing: border-box; }
        body {
            font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
            margin: 0; padding: 20px;
            background: #f5f5f5;
        }
        .container { max-width: 800px; margin: 0 auto; }
        h1 { color: #333; margin-bottom: 5px; }
        .subtitle { color: #666; margin-bottom: 20px; }
        .card {
            background: white;
            border-radius: 8px;
            padding: 20px;
            margin-bottom: 15px;
            box-shadow: 0 1px 3px rgba(0,0,0,0.1);
        }
        table { width: 100%; border-collapse: collapse; }
        th, td { padding: 12px; text-align: left; border-bottom: 1px solid #eee; }
        th { color: #666; font-weight: 500; font-size: 12px; text-transform: uppercase; }
        .progress-bar {
            width: 100%; height: 8px;
            background: #e0e0e0;
            border-radius: 4px;
            overflow: hidden;
        }
        .progress-fill {
            height: 100%;
            border-radius: 4px;
            transition: width 0.3s;
        }
        .green { background: #4caf50; }
        .yellow { background: #ff9800; }
        .red { background: #f44336; }
        .badge {
            display: inline-block;
            padding: 4px 8px;
            border-radius: 4px;
            font-size: 12px;
            font-weight: 500;
        }
        .badge-blocked { background: #ffebee; color: #c62828; }
        .badge-ok { background: #e8f5e9; color: #2e7d32; }
        .empty { text-align: center; color: #999; padding: 40px; }
        .refresh { color: #666; font-size: 14px; }
        .mono { font-family: monospace; }
    </style>
</head>
<body>
    <div class="container">
        <h1>StreamGuard</h1>
        <p class="subtitle">Daily quota: {{ format_time(quota_seconds) }} | Updated: {{ timestamp }}</p>

        <div class="card">
            {% if clients %}
            <table>
                <thead>
                    <tr>
                        <th>Client IP</th>
                        <th>Used</th>
                        <th>Progress</th>
                        <th>Remaining</th>
                        <th>Status</th>
                    </tr>
                </thead>
                <tbody>
                    {% for c in clients %}
                    <tr>
                        <td class="mono">{{ c.ip }}</td>
                        <td>{{ format_time(c.streaming_seconds) }}</td>
                        <td>
                            <div class="progress-bar">
                                <div class="progress-fill {{ c.color }}" style="width: {{ c.percentage_used }}%"></div>
                            </div>
                            <small>{{ "%.1f"|format(c.percentage_used) }}%</small>
                        </td>
                        <td>{{ format_time(c.remaining_seconds) }}</td>
                        <td>
                            {% if c.is_blocked %}
                            <span class="badge badge-blocked">BLOCKED</span>
                            {% else %}
                            <span class="badge badge-ok">OK</span>
                            {% endif %}
                        </td>
                    </tr>
                    {% endfor %}
                </tbody>
            </table>
            {% else %}
            <div class="empty">No clients tracked yet</div>
            {% endif %}
        </div>

        <p class="refresh">Auto-refresh: <a href="/">Refresh now</a></p>
    </div>
</body>
</html>
'''


def format_time(seconds):
    """Format seconds as HH:MM:SS or MM:SS."""
    seconds = max(0, int(seconds))
    if seconds >= 3600:
        h = seconds // 3600
        m = (seconds % 3600) // 60
        s = seconds % 60
        return f'{h}:{m:02d}:{s:02d}'
    m = seconds // 60
    s = seconds % 60
    return f'{m}:{s:02d}'


def load_state():
    """Load state from JSON file."""
    try:
        with open(STATE_FILE, 'r') as f:
            return json.load(f)
    except FileNotFoundError:
        return {'clients': []}
    except json.JSONDecodeError:
        return {'clients': []}


def enrich_client(client):
    """Add computed fields to client data."""
    used = client.get('streaming_seconds', 0)
    remaining = max(0, QUOTA_SECONDS - used)
    pct = min(100, (used / QUOTA_SECONDS) * 100) if QUOTA_SECONDS > 0 else 0

    if pct >= 100:
        color = 'red'
    elif pct >= 75:
        color = 'yellow'
    else:
        color = 'green'

    return {
        'ip': client.get('ip', 'unknown'),
        'streaming_seconds': used,
        'quota_seconds': QUOTA_SECONDS,
        'remaining_seconds': remaining,
        'percentage_used': round(pct, 1),
        'last_reset_date': client.get('last_reset_date', ''),
        'is_blocked': client.get('is_blocked', False),
        'color': color
    }


@app.route('/')
def dashboard():
    """HTML dashboard."""
    state = load_state()
    clients = [enrich_client(c) for c in state.get('clients', [])]
    clients.sort(key=lambda x: x['streaming_seconds'], reverse=True)

    return render_template_string(
        HTML_TEMPLATE,
        clients=clients,
        quota_seconds=QUOTA_SECONDS,
        timestamp=datetime.now().strftime('%H:%M:%S'),
        format_time=format_time
    )


@app.route('/api/clients')
def api_clients():
    """JSON API: list all clients."""
    state = load_state()
    clients = [enrich_client(c) for c in state.get('clients', [])]

    return jsonify({
        'clients': clients,
        'quota_seconds': QUOTA_SECONDS,
        'timestamp': datetime.now().isoformat()
    })


@app.route('/api/clients/<ip>')
def api_client(ip):
    """JSON API: get specific client."""
    state = load_state()

    for client in state.get('clients', []):
        if client.get('ip') == ip:
            return jsonify(enrich_client(client))

    return jsonify({'error': 'Client not found'}), 404


if __name__ == '__main__':
    port = int(os.environ.get('PORT', '8080'))
    print(f'StreamGuard Web Interface')
    print(f'State file: {STATE_FILE}')
    print(f'Quota: {format_time(QUOTA_SECONDS)}')
    print(f'Listening on http://0.0.0.0:{port}')
    app.run(host='0.0.0.0', port=port, debug=False)
