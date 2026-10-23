# Checklist — travail restant (Projet 3W, Équipe 7)

> **État de référence : 21/10/2026 14:20** — HEAD `flo b8a751d` + modifications locales en cours.
> ⚠️ Une autre session travaille en parallèle : vérifier `git status` avant d'éditer un fichier.
> Légende : `[x]` = fait/vérifié · `[ ]` = à faire · 🔴 bloquant · 🟠 majeur · 🟡 moyen · 🔵 qualité
> Cocher : remplacer `[ ]` par `[x]` (éditeur Markdown, VS Code, GitHub…).

---

## ✅ Déjà fait (vérifié le 21/10 à 14:20)

- [x] Compilation de tous les `.c` : **0 erreur**, 2 warnings (`setup.c` paramètre `file` non utilisé ; `grove_bme680.c` `write_reg` non utilisé).
- [x] Lien relogeable (`ld -r`) : **aucun doublon de symbole** (plus de double `main`, plus de doublons `hi2c1`/`huart2`/`hspi1`/`hadc1`).
- [x] Un seul `main()` : `main.c` (10 lignes) délègue à `interruption()`.
- [x] Appels `MX_*` fantômes supprimés → `Global_Init()` du BSP.
- [x] EXTI + anti-rebond 200 ms sur le bouton B1 (PC13), `EXTI15_10_IRQHandler` + `HAL_GPIO_EXTI_Callback`.
- [x] Corrections logiques : comparaisons booléennes RTC/SD (`!Grove...`), `RTC_DateTime date_time = {0}`, `light_text = "NA"` réellement utilisé, GPS parsé via `GPS_Data.raw_sentence`, boucle `gps_data` corrigée.
- [x] Fichiers modules créés (vides pour l'instant) : `src/capteur_manager.c`, `src/config_uart.c`, `src/led_control.c`, `src/mode_manager.c`, `src/stockage.c`.
- [x] Commit `b8a751d` « Correction des dernières erreurs dans interruption.c ».

---

## Phase 0 — Débloquer et stabiliser le socle 🔴

### Build & dépôt
- [x] **P0-1** Lancer `pio run` complet depuis VS Code/terminal et vérifier **0 erreur de lien** (le lien objet est OK d'après l'analyse ; confirmer avec HAL/startup).
- [x] **P0-2** Compléter `platformio.ini` : `monitor_speed = 115200`, activation FatFs (ou `lib_deps`), nettoyer les flags inutiles.
- [x] **P0-3** Nettoyer le dépôt : supprimer le fichier `test` (15 o, suivi par Git), `modifications-locales.patch` (0 o), `include/main.h` (vide, plus inclus).
- [x] **P0-4** Committer les nouveaux fichiers `src/*.c` non suivis ; pousser `flo` (2 commits non poussés) ; décider du sort de `.vscode/*` modifiés (fichiers générés).
- [x] **P0-5** Clarifier Git : `main` local en retard de 6, `origin/main` = 13 fichiers vides ; documenter la branche de référence (README ou commit).

### Architecture BSP
- [x] **P0-6** Inverser les responsabilités `main.c` / `interruption.c` : `main()` = init + boucle principale ; `interruption.c` = EXTI + file d'événements uniquement (supprimer le `while(1)` de `interruption()` et l'appel « programme » depuis `main`).
- [x] **P0-7** Centraliser les handles dans `setup.c` : déplacer `hadc1` depuis `grove_light.c`, supprimer `huart1` (jamais défini) et `huart4` (jamais initialisé) ou les utiliser ; exposer `GPS_UART_HANDLE` via le BSP/getter.
- [x] **P0-8** LED : ne pas réutiliser LD2/PA5 (conflit SPI1_SCK) ; signalisation sur le RGB (PA8/PB10), sinon documenter que LD2 reste inutilisée.
- [x] **P0-9** Retirer/sortir de `sd_logger.c` le diagnostic bit-bang PA5/PA7 (reconfigure le SCK SPI1 et le pin LED).

---

## Phase 1 — Socle applicatif 🟠

### Types & contrats
- [ ] **P1-1** `ModeSysteme` (STANDARD / CONFIGURATION / ECO / MAINTENANCE) dans `cesi_types.h`.
- [ ] **P1-2** `CodeErreur` complet (RTC, GPS, ACCES_CAPTEUR, CAPTEUR_INCOHERENT, SD_PLEINE, SD_ECRITURE, CONFIG_INVALIDE, MEMOIRE) + identification du capteur concerné.
- [ ] **P1-3** `Config` (LOG_INTERVAL, TIMEOUT par capteur, seuils, limite SD) avec unités, domaines, valeurs par défaut.
- [ ] **P1-4** `Mesure` (horodatage, T/HR/P, lumière, lat/lon, statut par grandeur) ; supprimer le doublon validité/erreur ; corriger `pressionv`.
- [ ] **P1-5** Créer les en-têtes des modules : `mode_manager.h`, `capteur_manager.h`, `stockage.h`, `config_uart.h`, `led_control.h`.

