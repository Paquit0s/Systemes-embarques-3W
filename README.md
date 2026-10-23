# Projet 3W — Système embarqué

Projet de système embarqué réalisé dans le cadre du **Bloc Système Embarqué** du CESI (Équipe 7). Le prototype repose sur une carte **STM32 Nucleo-L476RG** et rassemble des mesures de luminosité, une date/heure et des données GPS.

> **État du projet : prototype en cours de développement.** L’acquisition peut être déclenchée par le bouton B1 et les informations sont affichées sur le port série. L’écriture des mesures sur carte SD n’est pas encore implémentée.

## Fonctionnement actuel

Au démarrage, le programme initialise les périphériques, tente d’initialiser l’horloge RTC, le GPS et le capteur de luminosité, puis initialise la carte SD. Une pression sur **B1** déclenche une acquisition :

- date et heure lues sur le RTC DS1307 ;
- mesure brute du capteur de luminosité ;
- recherche d’une trame GPS GGA ;
- affichage de la ligne de mesure sur la console série.

La trame GPS enregistrée dans la ligne est la phrase NMEA brute, et non un relevé CSV de coordonnées formaté. Si une mesure n’est pas disponible, le programme utilise `NA` pour le champ concerné.

## Matériel

- Carte **NUCLEO-L476RG** ;
- shield Seeed Grove avec lecteur de carte SD ;
- module **Grove Light Sensor** ;
- module **Grove GPS Air530Z** ;
- module **Grove RTC DS1307** ;
- module **Grove BME680** (pilote présent, mesure environnementale réelle à compléter).

### Interfaces configurées

| Fonction | Interface / broches configurées |
| --- | --- |
| Console série | USART2, PA2/PA3, 115200 bauds |
| GPS Air530Z | UART4, PA0/PA1, 9600 bauds |
| RTC DS1307 et BME680 | I2C1, PB8 (SCL) / PB9 (SDA) |
| Capteur de luminosité | ADC1, PA4 |
| Carte SD | SPI1, PA5 (SCK), PA6 (MISO), PA7 (MOSI), CS sur PB5 |
| Déclenchement d’acquisition | Bouton B1, PC13, interruption EXTI avec anti-rebond de 200 ms |

Vérifiez le brochage réel du shield et des modules avant le câblage. Les affectations sont définies dans `include/board_pins.h` et l’initialisation des périphériques dans `src/setup.c`.

## Prérequis

- [Visual Studio Code](https://code.visualstudio.com/) ;
- [PlatformIO](https://platformio.org/) (extension VS Code ou CLI) ;
- une carte NUCLEO-L476RG et le câble USB ST-LINK ;
- les modules Grove indiqués ci-dessus pour les essais sur matériel.

Le projet utilise **STM32Cube** avec PlatformIO. La configuration de la cible se trouve dans `platformio.ini`.

## Compiler et téléverser

Depuis un terminal ouvert à la racine du dépôt :

```sh
pio run
pio run --target upload
pio device monitor --baud 115200
```

Dans VS Code, les tâches équivalentes sont disponibles via l’interface PlatformIO : **Build**, **Upload** et **Monitor**.

## Utilisation

1. Connecter la carte au PC et téléverser le programme.
2. Ouvrir le moniteur série à **115200 bauds**.
3. Raccorder les modules selon les interfaces configurées et alimenter la carte.
4. Appuyer sur **B1** pour demander une acquisition.
5. Consulter la sortie série pour voir la mesure ou un message d’erreur.

Le GPS peut nécessiter un délai et une vue dégagée du ciel pour obtenir un fix. L’absence de données GPS n’empêche pas le programme de poursuivre la tentative d’acquisition.

## Organisation du dépôt

```text
include/              Types, broches et configuration générale
lib/cesi_grove/       Pilotes Grove (RTC, GPS, luminosité, BME680, SD, RGB, bouton)
src/                  Point d’entrée, initialisation et logique applicative
platformio.ini        Configuration de compilation PlatformIO
CHECKLIST_Phases_Projet.md
                      Suivi de l’avancement et des travaux restants
```

## Limites connues et suite du développement

- `SDLogger_WriteLine()` n’écrit pas encore de fichier : le support FatFs et `diskio` restent à intégrer. Les acquisitions ne sont donc pas archivées sur la carte SD.
- Le pilote BME680 renvoie actuellement des valeurs de démonstration ; les mesures réelles et leur compensation doivent être développées.
- Les modules de gestion des modes, de configuration UART et de contrôle des LED sont encore à compléter.
- L’acquisition est déclenchée par bouton ; l’acquisition périodique n’est pas encore en place.
- La checklist `CHECKLIST_Phases_Projet.md` détaille les tâches restantes.
