// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

/**
 * Takeoff Distance Calculator dialog
 *
 * Inputs (automatically converted to SI before calculation):
 *   Search radius, runway selector, gross mass, reference mass,
 *   AFM ground roll, AFM 50 ft distance, wing area, CL_max,
 *   runway elevation, runway length, OAT, QNH, headwind, slope,
 *   surface type.
 *
 * Outputs (updated in real time on every field change):
 *   Density altitude, density ratio σ, V_stall, V_rotate,
 *   corrected ground roll, corrected distance to 50 ft obstacle,
 *   runway length warning (if ground roll exceeds runway length).
 *
 * The pilot selects a runway from a list of landable waypoints within
 * the configured search radius.  Selecting an entry auto-populates the
 * runway elevation and runway length fields, which remain individually
 * editable afterwards.
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
#include "Components.hpp"
#include "DataComponents.hpp"
#include "Engine/Waypoint/Waypoints.hpp"
#include "Engine/Waypoint/Waypoint.hpp"
#include "Engine/Waypoint/Ptr.hpp"

#include <algorithm>
#include <vector>
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
    /* --- runway selection --- */
    SearchRadius = 0,
    RunwaySelect,

    /* --- inputs --- */
    GrossMass,
    ReferenceMass,
    AfmGroundRoll,
    Afm50ftDist,
    WingArea,
    ClMax,
    RunwayElev,
    RunwayLength,
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
    RunwayWarningResult,
  };

  /**
   * Landable waypoints within the current search radius, sorted
   * ascending by distance from the reference location.  Entry N in
   * this vector corresponds to enum id N+1 in RunwaySelect
   * (id 0 is the "none" placeholder).
   */
  std::vector<WaypointPtr> nearby_waypoints_;

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
  /**
   * Rebuild the RunwaySelect dropdown from all landable waypoints
   * within the current search radius.  Auto-selects the closest
   * entry and populates elevation / length.
   */
  void UpdateRunwayList() noexcept;

  /**
   * Read the currently selected waypoint from RunwaySelect and
   * populate RunwayElev and RunwayLength from its data.
   */
  void ApplySelectedWaypoint() noexcept;

  /* Recompute and refresh the result rows. */
  void UpdateResults() noexcept;

  /* DataFieldListener */
  void OnModified(DataField &df) noexcept override;
};

// ---------------------------------------------------------------------------

void
TakeoffCalculatorPanel::OnModified(DataField &df) noexcept
{
  if (&df == GetDataField(SearchRadius)) {
    UpdateRunwayList();
  } else if (&df == GetDataField(RunwaySelect)) {
    ApplySelectedWaypoint();
  }
  UpdateResults();
}

// ---------------------------------------------------------------------------

