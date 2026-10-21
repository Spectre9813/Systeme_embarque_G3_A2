#!/usr/bin/env python3
"""
Contrôle des fichiers LOG écrits par la station 3W sur la carte SD.

Utilisé à la fin de la démonstration : la carte est lue sur le PC et le
script vérifie ce que le cahier des charges demande.

  python scripts/verifier_logs.py E:\\ --file-max-size 2048
  python scripts/verifier_logs.py /media/carte --file-max-size 512

Contrôles :
  - noms AAMMJJ_n.LOG, un _0 par jour, archives numérotées sans trou ;
  - chaque fichier commence par la ligne d'en-tête ;
  - aucun fichier ne dépasse FILE_MAX_SIZE ;
  - chaque ligne a le bon nombre de champs, une date et une heure valides
    (ou NA) et des valeurs numériques, NA ou vides ;
  - écart entre deux mesures consécutives d'un même jour.
"""
import argparse
import datetime as dt
import os
import re
import sys
from collections import defaultdict

HEADER = "DATE;HEURE;LATITUDE;LONGITUDE;TEMP_AIR;HYGR;PRESSURE;LUMIN;LUMIN_NIV"
NAME_RE = re.compile(r"^(\d{6}|NODATE)_(\d+)\.LOG$", re.IGNORECASE)
LEVELS = {"FAIBLE", "MOYENNE", "FORTE", "NA", ""}


def is_number(v):
    try:
        float(v)
        return True
    except ValueError:
        return False


def check_line(line, ncols):
    """Retourne (erreur ou None, horodatage ou None, compteurs)."""
    f = line.split(";")
    if len(f) != ncols:
        return f"{len(f)} champs au lieu de {ncols}", None, {}
    stamp = None
    if f[0] != "NA" or f[1] != "NA":
        try:
            stamp = dt.datetime.strptime(f[0] + " " + f[1], "%Y-%m-%d %H:%M:%S")
        except ValueError:
            return f"date ou heure invalide : {f[0]} {f[1]}", None, {}
    counts = {"NA": 0, "vide": 0}
    for v in f[2:-1]:
        if v == "NA":
            counts["NA"] += 1
        elif v == "":
            counts["vide"] += 1
        elif not is_number(v):
            return f"valeur non numérique : {v!r}", None, {}
    if f[-1] not in LEVELS:
        return f"niveau de luminosité inconnu : {f[-1]!r}", None, {}
    return None, stamp, counts


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("dossier", help="racine de la carte SD")
    ap.add_argument("--file-max-size", type=int, default=2048, help="FILE_MAX_SIZE configuré (octets)")
    args = ap.parse_args()

    ncols = len(HEADER.split(";"))
    files = sorted(n for n in os.listdir(args.dossier) if NAME_RE.match(n))
    if not files:
        print("Aucun fichier AAMMJJ_n.LOG trouvé.")
        return 1

    errors = 0
    by_day = defaultdict(list)
    total_lines = 0
    total_na = total_empty = 0
    stamps = defaultdict(list)

    print(f"{'Fichier':<16} {'Octets':>7} {'Lignes':>7}  Remarques")
    for name in files:
        base, rev = NAME_RE.match(name).groups()
        by_day[base.upper()].append(int(rev))
        path = os.path.join(args.dossier, name)
        raw = open(path, "rb").read()
        notes = []
        if len(raw) > args.file_max_size:
            notes.append(f"DÉPASSE FILE_MAX_SIZE ({args.file_max_size})")
            errors += 1
        text = raw.decode("ascii", errors="replace")
        lines = text.split("\r\n")
        if lines and lines[-1] == "":
            lines.pop()
        else:
            notes.append("dernière ligne incomplète")
        if not lines or lines[0] != HEADER:
            notes.append("EN-TÊTE ABSENT")
            errors += 1
        data = lines[1:]
        for i, line in enumerate(data, start=2):
            err, stamp, counts = check_line(line, ncols)
            if err:
                notes.append(f"ligne {i} : {err}")
                errors += 1
                continue
            total_na += counts["NA"]
            total_empty += counts["vide"]
            if stamp:
                stamps[base.upper()].append(stamp)
        total_lines += len(data)
        print(f"{name:<16} {len(raw):>7} {len(data):>7}  {'; '.join(notes) or 'OK'}")

    print()
    for base, revs in sorted(by_day.items()):
        revs.sort()
        archives = [r for r in revs if r > 0]
        expected = list(range(1, len(archives) + 1))
        msg = f"{base} : {len(archives)} archive(s)"
        if 0 not in revs:
            msg += ", PAS DE FICHIER _0"
            errors += 1
        if archives != expected:
            msg += f", NUMÉROS ATTENDUS {expected}"
            errors += 1
        s = sorted(stamps.get(base, []))
        if len(s) >= 2:
            gaps = [(b - a).total_seconds() for a, b in zip(s, s[1:])]
            msg += f", écart entre mesures : min {min(gaps):.0f} s, max {max(gaps):.0f} s"
        print(msg)

    print(f"\n{total_lines} mesures, {total_na} valeurs NA, {total_empty} champs vides.")
    print("RÉSULTAT :", "conforme" if errors == 0 else f"{errors} problème(s)")
    return 0 if errors == 0 else 2


if __name__ == "__main__":
    sys.exit(main())
