/**
 * This file is part of the "FnordMetric" project
 *   Copyright (c) 2011-2014 Paul Asmuth, Google Inc.
 *
 * FnordMetric is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License v3.0. You should have received a
 * copy of the GNU General Public License along with this program. If not, see
 * <http://www.gnu.org/licenses/>.
 */
#ifndef _FNORDMETRIC_AREACHART_H
#define _FNORDMETRIC_AREACHART_H
#include <stdlib.h>
#include <assert.h>
#include <fnordmetric/base/series.h>
#include <fnordmetric/ui/axisdefinition.h>
#include <fnordmetric/ui/domain.h>
#include <fnordmetric/ui/drawable.h>
#include <fnordmetric/ui/canvas.h>
#include <fnordmetric/ui/colorpalette.h>
#include <fnordmetric/ui/rendertarget.h>

namespace fnordmetric {
namespace ui {

class AreaChart : public Drawable {
public:
  static char kDefaultPointStyle[];
  static char kDefaultPointSize[];
  static char kDefaultLineStyle[];
  static char kDefaultLineWidth[];

  /**
   * Create a new area chart
   *
   * @param canvas the canvas to draw this chart on. does not transfer ownership
   * @param stacked stack areas?
   */
  AreaChart(
      Canvas* canvas,
      bool stacked = false);

protected:
  bool stacked_;
};

template <typename TX_, typename TY_, typename TZ_>
class AreaChart3D : public AreaChart {
public:
  typedef TX_ TX;
  typedef TY_ TY;
  typedef TZ_ TZ;

  /**
   * Create a new area chart
   *
   * @param canvas the canvas to draw this chart on. does not transfer ownership
   * @param stacked stack areas?
   */
  AreaChart3D(Canvas* canvas, bool stacked = false);

  /**
   * Create a new area chart
   *
   * @param canvas the canvas to draw this chart on. does not transfer ownership
   * @param x_domain the x domain. does not transfer ownership
   * @param y_domain the y domain. does not transfer ownership
   * @param stacked stack areas?
   */
  AreaChart3D(
      Canvas* canvas,
      AnyDomain* x_domain,
      AnyDomain* y_domain,
      bool stacked = false);

  /**
   * Add a (x: string, y: double, z: double) series. This will draw an area that
   * covers the surface between height y and z for each x.
   *
   * @param series the series to add. does not transfer ownership
   */
  void addSeries(Series3D<TX, TY, TZ>* series);

  /**
   * Add an axis to the chart.
   *
   * The returned pointer is owned by the canvas object and must not be freed
   * by the caller!
   *
   * @param position the position/placement of the axis
   */
   AxisDefinition* addAxis(AxisDefinition::kPosition position);

  /**
   * Get the {x,y} domain of this chart. Will raise an exception if z domain
   * is requested.
   *
   * The returned pointer is owned by the chart object and must not be freed
   * by the caller!
   *
   * @param dimension the dimension (x,y)
   */
  AnyDomain* getDomain(AnyDomain::kDimension dimension) override;

protected:

  struct Area {
    std::string color;
    std::string line_style;
    double line_width;
    std::string point_style;
    double point_size;
    std::vector<std::tuple<TX, TY, TZ>> points;
  };

  void render(
      RenderTarget* target,
      Viewport* viewport) const override;

  DomainAdapter x_domain_;
  DomainAdapter y_domain_;
  std::vector<Area> areas_;
  ColorPalette color_palette_;
};

template <typename TX_, typename TY_>
class AreaChart2D : public AreaChart3D<TX_, TY_, double> {
public:
  typedef TX_ TX;
  typedef TY_ TY;

  /**
   * Create a new area chart
   *
   * @param canvas the canvas to draw this chart on. does not transfer ownership
   * @param stacked stack areas?
   */
  AreaChart2D(Canvas* canvas, bool stacked = false);

  /**
   * Create a new area chart
   *
   * @param canvas the canvas to draw this chart on. does not transfer ownership
   * @param x_domain the x domain. does not transfer ownership
   * @param y_domain the y domain. does not transfer ownership
   * @param stacked stack areas?
   */
  AreaChart2D(
      Canvas* canvas,
      AnyDomain* x_domain,
      AnyDomain* y_domain,
      bool stacked = false);

