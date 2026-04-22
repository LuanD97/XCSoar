// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

/**
 * Aerotow Takeoff Calculator dialog
 *
 * A 4-tab dialog (Environment / Tug / Glider / Results) for computing
 * the aerotow takeoff distance of a tug+glider pair.
 *
 * Physics:
 *   - Phase 1: combined ground roll (both rolling, tug provides thrust)
 *   - Phase 2a (normal): glider lifts off first (higher CL, lower wing loading)
 *   - Phase 2b (abnormal): tug lifts off first → warning shown
 *
 * See TowTakeoffCalculator.hpp for full physics reference.
 */

#include "TakeoffDialog.hpp"
#include "TakeoffCalculator/TowTakeoffCalculator.hpp"
#include "TakeoffCalculator/TugStore.hpp"
#include "Dialogs/WidgetDialog.hpp"
#include "Widget/RowFormWidget.hpp"
#include "Widget/TabWidget.hpp"
#include "Form/DataField/Float.hpp"
#include "Form/DataField/Enum.hpp"
#include "Form/DataField/Listener.hpp"
#include "Form/Edit.hpp"
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
#include <functional>

// ---------------------------------------------------------------------------
// Surface / rolling-friction enum  (shared across panels)
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
// Shared update mechanism
// ---------------------------------------------------------------------------

/** Callback type used by input panels to notify the Results panel. */
using UpdateCallback = std::function<void()>;

// ===========================================================================
// Environment Panel (Tab 0)
// ===========================================================================

class EnvironmentPanel final
  : public RowFormWidget, DataFieldListener
{
  enum ControlIndex : unsigned {
    RunwayElev = 0,
    OutsideTemp,
    Qnh,
    Headwind,
    Slope,
    Surface,
  };

  UpdateCallback on_change_;

public:
  explicit EnvironmentPanel(UpdateCallback cb) noexcept
    :RowFormWidget(UIGlobals::GetDialogLook()),
     on_change_(std::move(cb)) {}

  void FillParams(TowTakeoffParameters &p) const noexcept
  {
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
      const unsigned idx =
          static_cast<const DataFieldEnum &>(GetDataField(Surface)).GetValue();
      constexpr unsigned kN =
          sizeof(kFrictionCoefficients) / sizeof(kFrictionCoefficients[0]);
      p.rolling_friction =
          kFrictionCoefficients[idx < kN ? idx : 0];
    }
  }

  /* RowFormWidget */
  void Prepare(ContainerWindow &parent, const PixelRect &rc) noexcept override
  {
    RowFormWidget::Prepare(parent, rc);

    const ComputerSettings &s = CommonInterface::GetComputerSettings();
    const double qnh_user =
        Units::ToUserPressure(s.pressure.GetHectoPascal());

    /* 0 RunwayElev */
    AddFloat(_("Runway elevation"),
             _("Runway threshold elevation above MSL."),
             "%.0f %s", "%.0f",
             Units::ToUserAltitude(-500),
             Units::ToUserAltitude(5000),
             Units::ToUserAltitude(10),
             false, UnitGroup::ALTITUDE, 0, this);

    /* 1 OutsideTemp */
    AddFloat(_("OAT"),
             _("Outside air temperature at runway level."),
             "%.1f %s", "%.1f",
             Temperature::FromCelsius(-40).ToUser(),
             Temperature::FromCelsius(55).ToUser(),
             1, false,
             s.forecast_temperature.ToUser(), this);
    {
      WndProperty &wp = GetControl(OutsideTemp);
      static_cast<DataFieldFloat &>(*wp.GetDataField())
          .SetUnits(Units::GetTemperatureName());
      wp.RefreshDisplay();
    }

    /* 2 QNH */
    AddFloat(_("QNH"),
             _("QNH altimeter setting."),
             GetUserPressureFormat(true), GetUserPressureFormat(),
             Units::ToUserPressure(Units::ToSysUnit(850,  Unit::HECTOPASCAL)),
             Units::ToUserPressure(Units::ToSysUnit(1080, Unit::HECTOPASCAL)),
             GetUserPressureStep(),
             false, qnh_user, this);
    {
      WndProperty &wp = GetControl(Qnh);
      static_cast<DataFieldFloat &>(*wp.GetDataField())
          .SetUnits(Units::GetPressureName());
      wp.RefreshDisplay();
    }

    /* 3 Headwind */
    AddFloat(_("Headwind"),
             _("Headwind along the takeoff run.  Negative = tailwind."),
             "%.1f %s", "%.1f",
             Units::ToUserWindSpeed(-30),
             Units::ToUserWindSpeed(30),
             Units::ToUserWindSpeed(1),
             false, UnitGroup::WIND_SPEED, 0, this);

    /* 4 Slope */
    AddFloat(_("Slope"),
             _("Runway gradient in the takeoff direction (%). "
               "Positive = uphill."),
             "%.1f %%", "%.1f",
             -5, 5, 0.1, false, 0, this);

    /* 5 Surface */
    AddEnum(_("Surface"),
            _("Runway surface type (sets the rolling friction coefficient)."),
            kSurfaceChoices, 0, this);
  }

  bool Save([[maybe_unused]] bool &changed) noexcept override
  {
    changed = false;
    return true;
  }

