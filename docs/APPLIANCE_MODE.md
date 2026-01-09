# StreamGuard Appliance Mode - Implementation Plan

## Goal
Make StreamGuard work as a **commercial product for Brazilian market** without requiring OpenWrt routers.

## Market Reality (Brazil)

### Good News: Regional ISPs Dominate!
| Metric | Value |
|--------|-------|
| Regional ISP market share | **52-56%** |
| Number of ISPs in Brazil | **20,000+** |
| Cities where small ISPs lead | **5,000+** |
| Households connected by regional ISPs | **60%+** |

### Two Types of ISP Setups

**Type A: Regional ISPs (52-56% of market)** - EASIER
- Separate ONT (fiber terminal) + customer's own router
- ISP provides PPPoE credentials
- Examples: Inetvip, Algar, Brisanet, Unifique, local providers
- StreamGuard can REPLACE the router directly!

**Type B: Big ISPs (44-48% of market)** - HARDER
- All-in-one locked device (ONU/ONT + router + WiFi)
- Vivo, Claro, Oi, TIM
- Examples: ZTE F670L, Huawei HG8145V5
- Requires StreamGuard WiFi Router approach

### Target Market Insight
Christian families in smaller cities (your target) are MORE likely to have regional ISPs!
This means the simpler "router replacement" approach works for the majority.

## Product Strategy: Two Main Products

### Product A: StreamGuard Router (For Regional ISPs - PRIMARY)
**Price: ~R$300-400 (USD $50-70)**
**Target: 52-56% of market (regional ISP customers)**

StreamGuard REPLACES customer's existing router:
```
[ISP's ONT] ──ethernet──► [StreamGuard Router] ~~~WiFi~~~► [All Devices]
  (fiber)                  (OpenWrt + nDPI)
```

Customer setup:
1. Unplug old router from ONT
2. Plug StreamGuard into ONT
3. Enter PPPoE username/password (from ISP)
4. Connect devices to StreamGuard WiFi
5. Done!

**Hardware options:**
- GL.iNet MT3000 (~$70) - Pre-built OpenWrt router, WiFi 6
- NanoPi R4S + USB WiFi (~$80) - More customizable
- TP-Link Archer C7 + custom firmware (~$40 used) - Budget option

### Product B: StreamGuard WiFi Add-on (For Big ISPs)
**Price: ~R$350-450 (USD $60-80)**
**Target: 44-48% of market (Vivo, Claro, Oi, TIM customers)**

StreamGuard adds WiFi layer over ISP's locked router:
```
[ISP All-in-One] ──ethernet──► [StreamGuard] ~~~WiFi~~~► [All Devices]
   (locked)                      (Router)
```

Customer setup:
1. Connect cable from ISP router to StreamGuard
2. Connect devices to StreamGuard WiFi
3. Optionally disable ISP router's WiFi

**Hardware:** Same as Product A, but simpler config (DHCP, not PPPoE)

---

## Technical Architecture

```
[ISP ONT/Router] ──eth0──► [StreamGuard] ~~~wlan0~~~► [All Devices]
                          │  (OpenWrt)
                          │
                          ├─ hostapd (WiFi AP)
                          ├─ dnsmasq (DHCP/DNS)
                          ├─ nftables (NAT + blocking)
                          ├─ libpcap (capture)
                          └─ nDPI (protocol detection)
```

### Software Stack
1. **OpenWrt** - Base OS (or Armbian)
2. **hostapd** - Creates WiFi access point
3. **dnsmasq** - DHCP server + local DNS
4. **nftables** - NAT + quota blocking
5. **StreamGuard** - DPI + quota tracking

## Implementation Steps (WiFi Router Appliance)

### 1. Add Router Mode to StreamGuard
New CLI options:
```
-w <iface>   WiFi interface to capture/monitor (e.g., wlan0)
-l           Local mode (use local nftables for blocking)
```

### 2. Update Blocking to Use Local nftables
```c
// For router/appliance mode, block locally
static void block_client_local(uint32_t client_ip) {
    snprintf(cmd, sizeof(cmd),
        "nft add element inet streamguard blocked_clients '{ %s timeout 24h }'",
        ip_str);
    system(cmd);
}
```

### 3. nftables Router Rules
```nft
table inet streamguard {
    set blocked_clients { type ipv4_addr; flags timeout; timeout 24h; }
    set streaming_destinations { type ipv4_addr; flags timeout; timeout 1h; }

    chain forward {
        type filter hook forward priority 0; policy accept;
        ip saddr @blocked_clients ip daddr @streaming_destinations drop
    }
}
```

### 4. Dynamic Destination Tracking (same as bridge)
```c
// When streaming traffic detected, add destination to set
if (is_trackable_traffic(proto)) {
    uint32_t stream_dst = is_lan_ip(src_ip) ? dst_ip : src_ip;
    add_streaming_destination(stream_dst);
}
```

