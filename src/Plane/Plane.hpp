// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

#include "util/StaticString.hxx"
#include "Polar/Shape.hpp"

/** Default maximum speed (m/s, ≈270 km/h) for device-provided polars
    when no plane-specific value is known */
static constexpr double DEFAULT_MAX_SPEED = 75.0;

struct Plane
{
  StaticString<32> registration;
  StaticString<6> competition_id;
  StaticString<32> type;

  StaticString<32> polar_name;

  PolarShape polar_shape;

  double empty_mass;
  double dry_mass_obsolete; // unused entry for plane file compatibility. to be removed 2023..
  double max_ballast;
  double max_speed;
  double wing_area;

  /** Time to drain full ballast (s) */
  unsigned dump_time;

  unsigned handicap;

  /**
   * Type of glider from a list, published by WeGlide server to select
   * the correct glider id for the flight to upload.  The list is
   * published on https://raw.githubusercontent.com/ the data of the
   * selected glider you can find on
   * https://api.weglide.org/v1/aircraft/$(ID)
   */
  unsigned weglide_glider_type;

  /**
   * Is a plane profile file active (not the default plane)?
   * This is set when a plane profile file is loaded from Profile::GetPath("PlanePath").
   */
  bool plane_profile_active;

  /**
   * Takeoff performance data used by the aerotow calculator.
   * These are optional; zero values indicate "not set".
   */
  struct TakeoffConfig {
    /**
     * AFM/POH ground roll at reference mass, sea-level ISA,
     * zero wind, level runway (m).  0 = not set.
     */
    double afm_ground_roll_m;

    /**
     * AFM/POH total distance to clear a 15 m (50 ft) obstacle at
     * reference conditions (m).  0 = not set (auto = 1.7 × ground roll).
     */
    double afm_distance_50ft_m;

    /**
     * Maximum lift coefficient at three flap settings:
     *   [0] Clean / no flaps  (typical 1.4 – 1.6)
     *   [1] Takeoff flap      (typical 1.6 – 2.0)
     *   [2] Full flap         (typical 2.0 – 2.4)
     * Zero means "not set".
     */
    double cl_max[3];
  } takeoff;
};
