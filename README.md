# Projet 3W — Station Météo Embarquée (Équipe 7)

Projet réalisé dans le cadre du **Bloc Système Embarqué** du CESI (Livrable 2).
Le système repose sur une carte **STM32 Nucleo-L476RG** cadencée à **80 MHz (PLL)** et assure l'acquisition périodique, l'horodatage, la validation de cohérence et l'archivage CSV sur carte microSD (FatFs) de données environnementales et GPS.

---

## 1. Architecture matérielle et brochage

### Matériel utilisé
- Carte **STM32 Nucleo-L476RG** (Cortex-M4 @ 80 MHz, 1 Mo Flash, 96 Ko SRAM)
- **Seeed Grove Base Shield v2** (sélecteur VCC positionné sur **3.3V**)
- **Seeed SD Card Shield v4.3** (microSD formatée en **FAT32**)
- **Grove RTC DS1307** (`I2C1`, adresse `0x68`)
- **Grove BME280 / BME680** (`I2C1`, adresse `0x76` ou `0x77`)
- **Grove Light Sensor v1.2** (`ADC1_IN9` sur port `A2`)
- **Grove GPS Air530Z** (`UART4` sur port `A0`, 9600 bauds)
- **Grove Chainable RGB LED P9813** (port `D6`)
- **2 Boutons Grove** : Bouton Vert (`D2`) et Bouton Rouge (`D3` / `B1`)

### Tableau de brochage (`include/board_pins.h`)

| Périphérique | Port Shield | Broche STM32 | Périphérique interne / Configuration |
| :--- | :--- | :--- | :--- |
| **Console Série (PC)** | USB ST-Link | `PA2` (TX) / `PA3` (RX) | `USART2` @ 115200 bauds (IT + Ring Buffer) |
| **GPS Air530Z** | Port Grove `A0` | `PA0` (TX) / `PA1` (RX) | `UART4` @ 9600 bauds (IT + Ring Buffer NMEA) |
| **RTC DS1307 & BME** | Ports Grove `I2C` | `PB8` (SCL) / `PB9` (SDA) | `I2C1` @ 100 kHz (`TIMINGR = 0x10909CEC`) |
| **Capteur Luminosité** | Port Grove `A2` | `PA4` | `ADC1_IN9` (12 bits `0..4095`, ramené à `0..1023` pour seuils) |
| **Carte SD (SPI1)** | `D11/D12/D13` + `ICSP` | `PA5` (SCK), `PA6` (MISO), `PA7` (MOSI) | `SPI1` (Init 312.5 kHz $\rightarrow$ Transfert 5 MHz) |
| **Carte SD (CS)** | `D4` (ou `D10`) | `PB5` (`D4`) / `PB6` (`D10`) | `GPIO_Output` (détection automatique `D4` puis `D10`) |
| **LED RGB P9813** | Port Grove `D6` | `PB10` (CLK) / `PA8` (DATA) | Protocole synchrone 2 fils P9813 |
| **Bouton Vert** | Port Grove `D2` | `PA10` (`EXTI10`) | `GPIO_PULLUP` (Active-LOW, anti-rebond 35 ms) |
| **Bouton Rouge** | Port Grove `D3` / `B1` | `PB3` (`EXTI3`) / `PC13` (`EXTI13`) | `GPIO_PULLUP` (Active-LOW, anti-rebond 35 ms) |