private:
  void OnModified([[maybe_unused]] DataField &df) noexcept override
  {
    if (on_change_) on_change_();
  }
};

// ===========================================================================
// Tug Panel (Tab 1)
// ===========================================================================

class TugPanel final
  : public RowFormWidget, DataFieldListener
{
  enum ControlIndex : unsigned {
    TugSelect = 0,
    TugGrossMass,
    TugReferenceMass,
    TugWingArea,
    TugClMax,
    TugAfmGroundRoll,
    TugAfm50ftDist,
  };

  UpdateCallback on_change_;

public:
  explicit TugPanel(UpdateCallback cb) noexcept
    :RowFormWidget(UIGlobals::GetDialogLook()),
     on_change_(std::move(cb)) {}

  void FillParams(TowTakeoffParameters &p) const noexcept
  {
    p.tug_gross_mass_kg =
        Units::ToSysMass(static_cast<const DataFieldFloat &>(
            GetDataField(TugGrossMass)).GetValue());

    p.tug_reference_mass_kg =
        Units::ToSysMass(static_cast<const DataFieldFloat &>(
            GetDataField(TugReferenceMass)).GetValue());

    p.tug_wing_area_m2 =
        static_cast<const DataFieldFloat &>(
            GetDataField(TugWingArea)).GetValue();

    p.tug_cl_max =
        static_cast<const DataFieldFloat &>(
            GetDataField(TugClMax)).GetValue();

    p.tug_afm_ground_roll_m =
        Units::ToSysAltitude(static_cast<const DataFieldFloat &>(
            GetDataField(TugAfmGroundRoll)).GetValue());

    p.tug_afm_distance_50ft_m =
        Units::ToSysAltitude(static_cast<const DataFieldFloat &>(
            GetDataField(TugAfm50ftDist)).GetValue());
  }

  /* RowFormWidget */
  void Prepare(ContainerWindow &parent, const PixelRect &rc) noexcept override
  {
    RowFormWidget::Prepare(parent, rc);

    /* Build the tug-selector enum from TugStore */
    const auto tugs = TugStore::GetAll();

    /* 0 TugSelect */
    {
      auto *df = new DataFieldEnum(this);
      for (unsigned i = 0; i < (unsigned)tugs.size(); ++i)
        df->AddChoice(i, tugs[i].name);
      df->AddChoice((unsigned)tugs.size(), _("Custom"));
      df->SetValue(0u);
      Add(_("Tug aircraft"),
          _("Select a tug from the built-in library, or choose Custom "
            "to enter data manually."),
          df);
    }

    /* 1 TugGrossMass */
    AddFloat(_("Tug gross mass"),
             _("Tug current takeoff mass (fuel + pilot)."),
             "%.0f %s", "%.0f",
             Units::ToUserMass(100),
             Units::ToUserMass(3000),
             Units::ToUserMass(5),
             false, UnitGroup::MASS,
             Units::ToUserMass(tugs.empty() ? 975 : tugs[0].mtow_kg),
             this);

    /* 2 TugReferenceMass */
    AddFloat(_("Tug reference mass"),
             _("AFM/POH reference (MTOW) used for the tabulated distances."),
             "%.0f %s", "%.0f",
             Units::ToUserMass(100),
             Units::ToUserMass(3000),
             Units::ToUserMass(5),
             false, UnitGroup::MASS,
             Units::ToUserMass(tugs.empty() ? 975 : tugs[0].mtow_kg),
             this);

    /* 3 TugWingArea */
    AddFloat(_("Tug wing area"),
             _("Tug wing reference area (m²)."),
             "%.1f m²", "%.1f",
             1, 50, 0.5, false,
             tugs.empty() ? 17.3 : tugs[0].wing_area_m2,
             this);

    /* 4 TugClMax */
    AddFloat(_("Tug CL max"),
             _("Tug maximum lift coefficient at takeoff flap setting."),
             "%.2f", "%.2f",
             0.80, 3.00, 0.05, false,
             tugs.empty() ? 1.45 : tugs[0].cl_max,
             this);

    /* 5 TugAfmGroundRoll */
    AddFloat(_("Tug AFM ground roll"),
             _("Tug AFM ground roll at MTOW, sea-level ISA, zero wind, "
               "level paved runway."),
             "%.0f %s", "%.0f",
             0, Units::ToUserAltitude(3000),
             Units::ToUserAltitude(10),
             false, UnitGroup::ALTITUDE,
             Units::ToUserAltitude(tugs.empty() ? 280 : tugs[0].afm_ground_roll_m),
             this);

    /* 6 TugAfm50ftDist */
    AddFloat(_("Tug AFM dist. to 50 ft"),
             _("Tug AFM distance to clear 15 m obstacle. "
               "Enter 0 to estimate as 1.7 × ground roll."),
             "%.0f %s", "%.0f",
             0, Units::ToUserAltitude(6000),
             Units::ToUserAltitude(10),
             false, UnitGroup::ALTITUDE,
             Units::ToUserAltitude(tugs.empty() ? 460 : tugs[0].afm_distance_50ft_m),
             this);

    // Apply first preset after all rows exist
    LoadTugPreset(0);
  }

  bool Save([[maybe_unused]] bool &changed) noexcept override
  {
    changed = false;
    return true;
  }

private:
  /** Load preset values from the TugStore for entry index i. */
  void LoadTugPreset(unsigned i) noexcept
  {
    const auto tugs = TugStore::GetAll();
    if (i >= (unsigned)tugs.size())
      return; // Custom — leave fields as-is

    const TugStore::Item &t = tugs[i];
    LoadValue(TugGrossMass,      Units::ToUserMass(t.mtow_kg),   UnitGroup::MASS);
    LoadValue(TugReferenceMass,  Units::ToUserMass(t.mtow_kg),   UnitGroup::MASS);
    LoadValue(TugWingArea,       t.wing_area_m2);
    LoadValue(TugClMax,          t.cl_max);
    LoadValue(TugAfmGroundRoll,  Units::ToUserAltitude(t.afm_ground_roll_m),   UnitGroup::ALTITUDE);
    LoadValue(TugAfm50ftDist,    Units::ToUserAltitude(t.afm_distance_50ft_m), UnitGroup::ALTITUDE);
  }

  void OnModified(DataField &df) noexcept override
  {
    // If the selector changed, prefill the other fields
    if (&df == &GetDataField(TugSelect)) {
      const unsigned sel =
          static_cast<const DataFieldEnum &>(df).GetValue();
      LoadTugPreset(sel);
    }
    if (on_change_) on_change_();
  }
};

