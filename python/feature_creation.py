import struct
import socket
import numpy as np
import pandas as pd
import pathlib
from joblib import load

config = {}
config['selected_features'] = ['ack_entropy',
                            'ttl_entropy',
                            'tcp_ack_var',
                            'udp_nports',
                            'pkt_entropy',
                            'dw_pkt_avg',
                            'dw_ttl_avg',
                            'dw_ttl_unique',
                            'dw_pkt_entropy',
                            'dl_pkt_avg']

config['lan-subnet'] = '192.168.0.0'
config['lan-subnet-mask'] = 24
# window of statistical analysis of tcp header data and grouping to create features 
config['window-size'] = 10 

config['path'] = {}
config['path']['raw'] = pathlib.Path(__file__).parent / 'training' / 'raw.h5'
config['model'] = pathlib.Path(__file__).parent / 'etree.joblib'

def update_hdf(df):
    dfdisk = pd.read_hdf(config['path']['raw'])
    dfdisk = pd.concat([dfdisk, df], ignore_index=True)
    dfdisk.to_hdf(config['path']['raw'], key="raw_packets", mode='w', format='table')

def read_hdf():
    df = pd.read_hdf(config['path']['raw'])
    return df

def load_model():    
    model = load(config['model'])
    return model


def preprocess(df):
    """
    Function to preprocess tcpdump raw tcp header data
    """

    # Function to convert IP string to integer
    def ip_to_int(ip):
        return struct.unpack("!I", socket.inet_aton(ip))[0]

    # Function to check if IPs are in subnet
    def is_ip_in_subnet(ip_array, subnet, mask_bits):
        subnet_int = ip_to_int(subnet)
        mask = (0xFFFFFFFF << (32 - mask_bits)) & 0xFFFFFFFF
        ip_ints = np.vectorize(ip_to_int)(ip_array)
        return (ip_ints & mask) == (subnet_int & mask)

    # Initialize new columns
    df['client'] = ''
    df['server'] = ''
    df['updown'] = 0  # -1 for download, +1 for upload

    # Perform vectorized subnet checks
    df.loc[:, 'src_in_subnet'] = is_ip_in_subnet(df['src_ip'].values, config['lan-subnet'], config['lan-subnet-mask'])
    df.loc[:, 'dst_in_subnet'] = is_ip_in_subnet(df['dst_ip'].values, config['lan-subnet'], config['lan-subnet-mask'])

    # Filter out rows where both src_ip and dst_ip are in the subnet (ignore internal traffic)
    internal_traffic_mask = df['src_in_subnet'] & df['dst_in_subnet']
    df = df[~internal_traffic_mask]
    # we might have empty df if no wan-lan traffic

    # Assign 'client' and 'server' based on whether src_ip or dst_ip is in the subnet
    df.loc[:, 'client'] = np.where(df['src_in_subnet'], df['src_ip'], df['dst_ip'])
    df.loc[:, 'server'] = np.where(df['src_in_subnet'], df['dst_ip'], df['src_ip'])

    # Assign 'updown' based on whether src_ip or dst_ip is in the subnet
    df.loc[:, 'updown'] = np.where(df['src_in_subnet'], 1, -1)

    # turn string classes on numerical values
    # df.loc[:, 'ip_flag'] = pd.factorize(df.ip_flags)[0]
    df.loc[:, 'tcp_flag'] = pd.factorize(df.tcp_flags)[0] 

    # Drop the temporary columns
    df = df.drop(columns=['src_in_subnet', 'dst_in_subnet', 'src_ip', 'dst_ip', 'ip_flags', 'tcp_flags'])

    return df


def csv_preprocess(path):
    """
        Function to preprocess tcpdump raw tcp header data read from csv file   
    Args:
        path (str): path to csv file created from scapy_sniffer.py on training mode
    """
    df = pd.read_csv(path)

    return preprocess(df)