### Événements & modes
- [ ] **P1-6** Deux boutons (rouge/vert) : pins définitifs (PA10/PB3 ou PC13 + un autre), EXTI, file d'événements horodatés.
- [ ] **P1-7** Anti-rebond centralisé (20–50 ms), événements `APPUI` / `RELACHE`, mesure de la durée d'appui.
- [ ] **P1-8** `mode_manager` : machine à états complète (dont ECO→STANDARD, sortie CONFIGURATION, MAINTENANCE→mode précédent) + timings `HAL_GetTick()`.
- [ ] **P1-9** `led_control` : mapping mode → couleur (vert/jaune/bleu/orange) + erreurs (séquences), priorité erreurs, aucun blocage.

### Acquisition & stockage
- [ ] **P1-10** `capteur_manager` : RTC + lumière + BME680 + GPS, timeouts par capteur, 2 timeouts consécutifs → erreur, seuils → `Mesure`.
- [ ] **P1-11** BME680 réel : lecture coefficients de calibration + oversampling + mode forcé + compensation Bosch (remplacer le placeholder 20 °C / 50 % / 1013,25 hPa).
- [ ] **P1-12** GPS : ring buffer UART, parsing GGA complet (UTC, fix, satellites, lat/lon), statut retourné par `GroveGPS_Init`.
- [ ] **P1-13** RTC : contrôle du bit CH, mise à l'heure, politique d'erreur (arrêt ou poursuite — trancher), `HAL_I2C_Mem_Read/Write`.
- [ ] **P1-14** `stockage` : FatFs (présent dans le framework) + `diskio` SPI + CSV (en-tête, unités, horodatage) + nommage/rotation + flush ; **remplacer `SDLogger_WriteLine` (stub)**.
- [ ] **P1-15** Archivage : limite de fichier (octets ou lignes), stratégie (renommer/supprimer), comportement si carte absente/pleine.
- [ ] **P1-16** `config_uart` : commandes (`SET`/`GET`/`HELP`/`SAVE`), validation syntaxe/domaine, confirmation/erreurs, persistance **flash** (pas d'EEPROM sur L476).
- [ ] **P1-17** `main` : init + ordonnanceur coopératif (aucun `HAL_Delay` en boucle), acquisition périodique selon `LOG_INTERVAL`, UART non bloquante.

### Intégration
- [ ] **P1-18** Acquisition périodique réelle en mode STANDARD (le bouton ne doit plus déclencher une mesure unique).
- [ ] **P1-19** Ligne de log complète : RTC + T/HR/P + lumière + GPS parsé (ou `NA`), pas la trame brute.
- [ ] **P1-20** Signalisation LED + console des erreurs, conforme à la table du livrable.

---

## Phase 2 — Robustesse & performances 🟡

- [ ] **P2-1** Horloge 80 MHz PLL (cohérence `F_CPU`) + recalcul `TIMINGR` I2C et prescaler SPI.
- [ ] **P2-2** SPI SD : init ≤ 400 kHz puis 4–8 MHz ; CMD16/CMD59 ; timeouts d'init adaptés.
- [ ] **P2-3** Calibration ADC + conversion lumière (lux/% ou seuils) documentée (ADC 12 bits → 0..4095).
- [ ] **P2-4** UART console en IT/DMA + ring buffer (plus de `HAL_UART_Receive` bloquant).
- [ ] **P2-5** Watchdog IWDG + reprise si SD retirée/absente ; arrêt propre du fichier en MAINTENANCE.
- [ ] **P2-6** Comportements par mode : ECO (intervalle ×2, GPS 1 cycle/2), CONFIG (30 min d'inactivité), MAINTENANCE (live UART, pas d'écriture SD).
- [ ] **P2-7** Tests de transitions sur plateau + chronométrage (appui 5 s, timeout 30 min).

---

## Phase 3 — Qualité & rendu 🔵

- [ ] **P3-1** Tests unitaires (env `native`) : parsing NMEA, conversion BCD, validation config, format CSV.
- [ ] **P3-2** README : câblage, build, utilisation, équipe + schéma de câblage.
- [ ] **P3-3** Nettoyage du code : commentaires `[CORRECTION]`, warnings, noms, en-têtes de fichiers, convention de nommage française/anglaise.
- [ ] **P3-4** Mettre à jour le Livrable 2 (section suivante) et le livrer avec le code.
- [ ] **P3-5** Revue croisée + commit final propre (+ tag éventuel).

---

## Livrable 2 — corrections 📄

### Contenu
- [ ] **L1-1** Ajouter une section « architecture matérielle / câblage » : pins, bus, adresses I2C (0x68, 0x76/0x77), bauds (115200 console, 9600 GPS), alimentation, shields.
- [ ] **L1-2** Mettre à jour le schéma ET le tableau des modules : couche pilotes (`lib/cesi_grove`) + BSP (`setup.c`) ; `mode_manager` au schéma ; `types.h` au tableau.
- [ ] **L1-3** Diagramme/tableau d'états complet des modes (toutes les transitions, conditions, timings).
- [ ] **L1-4** Mécanisme EXTI : événements, horodatage, anti-rebond, IRQ/NVIC, traitement différé.
- [ ] **L1-5** Config : paramètres (type/unité/min/max/défaut), syntaxe exacte des commandes, persistance **flash**.
- [ ] **L1-6** SD : format CSV, nommage, limite, archivage, flush, comportements d'erreur.
- [ ] **L1-7** Timeouts/retries/seuils par capteur + politique « NA ».
- [ ] **L1-8** `Mesure` corrigée : unités, statut par grandeur, `pressionv`, doublon validité/erreur.
- [ ] **L1-9** `CodeErreur` complété + mapping erreur → LED (séquences, priorité ; éviter bleu = ECO et erreur RTC).
- [ ] **L1-10** Corriger les incohérences : `boucle_principale()`, `enregistrer_mesure_sur_SD()` au tableau, `rtc_init()` vs lecture, double `capteurs_init()`.
- [ ] **L1-11** Matrice de traçabilité Livrable 1 → modules → fonctions → tests + plan de tests.

### Forme
- [ ] **L2-1** Style « Titre » (supprimer « ² » et tabulations), numérotation automatique des titres, supprimer les 2 titres Heading 2 vides.
- [ ] **L2-2** Réduire les tableaux 11 et 12 à ≤ 16 cm (actuellement 17,8 cm > 16,0 cm utiles).
- [ ] **L2-3** En-tête/pied de page + pagination, sommaire automatique, légendes figure/tableaux.
- [ ] **L2-4** Corriger les typos : `pressionv`, « qui son traités », « était stockés », « architecture général », « ²Livrable », double espace « Carte  SD », virgule finale…
- [ ] **L2-5** Métadonnées (titre, sujet, mots-clés) + conclusion + répartition/planning + section risques.

---

## Vérifications matérielles avant câblage final 🔌

- [ ] **M1-1** PA5 : ne jamais câbler la LED LD2 en même temps que le SCK de la SD.
- [ ] **M1-2** Port I2C Grove : vérifier A4/A5 (= PC1/PC0 / I2C3) vs PB8/PB9 (= D14/D15 / I2C1) ; choisir et câbler.
- [ ] **M1-3** Alimentation/level shifting du SD Card Shield (spécifié 3,5–5,5 V, typ. 5 V).
- [ ] **M1-4** Polarité des boutons Grove + pull interne (`GPIO_PULLDOWN` actuel probablement inversé).
- [ ] **M1-5** GPS : croisement TX/RX, pin swap, antenne, premier fix.
- [ ] **M1-6** RTC : pile CR1225 présente, adresse 0x68 ; BME680 0x76/0x77.
- [ ] **M1-7** Ajouter un scan I2C au démarrage (diagnostic RTC/BME680).

---

## Commandes utiles

```bash
cd "…/Bloc Système Embarqué/Projet/CESI---Projet-Systeme-Embarqu--main/Systemes-embarques-3W"

pio run                        # build complet
pio run -t upload              # flash la Nucleo
pio device monitor -b 115200   # console série

git status                     # ⚠️ vérifier avant d'éditer (autre session)
git log --oneline --decorate -5
git add -A && git commit -m "…"
git push origin flo
```

---

## Ordre conseillé pour reprendre

1. **Phase 0** (build, dépôt, rôles `main`/`interruption`) — 1 à 2 h.
2. **Types + événements + `mode_manager` + `led_control`** (le cœur visible du livrable).
3. **`capteur_manager` + BME680 + GPS** (acquisition réelle).
4. **`stockage` (FatFs) + `config_uart`** (persistance).
5. **`main` coopératif + intégration**, puis **Phase 2** (robustesse).
6. **Phase 3** (tests, README, rendu) et **Livrable 2** en parallèle de la rédaction du rapport.