// ===========================================================================
// Glider Panel (Tab 2)
// ===========================================================================

/** Flap-setting choices for the CL_max selector. */
static constexpr StaticEnumChoice kFlapChoices[] = {
  { 0, N_("Clean (no flaps)"),   nullptr },
  { 1, N_("Takeoff flap"),       nullptr },
  { 2, N_("Full flap"),          nullptr },
  nullptr
};

class GliderPanel final
  : public RowFormWidget, DataFieldListener
{
  enum ControlIndex : unsigned {
    FlapSelect = 0,
    GliderGrossMass,
    GliderReferenceMass,
    GliderWingArea,
    GliderClMax,
    GliderAfmGroundRoll,
    GliderAfm50ftDist,
  };

  UpdateCallback on_change_;

public:
  explicit GliderPanel(UpdateCallback cb) noexcept
    :RowFormWidget(UIGlobals::GetDialogLook()),
     on_change_(std::move(cb)) {}

  void FillParams(TowTakeoffParameters &p) const noexcept
  {
    p.glider_gross_mass_kg =
        Units::ToSysMass(static_cast<const DataFieldFloat &>(
            GetDataField(GliderGrossMass)).GetValue());

    p.glider_reference_mass_kg =
        Units::ToSysMass(static_cast<const DataFieldFloat &>(
            GetDataField(GliderReferenceMass)).GetValue());

    p.glider_wing_area_m2 =
        static_cast<const DataFieldFloat &>(
            GetDataField(GliderWingArea)).GetValue();

    p.glider_cl_max =
        static_cast<const DataFieldFloat &>(
            GetDataField(GliderClMax)).GetValue();

    p.glider_afm_ground_roll_m =
        Units::ToSysAltitude(static_cast<const DataFieldFloat &>(
            GetDataField(GliderAfmGroundRoll)).GetValue());
  }

  /* RowFormWidget */
  void Prepare(ContainerWindow &parent, const PixelRect &rc) noexcept override
  {
    RowFormWidget::Prepare(parent, rc);

    const ComputerSettings &settings = CommonInterface::GetComputerSettings();
    const PolarSettings    &polar    = settings.polar;
    const Plane            &plane    = settings.plane;

    const double ref_mass_kg   = polar.glide_polar_task.GetReferenceMass();
    const double gross_mass_kg = polar.glide_polar_task.GetTotalMass();
    const double wing_area_m2  = plane.wing_area;

    // Detect whether the plane profile has takeoff data
    const Plane::TakeoffConfig &tc = plane.takeoff;
    const bool has_tc = (tc.cl_max[0] > 0 || tc.cl_max[1] > 0 || tc.cl_max[2] > 0);
    const double default_cl =
        has_tc && tc.cl_max[1] > 0 ? tc.cl_max[1] :
        has_tc && tc.cl_max[0] > 0 ? tc.cl_max[0] : 1.50;

    /* 0 FlapSelect */
    AddEnum(_("Flap setting"),
            _("Select flap setting to choose the stored CL_max "
              "from the aircraft profile.  Ignored if no profile data."),
            kFlapChoices, 1, this);

    /* 1 GliderGrossMass */
    AddFloat(_("Glider gross mass"),
             _("Glider current gross mass (empty + pilot + ballast)."),
             "%.0f %s", "%.0f",
             Units::ToUserMass(100),
             Units::ToUserMass(3000),
             Units::ToUserMass(5),
             false, UnitGroup::MASS,
             Units::ToUserMass(gross_mass_kg > 10 ? gross_mass_kg : 400),
             this);

    /* 2 GliderReferenceMass */
    AddFloat(_("Glider reference mass"),
             _("AFM/POH reference mass (typically MTOW)."),
             "%.0f %s", "%.0f",
             Units::ToUserMass(100),
             Units::ToUserMass(3000),
             Units::ToUserMass(5),
             false, UnitGroup::MASS,
             Units::ToUserMass(ref_mass_kg > 10 ? ref_mass_kg : 500),
             this);

    /* 3 GliderWingArea */
    AddFloat(_("Glider wing area"),
             _("Glider wing reference area (m²)."),
             "%.1f m²", "%.1f",
             1, 100, 0.5, false,
             wing_area_m2 > 0.5 ? wing_area_m2 : 10.0,
             this);

    /* 4 GliderClMax */
    AddFloat(_("Glider CL max"),
             _("Maximum lift coefficient at the selected flap setting. "
               "Typical: clean 1.4–1.6, takeoff flap 1.6–2.0, full 2.0–2.4."),
             "%.2f", "%.2f",
             0.80, 3.00, 0.05, false,
             default_cl, this);

    /* 5 GliderAfmGroundRoll */
    AddFloat(_("Glider AFM ground roll"),
             _("Glider AFM ground roll at reference mass, sea-level ISA, "
               "zero wind, level runway.  0 if unknown."),
             "%.0f %s", "%.0f",
             0, Units::ToUserAltitude(3000),
             Units::ToUserAltitude(10),
             false, UnitGroup::ALTITUDE,
             Units::ToUserAltitude(
                 tc.afm_ground_roll_m > 0 ? tc.afm_ground_roll_m : 200),
             this);

    /* 6 GliderAfm50ftDist */
    AddFloat(_("Glider AFM dist. to 50 ft"),
             _("Glider AFM distance to clear 15 m obstacle. "
               "Enter 0 to estimate as 1.7 × ground roll."),
             "%.0f %s", "%.0f",
             0, Units::ToUserAltitude(6000),
             Units::ToUserAltitude(10),
             false, UnitGroup::ALTITUDE,
             Units::ToUserAltitude(
                 tc.afm_distance_50ft_m > 0 ? tc.afm_distance_50ft_m : 0),
             this);
  }

  bool Save([[maybe_unused]] bool &changed) noexcept override
  {
    changed = false;
    return true;
  }

private:
  /** Apply stored per-flap CL from the aircraft profile when available. */
  void ApplyFlapSetting(unsigned flap_idx) noexcept
  {
    const Plane &plane = CommonInterface::GetComputerSettings().plane;
    const double cl = plane.takeoff.cl_max[flap_idx];
    if (cl > 0)
      LoadValue(GliderClMax, cl);
  }

  void OnModified(DataField &df) noexcept override
  {
    if (GetCount() > 0 && &df == &GetDataField(FlapSelect)) {
      const unsigned flap =
          static_cast<const DataFieldEnum &>(df).GetValue();
      ApplyFlapSetting(flap);
    }
    if (on_change_) on_change_();
  }
};

