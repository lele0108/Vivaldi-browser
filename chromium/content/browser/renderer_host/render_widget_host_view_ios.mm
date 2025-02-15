// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/renderer_host/render_widget_host_view_ios.h"

#import <UIKit/UIKit.h>

#include "base/mac/scoped_nsobject.h"
#include "base/strings/sys_string_conversions.h"
#include "components/viz/common/surfaces/frame_sink_id_allocator.h"
#include "content/browser/renderer_host/browser_compositor_ios.h"
#include "content/browser/renderer_host/input/motion_event_web.h"
#include "content/browser/renderer_host/input/synthetic_gesture_target_ios.h"
#include "content/browser/renderer_host/input/web_input_event_builders_ios.h"
#include "content/browser/renderer_host/render_view_host_delegate_view.h"
#include "content/browser/renderer_host/render_widget_host_impl.h"
#include "content/browser/renderer_host/render_widget_host_input_event_router.h"
#include "content/browser/renderer_host/text_input_manager.h"
#include "content/browser/renderer_host/ui_events_helper.h"
#include "content/common/content_switches_internal.h"
#include "content/public/browser/browser_task_traits.h"
#include "ui/accelerated_widget_mac/ca_layer_frame_sink_provider.h"
#include "ui/accelerated_widget_mac/display_ca_layer_tree.h"
#include "ui/base/ime/text_input_mode.h"
#include "ui/base/ime/text_input_type.h"
#include "ui/display/screen.h"
#include "ui/events/gesture_detection/gesture_provider_config_helper.h"
#include "ui/gfx/geometry/size_conversions.h"

// Used for settng the requested renderer size when testing.
constexpr int kDefaultWidthForTesting = 980;
constexpr int kDefaultHeightForTesting = 735;

static void* kObservingContext = &kObservingContext;

@interface UIApplication (Testing)
- (BOOL)isRunningTests;
@end

@implementation UIApplication (Testing)
- (BOOL)isRunningTests {
  return NO;
}
@end

namespace {
bool IsTesting() {
  return [[UIApplication sharedApplication] isRunningTests];
}
}  // namespace

// TODO(dtapuska): Change this to be UITextInput and handle the other
// events to implement the composition and selection ranges.
@interface RenderWidgetUIViewTextInput : UIView <UIKeyInput> {
  raw_ptr<content::RenderWidgetHostViewIOS> _view;
}
- (void)onUpdateTextInputState:(const ui::mojom::TextInputState&)state
                    withBounds:(CGRect)bounds;
- (void)showKeyboard:(bool)has_text withBounds:(CGRect)bounds;
- (void)hideKeyboard;

@end

@interface RenderWidgetUIView : CALayerFrameSinkProvider {
  raw_ptr<content::RenderWidgetHostViewIOS> _view;
  absl::optional<gfx::Vector2dF> _view_offset_during_touch_sequence;
}

// TextInput state.
@property(nonatomic, strong) RenderWidgetUIViewTextInput* textInput;

/** The constraint between the top edge of @c contentView and its superview. */
@property(nonatomic, strong, nonnull)
    NSLayoutConstraint* contentViewTopConstraint;

/** The constraint between the bottom edge of @c contentView and its superview.
 */
@property(nonatomic, strong, nonnull)
    NSLayoutConstraint* contentViewBottomConstraint;

/** The constraint between the trailing edge of @c contentView and its
 * superview. */
@property(nonatomic, strong, nonnull)
    NSLayoutConstraint* contentViewTrailingConstraint;

- (void)updateView:(UIScrollView*)view;
- (void)removeView;
@end

@implementation CALayerFrameSinkProvider

- (ui::CALayerFrameSink*)frameSink {
  return nil;
}

@end

@implementation RenderWidgetUIViewTextInput {
  BOOL _hasText;
}

- (instancetype)initWithWidget:(content::RenderWidgetHostViewIOS*)view {
  _view = view;
  _hasText = NO;
  self.multipleTouchEnabled = YES;
  self.autoresizingMask =
      UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
  return [self init];
}

- (void)onUpdateTextInputState:(const ui::mojom::TextInputState&)state
                    withBounds:(CGRect)bounds {
  // Check for the visibility request and policy if VK APIs are enabled.
  if (state.vk_policy == ui::mojom::VirtualKeyboardPolicy::MANUAL) {
    // policy is manual.
    if (state.last_vk_visibility_request ==
        ui::mojom::VirtualKeyboardVisibilityRequest::SHOW) {
      [self showKeyboard:!state.value->empty() withBounds:bounds];
    } else if (state.last_vk_visibility_request ==
               ui::mojom::VirtualKeyboardVisibilityRequest::HIDE) {
      [self hideKeyboard];
    }
  } else {
    bool hide = state.always_hide_ime ||
                state.mode == ui::TextInputMode::TEXT_INPUT_MODE_NONE ||
                state.type == ui::TextInputType::TEXT_INPUT_TYPE_NONE;
    if (hide) {
      [self hideKeyboard];
    } else if (state.show_ime_if_needed) {
      [self showKeyboard:!state.value->empty() withBounds:bounds];
    }
  }
}

