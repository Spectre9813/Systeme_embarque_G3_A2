# Station météo embarquée 3W — groupe 3

Prototype de station météo pour navires (projet CESI Worldwide Weather Watcher),
sur carte STM32 Nucleo-L476RG, en C avec la HAL de ST sous PlatformIO.

## Compiler et téléverser

Prérequis : VS Code avec l'extension PlatformIO, carte branchée en USB (ST-Link).

| Action | Windows | Linux / macOS |
|---|---|---|
| Compiler la version client | `scripts\build build` | `scripts/build.sh build` |
| Compiler et téléverser | `scripts\build upload` | `scripts/build.sh upload` |
| Version démo + console | `scripts\build demo` | `scripts/build.sh demo` |
| Console série seule | `scripts\build monitor` | `scripts/build.sh monitor` |
| Tests sur PC | `scripts\build test` | `scripts/run_tests.sh` |
| Contrôler la carte SD | `scripts\build verify -Drive E:` | `scripts/build.sh verify /media/carte` |

Sans script : `pio run -e nucleo_l476rg -t upload` puis `pio device monitor`.
Si le ST-Link refuse le téléversement : `scripts\build upload -Copy` copie
`firmware.bin` sur le lecteur `NOD_L476RG`.

Deux environnements, un seul code :

- `nucleo_l476rg` : version client (LOG_INTERVAL en minutes, 30 min d'inactivité) ;
- `demo` : LOG_INTERVAL en secondes, retour du mode configuration après 60 s,
  TIMEOUT par défaut de 5 s. Les appuis de 5 s ne changent pas.

## Tests du matériel (programmes de la branche `test`)

Un programme autonome par composant, dans `test/materiel/`, chacun avec son
environnement : `test_boutons`, `test_bme280`, `test_gps`, `test_lumiere`,
`test_sd`. Dans PlatformIO : PROJECT TASKS → l'environnement → Upload, puis
Monitor. Ils ne touchent pas au programme de la station.

## Organisation

```
include/   types.h, constants.h, version.h et un .h par module
src/       main.c, setup.c (dépôt de base) et un .c par module
lib/       cesi_grove (dépôt de base), fatfs (R0.16), bme68x (API Bosch), bme280
test/      tests sur PC : test_station (modes, mesures, erreurs, console),
           test_storage (FatFs sur une carte simulée en RAM), stubs/
scripts/   build.ps1 / build.bat / build.sh, run_tests.sh, verifier_logs.py
```

Couches : application (`main`, `mode_manager`, `acquisition`, `console`),
services (`sensors`, `gps`, `rtc`, `storage`, `config`, `led`, `buttons`,
`serial`), pilotes (`board`, `sd_diskio`, `lib/`). Une couche n'appelle jamais
celle du dessus. Les fichiers du dépôt de base ne sont pas modifiés.

## Câblage (board_pins.h)

| Composant | Port Grove | Broches |
|---|---|---|
| BME280 (ou BME680, reconnu automatiquement), RTC DS1307 | I2C | PB8 / PB9 |
| Luminosité | A2 | PA4 |
| GPS Air530Z | A0 | PA0 / PA1 (UART4) |
| LED P9813 | D6 | PB10 / PA8 |
| Double bouton | D2 | PA10 (rouge) / PB3 (vert) |
| Carte SD | shield + 3 fils ICSP | PA5 / PA6 / PA7, CS PB5 |

Shield Grove sur 5 V (la DS1307 ne fonctionne pas en 3,3 V). Le niveau des
boutons (actifs à l'état haut ou bas) est mesuré au démarrage sur le bouton
vert : ne pas le tenir pendant la mise sous tension. Si rouge et vert sont
inversés : `RED_IS_BUTTON1 0` ; pour forcer le niveau : `BUTTON_PRESSED_LEVEL` (0 ou 1),
dans `constants.h`.

## Console (115 200 bauds, mode configuration)

`PARAMETRE=valeur`, `CLOCK=hh:mm:ss`, `DATE=mm,jj,aaaa`, `DAY=MON..SUN`,
`RESET`, `VERSION`, ainsi que `SHOW` (valeurs actuelles) et `HELP`.