// ===========================================================================
// Results Panel (Tab 3)
// ===========================================================================

class ResultsPanel final : public RowFormWidget
{
  enum ControlIndex : unsigned {
    DensityAlt = 0,
    DensityRatio,
    GliderVLof,
    GliderGroundRoll,
    TugVLof,
    TugGroundRoll,
    CombinedGroundRoll,
    WarningRow,
  };

  const EnvironmentPanel &env_;
  const TugPanel         &tug_;
  const GliderPanel      &glider_;
  bool prepared_ = false;

public:
  ResultsPanel(const EnvironmentPanel &env,
               const TugPanel &tug,
               const GliderPanel &glider) noexcept
    :RowFormWidget(UIGlobals::GetDialogLook()),
     env_(env), tug_(tug), glider_(glider) {}

  void Refresh() noexcept
  {
    if (!prepared_)
      return;

    TowTakeoffParameters p{};
    p.rope_drag_factor = 0.03;

    env_.FillParams(p);
    tug_.FillParams(p);
    glider_.FillParams(p);

    // Guard: need physically meaningful inputs
    if (p.tug_gross_mass_kg < 1.0 || p.tug_reference_mass_kg < 1.0 ||
        p.tug_wing_area_m2 < 0.1  || p.tug_cl_max < 0.1 ||
        p.tug_afm_ground_roll_m < 1.0 ||
        p.glider_gross_mass_kg < 1.0 || p.glider_reference_mass_kg < 1.0 ||
        p.glider_wing_area_m2 < 0.1  || p.glider_cl_max < 0.1 ||
        p.glider_afm_ground_roll_m < 1.0)
      return;

    const TowTakeoffResult r = ComputeTowTakeoff(p);

    LoadValue(DensityAlt,         r.density_altitude_m,       UnitGroup::ALTITUDE);
    LoadValue(DensityRatio,       r.density_ratio);
    LoadValue(GliderVLof,         r.glider.v_liftoff_ms,      UnitGroup::HORIZONTAL_SPEED);
    LoadValue(GliderGroundRoll,   r.glider.ground_roll_m,     UnitGroup::ALTITUDE);
    LoadValue(TugVLof,            r.tug.v_liftoff_ms,         UnitGroup::HORIZONTAL_SPEED);
    LoadValue(TugGroundRoll,      r.tug.ground_roll_m,        UnitGroup::ALTITUDE);
    LoadValue(CombinedGroundRoll, r.combined_ground_roll_m,   UnitGroup::ALTITUDE);

    // Warning row: show text if tug lifts off before glider
    if (r.tug_liftoff_before_glider)
      GetControl(WarningRow).SetText(
          _("WARNING: Tug lifts off before glider. "
            "Check configuration."));
    else
      GetControl(WarningRow).SetText(
          _("Normal sequence: glider lifts off before tug."));
  }

