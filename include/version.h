/**
 * @file version.h
 * @brief Version du programme et numéro de lot (commande VERSION).
 */
#ifndef VERSION_H
#define VERSION_H

#define FW_VERSION   "1.0.0"
#define LOT_NUMBER   "G3-2026-01"

#ifdef DEMO_BUILD
#define FW_VARIANT   "demo"
#else
#define FW_VARIANT   "production"
#endif

#endif /* VERSION_H */
