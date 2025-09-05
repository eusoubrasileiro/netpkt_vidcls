import pathlib

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
config['class-1-threshold'] = 0.6
config['streaming-limit-seconds'] = 3600 # 1 hour

config['path'] = {}
config['path']['raw'] = pathlib.Path(__file__).parent / 'training' / 'raw.h5'
config['model'] = pathlib.Path(__file__).parent / 'etree.joblib'