  /* RowFormWidget */
  void Prepare(ContainerWindow &parent, const PixelRect &rc) noexcept override
  {
    RowFormWidget::Prepare(parent, rc);

    /* 0 DensityAlt */
    AddReadOnly(_("Density altitude"),
                _("Altitude in the ISA atmosphere with the same air density "
                  "as the current conditions."),
                "%.0f %s", UnitGroup::ALTITUDE, 0);

    /* 1 DensityRatio */
    AddReadOnly(_("Density ratio \xCF\x83"),
                _("Actual density / ISA sea-level density."),
                "%.4f", 1.0);

    /* 2 GliderVLof */
    AddReadOnly(_("Glider V liftoff"),
                _("Glider liftoff speed = 1.15 × V stall."),
                "%.1f %s", UnitGroup::HORIZONTAL_SPEED, 0);

    /* 3 GliderGroundRoll */
    AddReadOnly(_("Glider solo roll"),
                _("Corrected glider solo ground roll (without tug effect)."),
                "%.0f %s", UnitGroup::ALTITUDE, 0);

    /* 4 TugVLof */
    AddReadOnly(_("Tug V liftoff"),
                _("Tug liftoff speed = 1.15 × V stall."),
                "%.1f %s", UnitGroup::HORIZONTAL_SPEED, 0);

    /* 5 TugGroundRoll */
    AddReadOnly(_("Tug solo roll"),
                _("Corrected tug solo ground roll (without glider)."),
                "%.0f %s", UnitGroup::ALTITUDE, 0);

    /* 6 CombinedGroundRoll */
    AddReadOnly(_("Combined ground roll"),
                _("Estimated aerotow combined ground roll (tug + glider pair)."),
                "%.0f %s", UnitGroup::ALTITUDE, 0);

    /* 7 WarningRow */
    AddReadOnly(_("Sequence"), nullptr, "");

    prepared_ = true;
    Refresh();
  }

