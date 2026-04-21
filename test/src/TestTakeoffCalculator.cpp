// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

/**
 * Unit tests for TakeoffCalculator::ComputeTakeoff().
 *
 * Reference aircraft: Cessna 172R (POH / AFM data)
 *
 * Standard reference conditions used throughout (AFM baseline):
 *   - Gross mass  : 2 450 lb  = 1 111.30 kg  (MTOW)
 *   - Wing area   :   174 ft² =    16.17 m²
 *   - CLmax (10° flap) : 1.80
 *   - Ground roll :   890 ft  =   271.27 m   (sea level, ISA, MTOW, nil wind,
 *                                              level dry-paved runway)
 *   - Total to 50 ft: 1 685 ft = 513.59 m
 *
 * Computed expected values were derived by independently re-implementing the
 * same formulas in Python (double precision) and confirmed to match the C++
 * output to better than 0.1 %.
 */

#define ACCURACY 1000  // 0.1 % tolerance for physical results

#include "TestUtil.hpp"
#include "TakeoffCalculator/TakeoffCalculator.hpp"

#include <cmath>

// ---------------------------------------------------------------------------
// C172R POH reference constants
// ---------------------------------------------------------------------------

static constexpr double kC172R_MTOW_KG       = 2450.0 * 0.45359237; ///< 1111.30 kg
static constexpr double kC172R_WING_AREA_M2  = 174.0  * 0.09290304; ///< 16.165 m²
static constexpr double kC172R_CL_MAX        = 1.80;

/// AFM reference distances (sea level ISA, MTOW, nil wind, level dry paved)
static constexpr double kC172R_AFM_ROLL_M    = 890.0  * 0.3048;     ///< 271.27 m
static constexpr double kC172R_AFM_50FT_M    = 1685.0 * 0.3048;     ///< 513.59 m

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/**
 * Build a TakeoffParameters struct from individual arguments.
 * Follows the field order in TakeoffCalculator.hpp.
 */
static TakeoffParameters
MakeParams(double gross_mass_kg,
           double reference_mass_kg,
           double wing_area_m2,
           double cl_max,
           double afm_ground_roll_m,
           double afm_distance_50ft_m,
           double runway_elevation_m,
           double oat_celsius,
           double qnh_hpa,
           double headwind_mps,
           double slope_percent,
           double rolling_friction) noexcept
{
  TakeoffParameters p;
  p.gross_mass_kg       = gross_mass_kg;
  p.reference_mass_kg   = reference_mass_kg;
  p.wing_area_m2        = wing_area_m2;
  p.cl_max              = cl_max;
  p.afm_ground_roll_m   = afm_ground_roll_m;
  p.afm_distance_50ft_m = afm_distance_50ft_m;
  p.runway_elevation_m  = runway_elevation_m;
  p.oat_celsius         = oat_celsius;
  p.qnh_hpa             = qnh_hpa;
  p.headwind_mps        = headwind_mps;
  p.slope_percent       = slope_percent;
  p.rolling_friction    = rolling_friction;
  return p;
}

/** Standard sea-level ISA reference-condition parameters for the C172R. */
static TakeoffParameters
C172R_ReferenceParams() noexcept
{
  return MakeParams(kC172R_MTOW_KG, kC172R_MTOW_KG,
                    kC172R_WING_AREA_M2, kC172R_CL_MAX,
                    kC172R_AFM_ROLL_M, kC172R_AFM_50FT_M,
                    /*elevation=*/0.0,
                    /*oat_celsius=*/15.0,
                    /*qnh_hpa=*/1013.25,
                    /*headwind_mps=*/0.0,
                    /*slope_percent=*/0.0,
                    /*rolling_friction=*/0.02);
}

/// 1 knot expressed in m/s
static constexpr double kKt2Mps = 0.51444;

// ---------------------------------------------------------------------------
// Test 1: Identity – all corrections ≈ 1.0 at reference conditions
// ---------------------------------------------------------------------------

