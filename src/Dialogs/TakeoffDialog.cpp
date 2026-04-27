// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

/**
 * Takeoff Distance Calculator dialog
 *
 * Inputs (automatically converted to SI before calculation):
 *   Gross mass, reference mass, AFM ground roll, AFM 50 ft distance,
 *   wing area, CL_max, runway elevation, OAT, QNH, headwind,
 *   slope, surface type.
 *
 * Outputs (updated in real time on every field change):
 *   Density altitude, density ratio σ, V_stall, V_rotate,
 *   corrected ground roll, corrected distance to 50 ft obstacle.
 *
 * Physics reference: see TakeoffCalculator.hpp
 */

#include "TakeoffDialog.hpp"
#include "TakeoffCalculator/TakeoffCalculator.hpp"
#include "Dialogs/WidgetDialog.hpp"
#include "Widget/RowFormWidget.hpp"
#include "Form/DataField/Float.hpp"
#include "Form/DataField/Enum.hpp"
#include "Form/DataField/Listener.hpp"
#include "Units/Units.hpp"
#include "Units/Group.hpp"
#include "Formatter/UserUnits.hpp"
#include "Atmosphere/Temperature.hpp"
#include "Interface.hpp"
#include "Computer/Settings.hpp"
#include "GlideSolvers/GlidePolar.hpp"
#include "Language/Language.hpp"
#include "UIGlobals.hpp"

#include <stdio.h>

// ---------------------------------------------------------------------------
// Surface / rolling-friction enum
// ---------------------------------------------------------------------------

static constexpr double kFrictionCoefficients[] = {
  0.020,   // dry paved
  0.040,   // wet paved
  0.050,   // short dry grass
  0.080,   // long dry grass
  0.100,   // soft / wet ground
};

static constexpr StaticEnumChoice kSurfaceChoices[] = {
  { 0, N_("Dry paved"),       nullptr },
  { 1, N_("Wet paved"),       nullptr },
  { 2, N_("Short dry grass"), nullptr },
  { 3, N_("Long dry grass"),  nullptr },
  { 4, N_("Soft/wet ground"), nullptr },
  nullptr
};

// ---------------------------------------------------------------------------
// Widget
// ---------------------------------------------------------------------------

class TakeoffCalculatorPanel final
  : public RowFormWidget, DataFieldListener
{
  enum ControlIndex : unsigned {
    /* --- inputs --- */
    GrossMass = 0,
    ReferenceMass,
    AfmGroundRoll,
    Afm50ftDist,
    WingArea,
    ClMax,
    RunwayElev,
    OutsideTemp,
    Qnh,
    Headwind,
    Slope,
    Surface,

    /* --- outputs (read-only) --- */
    DensityAltResult,
    DensityRatioResult,
    VStallResult,
    VRotateResult,
    GroundRollResult,
    Distance50ftResult,
  };

public:
  explicit TakeoffCalculatorPanel() noexcept
    :RowFormWidget(UIGlobals::GetDialogLook()) {}

  /* RowFormWidget interface */
  void Prepare(ContainerWindow &parent, const PixelRect &rc) noexcept override;

  bool Save([[maybe_unused]] bool &changed) noexcept override
  {
    changed = false;
    return true;
  }

private:
  /* Recompute and refresh the result rows. */
  void UpdateResults() noexcept;

  /* DataFieldListener */
  void OnModified([[maybe_unused]] DataField &df) noexcept override
  {
    UpdateResults();
  }
};

// ---------------------------------------------------------------------------