void
TakeoffCalculatorPanel::UpdateRunwayList() noexcept
{
  nearby_waypoints_.clear();

  // Read search radius (user distance units → SI metres)
  const double radius_m =
      Units::ToSysDistance(static_cast<const DataFieldFloat &>(
          GetDataField(SearchRadius)).GetValue());

  // Get reference position (GPS fix preferred, else skip)
  const auto &basic = CommonInterface::Basic();
  const bool have_gps = static_cast<bool>(basic.location_available);

  auto &df = static_cast<DataFieldEnum &>(
      *GetControl(RunwaySelect).GetDataField());
  df.ClearChoices();

  if (!have_gps ||
      data_components == nullptr ||
      data_components->waypoints == nullptr ||
      radius_m < 1.0) {
    df.addEnumText(_("– (no GPS fix or zero radius) –"));
    GetControl(RunwaySelect).RefreshDisplay();
    return;
  }

  const GeoPoint ref = basic.location;

  // Collect all landable waypoints within the radius
  data_components->waypoints->VisitWithinRange(
      ref, radius_m,
      [this](const WaypointPtr &wp) {
        if (wp->IsLandable())
          nearby_waypoints_.push_back(wp);
      });

  // Sort by distance ascending
  std::sort(nearby_waypoints_.begin(), nearby_waypoints_.end(),
            [&ref](const WaypointPtr &a, const WaypointPtr &b) {
              return a->location.Distance(ref) < b->location.Distance(ref);
            });

  // Limit to 50 entries to keep the list manageable
  if (nearby_waypoints_.size() > 50)
    nearby_waypoints_.resize(50);

  if (nearby_waypoints_.empty()) {
    df.addEnumText(_("– (none within range) –"));
    GetControl(RunwaySelect).RefreshDisplay();
    return;
  }

  // id=0 → no selection
  df.addEnumText(_("– (none) –"));

  // Build a label for each waypoint: "Name (Xkm, Ym elev, Zm rwy)"
  char label[128];
  for (const auto &wp : nearby_waypoints_) {
    const double dist_m = wp->location.Distance(ref);
    const double dist_u = Units::ToUserDistance(dist_m);
    const char *dist_name = Units::GetDistanceName();

    if (wp->has_elevation && wp->runway.IsLengthDefined()) {
      snprintf(label, sizeof(label),
               "%s (%.1f%s, %.0f%s, rwy %.0f%s)",
               wp->name.c_str(),
               dist_u, dist_name,
               Units::ToUserAltitude(wp->elevation), Units::GetAltitudeName(),
               Units::ToUserAltitude(wp->runway.GetLength()), Units::GetAltitudeName());
    } else if (wp->has_elevation) {
      snprintf(label, sizeof(label),
               "%s (%.1f%s, %.0f%s)",
               wp->name.c_str(),
               dist_u, dist_name,
               Units::ToUserAltitude(wp->elevation), Units::GetAltitudeName());
    } else if (wp->runway.IsLengthDefined()) {
      snprintf(label, sizeof(label),
               "%s (%.1f%s, rwy %.0f%s)",
               wp->name.c_str(),
               dist_u, dist_name,
               Units::ToUserAltitude(wp->runway.GetLength()), Units::GetAltitudeName());
    } else {
      snprintf(label, sizeof(label),
               "%s (%.1f%s)",
               wp->name.c_str(),
               dist_u, dist_name);
    }
    df.addEnumText(label);  // assigned id = current entries.size() before insert
  }

  // Auto-select the nearest (first) entry
  df.SetValue(1u);
  GetControl(RunwaySelect).RefreshDisplay();

  // Populate elevation / length from the auto-selected waypoint
  ApplySelectedWaypoint();
}

// ---------------------------------------------------------------------------

