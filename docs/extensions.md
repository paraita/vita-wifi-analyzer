# Extensions planifiées

## 1) Latence (ping applicatif) - baseline livrée

Objectif: mesurer le RTT sans raw socket ni privilèges kernel.

- Module `src/latency_probe.*` présent.
- Implémenté:
  - sonde TCP connect-time (thread dédié),
  - sonde UDP echo (nécessite serveur qui renvoie le payload).
- Exposé:
  - RTT courant,
  - min/max/moyenne + EMA,
  - perte (ok/sent).

## 2) Perte de paquets - baseline livrée

- Sur N sondes:
  - paquets envoyés/reçus,
  - perte en pourcentage.
- Buffer temporel des dernières sondes exposé au rendu.
- Timeline visuelle succès/pertes dans l'écran `Stats`.

## 3) Écran statistiques - baseline livrée

- État d’UI avec navigation `L/R` livré:
  - écran radar,
  - écran stats détaillées.
- Affiché actuellement:
  - min/max/moyenne RSSI,
  - métriques RTT/perte,
  - percentiles RTT p50/p95,
  - uptime de connexion courant.

## 4) Mode terminal réseau (proxy externe)

- Canal TCP local configuré (IP de Raspberry Pi + port).
- Le proxy envoie des lignes de métriques texte (JSON compact ou CSV).
- Côté Vita:
  - parser robuste non bloquant,
  - affichage console pixel-art,
  - ring buffer de lignes.

## 5) Architecture pour rester fluide

- Garder rendu à 60 fps.
- Polling réseau et sondes dans une cadence lente (5-10 Hz), non bloquante.
- Limiter allocations dynamiques dans la boucle frame.