void
TakeoffCalculatorPanel::Prepare(ContainerWindow &parent,
                                const PixelRect &rc) noexcept
{
  RowFormWidget::Prepare(parent, rc);

  const ComputerSettings &settings = CommonInterface::GetComputerSettings();
  const PolarSettings    &polar    = settings.polar;
  const Plane            &plane    = settings.plane;

  // Pre-fill sensible defaults from the current flight setup.
  const double ref_mass_kg  = polar.glide_polar_task.GetReferenceMass();
  const double gross_mass_kg =
      polar.glide_polar_task.GetTotalMass();   // crew + ballast + empty
  const double wing_area_m2 = plane.wing_area;
  const double oat_user     = settings.forecast_temperature.ToUser();
  const double qnh_user     =
      Units::ToUserPressure(settings.pressure.GetHectoPascal());

  // -------------------------------------------------------------------------
  // Input rows
  // -------------------------------------------------------------------------

  /* 0 GrossMass */
  AddFloat(_("Gross mass"),
           _("Current gross mass: empty + crew + ballast + fuel."),
           "%.0f %s", "%.0f",
           Units::ToUserMass(100),
           Units::ToUserMass(3000),
           Units::ToUserMass(5),
           false,
           UnitGroup::MASS,
           Units::ToUserMass(gross_mass_kg > 10 ? gross_mass_kg : 500),
           this);

  /* 1 ReferenceMass */
  AddFloat(_("Reference mass"),
           _("AFM/POH reference mass (typically MTOW)."),
           "%.0f %s", "%.0f",
           Units::ToUserMass(100),
           Units::ToUserMass(3000),
           Units::ToUserMass(5),
           false,
           UnitGroup::MASS,
           Units::ToUserMass(ref_mass_kg > 10 ? ref_mass_kg : 600),
           this);

  /* 2 AfmGroundRoll */
  AddFloat(_("AFM ground roll"),
           _("AFM/POH ground roll at reference mass, sea-level ISA, "
             "zero wind, level runway (m or ft)."),
           "%.0f %s", "%.0f",
           0,
           Units::ToUserAltitude(3000),
           Units::ToUserAltitude(10),
           false,
           UnitGroup::ALTITUDE,
           Units::ToUserAltitude(300),
           this);

  /* 3 Afm50ftDist */
  AddFloat(_("AFM dist. to 50 ft"),
           _("AFM/POH total distance to clear a 15 m obstacle at "
             "reference conditions.  Enter 0 to estimate automatically "
             "as 1.7 × ground roll."),
           "%.0f %s", "%.0f",
           0,
           Units::ToUserAltitude(6000),
           Units::ToUserAltitude(10),
           false,
           UnitGroup::ALTITUDE,
           0,
           this);

  /* 4 WingArea */
  AddFloat(_("Wing area"),
           _("Wing reference area (m²)."),
           "%.1f m²", "%.1f",
           1, 100, 0.5, false,
           wing_area_m2 > 0.5 ? wing_area_m2 : 10.0,
           this);

  /* 5 ClMax */
  AddFloat(_("CL max"),
           _("Maximum lift coefficient at the takeoff flap setting. "
             "Typical: no flaps 1.4–1.6, partial 1.6–2.0, full 2.0–2.4."),
           "%.2f", "%.2f",
           0.80, 3.00, 0.05, false,
           1.50,
           this);

  /* 6 RunwayElev */
  AddFloat(_("Runway elevation"),
           _("Runway threshold elevation above MSL."),
           "%.0f %s", "%.0f",
           Units::ToUserAltitude(-500),
           Units::ToUserAltitude(5000),
           Units::ToUserAltitude(10),
           false,
           UnitGroup::ALTITUDE,
           0,
           this);

  /* 7 OutsideTemp */
  AddFloat(_("OAT"),
           _("Outside air temperature at runway level."),
           "%.1f %s", "%.1f",
           Temperature::FromCelsius(-40).ToUser(),
           Temperature::FromCelsius(55).ToUser(),
           1, false,
           oat_user,
           this);
  {
    /* Attach unit label. */
    WndProperty &wp = GetControl(OutsideTemp);
    auto &df = static_cast<DataFieldFloat &>(*wp.GetDataField());
    df.SetUnits(Units::GetTemperatureName());
    wp.RefreshDisplay();
  }

  /* 8 QNH */
  AddFloat(_("QNH"),
           _("QNH altimeter setting."),
           GetUserPressureFormat(true),
           GetUserPressureFormat(),
           Units::ToUserPressure(Units::ToSysUnit(850,  Unit::HECTOPASCAL)),
           Units::ToUserPressure(Units::ToSysUnit(1080, Unit::HECTOPASCAL)),
           GetUserPressureStep(),
           false,
           qnh_user,
           this);
  {
    WndProperty &wp = GetControl(Qnh);
    auto &df = static_cast<DataFieldFloat &>(*wp.GetDataField());
    df.SetUnits(Units::GetPressureName());
    wp.RefreshDisplay();
  }

  /* 9 Headwind */
  AddFloat(_("Headwind"),
           _("Headwind component along the takeoff run. "
             "Negative value for tailwind."),
           "%.1f %s", "%.1f",
           Units::ToUserWindSpeed(-30),
           Units::ToUserWindSpeed(30),
           Units::ToUserWindSpeed(1),
           false,
           UnitGroup::WIND_SPEED,
           0,
           this);

  /* 10 Slope */
  AddFloat(_("Slope"),
           _("Runway gradient in the takeoff direction (%). "
             "Positive = uphill, negative = downhill."),
           "%.1f %%", "%.1f",
           -5, 5, 0.1, false,
           0, this);

  /* 11 Surface */
  AddEnum(_("Surface"),
          _("Runway surface type (sets the rolling friction coefficient)."),
          kSurfaceChoices,
          0, this);

  // -------------------------------------------------------------------------
  // Output rows (read-only)
  // -------------------------------------------------------------------------

  /* 12 DensityAltResult */
  AddReadOnly(_("Density altitude"),
              _("Altitude in the ISA atmosphere with the same air density "
                "as the current conditions."),
              "%.0f %s",
              UnitGroup::ALTITUDE, 0);

  /* 13 DensityRatioResult */
  AddReadOnly(_("Density ratio \xCF\x83"),
              _("Actual air density divided by ISA sea-level density (1.225 kg/m³). "
                "Values < 1 indicate performance-degrading high-density-altitude "
                "conditions."),
              "%.4f",
              1.0);

  /* 14 VStallResult */
  AddReadOnly(_("V stall"),
              _("1-g stall speed at current gross mass and field density."),
              "%.1f %s",
              UnitGroup::HORIZONTAL_SPEED, 0);

  /* 15 VRotateResult */
  AddReadOnly(_("V rotate"),
              _("Rotation speed = 1.10 × V stall."),
              "%.1f %s",
              UnitGroup::HORIZONTAL_SPEED, 0);

  /* 16 GroundRollResult */
  AddReadOnly(_("Ground roll (corrected)"),
              _("Corrected ground roll accounting for weight, density altitude, "
                "headwind, slope, and surface friction."),
              "%.0f %s",
              UnitGroup::ALTITUDE, 0);

  /* 17 Distance50ftResult */
  AddReadOnly(_("Dist. to 50 ft (corrected)"),
              _("Corrected total takeoff distance to clear a 15 m (50 ft) obstacle."),
              "%.0f %s",
              UnitGroup::ALTITUDE, 0);

  // Compute initial display
  UpdateResults();
}

