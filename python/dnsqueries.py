## read and print dns queries from router
import subprocess

# SSH command to stream logread
ssh_command = [
    "ssh", "root@router", 
    "logread -f"
]

# Open SSH process
with subprocess.Popen(ssh_command, stdout=subprocess.PIPE, bufsize=1, universal_newlines=True) as process:
    for line in process.stdout:
        if 'dnsmasq' in line:
            print(line.strip())