# Vita Wi-Fi Scope

Application homebrew PS Vita (VitaSDK + `vita2d`) pour surveiller le Wi-Fi et scanner le LAN en userland.

## Ce que fait l'application

- Monitoring Wi-Fi en temps réel:
  - SSID, canal, RSSI instantané + EMA
  - IP locale, masque, passerelle, DNS
  - historique RSSI (ring buffer)
- Sonde de latence active:
  - TCP/UDP configurable
  - RTT last/min/max/avg/EMA
  - p50/p95, pertes globales et récentes
- Scanner LAN `/24`:
  - découverte hybride (ICMP/TCP + mDNS/SSDP/NBNS)
  - détection d'hôtes, ports ouverts, rôle gateway
  - tri et filtres dans la vue SCAN
- Alertes runtime:
  - nouveaux hôtes, départs, erreurs scanner
- Exports JSON + visualisation interprétée:
  - liste snapshots
  - comparaison baseline (ajouts/suppressions)
- Outils Bluetooth:
  - état BT, appareils appairés, événements inquiry

## Écrans disponibles

- `RADAR`
- `STATS`
- `SCAN`
- `DETAIL`
- `ALERTS`
- `SETTINGS`
- `BT`
- `EXPORT`

Navigation globale:
- `L / R`: écran précédent/suivant
- `START`: quitter

## Contrôles principaux

### SCAN
- `TRIANGLE`: démarrer/arrêter scan local
- `SELECT`: relancer scan
- `UP / DOWN`: sélection ligne
- `LEFT / RIGHT`: changer tri
- `CIRCLE`: changer filtre
- `CROSS`: ouvrir détail hôte

Note: le scan ne se lance pas automatiquement au démarrage; lancement manuel requis.

### DETAIL
- `CIRCLE`: retour SCAN

### ALERTS
- `UP / DOWN`: scroll
- `TRIANGLE`: clear alerts

### SETTINGS
- `UP / DOWN`: sélectionner option
- `CROSS`: appliquer/toggle

### BT
- `TRIANGLE`: start/stop inquiry
- `SELECT`: refresh

### EXPORT
- `UP / DOWN`: sélectionner snapshot
- `LEFT / RIGHT`: scroller la liste
- `TRIANGLE`: créer un snapshot
- `CROSS`: définir baseline
- `CIRCLE`: clear baseline

## Build

Prérequis:
- VitaSDK configuré (`VITASDK`)
- `cmake`, `make`
- `vita2d` installée dans l'environnement VitaSDK

Build standard:

```bash
mkdir -p build
cd build
cmake ..
make -j
```

Build + incrément version:

```bash
./scripts/build_and_bump.sh
```

Artefacts:
- `build/vita_wifi_scope.vpk`
- `build/vita_wifi_scope.self`

## Déploiement FTP (optionnel)

Script de déploiement (vers Vita FTP):

```bash
FTP_HOST=10.0.0.38 ./scripts/deploy_ftp.sh
```

Chemin cible par défaut:
- `ux0:/downloads/vita_wifi_scope.vpk`

## Arborescence utile

- `src/main.c`: boucle principale, inputs, orchestration
- `src/render.*`: rendu UI
- `src/net_monitor.*`: télémétrie Wi-Fi
- `src/latency_probe.*`: sonde latence
- `src/lan_scanner.*`: scan LAN
- `src/discovery.*`: mDNS/SSDP/NBNS
- `src/alerts.*`: file d'alertes
- `src/export_json.*`: écriture snapshots
- `src/export_viewer.*`: lecture/synthèse exports
- `src/bt_monitor.*`: infos Bluetooth
- `src/ui_audio.*`: feedback audio UI

## Limites connues

- Le scan LAN est effectué en userland: fiabilité variable selon routeur/firewall.
- Certains réseaux filtrent ICMP/TCP scan.
- Aucun mode monitor/sniff 802.11 (API officielles userland uniquement).
