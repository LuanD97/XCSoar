// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "TugStore.hpp"

/**
 * Built-in table of common aerotow tugs.
 *
 * Fields: name, mtow_kg, empty_mass_kg, wing_area_m2, cl_max,
 *         afm_ground_roll_m, afm_distance_50ft_m, turbocharged
 *
 * Data is drawn from published POH / TCDS values representative of
 * sea-level ISA, MTOW, paved runway, zero wind.  Ground-roll figures
 * reflect the AFM book value; pilots should verify against their
 * specific aircraft's POH.
 *
 * Turbocharged column: set true for engines whose power output is
 * substantially maintained with altitude (e.g. TIO-540 in Pawnee T).
 */
static constexpr TugStore::Item kTugs[] = {
  // Piper PA-25 Pawnee 235
  // TCDS A7SO: MTOW 975 kg, empty ~599 kg, wing 17.30 m²
  // POH: ground roll ~280 m at MTOW, 50-ft dist ~460 m
  { "Piper PA-25 Pawnee 235",
    975, 599, 17.30, 1.45, 280, 460, false },

  // Piper PA-25 Pawnee 260
  // TCDS A7SO: MTOW 975 kg, empty ~617 kg, wing 17.30 m²
  // POH: ground roll ~265 m at MTOW, 50-ft dist ~435 m
  { "Piper PA-25 Pawnee 260",
    975, 617, 17.30, 1.45, 265, 435, false },

  // Piper PA-18 Super Cub 150
  // TCDS A-691: MTOW 794 kg, empty ~422 kg, wing 16.58 m²
  // POH: ground roll ~195 m at MTOW, 50-ft dist ~320 m
  { "Piper PA-18 Super Cub 150",
    794, 422, 16.58, 1.50, 195, 320, false },

  // Robin DR400/180 Regent
  // TCDS EASA.A.004: MTOW 1100 kg, empty ~618 kg, wing 14.19 m²
  // AFM: ground roll ~310 m at MTOW, 50-ft dist ~525 m
  { "Robin DR400/180",
    1100, 618, 14.19, 1.55, 310, 525, false },

  // Cessna R182 Skylane RG
  // TCDS 3A13: MTOW 1406 kg, empty ~843 kg, wing 16.17 m²
  // POH: ground roll ~366 m at MTOW, 50-ft dist ~605 m
  { "Cessna R182 Skylane RG",
    1406, 843, 16.17, 1.60, 366, 605, false },

  // Maule M-7-235C
  // TCDS A22CE: MTOW 1089 kg, empty ~590 kg, wing 17.00 m²
  // AFM: ground roll ~244 m at MTOW, 50-ft dist ~405 m
  { "Maule M-7-235C",
    1089, 590, 17.00, 1.55, 244, 405, false },
};

std::span<const TugStore::Item>
TugStore::GetAll() noexcept
{
  return {kTugs, std::size(kTugs)};
}
