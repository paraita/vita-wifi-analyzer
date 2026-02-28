# Vita Wi-Fi Scope

Squelette homebrew PS Vita (VitaSDK + `vita2d`) pour visualiser la qualité du Wi-Fi connecté, en mode userland uniquement.

## Fonctionnalités actuelles

- Collecte réseau via `sceNetCtlInetGetInfo`:
  - SSID
  - canal Wi-Fi
  - RSSI
  - IP locale / masque
  - passerelle (route par défaut)
  - DNS primaire / secondaire
- Polling réseau à `8 Hz` (découplé du rendu).
- Lissage RSSI par moyenne exponentielle (EMA).
- Historique RSSI dans ring buffer (60 s).
- Rendu `vita2d` à `60 fps`:
  - radar animé
  - oscilloscope temporel RSSI
  - panneau texte (SSID/canal/RSSI + min/max/moyenne)
- Sonde de latence active en thread dédié:
  - mode TCP (par défaut)
  - mode UDP echo (prévu pour serveur écho)
  - RTT last/min/max/avg/EMA + percentiles p50/p95 + perte
- Écran `Stats` séparé (navigation manette).
  - uptime de connexion Wi-Fi
  - timeline récente des sondes (succès/pertes)
- Écran `Scan`:
  - scan LAN `/24` (hôtes actifs + ports ouverts usuels)
  - bascule source `LOCAL` / `PROXY` (TCP line protocol)

## Arborescence

- `src/main.c`: init VitaSDK, boucle principale, lifecycle.
- `src/net_monitor.*`: collecte des infos réseau et historique RSSI.
- `src/latency_probe.*`: sonde RTT active (thread non bloquant).
- `src/render.*`: rendu 2D style pixel-art.
- `assets/`: réservé aux ressources visuelles (sprites, fonts, palettes).
- `docs/`: notes d’architecture et extensions.

## Prérequis

- VitaSDK installé et configuré (`VITASDK` défini).
- `cmake`, `make`, toolchain VitaSDK disponible dans le `PATH`.
- Bibliothèque `vita2d` installée dans l’environnement VitaSDK.

## Build

```bash
mkdir -p build
cd build
cmake ..
make -j
```

Build avec incrément auto de version:

```bash
./scripts/build_and_bump.sh
```

Artefacts principaux:
- `build/vita_wifi_scope.vpk`
- `build/vita_wifi_scope.self`

## Contrôles

- `START` pour quitter l’application.
- `L/R` pour changer de vue (`RADAR`, `STATS`, `SCAN`).
- `SQUARE` pour aller directement à `SCAN`.
- En vue `SCAN`:
  - `TRIANGLE` démarre/arrête le scan local,
  - `SELECT` relance un scan local,
  - `UP/DOWN` scroll des résultats.

## Configuration rapide de la sonde latence

Modifier `probe_cfg` dans `src/main.c`:
- `target_ip` (IP cible),
- `target_port`,
- `protocol` (`LATENCY_PROTOCOL_TCP` ou `LATENCY_PROTOCOL_UDP`),
- `interval_ms`, `timeout_ms`.

## Notes sécurité / périmètre

- Aucun sniff Wi-Fi brut.
- Aucun mode monitor.
- Aucune capture de trames 802.11.
- APIs userland officielles VitaSDK uniquement.

## Extensions prévues

Voir `docs/extensions.md` pour préparer:
- latence active (UDP/TCP ping applicatif),
- perte de paquets,
- écran statistiques avancées,
- mode terminal réseau alimenté par proxy externe (Raspberry Pi).