// ---------------------------------------------------------------------------

void
TakeoffCalculatorPanel::UpdateResults() noexcept
{
  // -----------------------------------------------------------------------
  // Read inputs (all values come back in user units; convert to SI)
  // -----------------------------------------------------------------------
  TakeoffParameters p;

  p.gross_mass_kg =
      Units::ToSysMass(static_cast<const DataFieldFloat &>(
          GetDataField(GrossMass)).GetValue());

  p.reference_mass_kg =
      Units::ToSysMass(static_cast<const DataFieldFloat &>(
          GetDataField(ReferenceMass)).GetValue());

  p.afm_ground_roll_m =
      Units::ToSysAltitude(static_cast<const DataFieldFloat &>(
          GetDataField(AfmGroundRoll)).GetValue());

  p.afm_distance_50ft_m =
      Units::ToSysAltitude(static_cast<const DataFieldFloat &>(
          GetDataField(Afm50ftDist)).GetValue());

  p.wing_area_m2 =
      static_cast<const DataFieldFloat &>(GetDataField(WingArea)).GetValue();

  p.cl_max =
      static_cast<const DataFieldFloat &>(GetDataField(ClMax)).GetValue();

  p.runway_elevation_m =
      Units::ToSysAltitude(static_cast<const DataFieldFloat &>(
          GetDataField(RunwayElev)).GetValue());

  p.oat_celsius =
      Temperature::FromUser(static_cast<const DataFieldFloat &>(
          GetDataField(OutsideTemp)).GetValue()).ToCelsius();

  p.qnh_hpa =
      Units::FromUserPressure(static_cast<const DataFieldFloat &>(
          GetDataField(Qnh)).GetValue()).GetHectoPascal();

  p.headwind_mps =
      Units::ToSysWindSpeed(static_cast<const DataFieldFloat &>(
          GetDataField(Headwind)).GetValue());

  p.slope_percent =
      static_cast<const DataFieldFloat &>(GetDataField(Slope)).GetValue();

  {
    const unsigned surface_idx =
        static_cast<const DataFieldEnum &>(GetDataField(Surface)).GetValue();
    constexpr unsigned kNumSurfaces =
        sizeof(kFrictionCoefficients) / sizeof(kFrictionCoefficients[0]);
    p.rolling_friction =
        kFrictionCoefficients[surface_idx < kNumSurfaces ? surface_idx : 0];
  }

  // Guard: need physically meaningful inputs before computing
  if (p.gross_mass_kg < 1.0   ||
      p.reference_mass_kg < 1.0 ||
      p.wing_area_m2 < 0.1   ||
      p.cl_max < 0.1         ||
      p.afm_ground_roll_m < 1.0)
    return;

  // -----------------------------------------------------------------------
  // Calculate
  // -----------------------------------------------------------------------
  const TakeoffResult r = ComputeTakeoff(p);

  // -----------------------------------------------------------------------
  // Update result display
  // -----------------------------------------------------------------------
  LoadValue(DensityAltResult,  r.density_altitude_m, UnitGroup::ALTITUDE);
  LoadValue(DensityRatioResult, r.density_ratio);
  LoadValue(VStallResult,      r.v_stall_ms,         UnitGroup::HORIZONTAL_SPEED);
  LoadValue(VRotateResult,     r.v_rotate_ms,        UnitGroup::HORIZONTAL_SPEED);
  LoadValue(GroundRollResult,  r.ground_roll_m,      UnitGroup::ALTITUDE);
  LoadValue(Distance50ftResult, r.distance_50ft_m,   UnitGroup::ALTITUDE);
}

// ---------------------------------------------------------------------------
// Public entry point
// ---------------------------------------------------------------------------

void
ShowTakeoffCalculator() noexcept
{
  auto *widget = new TakeoffCalculatorPanel();

  WidgetDialog dialog(WidgetDialog::Auto{},
                      UIGlobals::GetMainWindow(),
                      UIGlobals::GetDialogLook(),
                      _("Takeoff Calculator"),
                      widget);

  dialog.AddButton(_("Close"), mrOK);
  dialog.ShowModal();
}
