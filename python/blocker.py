import json
import time
from datetime import datetime
from config import config

class Blocker:
    def __init__(self, state_file='clients.json', blocked_ips_file='/etc/blocked-ips-v4.txt'):
        self.state_file = state_file
        self.blocked_ips_file = blocked_ips_file
        self.streaming_limit_seconds = config['streaming-limit-seconds']
        self.clients = self.load_state()

    def load_state(self):
        try:
            with open(self.state_file, 'r') as f:
                return json.load(f)
        except (FileNotFoundError, json.JSONDecodeError):
            return {}

    def save_state(self):
        with open(self.state_file, 'w') as f:
            json.dump(self.clients, f, indent=4)

    def update_client_status(self, client_ip, is_streaming, server_ips):
        if client_ip not in self.clients:
            self.clients[client_ip] = {
                'is_streaming': False,
                'consecutive_streaming_count': 0,
                'consecutive_not_streaming_count': 0,
                'streaming_start_time': None,
                'total_streaming_time': 0,
                'last_seen': time.time(),
                'server_ips': {}
            }

        client = self.clients[client_ip]
        client['last_seen'] = time.time()

        if is_streaming:
            client['consecutive_streaming_count'] += 1
            client['consecutive_not_streaming_count'] = 0
        else:
            client['consecutive_not_streaming_count'] += 1
            client['consecutive_streaming_count'] = 0

        if not client['is_streaming'] and client['consecutive_streaming_count'] >= 3:
            client['is_streaming'] = True
            client['streaming_start_time'] = time.time()
            print(f"Client {client_ip} started streaming.")

        elif client['is_streaming'] and client['consecutive_not_streaming_count'] >= 3:
            client['is_streaming'] = False
            if client['streaming_start_time']:
                streaming_duration = time.time() - client['streaming_start_time']
                client['total_streaming_time'] += streaming_duration
            client['streaming_start_time'] = None
            print(f"Client {client_ip} stopped streaming.")

        if client['is_streaming']:
            if client['streaming_start_time']:
                streaming_duration = time.time() - client['streaming_start_time']
                client['total_streaming_time'] += streaming_duration
                client['streaming_start_time'] = time.time() # Reset start time for the next interval

            for ip in server_ips:
                client['server_ips'][ip] = client['server_ips'].get(ip, 0) + 1 # Simple traffic count

        self.check_quota_and_block(client_ip)
        self.save_state()

    def check_quota_and_block(self, client_ip):
        client = self.clients[client_ip]
        if client['is_streaming'] and client['total_streaming_time'] > self.streaming_limit_seconds:
            if not client.get('is_blocked'):
                print(f"Client {client_ip} has exceeded the streaming quota.")

            # Get the server IP with the most traffic
            if client['server_ips']:
                # Sort by traffic count (descending)
                sorted_ips = sorted(client['server_ips'].items(), key=lambda item: item[1], reverse=True)

                # Find the first IP that is not already in the blocklist
                with open(self.blocked_ips_file, 'a+') as f:
                    f.seek(0)
                    blocked_ips = [line.strip() for line in f.readlines()]

                    for ip, count in sorted_ips:
                        if ip not in blocked_ips:
                            print(f"Blocking server {ip} for client {client_ip}.")
                            f.write(f"{ip}\n")
                            client['is_blocked'] = True
                            # We only block one IP per call, as per the README
                            break

    def cleanup_clients(self, timeout=3600):
        """Remove clients that haven't been seen for a while."""
        now = time.time()
        for client_ip, client_data in list(self.clients.items()):
            if now - client_data['last_seen'] > timeout:
                print(f"Removing inactive client {client_ip}.")
                del self.clients[client_ip]
        self.save_state()