  bool Save([[maybe_unused]] bool &changed) noexcept override
  {
    changed = false;
    return true;
  }
};

// ===========================================================================
// Public entry point
// ===========================================================================

void
ShowTakeoffCalculator() noexcept
{
  const DialogLook &look = UIGlobals::GetDialogLook();

  WidgetDialog dialog(WidgetDialog::Full{},
                      UIGlobals::GetMainWindow(),
                      look,
                      _("Aerotow Takeoff Calculator"));

  dialog.FinishPreliminary(
      std::make_unique<TabWidget>(TabWidget::Orientation::AUTO));

  dialog.PrepareWidget();
  auto &tab = static_cast<TabWidget &>(dialog.GetWidget());

  // Create panels in heap; results needs pointers to env/tug/glider
  // The TabWidget owns them via unique_ptr after AddTab().
  // We keep raw pointers for cross-panel references.

  UpdateCallback update_cb;

  // Shared forwarder: each input panel calls this to propagate changes
  // to the Results panel.  update_cb is assigned after results_ptr is
  // created (below) so it is safe to call it at any time after that.
  auto notify = [&update_cb]() { if (update_cb) update_cb(); };

  auto env_ptr    = std::make_unique<EnvironmentPanel>(UpdateCallback(notify));
  auto tug_ptr    = std::make_unique<TugPanel>(UpdateCallback(notify));
  auto glider_ptr = std::make_unique<GliderPanel>(UpdateCallback(notify));
  auto results_ptr = std::make_unique<ResultsPanel>(
                      *env_ptr, *tug_ptr, *glider_ptr);

  ResultsPanel *results_raw = results_ptr.get();

  update_cb = [results_raw]() noexcept { results_raw->Refresh(); };

  tab.AddTab(std::move(env_ptr),    _("Environment"));
  tab.AddTab(std::move(tug_ptr),    _("Tug"));
  tab.AddTab(std::move(glider_ptr), _("Glider"));
  tab.AddTab(std::move(results_ptr), _("Results"));

  // Refresh results whenever the user flips to that tab
  tab.SetPageFlippedCallback([&tab, results_raw]() {
    if (tab.GetCurrentIndex() == 3)
      results_raw->Refresh();
  });

  dialog.AddButton(_("Close"), mrOK);
  dialog.ShowModal();
}