  /**
   * Add a (x: string, y: double) series. This will draw an area that covers
   * the surface between 0 and y for each x.
   *
   * @param series the series to add. does not transfer ownership
   */
  void addSeries(Series2D<TX, TY>* series);

};

template <typename TX, typename TY, typename TZ>
AreaChart3D<TX, TY, TZ>::AreaChart3D(
    Canvas* canvas,
    bool stacked /* = false */) : 
    AreaChart(canvas, stacked) {}

template <typename TX, typename TY, typename TZ>
AreaChart3D<TX, TY, TZ>::AreaChart3D(
    Canvas* canvas,
    AnyDomain* x_domain,
    AnyDomain* y_domain,
    bool stacked /* = false */) :
    AreaChart(canvas, stacked) {
  x_domain_.reset(x_domain);
  y_domain_.reset(y_domain);
}

template <typename TX, typename TY, typename TZ>
void AreaChart3D<TX, TY, TZ>::addSeries(Series3D<TX, TY, TZ>* series) {
  Domain<TX>* x_domain;
  if (x_domain_.empty()) {
    x_domain = Domain<TX>::mkDomain();
    x_domain_.reset(x_domain, true);
  } else {
    x_domain = x_domain_.getAs<Domain<TX>>();
  }

  Domain<TY>* y_domain;
  if (y_domain_.empty()) {
    y_domain = Domain<TY>::mkDomain();
    y_domain_.reset(y_domain, true);

    auto cont = dynamic_cast<AnyContinuousDomain*>(y_domain);
    if (cont != nullptr) {
      cont->setPadding(
          AnyDomain::kDefaultDomainPadding,
          AnyDomain::kDefaultDomainPadding);
    }
  } else {
    y_domain = y_domain_.getAs<Domain<TY>>();
  }

  if (!series->hasProperty(Series::P_COLOR)) {
    color_palette_.setNextColor(series);
  }

  series->setDefaultProperty(
      Series::P_LINE_STYLE,
      AreaChart::kDefaultLineStyle);

  series->setDefaultProperty(
      Series::P_LINE_WIDTH,
      AreaChart::kDefaultLineWidth);

  series->setDefaultProperty(
      Series::P_POINT_STYLE,
      AreaChart::kDefaultPointStyle);

  series->setDefaultProperty(
      Series::P_POINT_SIZE,
      AreaChart::kDefaultPointSize);

  // FIXPAUL catch conversion errors
  Area area;
  area.color = series->getProperty(Series::P_COLOR);
  area.line_style = series->getProperty(Series::P_LINE_STYLE);
  area.line_width = std::stod(series->getProperty(Series::P_LINE_WIDTH));
  area.point_style = series->getProperty(Series::P_POINT_STYLE);
  area.point_size = std::stod(series->getProperty(Series::P_POINT_SIZE));

  for (const auto& point : series->getData()) {
    x_domain->addValue(point.x());
    y_domain->addValue(point.y());
    y_domain->addValue(point.z());
    area.points.emplace_back(
        point.x(),
        point.y(),
        point.z());
  }

  // FIXPAUL: stacked areas, missing data
  areas_.emplace_back(area);
}

template <typename TX, typename TY, typename TZ>
void AreaChart3D<TX, TY, TZ>::render(
    RenderTarget* target,
    Viewport* viewport) const {
  target->beginGroup("areas");
  target->finishGroup();
}

template <typename TX, typename TY, typename TZ>
AxisDefinition* AreaChart3D<TX, TY, TZ>::addAxis(
    AxisDefinition::kPosition position) {
  auto axis = canvas_->addAxis(position);

  switch (position) {

    case AxisDefinition::TOP:
      axis->setDomain(&x_domain_);
      return axis;

    case AxisDefinition::RIGHT:
      axis->setDomain(&y_domain_);
      return axis;

    case AxisDefinition::BOTTOM:
      axis->setDomain(&x_domain_);
      return axis;

    case AxisDefinition::LEFT:
      axis->setDomain(&y_domain_);
      return axis;

  }
}

template <typename TX, typename TY, typename TZ>
AnyDomain* AreaChart3D<TX, TY, TZ>::getDomain(
    AnyDomain::kDimension dimension) {
  switch (dimension) {
    case AnyDomain::DIM_X:
      return x_domain_.get();

    case AnyDomain::DIM_Y:
      return y_domain_.get();

    case AnyDomain::DIM_Z:
      RAISE(util::RuntimeException, "AreaChart3D does not have a Z domain");
      return nullptr;
  }
}

template <typename TX, typename TY>
AreaChart2D<TX, TY>::AreaChart2D(
    Canvas* canvas,
    bool stacked /* = false */) :
    AreaChart3D<TX, TY, TY>(canvas, stacked) {}

template <typename TX, typename TY>
AreaChart2D<TX, TY>::AreaChart2D(
    Canvas* canvas,
    AnyDomain* x_domain,
    AnyDomain* y_domain,
    bool stacked /* = false */) :
    AreaChart3D<TX, TY, TY>(canvas, x_domain, y_domain, stacked) {}

template <typename TX, typename TY>
void AreaChart2D<TX, TY>::addSeries(Series2D<TX, TY>* series) {
  auto series3d = new Series3D<TX, TY, TY>(); // FIXPAUL: never free'd!
  auto copy_labels = series->hasProperty(Series::P_LABEL);

  for (const auto& point : series->getData()) {
    series3d->addDatum(
        Series::Coord<TX>(point.x()),
        Series::Coord<TY>(nullptr),
        Series::Coord<TY>(point.y()));

    if (copy_labels) {
      series3d->setProperty(
          Series::P_LABEL,
          &series3d->getData().back(),
          series->getProperty(Series::P_LABEL, &point));
    }
  }

  if (series->hasProperty(Series::P_COLOR)) {
    series3d->setDefaultProperty(
        Series::P_COLOR,
        series->getProperty(Series::P_COLOR));
  }

  AreaChart3D<TX, TY, TY>::addSeries(series3d);
}

}
}
#endif