- (void)showKeyboard:(bool)has_text withBounds:(CGRect)bounds {
  self.frame = bounds;
  [self becomeFirstResponder];
  _hasText = has_text;
}

- (void)hideKeyboard {
  [self resignFirstResponder];
  _hasText = NO;
}

- (BOOL)canBecomeFirstResponder {
  return YES;
}

- (BOOL)hasText {
  return _hasText;
}

- (void)insertText:(NSString*)text {
  _view->ImeCommitText(base::SysNSStringToUTF16(text),
                       gfx::Range::InvalidRange(), 0);
}

- (void)deleteBackward {
  std::vector<ui::ImeTextSpan> ime_text_spans;
  _view->ImeSetComposition(std::u16string(), ime_text_spans,
                           gfx::Range::InvalidRange(), -1, 0);
  _view->ImeCommitText(std::u16string(), gfx::Range::InvalidRange(), 0);
}

- (BOOL)becomeFirstResponder {
  BOOL result = [super becomeFirstResponder];
  if (result) {
    _view->OnFirstResponderChanged();
  }
  return result;
}

- (BOOL)resignFirstResponder {
  BOOL result = [super resignFirstResponder];
  if (result) {
    _view->OnFirstResponderChanged();
  }
  return result;
}

@end

@implementation RenderWidgetUIView
@synthesize textInput = _text_input;

- (instancetype)initWithWidget:(content::RenderWidgetHostViewIOS*)view {
  self = [self init];
  if (self) {
    _view = view;
    self.multipleTouchEnabled = YES;
    self.autoresizingMask =
        UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    _text_input = [[RenderWidgetUIViewTextInput alloc] initWithWidget:view];
    [self addSubview:_text_input];
  }
  return self;
}
- (void)layoutSubviews {
  [super layoutSubviews];
  _view->UpdateScreenInfo();

  // TODO(dtapuska): This isn't correct, we need to figure out when the window
  // gains/loses focus.
  _view->SetActive(true);
}

- (ui::CALayerFrameSink*)frameSink {
  return _view;
}

- (BOOL)canBecomeFirstResponder {
  return YES;
}

- (BOOL)becomeFirstResponder {
  BOOL result = [super becomeFirstResponder];
  if (result || _view->CanBecomeFirstResponderForTesting()) {
    _view->OnFirstResponderChanged();
  }
  return result;
}

- (BOOL)resignFirstResponder {
  BOOL result = [super resignFirstResponder];
  if (result || _view->CanResignFirstResponderForTesting()) {
    _view->OnFirstResponderChanged();
  }
  return result;
}

- (void)touchesBegan:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
  if (!_view->HasFocus()) {
    if ([self becomeFirstResponder]) {
      _view->OnFirstResponderChanged();
    }
  }
  for (UITouch* touch in touches) {
    blink::WebTouchEvent webTouchEvent = content::WebTouchEventBuilder::Build(
        blink::WebInputEvent::Type::kTouchStart, touch, event, self,
        _view_offset_during_touch_sequence);
    if (!_view_offset_during_touch_sequence) {
      _view_offset_during_touch_sequence =
          webTouchEvent.touches[0].PositionInWidget() -
          webTouchEvent.touches[0].PositionInScreen();
    }
    _view->OnTouchEvent(std::move(webTouchEvent));
  }
}

- (void)touchesEnded:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
  for (UITouch* touch in touches) {
    _view->OnTouchEvent(content::WebTouchEventBuilder::Build(
        blink::WebInputEvent::Type::kTouchEnd, touch, event, self,
        _view_offset_during_touch_sequence));
  }
  if (event.allTouches.count == 1) {
    _view_offset_during_touch_sequence.reset();
  }
}

- (void)touchesMoved:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
  for (UITouch* touch in touches) {
    _view->OnTouchEvent(content::WebTouchEventBuilder::Build(
        blink::WebInputEvent::Type::kTouchMove, touch, event, self,
        _view_offset_during_touch_sequence));
  }
}

- (void)touchesCancelled:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
  for (UITouch* touch in touches) {
    _view->OnTouchEvent(content::WebTouchEventBuilder::Build(
        blink::WebInputEvent::Type::kTouchCancel, touch, event, self,
        _view_offset_during_touch_sequence));
  }
  _view_offset_during_touch_sequence.reset();
}

