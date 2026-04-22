// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "TowTakeoffCalculator.hpp"
#include "TakeoffCalculator.hpp"

#include <algorithm>
#include <cmath>

// ---------------------------------------------------------------------------
// ISA constants (same as TakeoffCalculator.cpp)
// ---------------------------------------------------------------------------

static constexpr double kISA_P0   = 101325.0;
static constexpr double kISA_T0   = 288.15;
static constexpr double kISA_RHO0 = 1.225;
static constexpr double kISA_L    = 0.0065;
static constexpr double kGravity  = 9.80665;
static constexpr double kR_air    = 287.058;
static constexpr double kISA_exp  = kGravity / (kR_air * kISA_L);

// ---------------------------------------------------------------------------

/**
 * Compute air density σ and associated results at the runway.
 * Fills density_ratio and density_altitude_m in the result struct.
 */
static void
ComputeAtmosphere(double runway_elevation_m, double oat_celsius,
                  double qnh_hpa,
                  double &density_ratio_out,
                  double &density_altitude_m_out,
                  double &air_density_out) noexcept
{
  const double qnh_pa  = qnh_hpa * 100.0;
  const double t_ratio = 1.0 - kISA_L * runway_elevation_m / kISA_T0;
  const double safe_tr = t_ratio > 1e-4 ? t_ratio : 1e-4;
  const double p_field = qnh_pa * std::pow(safe_tr, kISA_exp);

  const double t_actual_k = oat_celsius + 273.15;
  air_density_out   = p_field / (kR_air * t_actual_k);
  density_ratio_out = air_density_out / kISA_RHO0;

  const double sigma = std::max(density_ratio_out, 1e-3);
  density_altitude_m_out =
      (kISA_T0 / kISA_L) *
      (1.0 - std::pow(sigma, 1.0 / (kISA_exp - 1.0)));
}

/**
 * Compute liftoff speed for one aircraft given actual air density.
 *   V_LOF = 1.15 × V_S = 1.15 × √(2mg / (ρ S CL))
 */
static double
ComputeVLof(double mass_kg, double wing_area_m2, double cl_max,
            double air_density) noexcept
{
  const double S   = std::max(wing_area_m2, 0.1);
  const double CL  = std::max(cl_max,       0.1);
  const double rho = std::max(air_density,  1e-3);
  const double v_s = std::sqrt(2.0 * mass_kg * kGravity / (rho * S * CL));
  return 1.15 * v_s;
}

/**
 * Compute stall speed (1 g) for one aircraft.
 */
static double
ComputeVStall(double mass_kg, double wing_area_m2, double cl_max,
              double air_density) noexcept
{
  const double S   = std::max(wing_area_m2, 0.1);
  const double CL  = std::max(cl_max,       0.1);
  const double rho = std::max(air_density,  1e-3);
  return std::sqrt(2.0 * mass_kg * kGravity / (rho * S * CL));
}

/**
 * Compute the combined-correction-factor ground roll for a single
 * aircraft using the reference-based method from TakeoffCalculator.
 */
static double
CorrectedGroundRoll(double gross_mass_kg, double reference_mass_kg,
                    double afm_ground_roll_m,
                    double v_liftoff_ms, double headwind_mps,
                    double density_ratio,
                    double slope_percent, double rolling_friction) noexcept
{
  // Weight factor
  const double w_ratio  = gross_mass_kg / std::max(reference_mass_kg, 1.0);
  const double k_weight = w_ratio * w_ratio;

  // Density factor (naturally aspirated)
  const double k_density = 1.0 / std::max(density_ratio, 0.01);

  // Wind factor
  const double v_lof = std::max(v_liftoff_ms, 1.0);
  double v_eff = v_lof - headwind_mps;
  v_eff = std::max(v_eff, 0.5 * v_lof);
  const double k_wind = (v_eff / v_lof) * (v_eff / v_lof);

  // Slope + friction factor
  static constexpr double kMuRef       = 0.02;
  static constexpr double kSlopeFactor = 0.08;
  const double mu_excess = std::max(rolling_friction - kMuRef, 0.0);
  const double mu_cor    = 1.0 + 0.50 * (mu_excess / kMuRef);
  double k_slope = (1.0 + kSlopeFactor * slope_percent) * mu_cor;
  k_slope = std::max(k_slope, 0.5);

  return afm_ground_roll_m * k_weight * k_density * k_wind * k_slope;
}

// ---------------------------------------------------------------------------