### 5. Router System Configuration
Scripts to configure the device as a WiFi router:
- `hostapd.conf` - WiFi access point config
- `dnsmasq.conf` - DHCP (192.168.2.x) + DNS
- NAT masquerade: `nft add rule inet streamguard postrouting masquerade`

---

## Files to Modify

| File | Changes |
|------|---------|
| `src/streamguard.c` | Add `-w`/`-l` flags, local blocking backend |
| `src/Makefile` | No changes needed |
| `CLAUDE.md` | Document router/appliance mode |

## New Files to Create

| File | Purpose |
|------|---------|
| `scripts/router/setup.sh` | Router mode setup (hostapd, dnsmasq, NAT) |
| `scripts/router/hostapd.conf` | WiFi AP configuration |
| `scripts/router/dnsmasq.conf` | DHCP/DNS configuration |
| `scripts/router/streamguard.nft` | nftables rules |
| `docs/ROUTER_MODE.md` | Router appliance documentation |

---

## ISP-Specific Guides (Brazil)

Create guides for disabling WiFi on common ISP routers:

### Vivo Fibra
- Model: Mitrastar GPT-2742GX4X5, Askey RTF8225VW
- Access: http://192.168.15.1 (user: admin, pass: on sticker)
- WiFi disable: Rede WiFi > Desativar (if available)
- Note: Some models don't allow disabling WiFi

### Claro Fibra
- Model: ZTE F6645P, KAON PG2449
- Access: http://192.168.0.1
- WiFi disable: WLAN > Disable
- Also: Disable #NET-CLARO-WIFI public hotspot

### Oi Fibra
- Model: Huawei HG8145V5
- Access: http://192.168.1.1 (user: root, pass: on sticker)
- WiFi disable: WLAN > Basic > Disable

### TIM Ultra Fibra
- Model: Sagemcom F@st 5670
- Access: http://192.168.1.1
- WiFi disable: WiFi > Desativar

**Note:** Even if WiFi can't be disabled, customer can simply use StreamGuard's WiFi and ignore ISP router's WiFi.

---

## Customer Setup Guides

### Guide A: Regional ISP (PPPoE) - PRIMARY TARGET

**For customers with:** Inetvip, Algar, Brisanet, Unifique, and other regional providers

#### What's in the Box
- StreamGuard WiFi router
- Power adapter
- 1x Ethernet cable
- Quick start guide with your WiFi password

#### Setup (10 minutes)

**Step 1:** Get your PPPoE credentials from your ISP
- Call your provider and ask for "usuario e senha do PPPoE"
- Example: Username: `20b1eec1` Password: `******`

**Step 2:** Unplug your old router from the fiber terminal (ONT)

```
   Conversor de Fibra (ONT)       StreamGuard
  ┌───────────────────────┐      ┌──────────────┐
  │  ◉ Fibra   [LAN●]     │ ───► │  [WAN●] 📶   │
  │     (luz verde)       │cable │  StreamGuard │
  └───────────────────────┘      └──────────────┘
         │                              │
    (guarda o roteador                (seu novo
     antigo na gaveta)                 roteador!)
```

**Step 3:** Power on StreamGuard - wait for green light

**Step 4:** Connect to StreamGuard WiFi
- Network: **StreamGuard** (or custom name on label)
- Password: **on the label**

**Step 5:** Open browser, go to http://192.168.2.1
- Enter PPPoE username and password
- Click Save

**Step 6:** Done! Internet should work in 30 seconds.

#### FAQ (Regional ISP)

**Q: Where do I get my PPPoE credentials?**
A: Call your ISP support. Say "Preciso do usuario e senha PPPoE para usar meu proprio roteador."

**Q: My old router had a different IP (192.168.0.x). Is that a problem?**
A: No! StreamGuard uses 192.168.2.x by default. All your devices will get new IPs automatically.

**Q: Can I keep my old WiFi name and password?**
A: Yes! You can change the WiFi name and password in the StreamGuard settings.

---

### Guide B: Big ISP (Vivo/Claro/Oi/TIM)

**For customers with:** Vivo Fibra, Claro Fibra, Oi Fibra, TIM Ultra Fibra

#### Setup (5 minutes)

**Step 1:** Find a LAN port on your ISP router

**Step 2:** Connect ethernet cable from ISP router to StreamGuard

```
   Roteador da Operadora          StreamGuard
  ┌─────────────────────┐        ┌──────────────┐
  │  📶 WiFi (ignore)   │        │  📶 WiFi     │ <-- Use this!
  │                     │        │  "StreamGuard"│
  │  Fibra   [LAN●]     │ ─────► │  [WAN●]      │
  └─────────────────────┘ cable  └──────────────┘
```

**Step 3:** Power on StreamGuard - wait for green light

**Step 4:** Connect your devices to StreamGuard WiFi
- Ignore your old ISP WiFi (or disable it)