- (void)observeValueForKeyPath:(NSString*)keyPath
                      ofObject:(id)object
                        change:(NSDictionary*)change
                       context:(void*)context {
  if (context == kObservingContext) {
    _view->ContentInsetChanged();
  } else {
    [super observeValueForKeyPath:keyPath
                         ofObject:object
                           change:change
                          context:context];
  }
}

- (void)removeView {
  UIScrollView* view = (UIScrollView*)[self superview];
  [view removeObserver:self
            forKeyPath:NSStringFromSelector(@selector(contentInset))];
  [self removeFromSuperview];
}

- (void)updateView:(UIScrollView*)view {
  [view addSubview:self];
  view.scrollEnabled = NO;
  CGRect parentBounds = [view bounds];
  CGRect frameBounds = CGRectZero;
  frameBounds.size = parentBounds.size;
  self.frame = frameBounds;
  // Remove all existing gestureRecognizers since the header might be reused.
  for (UIGestureRecognizer* recognizer in view.gestureRecognizers) {
    [view removeGestureRecognizer:recognizer];
  }
  [view addObserver:self
         forKeyPath:NSStringFromSelector(@selector(contentInset))
            options:NSKeyValueObservingOptionNew | NSKeyValueObservingOptionOld
            context:kObservingContext];
}

@end