def make_windowed_features(df):

    # Set time as index
    # needed for resample bellow
    df['dttime'] = pd.to_datetime(df['time'], unit='s')
    df.set_index('dttime', inplace=True)
    # Now group by the window (resample) and calculate features
    # has no effect on real time classification since it's already grouped by client
    # and on 10 seconds window
    grouped = df.resample(f'{config["window-size"]}s')

    def entropy(df, colname):        
        prob_dist = df[colname].value_counts(normalize=True)# Step 1: Calculate probabilities        
        entropy = -np.sum(prob_dist * np.log2(prob_dist))  # Step 2: Calculate entropy
        return entropy

    # Function to calculate features for each client in each window
    def calculate_window_features(group):
        # Upload/Download packets 
        up_mask = group['updown'] > 0
        up_packets = group.loc[up_mask]
        dw_packets = group.loc[~up_mask]        
        up_pkt_avg = up_packets['packet_size'].mean()
        dw_pkt_avg = dw_packets['packet_size'].mean()
        up_pkt_var = up_packets['packet_size'].var()
        dw_pkt_var = dw_packets['packet_size'].var()
        up_pkt_sum = up_packets['packet_size'].sum()
        dw_pkt_sum = dw_packets['packet_size'].sum()      
        net_updown = up_pkt_sum - dw_pkt_sum  
        div_updown = up_packets['packet_size'].sum() / (dw_pkt_sum if dw_pkt_sum else 1)
        div_updown_var = up_pkt_var / (dw_pkt_var if dw_pkt_var else 1)

        pkt_entropy = entropy(group, 'packet_size')
        up_pkt_entropy = entropy(up_packets, 'packet_size')
        dw_pkt_entropy = entropy(dw_packets, 'packet_size')
        pkt_ack_entropy = entropy(group, 'tcp_ack')
        pkt_ttl_entropy = entropy(group, 'ttl')
        
        dw_ttl_unique = dw_packets['ttl'].nunique()
        dw_ttl_avg = dw_packets['ttl'].mean()
        tcp_ack_var = group['tcp_ack'].var()
        tcp_nports = group['tcp_sport'].nunique() + group['tcp_dport'].nunique() 
        udp_nports = group['udp_sport'].nunique() + group['udp_dport'].nunique() 
        # ip_flag = group['ip_flag'].nunique() - useless 2 values
        tcp_seq = group['tcp_seq'].nunique()
        tcp_ack = group['tcp_ack'].nunique()
        tcp_flag = group['tcp_flag'].nunique()

        # Packet delay average and variance
        group['time_diff'] = group.time.diff()  # Time difference between packets
        packet_delay_average =  group['time_diff'].mean()
        jitter =  group['time_diff'].var() # packet_delay_variance is jitter
        packet_delay_entropy = entropy(group, 'time_diff')

        # Connection multiplexing (unique destination IPs for each client)
        num_unique_ips = group['server'].nunique()

        return pd.Series({
            'pkt_entropy' : pkt_entropy,
            'up_pkt_entropy' : up_pkt_entropy,
            'dw_pkt_entropy' : dw_pkt_entropy,
            'up_speed': up_packets['packet_size'].sum()/config['window-size'],
            'dw_speed': dw_packets['packet_size'].sum()/config['window-size'],
            'net_updown' : net_updown,
            'div_updown' : div_updown,
            'div_updown_var' : div_updown_var,
            'up_pkt_var' : up_packets['packet_size'].var(),
            'dw_pkt_var' : dw_packets['packet_size'].var(),
            'up_pkt_avg' : up_pkt_avg,
            'dw_pkt_avg' : dw_pkt_avg,
            'dw_ttl_unique' : dw_ttl_unique,
            'dw_ttl_avg' : dw_ttl_avg,
            'tcp_ack_var' : tcp_ack_var,
            'updw_pkt' : dw_pkt_avg-up_pkt_avg,
            'dl_pkt_avg': packet_delay_average,
            'dl_pkt_entropy' : packet_delay_entropy,
            'jitter': jitter,
            'num_unique_ips': num_unique_ips,
            'tcp_nports' : tcp_nports,
            'udp_nports': udp_nports,
            'tcp_seq' : tcp_seq,
            'tcp_ack' : tcp_ack,
            'tcp_flags' : tcp_flag,
            'ttl_entropy' : pkt_ttl_entropy,
            'ack_entropy' : pkt_ack_entropy,
        })

    # groups of features on windows of 10 seconds
    gfeatures = grouped.apply(calculate_window_features).dropna()
    return gfeatures