void
TakeoffCalculatorPanel::ApplySelectedWaypoint() noexcept
{
  const unsigned sel = static_cast<const DataFieldEnum &>(
      GetDataField(RunwaySelect)).GetValue();

  // id 0 → no selection; id N → nearby_waypoints_[N-1]
  if (sel == 0 || sel > nearby_waypoints_.size())
    return;

  const auto &wp = nearby_waypoints_[sel - 1];

  if (wp->has_elevation)
    LoadValue(RunwayElev, wp->elevation, UnitGroup::ALTITUDE);

  if (wp->runway.IsLengthDefined())
    LoadValue(RunwayLength, static_cast<double>(wp->runway.GetLength()),
              UnitGroup::ALTITUDE);
}

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
  // Runway selection rows
  // -------------------------------------------------------------------------

  /* 0 SearchRadius */
  AddFloat(_("Search radius"),
           _("Radius used to search for nearby runways in the waypoints "
             "database. Changing this value rebuilds the runway list below."),
           "%.0f %s", "%.0f",
           Units::ToUserDistance(1000),      // min 1 km
           Units::ToUserDistance(500000),    // max 500 km
           Units::ToUserDistance(1000),      // step 1 km
           false,
           UnitGroup::DISTANCE,
           Units::ToUserDistance(50000),     // default 50 km
           this);

  /* 1 RunwaySelect */
  {
    auto *df = new DataFieldEnum(this);
    df->addEnumText(_("– (building list…) –"));
    Add(_("Runway"), nullptr)->SetDataField(df);
  }

  // -------------------------------------------------------------------------
  // Input rows
  // -------------------------------------------------------------------------

  /* 2 GrossMass */
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

  /* 3 ReferenceMass */
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

  /* 4 AfmGroundRoll */
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

  /* 5 Afm50ftDist */
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

  /* 6 WingArea */
  AddFloat(_("Wing area"),
           _("Wing reference area (m²)."),
           "%.1f m²", "%.1f",
           1, 100, 0.5, false,
           wing_area_m2 > 0.5 ? wing_area_m2 : 10.0,
           this);

  /* 7 ClMax */
  AddFloat(_("CL max"),
           _("Maximum lift coefficient at the takeoff flap setting. "
             "Typical: no flaps 1.4–1.6, partial 1.6–2.0, full 2.0–2.4."),
           "%.2f", "%.2f",
           0.80, 3.00, 0.05, false,
           1.50,
           this);

  /* 8 RunwayElev */
  AddFloat(_("Runway elevation"),
           _("Runway threshold elevation above MSL. "
             "Populated automatically when a runway is selected above; "
             "can be edited manually."),
           "%.0f %s", "%.0f",
           Units::ToUserAltitude(-500),
           Units::ToUserAltitude(5000),
           Units::ToUserAltitude(10),
           false,
           UnitGroup::ALTITUDE,
           0,
           this);

  /* 9 RunwayLength */
  AddFloat(_("Runway length"),
           _("Available runway length. "
             "Populated automatically when a runway is selected above; "
             "can be edited manually. "
             "Set to 0 to disable the runway exceedance warning."),
           "%.0f %s", "%.0f",
           0,
           Units::ToUserAltitude(6000),
           Units::ToUserAltitude(10),
           false,
           UnitGroup::ALTITUDE,
           0,
           this);

  /* 10 OutsideTemp */
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

  /* 11 QNH */
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

  /* 12 Headwind */
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

  /* 13 Slope */
  AddFloat(_("Slope"),
           _("Runway gradient in the takeoff direction (%). "
             "Positive = uphill, negative = downhill."),
           "%.1f %%", "%.1f",
           -5, 5, 0.1, false,
           0, this);

  /* 14 Surface */
  AddEnum(_("Surface"),
          _("Runway surface type (sets the rolling friction coefficient)."),
          kSurfaceChoices,
          0, this);

  // -------------------------------------------------------------------------
  // Output rows (read-only)
  // -------------------------------------------------------------------------

  /* 15 DensityAltResult */
  AddReadOnly(_("Density altitude"),
              _("Altitude in the ISA atmosphere with the same air density "
                "as the current conditions."),
              "%.0f %s",
              UnitGroup::ALTITUDE, 0);

  /* 16 DensityRatioResult */
  AddReadOnly(_("Density ratio \xCF\x83"),
              _("Actual air density divided by ISA sea-level density (1.225 kg/m³). "
                "Values < 1 indicate performance-degrading high-density-altitude "
                "conditions."),
              "%.4f",
              1.0);

  /* 17 VStallResult */
  AddReadOnly(_("V stall"),
              _("1-g stall speed at current gross mass and field density."),
              "%.1f %s",
              UnitGroup::HORIZONTAL_SPEED, 0);

  /* 18 VRotateResult */
  AddReadOnly(_("V rotate"),
              _("Rotation speed = 1.10 × V stall."),
              "%.1f %s",
              UnitGroup::HORIZONTAL_SPEED, 0);

  /* 19 GroundRollResult */
  AddReadOnly(_("Ground roll (corrected)"),
              _("Corrected ground roll accounting for weight, density altitude, "
                "headwind, slope, and surface friction."),
              "%.0f %s",
              UnitGroup::ALTITUDE, 0);

  /* 20 Distance50ftResult */
  AddReadOnly(_("Dist. to 50 ft (corrected)"),
              _("Corrected total takeoff distance to clear a 15 m (50 ft) obstacle."),
              "%.0f %s",
              UnitGroup::ALTITUDE, 0);

  /* 21 RunwayWarningResult */
  AddReadOnly(_("Runway status"),
              _("Warning shown when the corrected ground roll exceeds "
                "the entered runway length."));

  // Build initial runway list (may auto-populate elevation + length)
  UpdateRunwayList();

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

  const double runway_length_m =
      Units::ToSysAltitude(static_cast<const DataFieldFloat &>(
          GetDataField(RunwayLength)).GetValue());

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

  // -----------------------------------------------------------------------
  // Runway length warning
  // -----------------------------------------------------------------------
  if (runway_length_m > 0.0) {
    char buf[256];
    if (r.ground_roll_m > runway_length_m) {
      snprintf(buf, sizeof(buf),
               _("WARNING: Ground roll (%.0f %s) exceeds runway (%.0f %s)!"),
               Units::ToUserAltitude(r.ground_roll_m),
               Units::GetAltitudeName(),
               Units::ToUserAltitude(runway_length_m),
               Units::GetAltitudeName());
    } else {
      snprintf(buf, sizeof(buf),
               _("OK: Ground roll (%.0f %s) within runway (%.0f %s)."),
               Units::ToUserAltitude(r.ground_roll_m),
               Units::GetAltitudeName(),
               Units::ToUserAltitude(runway_length_m),
               Units::GetAltitudeName());
    }
    GetControl(RunwayWarningResult).SetText(buf);
  } else {
    GetControl(RunwayWarningResult).SetText("");
  }
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