> ⚠️ **Important — Pontage SPI obligatoire pour le Seeed SD Card Shield v4.3 sur STM32 Nucleo-L476RG :**
> - La carte **STM32 Nucleo-L476RG** ne possède **pas** de connecteur mâle `ICSP` 6 broches en bas au centre : ses signaux `SPI1` sortent uniquement sur les connecteurs externes **`D11` (`PA7`), `D12` (`PA6`), `D13` (`PA5`)**.
> - Le **Seeed SD Card Shield v4.3** route ses signaux `MOSI`, `MISO`, `SCK` uniquement sur son connecteur central femelle **`ICSP` 6 broches** (ses broches `D11`, `D12`, `D13` ne sont pas reliées au lecteur SD sur le PCB du shield, et le connecteur ICSP du Grove Base Shield est un simple passe-plat vertical sans liaison vers `D11-D13`).
> - Il faut donc relier **3 fils Dupont** entre les broches externes `D11/D12/D13` et le connecteur `ICSP` :
>   - **`D11` (`PA7` - MOSI)** $\longrightarrow$ **`ICSP Pin 4` (MOSI)** *(rangée droite, broche du milieu)*
>   - **`D12` (`PA6` - MISO)** $\longrightarrow$ **`ICSP Pin 1` (MISO)** *(rangée droite, broche côté point blanc/encoche)*
>   - **`D13` (`PA5` - SCK)** $\longrightarrow$ **`ICSP Pin 3` (SCK)** *(rangée gauche, broche du milieu)*

```text
Connecteur ICSP 6 broches (vue de dessus, point blanc ● en haut à droite) :
        ┌───────────┐
  MISO  │ 1 ●     2 │  +5V
  SCK   │ 3       4 │  MOSI
  RST   │ 5       6 │  GND
        └───────────┘
```

---

## 2. Modes de fonctionnement et signalisation LED

| Mode | LED RGB | Acquisition | Stockage SD | Transitions |
| :--- | :--- | :--- | :--- | :--- |
| **STANDARD** | 🟢 Vert fixe | Périodique (`LOG_INTERVALL`, défaut 10 min) | Actif (`LOG_xxxx.CSV`) | Appui 5 s Bouton Vert $\rightarrow$ **ÉCO**<br>Appui 5 s Bouton Rouge $\rightarrow$ **MAINTENANCE** |
| **ÉCO** | 🔵 Bleu fixe | Périodique (`2 × LOG_INTERVALL`, GPS 1 acquisition sur 2) | Actif (`LOG_xxxx.CSV`) | Appui 5 s Bouton Vert $\rightarrow$ **STANDARD**<br>Appui 5 s Bouton Rouge $\rightarrow$ **MAINTENANCE** |
| **MAINTENANCE** | 🟠 Orange fixe | Temps réel console série | **Suspendu** (fichier fermé proprement, retrait SD sûr) | Appui 5 s Bouton Rouge $\rightarrow$ Retour au mode précédent (**STANDARD** ou **ÉCO**) |
| **CONFIGURATION** | 🟡 Jaune fixe | Suspendue | Suspendu | Maintenir Bouton Rouge au démarrage (ou pendant les 3 s suivant le boot). Sortie automatique après **30 min d'inactivité UART** $\rightarrow$ **STANDARD** |

### Signaux d'erreurs LED RGB (prioritaires sur la couleur du mode)
- 🔴/🔵 **Rouge / Bleu (1 Hz, 50/50)** : Erreur d'accès à l'horloge RTC (`ERR_RTC_INACCESSIBLE`)
- 🔴/🟡 **Rouge / Jaune (1 Hz, 50/50)** : Erreur d'accès aux données du GPS (`ERR_GPS_TIMEOUT` / `ERR_GPS_PAS_FIX`)
- 🔴/🟢 **Rouge / Vert (1 Hz, 50/50)** : Erreur d'accès aux données d'un capteur environnemental (`ERR_ACCES_CAPTEUR`)
- 🔴/🟢 **Rouge / Vert (1 Hz, Vert 2× plus long : 333 ms / 667 ms)** : Données reçues d'un capteur incohérentes (`ERR_CAPTEUR_INCOHERENT`)
- 🔴/⚪ **Rouge / Blanc (1 Hz, 50/50)** : Carte SD pleine (`ERR_SD_PLEINE`)
- 🔴/⚪ **Rouge / Blanc (1 Hz, Blanc 2× plus long : 333 ms / 667 ms)** : Erreur d'accès ou d'écriture sur la carte SD (`ERR_SD_ABSENTE` / `ERR_SD_ECRITURE`)

