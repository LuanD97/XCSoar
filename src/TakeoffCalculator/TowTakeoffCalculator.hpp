// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

/**
 * Aerotow takeoff distance calculator: tug + glider pair.
 *
 * Physics overview
 * ================
 * An aerotow involves two aircraft connected by a rope.  The sequence
 * of events on a normal takeoff run is:
 *
 *   Phase 1  — Combined ground roll
 *     Both aircraft roll simultaneously from rest.  The tug provides
 *     all thrust; the glider's weight, wheel friction, and rope drag
 *     add to the effective rolling resistance.  This phase ends when
 *     the first aircraft reaches its liftoff speed.
 *
 *   Phase 2a — Glider airborne, tug still rolling  (NORMAL)
 *     Because the glider typically has a higher CL_max and lower wing
 *     loading it lifts off before the tug.  While the glider is in the
 *     "low hover" phase above the runway the rope still loads the tug
 *     (glider weight minus its lift).  A small residual rope-drag
 *     factor ε ≈ 0.03 is applied to the tug's ground roll in this
 *     phase.
 *
 *   Phase 2b — Tug airborne first  (UNUSUAL — WARNING)
 *     If the tug lifts off before the glider the rope pull acts
 *     downward on the glider and simultaneously pitches the tug's
 *     nose down / tail up.  This is aerodynamically hazardous.
 *     The flag TowTakeoffResult::tug_liftoff_before_glider is set and
 *     the result should be presented with a warning.
 *
 *   Phase 3  — Both aircraft airborne
 *     Normal steady tow climb; not modelled here (out of scope).
 *
 * Reference physics
 * =================
 *   • Gliding Australia GPC-14A, "Aerotowing Procedures"
 *   • OSTIV Technical Soaring, Litt & Maughmer (1993): "Ground Roll of
 *     a Glider–Tug Pair"
 *   • Raymer, "Aircraft Design: A Conceptual Approach" §17.5
 */

struct TowTakeoffParameters {

  // -----------------------------------------------------------------------
  // Tug data
  // -----------------------------------------------------------------------

  /** Current tug gross mass (kg). */
  double tug_gross_mass_kg;

  /** Tug reference (AFM/POH table) mass (kg). */
  double tug_reference_mass_kg;

  /** Tug wing area (m²). */
  double tug_wing_area_m2;

  /** Tug maximum lift coefficient at takeoff setting. */
  double tug_cl_max;

  /** Tug AFM ground roll at reference conditions (m). */
  double tug_afm_ground_roll_m;

  /** Tug AFM distance to 50 ft obstacle (m); 0 → 1.7 × ground roll. */
  double tug_afm_distance_50ft_m;

  // -----------------------------------------------------------------------
  // Glider data
  // -----------------------------------------------------------------------

  /** Glider current gross mass (kg). */
  double glider_gross_mass_kg;

  /** Glider reference (AFM/POH table) mass (kg). */
  double glider_reference_mass_kg;

  /** Glider wing area (m²). */
  double glider_wing_area_m2;

  /** Glider maximum lift coefficient at takeoff flap setting. */
  double glider_cl_max;

  /** Glider AFM ground roll at reference conditions (m). */
  double glider_afm_ground_roll_m;

  // -----------------------------------------------------------------------
  // Tow rope
  // -----------------------------------------------------------------------

  /**
   * Residual rope-drag factor applied to the tug during Phase 2a
   * (glider hovering, tug still rolling).
   * Dimensionless; default 0.03 is a reasonable estimate.
   */
  double rope_drag_factor;

  // -----------------------------------------------------------------------
  // Field / atmosphere (shared)
  // -----------------------------------------------------------------------

  /** Runway threshold elevation (m). */
  double runway_elevation_m;

  /** Outside air temperature (°C). */
  double oat_celsius;

  /** QNH (hPa). */
  double qnh_hpa;

  /** Headwind component (m/s); negative = tailwind. */
  double headwind_mps;

  /** Runway gradient in takeoff direction (%). */
  double slope_percent;

  /** Rolling friction coefficient (–). */
  double rolling_friction;
};

// ---------------------------------------------------------------------------

/** Per-aircraft speeds and distances. */
struct TowAircraftResult {
  double v_stall_ms;     ///< Stall speed (m/s)
  double v_liftoff_ms;   ///< Liftoff speed (m/s)
  double ground_roll_m;  ///< Corrected solo ground roll (m)
};

/** Full aerotow result. */
struct TowTakeoffResult {

  // -----------------------------------------------------------------------
  // Atmosphere
  // -----------------------------------------------------------------------

  double density_altitude_m;  ///< Density altitude (m)
  double density_ratio;       ///< σ = ρ/ρ₀ (–)

  // -----------------------------------------------------------------------
  // Individual aircraft performance
  // -----------------------------------------------------------------------

  TowAircraftResult tug;
  TowAircraftResult glider;

  // -----------------------------------------------------------------------
  // Combined tow result
  // -----------------------------------------------------------------------

  /**
   * Estimated combined takeoff ground roll (m).
   *
   * Accounts for the extra rolling resistance the glider imposes on
   * the tug during Phase 1 (both rolling) and the residual rope load
   * during Phase 2a (glider hovering).
   */
  double combined_ground_roll_m;

  /**
   * True when the tug is calculated to lift off before the glider.
   * This is the abnormal/hazardous sequence.  The UI should display
   * a prominent warning.
   */
  bool tug_liftoff_before_glider;
};

// ---------------------------------------------------------------------------

/**
 * Compute aerotow takeoff performance.
 *
 * @param p  Fully populated TowTakeoffParameters.
 * @return   Populated TowTakeoffResult.
 */
[[gnu::pure]]
TowTakeoffResult
ComputeTowTakeoff(const TowTakeoffParameters &p) noexcept;