static void
TestReferenceConditions()
{
  const auto r = ComputeTakeoff(C172R_ReferenceParams());

  // At sea-level ISA the density ratio is 1.000 (within numerical precision).
  ok1(r.density_ratio > 0.999 && r.density_ratio < 1.001);

  // All individual correction factors must be (very close to) 1.0.
  ok1(equals(r.k_weight,  1.0));
  ok1(r.k_density > 0.999 && r.k_density < 1.001);
  ok1(equals(r.k_wind,    1.0));
  ok1(equals(r.k_slope,   1.0));

  // Corrected distances must equal AFM reference distances.
  ok1(equals(r.ground_roll_m,   kC172R_AFM_ROLL_M));
  ok1(equals(r.distance_50ft_m, kC172R_AFM_50FT_M));

  // Pressure altitude at sea level with standard QNH must be ≈ 0 m.
  ok1(r.pressure_altitude_m > -1.0 && r.pressure_altitude_m < 1.0);

  // Stall speed: V_S = √(2·m·g / (ρ·S·CL)).  At ISA SL for the C172R
  // MTOW this should be about 24.7 m/s (48 kt).
  ok1(r.v_stall_ms > 23.0 && r.v_stall_ms < 27.0);
  // Rotation / liftoff speed ratios must be exactly 1.10 and 1.15.
  ok1(equals(r.v_rotate_ms,  1.10 * r.v_stall_ms));
  ok1(equals(r.v_liftoff_ms, 1.15 * r.v_stall_ms));
}

// ---------------------------------------------------------------------------
// Test 2: Weight correction – 90 % MTOW reduces ground roll
// ---------------------------------------------------------------------------

static void
TestWeightCorrection()
{
  constexpr double kLightMass = 0.90 * kC172R_MTOW_KG; ///< 1 000.17 kg

  auto p = C172R_ReferenceParams();
  p.gross_mass_kg = kLightMass;

  const auto r = ComputeTakeoff(p);

  // k_weight = (W / W_ref)² = 0.9² = 0.81  (exact)
  ok1(equals(r.k_weight, 0.81));

  // Ground roll must decrease relative to reference.
  ok1(r.ground_roll_m < kC172R_AFM_ROLL_M);

  // Corrected ground roll = AFM × 0.81 → 219.73 m (720.9 ft)
  ok1(equals(r.ground_roll_m, kC172R_AFM_ROLL_M * 0.81));

  // 50-ft distance similarly reduced.
  ok1(r.distance_50ft_m < kC172R_AFM_50FT_M);
}

// ---------------------------------------------------------------------------
// Test 3: Density correction – Denver altitude (~5 280 ft / 1 609 m), ISA day
// ---------------------------------------------------------------------------

static void
TestDensityAltitude()
{
  constexpr double kElevM = 1609.0;                      ///< ~5 280 ft MSL
  // ISA temperature at that altitude: 15 - 6.5·(h/1000)
  constexpr double kOAT = 15.0 - 6.5 * kElevM / 1000.0; ///< 4.54 °C

  auto p = C172R_ReferenceParams();
  p.runway_elevation_m = kElevM;
  p.oat_celsius        = kOAT;

  const auto r = ComputeTakeoff(p);

  // Density ratio must be less than 1 at altitude.
  ok1(r.density_ratio < 1.0);

  // Pressure altitude at (elevation=1609 m, QNH=1013.25 hPa) is ~1 609 m.
  ok1(r.pressure_altitude_m > 1600.0 && r.pressure_altitude_m < 1620.0);

  // Density altitude at ISA temperature approximately equals pressure altitude.
  ok1(r.density_altitude_m > 1600.0 && r.density_altitude_m < 1620.0);

  // k_density = 1/σ > 1 → ground roll must increase.
  ok1(r.k_density > 1.0);
  ok1(r.ground_roll_m > kC172R_AFM_ROLL_M);

  // Expected ground roll ≈ 317.5 m (Python reference)
  ok1(r.ground_roll_m > 310.0 && r.ground_roll_m < 325.0);

  // Expected 50-ft distance ≈ 601.1 m (Python reference)
  ok1(r.distance_50ft_m > 590.0 && r.distance_50ft_m < 615.0);
}

// ---------------------------------------------------------------------------
// Test 4: Hot-day density – ISA + 20 °C at sea level
// ---------------------------------------------------------------------------

