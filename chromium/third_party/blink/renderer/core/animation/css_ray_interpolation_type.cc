// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/animation/css_ray_interpolation_type.h"

#include <memory>
#include <utility>

#include "base/memory/ptr_util.h"
#include "third_party/blink/renderer/core/animation/interpolable_length.h"
#include "third_party/blink/renderer/core/css/basic_shape_functions.h"
#include "third_party/blink/renderer/core/css/css_value_list.h"
#include "third_party/blink/renderer/core/css/resolver/style_resolver_state.h"
#include "third_party/blink/renderer/core/style/computed_style.h"
#include "third_party/blink/renderer/core/style/shape_offset_path_operation.h"
#include "third_party/blink/renderer/core/style/style_ray.h"

namespace blink {

namespace {

class RayMode {
 public:
  RayMode(StyleRay::RaySize size, bool contain)
      : size_(size), contain_(contain) {}

  explicit RayMode(const StyleRay& style_ray)
      : size_(style_ray.Size()), contain_(style_ray.Contain()) {}

  StyleRay::RaySize Size() const { return size_; }
  bool Contain() const { return contain_; }

  bool operator==(const RayMode& other) const {
    return size_ == other.size_ && contain_ == other.contain_;
  }
  bool operator!=(const RayMode& other) const { return !(*this == other); }

 private:
  StyleRay::RaySize size_;
  bool contain_;
};

}  // namespace

class CSSRayNonInterpolableValue : public NonInterpolableValue {
 public:
  static scoped_refptr<CSSRayNonInterpolableValue> Create(const RayMode& mode) {
    return base::AdoptRef(new CSSRayNonInterpolableValue(mode));
  }

  const RayMode& Mode() const { return mode_; }

  DECLARE_NON_INTERPOLABLE_VALUE_TYPE();

 private:
  explicit CSSRayNonInterpolableValue(const RayMode& mode) : mode_(mode) {}

  const RayMode mode_;
};

DEFINE_NON_INTERPOLABLE_VALUE_TYPE(CSSRayNonInterpolableValue);
template <>
struct DowncastTraits<CSSRayNonInterpolableValue> {
  static bool AllowFrom(const NonInterpolableValue* value) {
    return value && AllowFrom(*value);
  }
  static bool AllowFrom(const NonInterpolableValue& value) {
    return value.GetType() == CSSRayNonInterpolableValue::static_type_;
  }
};

namespace {

// Returns the offset-path ray() value.
// If the offset-path is not a ray(), returns nullptr.
const StyleRay* GetRay(const ComputedStyle& style) {
  const auto* offset_shape =
      DynamicTo<ShapeOffsetPathOperation>(style.OffsetPath());
  if (!offset_shape) {
    return nullptr;
  }
  const BasicShape& shape = offset_shape->GetBasicShape();
  return DynamicTo<StyleRay>(shape);
}

class UnderlyingRayModeChecker
    : public CSSInterpolationType::CSSConversionChecker {
 public:
  explicit UnderlyingRayModeChecker(const RayMode& mode) : mode_(mode) {}

  bool IsValid(const StyleResolverState&,
               const InterpolationValue& underlying) const final {
    return mode_ ==
           To<CSSRayNonInterpolableValue>(*underlying.non_interpolable_value)
               .Mode();
  }

 private:
  const RayMode mode_;
};

class InheritedRayChecker : public CSSInterpolationType::CSSConversionChecker {
 public:
  explicit InheritedRayChecker(scoped_refptr<const StyleRay> style_ray)
      : style_ray_(std::move(style_ray)) {
    DCHECK(style_ray_);
  }

 private:
  bool IsValid(const StyleResolverState& state,
               const InterpolationValue&) const final {
    return GetRay(*state.ParentStyle()) == style_ray_.get();
  }

