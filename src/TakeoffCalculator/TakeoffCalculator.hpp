// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

/**
 * Input parameters for the takeoff distance calculator.
 *
 * All quantities use SI units internally unless the field comment
 * states otherwise.
 *
 * Method: reference-based correction (AFM/POH baseline + factors).
 *   The user provides the AFM distances measured at standard reference
 *   conditions (sea level, ISA, zero wind, level runway, MTOW) and
 *   the calculator applies multiplicative corrections for the actual
 *   field conditions.
 *
 * Correction factors applied (in order):
 *   k_weight  = (W / W_ref)²
 *               Follows from V_LOF ∝ √W and S_G ∝ V_LOF².
 *
 *   k_density = 1 / σ  where σ = ρ_actual / ρ_ISA_SL
 *               For a naturally-aspirated piston (T ∝ σ) the net
 *               correction is 1/σ.  Higher-performance analysis may
 *               use 1/σ^1.5 (see notes in .cpp).
 *
 *   k_wind    = ((V_LOF − V_headwind) / V_LOF)²
 *               Classic energy-method result.
 *
 *   k_slope   = (1 + 0.08 × slope_%)  ×  µ_correction
 *               ≈ 8 % increase per 1 % upslope; friction deviation
 *               relative to dry-paved reference (µ_ref = 0.02).
 *
 * Physics references:
 *   • ICAO Doc 7488/3  – Manual of the ICAO Standard Atmosphere
 *   • CS-23 / FAR Part 23 Amendment 64 – takeoff performance
 *   • Torenbeek, "Synthesis of Subsonic Airplane Design" (1982)
 *   • Roskam, "Airplane Design" Part VII (1988)
 *
 * Open-source verification sources:
 *   • JSBSim Flight Dynamics Model (BSD licence)
 *     https://github.com/JSBSim-Team/jsbsim
 *     Models complete ground roll physics; useful to cross-check.
 *
 *   • OpenPilot / ArduPlane ground-roll estimates (GPLv3)
 *     https://github.com/ArduPilot/ardupilot
 *
 *   • SKYbrary / EASA Performance Documents
 *     https://skybrary.aero/articles/take-performance
 *
 *   • FAA AC 23-8C, Flight Test Guide for Certification of Part 23
 *     Airplanes (public domain, freely downloadable).
 */
struct TakeoffParameters {

  // -----------------------------------------------------------------------
  // Aircraft data
  // -----------------------------------------------------------------------

  /** Current gross mass including crew, ballast, and fuel (kg). */
  double gross_mass_kg;

  /**
   * Reference mass used in the AFM/POH takeoff tables (kg).
   * Typically MTOW.
   */
  double reference_mass_kg;

  /** Wing reference area (m²). */
  double wing_area_m2;

  /**
   * Maximum lift coefficient at the takeoff flap setting (–).
   * Typical values:
   *   Clean / no flaps      : 1.4 – 1.6
   *   Partial flaps (10–20°): 1.6 – 2.0
   *   Full flaps            : 2.0 – 2.4
   * Used only to compute stall / rotation speeds.
   */
  double cl_max;

  // -----------------------------------------------------------------------
  // AFM/POH reference distances at standard conditions
  //   (sea level, ISA day: 15 °C / 1013.25 hPa, zero wind, level runway,
  //    reference_mass_kg)
  // -----------------------------------------------------------------------

  /** Ground roll to liftoff (m). */
  double afm_ground_roll_m;

  /**
   * Total distance to clear a 15 m (50 ft) obstacle (m).
   * Set to 0 to let the calculator estimate 1.7 × afm_ground_roll_m.
   */
  double afm_distance_50ft_m;

  // -----------------------------------------------------------------------
  // Field / atmospheric conditions
  // -----------------------------------------------------------------------

  /** Runway threshold elevation above MSL (m, i.e. QNH altitude). */
  double runway_elevation_m;

  /** Outside air temperature at runway level (°C). */
  double oat_celsius;

  /** QNH altimeter setting (hPa). */
  double qnh_hpa;

  /**
   * Headwind component along the takeoff run (m/s).
   * Positive = headwind (reduces takeoff distance).
   * Negative = tailwind (increases takeoff distance).
   */
  double headwind_mps;

  /**
   * Runway gradient in the direction of takeoff (%).
   * Positive = uphill (increases distance).
   * Negative = downhill (decreases distance).
   */
  double slope_percent;

  /**
   * Rolling friction coefficient (–).
   * Representative values:
   *   0.020  dry paved
   *   0.040  wet paved
   *   0.050  short dry grass
   *   0.080  long dry grass
   *   0.100  soft / wet ground
   */
  double rolling_friction;
};

// ---------------------------------------------------------------------------

/** Results produced by ComputeTakeoff(). */
struct TakeoffResult {

  // -----------------------------------------------------------------------
  // Atmosphere
  // -----------------------------------------------------------------------

  /** Pressure altitude of the runway (m). */
  double pressure_altitude_m;

  /** Density ratio σ = ρ_actual / ρ_ISA_SL (dimensionless). */
  double density_ratio;

  /**
   * Density altitude (m): the altitude in the ISA standard atmosphere
   * at which the air density equals ρ_actual.
   */
  double density_altitude_m;

  /** Actual air density (kg/m³). */
  double air_density_kgm3;

  // -----------------------------------------------------------------------
  // Reference speeds at gross mass and actual field conditions
  // -----------------------------------------------------------------------

  /** 1-g stall speed V_S (m/s). */
  double v_stall_ms;

  /** Rotation speed V_R = 1.10 × V_S (m/s). */
  double v_rotate_ms;

  /** Liftoff speed V_LOF = 1.15 × V_S (m/s). */
  double v_liftoff_ms;

  // -----------------------------------------------------------------------
  // Corrected distances
  // -----------------------------------------------------------------------

  /** Corrected ground roll (m). */
  double ground_roll_m;

  /** Corrected total distance to clear a 15 m obstacle (m). */
  double distance_50ft_m;

  // -----------------------------------------------------------------------
  // Individual correction factors (for pilot awareness)
  // -----------------------------------------------------------------------

  double k_weight;   /**< Weight correction    (W/W_ref)²             */
  double k_density;  /**< Density correction   1/σ                    */
  double k_wind;     /**< Wind correction      ((V_LOF−V_hw)/V_LOF)²  */
  double k_slope;    /**< Slope + friction correction                  */
};

// ---------------------------------------------------------------------------

/**
 * Compute corrected takeoff distances.
 *
 * All input distances and masses must be positive.  Degenerate or
 * unphysical inputs are clamped silently.
 *
 * @param p  Fully populated TakeoffParameters struct.
 * @return   Populated TakeoffResult.
 */
[[gnu::pure]]
TakeoffResult
ComputeTakeoff(const TakeoffParameters &p) noexcept;