---

## 3. Commandes UART en mode CONFIGURATION (`115200 bauds`)

Le module `config_uart.c` supporte la syntaxe officielle `CLE=VALEUR` du Cahier des Charges ainsi que les commandes interactives (`HELP`, `GET`, `SAVE`, `RESET`, `VERSION`) avec persistance en Flash interne STM32L4 (Page 511, `0x080FF800`) :

- `LOG_INTERVALL=<1..1440>` : Intervalle entre 2 mesures en minutes (défaut : `10`)
- `FILE_MAX_SIZE=<256..65535>` : Taille maximale d'un fichier de log en octets avant archivage/rotation (défaut : `2048`)
- `TIMEOUT=<1..300>` : Timeout d'acquisition capteur en secondes (défaut : `30`)
- `LUMIN=<0|1>`, `LUMIN_LOW=<0..1023>` (défaut `255`), `LUMIN_HIGH=<0..1023>` (défaut `768`)
- `TEMP_AIR=<0|1>`, `MIN_TEMP_AIR=<-40..85>` (défaut `-10`), `MAX_TEMP_AIR=<-40..85>` (défaut `60`)
- `HYGR=<0|1>`, `HYGR_MINT=<-40..85>` (défaut `0`), `HYGR_MAXT=<-40..85>` (défaut `50`)
- `PRESSURE=<0|1>`, `PRESSURE_MIN=<300..1100>` (défaut `850`), `PRESSURE_MAX=<300..1100>` (défaut `1080`)
- `CLOCK=HH:MM:SS`, `DATE=MM,DD,YYYY`, `DAY=MON..SUN` : Réglage de l'horloge RTC DS1307
- `RESET` : Réinitialisation des paramètres aux valeurs par défaut
- `VERSION` : Affiche la version du programme et le numéro de lot
- `SAVE` : Sauvegarde immédiate en mémoire Flash non volatile

---

## 4. Compilation, Téléversement et Tests Unitaires

### Compiler et téléverser le firmware principal
```powershell
py -3.10 -m platformio run -e nucleo_l476rg
py -3.10 -m platformio run -e nucleo_l476rg -t upload
py -3.10 -m platformio device monitor -p COM4 -b 115200
```

### Exécuter la suite de tests unitaires Unity (`P3-1`)
La suite de tests unitaires située dans `test/test_unit/test_main.c` vérifie :
1. `test_rtc_bcd_conversion` : Conversions BCD $\leftrightarrow$ Décimal de l'horloge RTC DS1307
2. `test_nmea_checksum_validation` : Calcul et vérification du checksum XOR des trames NMEA
3. `test_nmea_coordinates_conversion` : Conversion des coordonnées NMEA `ddmm.mmmm` (N/S/E/W) en degrés décimaux
4. `test_config_domain_validation` : Validation des plages de valeurs et valeurs par défaut du Cahier des Charges
5. `test_csv_format_nominal_and_na` : Formatage d'une ligne CSV nominale et dégradée (`NA` / `PARTIAL`)

```powershell
py -3.10 -m platformio test -e nucleo_l476rg
```

---

## 5. Organisation du dépôt

```text
include/              Contrats de types (cesi_types.h), brochage (board_pins.h) et en-têtes applicatifs
lib/cesi_grove/       Pilotes matériels Grove (RTC DS1307, BME680/280, GPS Air530Z, Light, RGB P9813, Button, SD Logger)
src/                  Couche BSP (setup.c, sd_diskio_spi.c) et application (main.c, mode_manager.c, capteur_manager.c, stockage.c, config_uart.c, led_control.c, interruption.c)
test/test_unit/       Tests unitaires Unity (test_main.c)
platformio.ini        Configuration PlatformIO (STM32Cube L4, FatFs, Unity)
CHECKLIST_Phases_Projet.md
                      Suivi d'avancement des phases 0, 1, 2 et 3
```