namespace content {

// This class holds a scoped_nsobject so we don't leak that in the header
// of the RenderWidgetHostViewIOS.
class UIViewHolder {
 public:
  base::scoped_nsobject<RenderWidgetUIView> view_;
};

///////////////////////////////////////////////////////////////////////////////
// RenderWidgetHostViewIOS, public:

RenderWidgetHostViewIOS::RenderWidgetHostViewIOS(RenderWidgetHost* widget)
    : RenderWidgetHostViewBase(widget),
      gesture_provider_(
          ui::GetGestureProviderConfig(
              ui::GestureProviderConfigType::CURRENT_PLATFORM,
              content::GetUIThreadTaskRunner({BrowserTaskType::kUserInput})),
          this) {
  ui_view_ = std::make_unique<UIViewHolder>();
  ui_view_->view_ = base::scoped_nsobject<RenderWidgetUIView>(
      [[RenderWidgetUIView alloc] initWithWidget:this]);

  display_tree_ =
      std::make_unique<ui::DisplayCALayerTree>([ui_view_->view_ layer]);

  auto* screen = display::Screen::GetScreen();
  screen_infos_ =
      screen->GetScreenInfosNearestDisplay(screen->GetPrimaryDisplay().id());

  browser_compositor_ = std::make_unique<BrowserCompositorIOS>(
      ui_view_->view_.get(), this, host()->is_hidden(),
      host()->GetFrameSinkId());

  if (IsTesting()) {
    browser_compositor_->UpdateSurfaceFromUIView(GetViewBounds().size());
  }

  CHECK(host()->GetFrameSinkId().is_valid());

  // Let the page-level input event router know about our surface ID
  // namespace for surface-based hit testing.
  if (ShouldRouteEvents()) {
    host()->delegate()->GetInputEventRouter()->AddFrameSinkIdOwner(
        GetFrameSinkId(), this);
  }

  if (GetTextInputManager()) {
    text_input_manager_->AddObserver(this);
  }

  host()->render_frame_metadata_provider()->AddObserver(this);
  host()->SetView(this);
}

RenderWidgetHostViewIOS::~RenderWidgetHostViewIOS() = default;

void RenderWidgetHostViewIOS::Destroy() {
  host()->render_frame_metadata_provider()->RemoveObserver(this);
  if (text_input_manager_) {
    text_input_manager_->RemoveObserver(this);
  }
  browser_compositor_.reset();
  // Call this before the derived class is destroyed so that virtual function
  // calls back into `this` still work.
  NotifyObserversAboutShutdown();
  RenderWidgetHostViewBase::Destroy();
  delete this;
}

bool RenderWidgetHostViewIOS::IsSurfaceAvailableForCopy() {
  return browser_compositor_->GetDelegatedFrameHost()
      ->CanCopyFromCompositingSurface();
}

void RenderWidgetHostViewIOS::CopyFromSurface(
    const gfx::Rect& src_rect,
    const gfx::Size& dst_size,
    base::OnceCallback<void(const SkBitmap&)> callback) {
  base::WeakPtr<RenderWidgetHostImpl> popup_host;
  base::WeakPtr<DelegatedFrameHost> popup_frame_host;
  RenderWidgetHostViewBase::CopyMainAndPopupFromSurface(
      host()->GetWeakPtr(),
      browser_compositor_->GetDelegatedFrameHost()->GetWeakPtr(), popup_host,
      popup_frame_host, src_rect, dst_size, GetDeviceScaleFactor(),
      std::move(callback));
}

void RenderWidgetHostViewIOS::InitAsChild(gfx::NativeView parent_view) {}
void RenderWidgetHostViewIOS::SetSize(const gfx::Size& size) {}
void RenderWidgetHostViewIOS::SetBounds(const gfx::Rect& rect) {}
gfx::NativeView RenderWidgetHostViewIOS::GetNativeView() {
  return gfx::NativeView(ui_view_->view_.get());
}
gfx::NativeViewAccessible RenderWidgetHostViewIOS::GetNativeViewAccessible() {
  return {};
}

void RenderWidgetHostViewIOS::Focus() {
  // Ignore redundant calls, as they can cause unending loops of focus-setting.
  // crbug.com/998123, crbug.com/804184.
  if (is_first_responder_ || is_getting_focus_) {
    return;
  }

  base::AutoReset<bool> is_getting_focus_bit(&is_getting_focus_, true);
  [ui_view_->view_ becomeFirstResponder];
}

bool RenderWidgetHostViewIOS::HasFocus() {
  return is_first_responder_;
}

gfx::Rect RenderWidgetHostViewIOS::GetViewBounds() {
  // When testing, we will not have a windowScene and, as a consequence, we will
  // not have an intrinsic renderer size. This will cause tests to fail, though,
  // so we will instead set a default size.
  return IsTesting()
             ? gfx::Rect(kDefaultWidthForTesting, kDefaultHeightForTesting)
             : gfx::Rect([ui_view_->view_ bounds]);
}
blink::mojom::PointerLockResult RenderWidgetHostViewIOS::LockMouse(bool) {
  return {};
}
blink::mojom::PointerLockResult RenderWidgetHostViewIOS::ChangeMouseLock(bool) {
  return {};
}
void RenderWidgetHostViewIOS::UnlockMouse() {}

uint32_t RenderWidgetHostViewIOS::GetCaptureSequenceNumber() const {
  return latest_capture_sequence_number_;
}

void RenderWidgetHostViewIOS::EnsureSurfaceSynchronizedForWebTest() {
  ++latest_capture_sequence_number_;
  browser_compositor_->ForceNewSurfaceId();
}

void RenderWidgetHostViewIOS::TakeFallbackContentFrom(
    RenderWidgetHostView* view) {}

std::unique_ptr<SyntheticGestureTarget>
RenderWidgetHostViewIOS::CreateSyntheticGestureTarget() {
  RenderWidgetHostImpl* host =
      RenderWidgetHostImpl::From(GetRenderWidgetHost());
  return std::make_unique<SyntheticGestureTargetIOS>(host);
}

const viz::LocalSurfaceId& RenderWidgetHostViewIOS::GetLocalSurfaceId() const {
  return browser_compositor_->GetRendererLocalSurfaceId();
}

const viz::FrameSinkId& RenderWidgetHostViewIOS::GetFrameSinkId() const {
  return browser_compositor_->GetDelegatedFrameHost()->frame_sink_id();
}

viz::FrameSinkId RenderWidgetHostViewIOS::GetRootFrameSinkId() {
  return browser_compositor_->GetRootFrameSinkId();
}

viz::SurfaceId RenderWidgetHostViewIOS::GetCurrentSurfaceId() const {
  // |browser_compositor_| could be null if this method is called during its
  // destruction.
  if (!browser_compositor_) {
    return viz::SurfaceId();
  }
  return browser_compositor_->GetDelegatedFrameHost()->GetCurrentSurfaceId();
}

void RenderWidgetHostViewIOS::InitAsPopup(
    RenderWidgetHostView* parent_host_view,
    const gfx::Rect& pos,
    const gfx::Rect& anchor_rect) {}
void RenderWidgetHostViewIOS::UpdateCursor(const ui::Cursor& cursor) {}
void RenderWidgetHostViewIOS::SetIsLoading(bool is_loading) {}

void RenderWidgetHostViewIOS::RenderProcessGone() {
  Destroy();
}

void RenderWidgetHostViewIOS::ShowWithVisibility(
    PageVisibilityState page_visibility) {
  if (IsTesting() && !is_visible_) {
    // There is some circularity in how UpdateScreenInfo works. The base class
    // sets up some state needed by the browser compositor. The base class also
    // depends on an update from the browser compositor. In practice this is a
    // non issue because the function is called many times and values converge,
    // but this is not necessarily the case in tests. This could be resolved
    // by rewriting UpdateScreenInfo to interleave the work (see the mac
    // implementation, eg), but for now we will simply may another call to the
    // base class.
    RenderWidgetHostViewBase::UpdateScreenInfo();
    UpdateScreenInfo();
  }
  is_visible_ = true;
  browser_compositor_->SetViewVisible(is_visible_);
  OnShowWithPageVisibility(page_visibility);
}

void RenderWidgetHostViewIOS::NotifyHostAndDelegateOnWasShown(
    blink::mojom::RecordContentToVisibleTimeRequestPtr visible_time_request) {
  browser_compositor_->SetRenderWidgetHostIsHidden(false);

  host()->WasShown(blink::mojom::RecordContentToVisibleTimeRequestPtr());
}

void RenderWidgetHostViewIOS::Hide() {
  is_visible_ = false;
  browser_compositor_->SetViewVisible(is_visible_);
  browser_compositor_->SetRenderWidgetHostIsHidden(true);
  if (!host() || host()->is_hidden()) {
    return;
  }

  // Inform the renderer that we are being hidden so it can reduce its resource
  // utilization.
  host()->WasHidden();
}

bool RenderWidgetHostViewIOS::IsShowing() {
  return is_visible_ && [ui_view_->view_ window];
}

gfx::Rect RenderWidgetHostViewIOS::GetBoundsInRootWindow() {
  return GetViewBounds();
}

gfx::Size RenderWidgetHostViewIOS::GetRequestedRendererSize() {
  return !IsTesting() ? browser_compositor_->GetRendererSize()
                      : GetViewBounds().size();
}

absl::optional<DisplayFeature> RenderWidgetHostViewIOS::GetDisplayFeature() {
  return absl::nullopt;
}
void RenderWidgetHostViewIOS::SetDisplayFeatureForTesting(
    const DisplayFeature* display_feature) {}
void RenderWidgetHostViewIOS::UpdateBackgroundColor() {}

void RenderWidgetHostViewIOS::
    RequestSuccessfulPresentationTimeFromHostOrDelegate(
        blink::mojom::RecordContentToVisibleTimeRequestPtr
            visible_time_request) {
  host()->RequestSuccessfulPresentationTimeForNextFrame(
      std::move(visible_time_request));
}
void RenderWidgetHostViewIOS::
    CancelSuccessfulPresentationTimeRequestForHostAndDelegate() {
  host()->CancelSuccessfulPresentationTimeRequest();
}

SkColor RenderWidgetHostViewIOS::BrowserCompositorIOSGetGutterColor() {
  // When making an element on the page fullscreen the element's background
  // may not match the page's, so use black as the gutter color to avoid
  // flashes of brighter colors during the transition.
  if (host()->delegate() && host()->delegate()->IsFullscreen()) {
    return SK_ColorBLACK;
  }
  if (GetBackgroundColor()) {
    return *GetBackgroundColor();
  }
  return SK_ColorWHITE;
}

bool RenderWidgetHostViewIOS::OnBrowserCompositorSurfaceIdChanged() {
  return host()->SynchronizeVisualProperties();
}

void RenderWidgetHostViewIOS::OnFrameTokenChanged(
    uint32_t frame_token,
    base::TimeTicks activation_time) {
  OnFrameTokenChangedForView(frame_token, activation_time);
}

std::vector<viz::SurfaceId>
RenderWidgetHostViewIOS::CollectSurfaceIdsForEviction() {
  return {};
}

void RenderWidgetHostViewIOS::UpdateScreenInfo() {
  browser_compositor_->UpdateSurfaceFromUIView(GetViewBounds().size());
  RenderWidgetHostViewBase::UpdateScreenInfo();
}

void RenderWidgetHostViewIOS::OnSynchronizedDisplayPropertiesChanged(
    bool rotation) {
  host()->SynchronizeVisualProperties();
}

void RenderWidgetHostViewIOS::UpdateCALayerTree(
    const gfx::CALayerParams& ca_layer_params) {
  DCHECK(display_tree_);
  display_tree_->UpdateCALayerTree(ca_layer_params);
}

void RenderWidgetHostViewIOS::DidNavigate() {
  browser_compositor_->DidNavigate();
}

viz::ScopedSurfaceIdAllocator
RenderWidgetHostViewIOS::DidUpdateVisualProperties(
    const cc::RenderFrameMetadata& metadata) {
  base::OnceCallback<void()> allocation_task = base::BindOnce(
      base::IgnoreResult(
          &RenderWidgetHostViewIOS::OnDidUpdateVisualPropertiesComplete),
      weak_factory_.GetWeakPtr(), metadata);
  return browser_compositor_->GetScopedRendererSurfaceIdAllocator(
      std::move(allocation_task));
}

void RenderWidgetHostViewIOS::OnDidUpdateVisualPropertiesComplete(
    const cc::RenderFrameMetadata& metadata) {
  browser_compositor_->UpdateSurfaceFromChild(
      host()->auto_resize_enabled(), metadata.device_scale_factor,
      metadata.viewport_size_in_pixels,
      metadata.local_surface_id.value_or(viz::LocalSurfaceId()));
}

void RenderWidgetHostViewIOS::ClearFallbackSurfaceForCommitPending() {
  browser_compositor_->GetDelegatedFrameHost()
      ->ClearFallbackSurfaceForCommitPending();
  browser_compositor_->InvalidateLocalSurfaceIdOnEviction();
}

void RenderWidgetHostViewIOS::ResetFallbackToFirstNavigationSurface() {
  browser_compositor_->GetDelegatedFrameHost()
      ->ResetFallbackToFirstNavigationSurface();
}

bool RenderWidgetHostViewIOS::RequestRepaintForTesting() {
  return browser_compositor_->ForceNewSurfaceId();
}

void RenderWidgetHostViewIOS::TransformPointToRootSurface(gfx::PointF* point) {
  browser_compositor_->TransformPointToRootSurface(point);
}

bool RenderWidgetHostViewIOS::TransformPointToCoordSpaceForView(
    const gfx::PointF& point,
    RenderWidgetHostViewBase* target_view,
    gfx::PointF* transformed_point) {
  if (target_view == this) {
    *transformed_point = point;
    return true;
  }

  return target_view->TransformPointToLocalCoordSpace(
      point, GetCurrentSurfaceId(), transformed_point);
}

display::ScreenInfo RenderWidgetHostViewIOS::GetCurrentScreenInfo() const {
  return screen_infos_.current();
}

void RenderWidgetHostViewIOS::SetCurrentDeviceScaleFactor(
    float device_scale_factor) {
  // TODO(https://crbug.com/1337094): does this need to be upscaled by
  // scale_override_for_capture_ for HiDPI capture mode?
  screen_infos_.mutable_current().device_scale_factor = device_scale_factor;
}

void RenderWidgetHostViewIOS::SetActive(bool active) {
  if (host()) {
    UpdateActiveState(active);
    if (active) {
      if (HasFocus()) {
        host()->Focus();
      }
    } else {
      host()->Blur();
    }
  }
  // if (HasFocus())
  //  SetTextInputActive(active);
  if (!active) {
    UnlockMouse();
  }
}

bool RenderWidgetHostViewIOS::ShouldRouteEvents() const {
  DCHECK(host());
  return host()->delegate() && host()->delegate()->GetInputEventRouter();
}

void RenderWidgetHostViewIOS::OnTouchEvent(blink::WebTouchEvent web_event) {
  ui::FilteredGestureProvider::TouchHandlingResult result =
      gesture_provider_.OnTouchEvent(MotionEventWeb(web_event));
  if (!result.succeeded) {
    return;
  }

  web_event.moved_beyond_slop_region = result.moved_beyond_slop_region;
  ui::LatencyInfo latency_info(ui::SourceEventType::TOUCH);
  latency_info.AddLatencyNumber(ui::INPUT_EVENT_LATENCY_UI_COMPONENT);
  if (ShouldRouteEvents()) {
    host()->delegate()->GetInputEventRouter()->RouteTouchEvent(this, &web_event,
                                                               latency_info);
  } else {
    host()->ForwardTouchEventWithLatencyInfo(web_event, latency_info);
  }
}

void RenderWidgetHostViewIOS::ProcessAckedTouchEvent(
    const TouchEventWithLatencyInfo& touch,
    blink::mojom::InputEventResultState ack_result) {
  const bool event_consumed =
      ack_result == blink::mojom::InputEventResultState::kConsumed;
  gesture_provider_.OnTouchEventAck(
      touch.event.unique_touch_event_id, event_consumed,
      InputEventResultStateIsSetNonBlocking(ack_result));
  if (touch.event.touch_start_or_first_touch_move && event_consumed &&
      ShouldRouteEvents()) {
    host()
        ->delegate()
        ->GetInputEventRouter()
        ->OnHandledTouchStartOrFirstTouchMove(
            touch.event.unique_touch_event_id);
  }
}

void RenderWidgetHostViewIOS::OnGestureEvent(
    const ui::GestureEventData& gesture) {
  if ((gesture.type() == ui::ET_GESTURE_PINCH_BEGIN ||
       gesture.type() == ui::ET_GESTURE_PINCH_UPDATE ||
       gesture.type() == ui::ET_GESTURE_PINCH_END) &&
      !IsPinchToZoomEnabled()) {
    return;
  }

  blink::WebGestureEvent web_gesture =
      ui::CreateWebGestureEventFromGestureEventData(gesture);
  SendGestureEvent(web_gesture);
}

bool RenderWidgetHostViewIOS::RequiresDoubleTapGestureEvents() const {
  return true;
}

void RenderWidgetHostViewIOS::SendGestureEvent(
    const blink::WebGestureEvent& event) {
  ui::LatencyInfo latency_info =
      ui::WebInputEventTraits::CreateLatencyInfoForWebGestureEvent(event);
  InjectGestureEvent(event, latency_info);
}

void RenderWidgetHostViewIOS::InjectTouchEvent(
    const blink::WebTouchEvent& event,
    const ui::LatencyInfo& latency_info) {
  ui::FilteredGestureProvider::TouchHandlingResult result =
      gesture_provider_.OnTouchEvent(MotionEventWeb(event));
  if (!result.succeeded) {
    return;
  }

  if (ShouldRouteEvents()) {
    blink::WebTouchEvent touch_event(event);
    host()->delegate()->GetInputEventRouter()->RouteTouchEvent(
        this, &touch_event, latency_info);
  } else {
    host()->ForwardTouchEventWithLatencyInfo(event, latency_info);
  }
}

void RenderWidgetHostViewIOS::InjectGestureEvent(
    const blink::WebGestureEvent& event,
    const ui::LatencyInfo& latency_info) {
  if (ShouldRouteEvents()) {
    blink::WebGestureEvent gesture_event(event);
    host()->delegate()->GetInputEventRouter()->RouteGestureEvent(
        this, &gesture_event, latency_info);
  } else {
    host()->ForwardGestureEventWithLatencyInfo(event, latency_info);
  }
}

void RenderWidgetHostViewIOS::InjectMouseEvent(
    const blink::WebMouseEvent& web_mouse,
    const ui::LatencyInfo& latency_info) {
  NOTIMPLEMENTED();
}

void RenderWidgetHostViewIOS::InjectMouseWheelEvent(
    const blink::WebMouseWheelEvent& web_wheel,
    const ui::LatencyInfo& latency_info) {
  NOTIMPLEMENTED();
}

bool RenderWidgetHostViewIOS::CanBecomeFirstResponderForTesting() const {
  return IsTesting() && !is_first_responder_ && is_getting_focus_;
}

bool RenderWidgetHostViewIOS::CanResignFirstResponderForTesting() const {
  return IsTesting() && is_first_responder_;
}

void RenderWidgetHostViewIOS::UpdateNativeViewTree(gfx::NativeView view) {
  if (view) {
    [ui_view_->view_ updateView:(UIScrollView*)view.Get()];
  } else {
    [ui_view_->view_ removeView];
  }
}

void RenderWidgetHostViewIOS::ImeSetComposition(
    const std::u16string& text,
    const std::vector<ui::ImeTextSpan>& spans,
    const gfx::Range& replacement_range,
    int selection_start,
    int selection_end) {
  if (auto* widget_host = GetActiveWidget()) {
    widget_host->ImeSetComposition(text, spans, replacement_range,
                                   selection_start, selection_end);
  }
}

void RenderWidgetHostViewIOS::ImeCommitText(const std::u16string& text,
                                            const gfx::Range& replacement_range,
                                            int relative_position) {
  if (auto* widget_host = GetActiveWidget()) {
    widget_host->ImeCommitText(text, std::vector<ui::ImeTextSpan>(),
                               replacement_range, relative_position);
  }
}

void RenderWidgetHostViewIOS::ImeFinishComposingText(bool keep_selection) {
  if (auto* widget_host = GetActiveWidget()) {
    widget_host->ImeFinishComposingText(keep_selection);
  }
}

RenderWidgetHostImpl* RenderWidgetHostViewIOS::GetActiveWidget() {
  return text_input_manager_ ? text_input_manager_->GetActiveWidget() : nullptr;
}

void RenderWidgetHostViewIOS::OnFirstResponderChanged() {
  bool is_first_responder = [ui_view_->view_ isFirstResponder] ||
                            [[ui_view_->view_ textInput] isFirstResponder] ||
                            (IsTesting() && is_getting_focus_);

  if (is_first_responder_ == is_first_responder) {
    return;
  }
  is_first_responder_ = is_first_responder;

  if (is_first_responder_) {
    host()->GotFocus();
  } else {
    host()->LostFocus();
  }
}

void RenderWidgetHostViewIOS::OnUpdateTextInputStateCalled(
    TextInputManager* text_input_manager,
    RenderWidgetHostViewBase* updated_view,
    bool did_update_state) {
  if (text_input_manager->GetActiveWidget()) {
    [[ui_view_->view_ textInput]
        onUpdateTextInputState:*text_input_manager->GetTextInputState()
                    withBounds:[ui_view_->view_ bounds]];
  } else {
    // If there are no active widgets, the TextInputState.type should be
    // reported as none.
    [[ui_view_->view_ textInput]
        onUpdateTextInputState:ui::mojom::TextInputState()
                    withBounds:[ui_view_->view_ bounds]];
  }
}

ui::Compositor* RenderWidgetHostViewIOS::GetCompositor() {
  return browser_compositor_->GetCompositor();
}

void RenderWidgetHostViewIOS::GestureEventAck(
    const blink::WebGestureEvent& event,
    blink::mojom::InputEventResultState ack_result,
    blink::mojom::ScrollResultDataPtr scroll_result_data) {
  UIScrollView* scrollView = (UIScrollView*)[ui_view_->view_ superview];
  switch (event.GetType()) {
    case blink::WebInputEvent::Type::kGestureScrollBegin:
      is_scrolling_ = true;
      [[scrollView delegate] scrollViewWillBeginDragging:scrollView];
      break;
    case blink::WebInputEvent::Type::kGestureScrollUpdate:
      if (scroll_result_data && scroll_result_data->root_scroll_offset) {
        ApplyRootScrollOffsetChanged(*scroll_result_data->root_scroll_offset);
      }
      break;
    case blink::WebInputEvent::Type::kGestureScrollEnd: {
      is_scrolling_ = false;
      CGPoint targetOffset = [scrollView contentOffset];
      [[scrollView delegate] scrollViewWillEndDragging:scrollView
                                          withVelocity:CGPoint()
                                   targetContentOffset:&targetOffset];
      [[scrollView delegate] scrollViewDidEndDragging:scrollView
                                       willDecelerate:NO];
      host()->SynchronizeVisualProperties();
      break;
    }
    default:
      break;
  }
}

void RenderWidgetHostViewIOS::ChildDidAckGestureEvent(
    const blink::WebGestureEvent& event,
    blink::mojom::InputEventResultState ack_result,
    blink::mojom::ScrollResultDataPtr scroll_result_data) {
  if (scroll_result_data && scroll_result_data->root_scroll_offset) {
    ApplyRootScrollOffsetChanged(*scroll_result_data->root_scroll_offset);
  }
}

void RenderWidgetHostViewIOS::ApplyRootScrollOffsetChanged(
    const gfx::PointF& root_scroll_offset) {
  UIScrollView* scrollView = (UIScrollView*)[ui_view_->view_ superview];
  gfx::PointF scrollOffset = root_scroll_offset;
  UIEdgeInsets insets = [scrollView contentInset];
  scrollOffset.Offset(insets.left, insets.top);
  CGRect parentBounds = [[ui_view_->view_ superview] bounds];
  gfx::SizeF viewportSize(parentBounds.size);

  // Adjust the viewport so that it doesn't overhang the screen when the
  // min controls are shown, otherwise we won't be able to scroll to all
  // the content.
  RenderViewHostDelegateView* rvh_delegate_view =
      host()->delegate()->GetDelegateView();
  viewportSize.Enlarge(0, -(rvh_delegate_view->GetTopControlsMinHeight() +
                            rvh_delegate_view->GetBottomControlsMinHeight()));
  CGRect frameBounds;
  frameBounds.origin = scrollOffset.ToCGPoint();
  frameBounds.size = viewportSize.ToCGSize();

  [ui_view_->view_ setFrame:frameBounds];
  if (last_root_scroll_offset_ != root_scroll_offset) {
    [scrollView setContentOffset:root_scroll_offset.ToCGPoint()];
    last_root_scroll_offset_ = root_scroll_offset;
    [[scrollView delegate] scrollViewDidScroll:scrollView];
  }
}

void RenderWidgetHostViewIOS::OnRenderFrameMetadataChangedBeforeActivation(
    const cc::RenderFrameMetadata& metadata) {
  UIScrollView* scrollView = (UIScrollView*)[ui_view_->view_ superview];
  CGSize newContentSize = metadata.root_layer_size.ToCGSize();
  if (!CGSizeEqualToSize([scrollView contentSize], newContentSize)) {
    [scrollView setContentSize:newContentSize];
  }
  if (metadata.root_scroll_offset) {
    ApplyRootScrollOffsetChanged(*metadata.root_scroll_offset);
  }
}

void RenderWidgetHostViewIOS::ContentInsetChanged() {
  if (last_root_scroll_offset_) {
    ApplyRootScrollOffsetChanged(*last_root_scroll_offset_);
  }
  if (!is_scrolling_) {
    host()->SynchronizeVisualProperties();
  }
}

}  // namespace content
