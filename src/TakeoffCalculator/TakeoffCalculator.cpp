// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "TakeoffCalculator.hpp"

#include <algorithm>
#include <cmath>

// ---------------------------------------------------------------------------
// ISA constants (ICAO Doc 7488, troposphere, h < 11 000 m)
// ---------------------------------------------------------------------------

static constexpr double kISA_P0   = 101325.0;  ///< Sea-level pressure    (Pa)
static constexpr double kISA_T0   = 288.15;    ///< Sea-level temperature  (K)
static constexpr double kISA_RHO0 = 1.225;     ///< Sea-level density  (kg/m³)
static constexpr double kISA_L    = 0.0065;    ///< Lapse rate            (K/m)

static constexpr double kGravity  = 9.80665;   ///< Standard gravity     (m/s²)
static constexpr double kR_air    = 287.058;   ///< Specific gas constant (J/kg·K)

/// Pressure exponent = g / (R × L) ≈ 5.2561
static constexpr double kISA_exp  = kGravity / (kR_air * kISA_L);

// ---------------------------------------------------------------------------

TakeoffResult
ComputeTakeoff(const TakeoffParameters &p) noexcept
{
  TakeoffResult r{};

  // -------------------------------------------------------------------------
  // 1.  Air pressure at the runway (QNH-corrected ISA column)
  //
  //     P_field = QNH_Pa × (1 − L·h / T₀)^(g/RL)
  //
  //     The runway elevation is the QNH altitude (height above the QNH
  //     zero datum), so we apply the pressure-height formula directly.
  // -------------------------------------------------------------------------
  const double qnh_pa   = p.qnh_hpa * 100.0;
  const double t_ratio  = 1.0 - kISA_L * p.runway_elevation_m / kISA_T0;
  const double safe_tr  = t_ratio > 1e-4 ? t_ratio : 1e-4;  // guard h > ~44 km
  const double p_field  = qnh_pa * std::pow(safe_tr, kISA_exp);

  // -------------------------------------------------------------------------
  // 2.  Pressure altitude (altitude in ISA atmosphere with pressure p_field)
  //
  //     h_PA = T₀/L × (1 − (P_field/P₀)^(1/exp))
  // -------------------------------------------------------------------------
  r.pressure_altitude_m =
      (kISA_T0 / kISA_L) * (1.0 - std::pow(p_field / kISA_P0, 1.0 / kISA_exp));

  // -------------------------------------------------------------------------
  // 3.  Actual air density via the ideal-gas law:  ρ = P / (R · T)
  // -------------------------------------------------------------------------
  const double t_actual_k   = p.oat_celsius + 273.15;
  r.air_density_kgm3        = p_field / (kR_air * t_actual_k);
  r.density_ratio           = r.air_density_kgm3 / kISA_RHO0;

  // -------------------------------------------------------------------------
  // 4.  Density altitude (altitude in ISA with the same density)
  //
  //     In ISA: ρ/ρ₀ = (1 − L·h/T₀)^(exp−1)
  //     → h_DA = T₀/L × (1 − σ^(1/(exp−1)))
  //
  //     exp−1 ≈ 4.2561
  // -------------------------------------------------------------------------
  {
    const double sigma = std::max(r.density_ratio, 1e-3);
    r.density_altitude_m =
        (kISA_T0 / kISA_L) *
        (1.0 - std::pow(sigma, 1.0 / (kISA_exp - 1.0)));
  }

  // -------------------------------------------------------------------------
  // 5.  Stall / rotation / liftoff speeds
  //
  //     V_S = √(2 · m · g / (ρ · S · CL_max))
  //
  //     V_R   = 1.10 × V_S   (FAR 23.51 / CS-23 minimum: ≥1.05 × V_S1)
  //     V_LOF = 1.15 × V_S
  // -------------------------------------------------------------------------
  {
    const double S   = std::max(p.wing_area_m2, 0.1);
    const double CL  = std::max(p.cl_max,       0.1);
    const double rho = std::max(r.air_density_kgm3, 1e-3);

    r.v_stall_ms   = std::sqrt(2.0 * p.gross_mass_kg * kGravity /
                               (rho * S * CL));
    r.v_rotate_ms  = 1.10 * r.v_stall_ms;
    r.v_liftoff_ms = 1.15 * r.v_stall_ms;
  }

  // -------------------------------------------------------------------------
  // 6.  Correction factors
  // -------------------------------------------------------------------------

  // 6a.  Weight correction
  //      Derivation: S_G ∝ V_LOF² ∝ W/ρ  → at constant density S_G ∝ W
  //      Adding in the liftoff speed dependence on W gives S_G ∝ (W/W_ref)².
  //      This is consistent with FAR 23-era POH correction tables.
  {
    const double w_ref = std::max(p.reference_mass_kg, 1.0);
    const double ratio = p.gross_mass_kg / w_ref;
    r.k_weight = ratio * ratio;
  }

  // 6b.  Density correction
  //      For a normally-aspirated piston engine T ∝ σ, so
  //          S_G ∝ V_LOF² / (net-force/mass) ∝ (1/σ) / (σ/σ) = 1/σ
  //      Electric motors with constant power give ≈ 1/σ^1.5; for
  //      conservative planning use the 1/σ result for all types.
  r.k_density = 1.0 / std::max(r.density_ratio, 0.01);

  // 6c.  Wind correction
  //      The kinetic energy that must be supplied by thrust during the
  //      ground roll is proportional to V_LOF_ground² where
  //          V_LOF_ground = V_LOF − V_headwind
  //      Therefore S_G ∝ ((V_LOF − V_hw) / V_LOF)²
  //
  //      Physical limit: a strong headwind cannot reduce ground roll
  //      below a practical minimum (clamped at 0.50 × V_LOF^2).
  {
    const double v_lof = std::max(r.v_liftoff_ms, 1.0);
    double v_eff       = v_lof - p.headwind_mps;
    v_eff = std::max(v_eff, 0.5 * v_lof);        // clamp extreme headwind
    r.k_wind = (v_eff / v_lof) * (v_eff / v_lof);
  }

  // 6d.  Slope + friction correction
  //      Upslope angle θ adds a component g·sin(θ) ≈ g·θ to the
  //      deceleration; empirically ~8 % per 1 % upslope.
  //      Additional friction beyond the dry-paved reference (µ=0.02)
  //      reduces net acceleration; modelled as a linear factor.
  {
    static constexpr double kMuRef        = 0.02;
    static constexpr double kSlopeFactor  = 0.08;

    const double mu_excess = std::max(p.rolling_friction - kMuRef, 0.0);
    const double mu_cor    = 1.0 + 0.50 * (mu_excess / kMuRef);
    r.k_slope = (1.0 + kSlopeFactor * p.slope_percent) * mu_cor;
    // Downslope can reduce ground roll but not below 50 % of flat:
    r.k_slope = std::max(r.k_slope, 0.5);
  }

  // -------------------------------------------------------------------------
  // 7.  Corrected ground roll
  // -------------------------------------------------------------------------
  const double k_combined =
      r.k_weight * r.k_density * r.k_wind * r.k_slope;

  r.ground_roll_m = p.afm_ground_roll_m * k_combined;

  // -------------------------------------------------------------------------
  // 8.  Corrected distance to clear 15 m obstacle
  //
  //     The airborne segment (rotation to obstacle clearance) is less
  //     sensitive to wind than the ground roll.  We blend the corrections
  //     for the two segments proportionally.
  //
  //     Reference for blend: Torenbeek §5.4, Roskam Part VII §3.3.
  // -------------------------------------------------------------------------
  {
    const double afm50 = (p.afm_distance_50ft_m > p.afm_ground_roll_m)
                         ? p.afm_distance_50ft_m
                         : p.afm_ground_roll_m * 1.70;

    // Fraction of the reference distance that is airborne
    const double air_frac  = 1.0 - p.afm_ground_roll_m / afm50;

    // Airborne wind correction is lighter (√k_wind approximation)
    const double k_wind_air = std::sqrt(r.k_wind);
    const double k_air      =
        r.k_weight * r.k_density * k_wind_air * r.k_slope;
    const double k_total50  =
        (1.0 - air_frac) * k_combined + air_frac * k_air;

    r.distance_50ft_m = afm50 * k_total50;
  }

  return r;
}
