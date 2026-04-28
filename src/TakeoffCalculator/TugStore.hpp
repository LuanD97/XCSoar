// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

#include <span>

/**
 * Built-in library of common aerotow tug aircraft.
 *
 * Data sources: manufacturer POH / type-certificate data sheets, with values
 * representative of sea-level ISA, MTOW, hard paved runway, zero wind.
 */
namespace TugStore {

struct Item {
  /** Common name / variant */
  const char *name;

  /** Maximum take-off weight (kg) */
  double mtow_kg;

  /** Empty rigged mass (kg) */
  double empty_mass_kg;

  /** Wing reference area (m²) */
  double wing_area_m2;

  /**
   * Maximum lift coefficient (clean, at takeoff flap setting).
   * Used to compute V_LOF of the tug.
   */
  double cl_max;

  /**
   * AFM ground roll at MTOW, sea-level ISA, zero wind, level runway (m).
   */
  double afm_ground_roll_m;

  /**
   * AFM total distance to clear 15 m obstacle at reference conditions (m).
   * 0 = estimate as 1.7 × ground roll.
   */
  double afm_distance_50ft_m;

  /** True if the engine is turbocharged (affects density correction). */
  bool turbocharged;
};

[[gnu::const]]
std::span<const Item>
GetAll() noexcept;

} // namespace TugStore