  scoped_refptr<const StyleRay> style_ray_;
};

std::unique_ptr<InterpolableValue> ConvertCoordinate(
    const BasicShapeCenterCoordinate& coordinate,
    double zoom) {
  return InterpolableLength::MaybeConvertLength(coordinate.ComputedLength(),
                                                zoom);
}

std::unique_ptr<InterpolableValue> CreateNeutralInterpolableCoordinate() {
  return InterpolableLength::CreateNeutral();
}

BasicShapeCenterCoordinate CreateCoordinate(
    const InterpolableValue& interpolable_value,
    const CSSToLengthConversionData& conversion_data) {
  return BasicShapeCenterCoordinate(
      BasicShapeCenterCoordinate::kTopLeft,
      To<InterpolableLength>(interpolable_value)
          .CreateLength(conversion_data, Length::ValueRange::kAll));
}

enum RayComponentIndex : unsigned {
  kRayAngleIndex,
  kRayCenterXIndex,
  kRayCenterYIndex,
  kRayHasExplicitCenterIndex,
  kRayComponentIndexCount,
};

InterpolationValue CreateValue(const StyleRay& ray, double zoom) {
  auto list = std::make_unique<InterpolableList>(kRayComponentIndexCount);
  list->Set(kRayAngleIndex, std::make_unique<InterpolableNumber>(ray.Angle()));
  list->Set(kRayCenterXIndex, ConvertCoordinate(ray.CenterX(), zoom));
  list->Set(kRayCenterYIndex, ConvertCoordinate(ray.CenterY(), zoom));
  list->Set(kRayHasExplicitCenterIndex,
            std::make_unique<InterpolableNumber>(ray.HasExplicitCenter()));
  return InterpolationValue(std::move(list),
                            CSSRayNonInterpolableValue::Create(RayMode(ray)));
}

InterpolationValue CreateNeutralValue(const RayMode& mode) {
  auto list = std::make_unique<InterpolableList>(kRayComponentIndexCount);
  list->Set(kRayAngleIndex, std::make_unique<InterpolableNumber>(0));
  list->Set(kRayCenterXIndex, CreateNeutralInterpolableCoordinate());
  list->Set(kRayCenterYIndex, CreateNeutralInterpolableCoordinate());
  list->Set(kRayHasExplicitCenterIndex,
            std::make_unique<InterpolableNumber>(0));
  return InterpolationValue(std::move(list),
                            CSSRayNonInterpolableValue::Create(mode));
}

}  // namespace

void CSSRayInterpolationType::ApplyStandardPropertyValue(
    const InterpolableValue& interpolable_value,
    const NonInterpolableValue* non_interpolable_value,
    StyleResolverState& state) const {
  const auto& ray_non_interpolable_value =
      To<CSSRayNonInterpolableValue>(*non_interpolable_value);
  const auto& list = To<InterpolableList>(interpolable_value);
  scoped_refptr<StyleRay> style_ray = StyleRay::Create(
      To<InterpolableNumber>(list.Get(kRayAngleIndex))->Value(),
      ray_non_interpolable_value.Mode().Size(),
      ray_non_interpolable_value.Mode().Contain(),
      CreateCoordinate(*list.Get(kRayCenterXIndex),
                       state.CssToLengthConversionData()),
      CreateCoordinate(*list.Get(kRayCenterYIndex),
                       state.CssToLengthConversionData()),
      To<InterpolableNumber>(list.Get(kRayHasExplicitCenterIndex))->Value());
  // TODO(sakhapov): handle coord box.
  state.StyleBuilder().SetOffsetPath(
      ShapeOffsetPathOperation::Create(style_ray, CoordBox::kBorderBox));
}

void CSSRayInterpolationType::Composite(
    UnderlyingValueOwner& underlying_value_owner,
    double underlying_fraction,
    const InterpolationValue& value,
    double interpolation_fraction) const {
  const RayMode& underlying_mode =
      To<CSSRayNonInterpolableValue>(
          *underlying_value_owner.Value().non_interpolable_value)
          .Mode();
  const RayMode& ray_mode =
      To<CSSRayNonInterpolableValue>(*value.non_interpolable_value).Mode();
  if (underlying_mode == ray_mode) {
    underlying_value_owner.MutableValue().interpolable_value->ScaleAndAdd(
        underlying_fraction, *value.interpolable_value);
  } else {
    underlying_value_owner.Set(*this, value);
  }
}

InterpolationValue CSSRayInterpolationType::MaybeConvertNeutral(
    const InterpolationValue& underlying,
    ConversionCheckers& conversion_checkers) const {
  const RayMode& underlying_mode =
      To<CSSRayNonInterpolableValue>(*underlying.non_interpolable_value).Mode();
  conversion_checkers.push_back(
      std::make_unique<UnderlyingRayModeChecker>(underlying_mode));
  return CreateNeutralValue(underlying_mode);
}

InterpolationValue CSSRayInterpolationType::MaybeConvertInitial(
    const StyleResolverState&,
    ConversionCheckers&) const {
  // 'none' is not a ray().
  return nullptr;
}

InterpolationValue CSSRayInterpolationType::MaybeConvertInherit(
    const StyleResolverState& state,
    ConversionCheckers& conversion_checkers) const {
  if (!state.ParentStyle())
    return nullptr;

  const StyleRay* inherited_ray = GetRay(*state.ParentStyle());
  if (!inherited_ray)
    return nullptr;

  conversion_checkers.push_back(
      std::make_unique<InheritedRayChecker>(inherited_ray));
  return CreateValue(*inherited_ray, state.ParentStyle()->EffectiveZoom());
}

PairwiseInterpolationValue CSSRayInterpolationType::MaybeMergeSingles(
    InterpolationValue&& start,
    InterpolationValue&& end) const {
  const RayMode& start_mode =
      To<CSSRayNonInterpolableValue>(*start.non_interpolable_value).Mode();
  const RayMode& end_mode =
      To<CSSRayNonInterpolableValue>(*end.non_interpolable_value).Mode();
  if (start_mode != end_mode)
    return nullptr;
  return PairwiseInterpolationValue(std::move(start.interpolable_value),
                                    std::move(end.interpolable_value),
                                    std::move(start.non_interpolable_value));
}

InterpolationValue
CSSRayInterpolationType::MaybeConvertStandardPropertyUnderlyingValue(
    const ComputedStyle& style) const {
  const StyleRay* underlying_ray = GetRay(style);
  if (!underlying_ray)
    return nullptr;

  return CreateValue(*underlying_ray, style.EffectiveZoom());
}

InterpolationValue CSSRayInterpolationType::MaybeConvertValue(
    const CSSValue& value,
    const StyleResolverState* state,
    ConversionCheckers&) const {
  DCHECK(state);
  scoped_refptr<BasicShape> shape = nullptr;
  if (const auto* list = DynamicTo<CSSValueList>(value)) {
    if (list->First().IsRayValue()) {
      shape = BasicShapeForValue(*state, list->First());
    }
  } else if (value.IsRayValue()) {
    shape = BasicShapeForValue(*state, value);
  }
  if (!shape) {
    return nullptr;
  }
  return CreateValue(To<StyleRay>(*shape),
                     state->ParentStyle()->EffectiveZoom());
}

}  // namespace blink