static void
TestHotDayDensity()
{
  auto p = C172R_ReferenceParams();
  p.oat_celsius = 35.0; // ISA + 20 °C

  const auto r = ComputeTakeoff(p);

  // Warm air is less dense → k_density > 1 → ground roll increases.
  ok1(r.density_ratio < 1.0);
  ok1(r.k_density > 1.0);
  ok1(r.ground_roll_m > kC172R_AFM_ROLL_M);

  // Pressure altitude stays 0 m (same QNH, same field elevation).
  ok1(r.pressure_altitude_m > -1.0 && r.pressure_altitude_m < 1.0);

  // Density altitude must be positive even at sea level on a hot day
  // (≈ 694 m from Python reference).
  ok1(r.density_altitude_m > 600.0 && r.density_altitude_m < 800.0);

  // Expected ground roll ≈ 290.1 m.
  ok1(r.ground_roll_m > 285.0 && r.ground_roll_m < 295.0);
}

// ---------------------------------------------------------------------------
// Test 5: Headwind reduces ground roll
// ---------------------------------------------------------------------------

static void
TestHeadwind()
{
  constexpr double kHW_MPS = 10.0 * kKt2Mps; ///< 10 kt = 5.1444 m/s

  auto p = C172R_ReferenceParams();
  p.headwind_mps = kHW_MPS;

  const auto r = ComputeTakeoff(p);

  // Wind correction < 1 → shorter ground roll.
  ok1(r.k_wind < 1.0);
  ok1(r.ground_roll_m < kC172R_AFM_ROLL_M);
  ok1(r.distance_50ft_m < kC172R_AFM_50FT_M);

  // k_wind = ((V_LOF − V_hw) / V_LOF)².  Verify against Python reference
  // value of 0.6709 (V_LOF ≈ 28.44 m/s with 5.14 m/s headwind).
  ok1(r.k_wind > 0.66 && r.k_wind < 0.68);

  // Expected ground roll ≈ 182.0 m.
  ok1(r.ground_roll_m > 178.0 && r.ground_roll_m < 186.0);
}

// ---------------------------------------------------------------------------
// Test 6: Tailwind increases ground roll
// ---------------------------------------------------------------------------

static void
TestTailwind()
{
  constexpr double kTW_MPS = -10.0 * kKt2Mps; ///< 10 kt tailwind (negative)

  auto p = C172R_ReferenceParams();
  p.headwind_mps = kTW_MPS; // negative = tailwind

  const auto r = ComputeTakeoff(p);

  // k_wind > 1 → longer ground roll.
  ok1(r.k_wind > 1.0);
  ok1(r.ground_roll_m > kC172R_AFM_ROLL_M);
  ok1(r.distance_50ft_m > kC172R_AFM_50FT_M);

  // k_wind ≈ 1.3945 (Python reference).
  ok1(r.k_wind > 1.38 && r.k_wind < 1.41);

  // Expected ground roll ≈ 378.3 m.
  ok1(r.ground_roll_m > 373.0 && r.ground_roll_m < 384.0);
}

// ---------------------------------------------------------------------------
// Test 7: Upslope runway increases ground roll
// ---------------------------------------------------------------------------

static void
TestUpslopeRunway()
{
  auto p = C172R_ReferenceParams();
  p.slope_percent = 2.0; // 2 % upslope

  const auto r = ComputeTakeoff(p);

  // k_slope = (1 + 0.08 × 2) × 1.0 = 1.16 (dry paved, exact)
  ok1(equals(r.k_slope, 1.16));

  // Ground roll must increase.
  ok1(r.ground_roll_m > kC172R_AFM_ROLL_M);

  // Expected ground roll ≈ 314.7 m.
  ok1(r.ground_roll_m > 310.0 && r.ground_roll_m < 320.0);
}

// ---------------------------------------------------------------------------
// Test 8: Downslope runway is bounded at 50 % of flat distance
// ---------------------------------------------------------------------------

static void
TestDownslopeRunway()
{
  auto p = C172R_ReferenceParams();
  p.slope_percent = -2.0; // 2 % downslope

  const auto r = ComputeTakeoff(p);

  // k_slope = (1 + 0.08 × (−2)) × 1.0 = 0.84, above 0.50 clamp.
  ok1(equals(r.k_slope, 0.84));
  ok1(r.ground_roll_m < kC172R_AFM_ROLL_M);

  // Extremely steep downslope (−20 %) clamps at 0.50.
  auto p2 = C172R_ReferenceParams();
  p2.slope_percent = -20.0;
  const auto r2 = ComputeTakeoff(p2);
  ok1(equals(r2.k_slope, 0.50));
}