TowTakeoffResult
ComputeTowTakeoff(const TowTakeoffParameters &p) noexcept
{
  TowTakeoffResult r{};

  // -------------------------------------------------------------------------
  // 1.  Atmosphere
  // -------------------------------------------------------------------------
  double air_density;
  ComputeAtmosphere(p.runway_elevation_m, p.oat_celsius, p.qnh_hpa,
                    r.density_ratio, r.density_altitude_m, air_density);

  // -------------------------------------------------------------------------
  // 2.  Individual aircraft speeds
  // -------------------------------------------------------------------------
  r.glider.v_stall_ms   = ComputeVStall(p.glider_gross_mass_kg,
                                        p.glider_wing_area_m2,
                                        p.glider_cl_max, air_density);
  r.glider.v_liftoff_ms = 1.15 * r.glider.v_stall_ms;

  r.tug.v_stall_ms   = ComputeVStall(p.tug_gross_mass_kg,
                                     p.tug_wing_area_m2,
                                     p.tug_cl_max, air_density);
  r.tug.v_liftoff_ms = 1.15 * r.tug.v_stall_ms;

  // -------------------------------------------------------------------------
  // 3.  Solo corrected ground rolls (for display)
  //
  //     These are what each aircraft would need in isolation; used to
  //     show the pilot the individual numbers before the combined calc.
  // -------------------------------------------------------------------------
  r.glider.ground_roll_m = CorrectedGroundRoll(
      p.glider_gross_mass_kg, p.glider_reference_mass_kg,
      p.glider_afm_ground_roll_m,
      r.glider.v_liftoff_ms, p.headwind_mps,
      r.density_ratio, p.slope_percent, p.rolling_friction);

  r.tug.ground_roll_m = CorrectedGroundRoll(
      p.tug_gross_mass_kg, p.tug_reference_mass_kg,
      p.tug_afm_ground_roll_m,
      r.tug.v_liftoff_ms, p.headwind_mps,
      r.density_ratio, p.slope_percent, p.rolling_friction);

  // -------------------------------------------------------------------------
  // 4.  Liftoff sequence
  // -------------------------------------------------------------------------
  r.tug_liftoff_before_glider = (r.tug.v_liftoff_ms < r.glider.v_liftoff_ms);

  // -------------------------------------------------------------------------
  // 5.  Combined aerotow ground roll estimate
  //
  //     Phase 1: both rolling, tug pulls combined system.
  //       The glider adds to the rolling resistance; approximate by
  //       computing the tug ground roll at an effective mass equal to
  //       the tug mass plus the glider's wheel-drag contribution.
  //
  //       Effective extra mass ≈ glider_mass × µ_glider / T_tug × D_tug
  //       Simplified practical approximation (Ostiv 1993):
  //           effective_tug_mass = tug_mass × (1 + glider_mass/tug_mass × µ)
  //       where µ ≈ rolling_friction is a dimensionless penalty.
  //
  //     Phase 2a (normal — glider up first):
  //       Tug still rolling.  Residual rope drag factor ε applied
  //       as a multiplicative penalty on the tug's remaining roll.
  //       The tug's remaining ground-roll fraction after the glider
  //       lifts off is:
  //           fraction_remaining = 1 – (V_LOF_glider / V_LOF_tug)²
  //       (since S_G ∝ V_LOF² in the constant-force approximation).
  //
  //     Phase 2b (abnormal — tug up first): tug ground roll suffices;
  //       the glider is dragged with a downward rope component and
  //       continues rolling; combined roll = max(tug solo, glider solo).
  // -------------------------------------------------------------------------

  // Effective tug ground roll during Phase 1 accounting for glider drag:
  //   The glider's rolling resistance force = glider_mass × g × µ
  //   This reduces the net tug acceleration, extending the roll.
  //   We approximate by scaling the tug reference mass upward by the
  //   glider drag fraction and re-running the correction formula.
  const double glider_drag_fraction =
      (p.glider_gross_mass_kg * p.rolling_friction) /
       std::max(p.tug_gross_mass_kg, 1.0);

  const double effective_tug_mass =
      p.tug_gross_mass_kg * (1.0 + glider_drag_fraction);

  // Phase 1 combined roll to the first liftoff event
  const double combined_phase1 = CorrectedGroundRoll(
      effective_tug_mass, p.tug_reference_mass_kg,
      p.tug_afm_ground_roll_m,
      r.tug.v_liftoff_ms,   // tug drives the system; use tug V_LOF
      p.headwind_mps,
      r.density_ratio, p.slope_percent, p.rolling_friction);

  if (!r.tug_liftoff_before_glider) {
    // Normal case (Phase 2a): glider lifts off first.
    // The tug still has a fraction of its roll to complete after
    // the glider goes airborne.  The rope drag at this point is small
    // (glider is generating lift to support itself, residual load only).
    //
    // fraction_remaining ≈ 1 – (V_LOF_glider / V_LOF_tug)²
    const double ratio = std::min(r.glider.v_liftoff_ms /
                                  std::max(r.tug.v_liftoff_ms, 1.0), 1.0);
    const double frac_remaining = 1.0 - ratio * ratio;

    // Additional distance the tug rolls after glider liftoff with
    // rope drag penalty:
    const double phase2a_extra = r.tug.ground_roll_m *
        frac_remaining * (1.0 + p.rope_drag_factor);

    r.combined_ground_roll_m = combined_phase1 + phase2a_extra;
  } else {
    // Abnormal case (Phase 2b): tug lifts off first.
    // Take the larger of the two solo rolls as a conservative estimate.
    // The physics here are complex and hazardous; return a warning to
    // the pilot via the tug_liftoff_before_glider flag.
    r.combined_ground_roll_m =
        std::max(r.tug.ground_roll_m, r.glider.ground_roll_m);
  }

  return r;
}