#### FAQ (Big ISP)

**Q: Do I need to configure my ISP router?**
A: No! Just connect one cable and use our WiFi.

**Q: What about my old WiFi?**
A: Leave it on for guests, or disable it. Devices on old WiFi are NOT monitored.

---

### Common FAQ

**Q: How do I manage quotas?**
A: Connect to http://192.168.2.1 from any device on StreamGuard WiFi.

**Q: What if StreamGuard stops working?**
A: Plug your old router back in. Your internet provider doesn't change.

**Q: Can I set different quotas for different family members?**
A: Yes! Each device (by IP/MAC) can have its own quota.

### LED Indicators
- Green solid = Running normally
- Green blinking = Processing traffic
- Red = Someone is blocked (quota exceeded)
- Yellow = Starting up

## Verification

### Phase 1: Test on Development Machine
```bash
# Build StreamGuard
cd src && make

# Test with pcap file (dry-run, local mode)
sudo ./streamguard -l -r youtube_test.pcap
```

### Phase 2: Test on Your Archer C7 (OpenWrt)
Since you already have OpenWrt running on Archer C7:
1. Cross-compile StreamGuard for OpenWrt (mips/mipsel)
2. Install on Archer C7 via scp
3. Run with `-l` flag for local blocking
4. Test with your own devices

### Phase 3: Test Full Router Mode
1. StreamGuard captures on br-lan (WiFi + LAN)
2. Blocking via local nftables
3. Quota management via web UI (later)

---

## Implementation Plan (Focus: Product A - Regional ISPs)

### Step 1: Code Changes to streamguard.c
- [ ] Add `-l` flag for local nftables blocking
- [ ] Change table/set names from `inet fw4` to `inet streamguard`
- [ ] Add dynamic streaming destination tracking
- [ ] Add initialization of nftables table/sets on startup

### Step 2: Create Router Setup Scripts
- [ ] `scripts/router/streamguard.nft` - nftables rules
- [ ] `scripts/router/setup-openwrt.sh` - OpenWrt-specific setup
- [ ] `scripts/router/setup-armbian.sh` - Armbian-specific setup

### Step 3: Test on Your Home Network
- [ ] Deploy to Archer C7
- [ ] Test detection with YouTube, Netflix, Instagram
- [ ] Test blocking when quota exceeded
- [ ] Test PPPoE reconnection

### Step 4: Create OpenWrt Package
- [ ] Create Makefile for OpenWrt SDK
- [ ] Build .ipk package
- [ ] Test installation on fresh OpenWrt

### Step 5: Documentation
- [ ] Update CLAUDE.md with router mode
- [ ] Create customer setup guides (PT-BR)
- [ ] Create troubleshooting guide

---

## Hardware for Production

### Immediate (Testing)
- Your existing **TP-Link Archer C7** with OpenWrt

### Short-term (Pilot Sales)
- **GL.iNet GL-MT3000** (~$70) - Pre-installed OpenWrt, WiFi 6
- **GL.iNet GL-AXT1800** (~$80) - Similar, good availability in Brazil

### Long-term (Scale)
- Custom board design or bulk purchase deal
- Consider Banana Pi BPI-R3 for higher performance

---

## Sources

### Market Research
- [Tecnoblog - Pequenos provedores lideram em 5000 cidades](https://tecnoblog.net/noticias/pequenos-provedores-lideram-internet-fixa-em-5-mil-cidades-do-brasil/)
- [Teleco - Market share banda larga fixa](https://www.teleco.com.br/blarga.asp)
- [IPNews - Mercado provedores regionais 2025](https://ipnews.com.br/mercado-de-provedores-regionais-muda-de-direcao-em-2025/)
- [Minha Operadora - Principais provedores Brasil](https://www.minhaoperadora.com.br/2024/04/quais-sao-os-principais-provedores-de-internet-do-brasil-veja-panorama-e-dados-atuais.html)

### Technical
- [Forum Adrenaline - Eliminar ONU/ONT](https://forum.adrenaline.com.br/threads/eliminar-ou-trocar-onu-ont-hgu-da-operadora-provedor.699243/)
- [ZTE Modem Tools (GitHub)](https://github.com/douniwan5788/zte_modem_tools)
- [Tecnoblog - Desativar NET-CLARO-WIFI](https://tecnoblog.net/responde/como-desativar-a-rede-net-claro-wifi-do-meu-modem/)
- [nftables Bridge Filtering](https://wiki.nftables.org/wiki-nftables/index.php/Bridge_filtering)

### Hardware
- [GL.iNet MT3000 Beryl AX](https://www.gl-inet.com/products/gl-mt3000/)
- [NanoPi R4S Wiki](https://wiki.friendlyelec.com/wiki/index.php/NanoPi_R4S)
- [OpenWrt Table of Hardware](https://openwrt.org/toh/start)