// ---------------------------------------------------------------------------
// Test 9: Wet runway (rolling friction 0.04) increases ground roll
// ---------------------------------------------------------------------------

static void
TestWetRunway()
{
  auto p = C172R_ReferenceParams();
  p.rolling_friction = 0.04; // wet paved

  const auto r = ComputeTakeoff(p);

  // mu_excess = 0.04 − 0.02 = 0.02; mu_cor = 1 + 0.5 × (0.02/0.02) = 1.5
  // k_slope = 1.0 × 1.5 = 1.5 (flat runway, wet paved)
  ok1(equals(r.k_slope, 1.50));
  ok1(r.ground_roll_m > kC172R_AFM_ROLL_M);

  // Expected ground roll ≈ 406.9 m.
  ok1(r.ground_roll_m > 402.0 && r.ground_roll_m < 412.0);
}

// ---------------------------------------------------------------------------
// Test 10: 50-ft distance auto-estimated when afm_distance_50ft_m = 0
// ---------------------------------------------------------------------------

static void
TestAutoEstimate50ft()
{
  auto p = C172R_ReferenceParams();
  p.afm_distance_50ft_m = 0.0; // request auto-estimate

  const auto r = ComputeTakeoff(p);

  // Auto-estimate = 1.70 × ground roll.  At reference conditions
  // the corrected distance must equal 1.70 × AFM ground roll.
  const double expected50 = 1.70 * kC172R_AFM_ROLL_M;
  ok1(equals(r.distance_50ft_m, expected50));

  // Ground roll itself is unchanged.
  ok1(equals(r.ground_roll_m, kC172R_AFM_ROLL_M));
}

// ---------------------------------------------------------------------------
// Test 11: Combined – high altitude (Denver) + 10-kt headwind
// ---------------------------------------------------------------------------

static void
TestHighAltitudeHeadwind()
{
  constexpr double kElevM = 1609.0;
  constexpr double kOAT   = 15.0 - 6.5 * kElevM / 1000.0;
  constexpr double kHW    = 10.0 * kKt2Mps;

  auto p = C172R_ReferenceParams();
  p.runway_elevation_m = kElevM;
  p.oat_celsius        = kOAT;
  p.headwind_mps       = kHW;

  const auto r = ComputeTakeoff(p);

  // Density correction increases distance; headwind decreases it.
  // Net combined effect: Python reference gives ground roll ≈ 220.2 m,
  // which is slightly LESS than the sea-level AFM value of 271.3 m because
  // the 10-kt headwind correction outweighs the density penalty.
  ok1(r.k_density > 1.0);
  ok1(r.k_wind    < 1.0);

  ok1(r.ground_roll_m > 215.0 && r.ground_roll_m < 226.0);
  ok1(r.distance_50ft_m > 445.0 && r.distance_50ft_m < 470.0);
}

// ---------------------------------------------------------------------------
// Test 12: Extreme headwind clamp – V_eff ≥ 0.5 × V_LOF
// ---------------------------------------------------------------------------

static void
TestExtremeHeadwindClamp()
{
  // Very strong headwind: larger than V_LOF.  k_wind should clamp at 0.25.
  auto p = C172R_ReferenceParams();
  p.headwind_mps = 100.0; // far exceeds V_LOF ≈ 28 m/s

  const auto r = ComputeTakeoff(p);

  // Clamp: v_eff = max(v_lof − 100, 0.5 × v_lof) = 0.5 × v_lof
  // k_wind = (0.5)² = 0.25
  ok1(equals(r.k_wind, 0.25));
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int
main()
{
  plan_tests(54);

  TestReferenceConditions();    // 11 checks
  TestWeightCorrection();       //  4 checks
  TestDensityAltitude();        //  7 checks
  TestHotDayDensity();          //  6 checks
  TestHeadwind();               //  5 checks
  TestTailwind();               //  5 checks
  TestUpslopeRunway();          //  3 checks
  TestDownslopeRunway();        //  3 checks
  TestWetRunway();              //  3 checks
  TestAutoEstimate50ft();       //  2 checks
  TestHighAltitudeHeadwind();   //  4 checks
  TestExtremeHeadwindClamp();   //  1 check

  return exit_status();
}
