// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <wayland-server-protocol-core.h>
#include "ash/test/test_widget_builder.h"
#include "base/memory/raw_ptr.h"
#include "components/exo/display.h"
#include "components/exo/wayland/test/client_util.h"
#include "components/exo/wayland/test/server_util.h"
#include "components/exo/wayland/test/wayland_server_test.h"
#include "components/exo/wayland/xdg_shell.h"
#include "components/exo/xdg_shell_surface.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace exo::wayland {
namespace {

class ClientData : public test::TestClient::CustomData {
 public:
  struct TestShellSurface {
    std::unique_ptr<wl_surface> surface;
    std::unique_ptr<wl_shell_surface> shell_surface;
    std::unique_ptr<xdg_toplevel> xdg_toplevel;
    std::unique_ptr<xdg_surface> xdg_surface;
  };
  std::vector<TestShellSurface> test_surfaces_list;
};

class WaylandAuraShellServerTest : public test::WaylandServerTest {
 public:
  struct TestSurfaceKey {
    test::ResourceKey surface_key;
    test::ResourceKey shell_surface_key;
  };

  WaylandAuraShellServerTest() = default;

  WaylandAuraShellServerTest(const WaylandAuraShellServerTest&) = delete;
  WaylandAuraShellServerTest& operator=(const WaylandAuraShellServerTest&) =
      delete;

  ~WaylandAuraShellServerTest() override = default;

  // test::WaylandServerTest:
  void SetUp() override {
    WaylandServerTest::SetUp();
    display_ = server_->GetDisplay();
  }

  void TearDown() override { WaylandServerTest::TearDown(); }

  std::vector<TestSurfaceKey> SetupClientSurfaces(
      int num_test_surfaces_list = 1) {
    std::vector<TestSurfaceKey> keys;

    PostToClientAndWait([&](test::TestClient* client) {
      auto data = std::make_unique<ClientData>();
      for (int i = 0; i < num_test_surfaces_list; ++i) {
        ClientData::TestShellSurface test_surface;
        test_surface.surface.reset(
            wl_compositor_create_surface(client->compositor()));

        test_surface.xdg_surface.reset(xdg_wm_base_get_xdg_surface(
            client->globals().xdg_wm_base.get(), test_surface.surface.get()));
        test_surface.xdg_toplevel.reset(
            xdg_surface_get_toplevel(test_surface.xdg_surface.get()));

        keys.push_back(TestSurfaceKey{
            .surface_key =
                test::client_util::GetResourceKey(test_surface.surface.get()),
            .shell_surface_key = test::client_util::GetResourceKey(
                test_surface.xdg_surface.get()),
        });
        data->test_surfaces_list.push_back(std::move(test_surface));
      }
      client->set_data(std::move(data));
    });

    return keys;
  }

  void AttachBufferToSurfaces() {
    PostToClientAndWait([&](test::TestClient* client) {
      ASSERT_TRUE(client->InitShmBufferFactory(256 * 256 * 4));

      auto* data = client->GetDataAs<ClientData>();
      for (auto& test_surface : data->test_surfaces_list) {
        auto buffer = client->shm_buffer_factory()->CreateBuffer(0, 64, 64);
        wl_surface_attach(test_surface.surface.get(), buffer->resource(), 0, 0);
        wl_surface_commit(test_surface.surface.get());
      }
    });
  }

  struct FocusObserver {
    raw_ptr<wl_surface, ExperimentalAsh> gained_active;
    raw_ptr<wl_surface, ExperimentalAsh> lost_active;
    int32_t activated_call_count = 0;
  };

  const zaura_shell_listener kAuraShellListener = {
      [](void* data, struct zaura_shell* zaura_shell, uint32_t layout_mode) {},
      [](void* data, struct zaura_shell* zaura_shell, uint32_t id) {},
      [](void* data,
         struct zaura_shell* zaura_shell,
         struct wl_array* desk_names) {},
      [](void* data,
         struct zaura_shell* zaura_shell,
         int32_t active_desk_index) {},
      [](void* data,
         struct zaura_shell* zaura_shell,
         struct wl_surface* gained_active,
         struct wl_surface* lost_active) {
        auto* observer = static_cast<FocusObserver*>(data);
        observer->gained_active = gained_active;
        observer->lost_active = lost_active;
        observer->activated_call_count++;
      }};

  std::unique_ptr<FocusObserver> SetupFocusObservation() {
    auto observer = std::make_unique<FocusObserver>();
    PostToClientAndWait([&](test::TestClient* client) {
      zaura_shell_add_listener(client->aura_shell(), &kAuraShellListener,
                               observer.get());
    });
    return observer;
  }

  Surface* GetClientSurface(test::ResourceKey surface_key) {
    return test::server_util::GetUserDataForResource<Surface>(server_.get(),
                                                              surface_key);
  }

  raw_ptr<Display, ExperimentalAsh> display_;
};

// Home screen -> any window
TEST_F(WaylandAuraShellServerTest, HasFocusedClientChangedSendActivated) {
  auto keys = SetupClientSurfaces();
  auto observer = SetupFocusObservation();

  Surface* surface = GetClientSurface(keys[0].surface_key);
  ASSERT_TRUE(surface);

  display_->seat()->OnWindowFocused(surface->window(), nullptr);
  // Wait until all wayland events are sent.
  PostToClientAndWait([]() {});
  EXPECT_TRUE(observer->gained_active != nullptr);
  EXPECT_TRUE(observer->lost_active == nullptr);
  EXPECT_EQ(1, observer->activated_call_count);
}

// Exo client window -> Same exo client another window
TEST_F(WaylandAuraShellServerTest, FocusedClientChangedSendActivated) {
  auto keys = SetupClientSurfaces(2);
  auto observer = SetupFocusObservation();

  Surface* surface = GetClientSurface(keys[0].surface_key);
  ASSERT_TRUE(surface);

  display_->seat()->OnWindowFocused(surface->window(), nullptr);
  // Reset previous gained and lost active info.
  observer->gained_active = nullptr;
  observer->lost_active = nullptr;

  Surface* surface2 = GetClientSurface(keys[1].surface_key);
  ASSERT_TRUE(surface2);
  display_->seat()->OnWindowFocused(surface2->window(), surface->window());
  // Wait until all wayland events are sent.
  PostToClientAndWait([]() {});

  EXPECT_TRUE(observer->gained_active != nullptr);
  EXPECT_TRUE(observer->lost_active != nullptr);
  EXPECT_EQ(2, observer->activated_call_count);
}

// Exo client window -> Chrome window
TEST_F(WaylandAuraShellServerTest, FocusedClientChangedToNonExoSendActivated) {
  auto keys = SetupClientSurfaces(2);
  auto observer = SetupFocusObservation();

  Surface* surface = GetClientSurface(keys[0].surface_key);
  ASSERT_TRUE(surface);
  display_->seat()->OnWindowFocused(surface->window(), nullptr);

  // Reset previous gained and lost active info.
  observer->gained_active = nullptr;
  observer->lost_active = nullptr;

  Surface* surface2 = GetClientSurface(keys[1].surface_key);
  ASSERT_TRUE(surface2);
  // Chrome surface doesn't have wayland resource.
  SetSurfaceResource(surface2, nullptr);
  display_->seat()->OnWindowFocused(surface2->window(), surface->window());
  // Wait until all wayland events are sent.
  PostToClientAndWait([]() {});

  EXPECT_TRUE(observer->gained_active == nullptr);
  EXPECT_TRUE(observer->lost_active != nullptr);
  EXPECT_EQ(2, observer->activated_call_count);
}

// Chrome window -> Chrome window
TEST_F(WaylandAuraShellServerTest,
       NonExoFocusedClientChangedNotSendingActivated) {
  auto keys = SetupClientSurfaces(2);
  auto observer = SetupFocusObservation();

  Surface* surface = GetClientSurface(keys[0].surface_key);
  ASSERT_TRUE(surface);

  // Chrome surface doesn't have wayland resource.
  SetSurfaceResource(surface, nullptr);
  display_->seat()->OnWindowFocused(surface->window(), nullptr);

  // Reset previous gained and lost active info.
  observer->gained_active = nullptr;
  observer->lost_active = nullptr;

  Surface* surface2 = GetClientSurface(keys[1].surface_key);
  ASSERT_TRUE(surface2);
  // Chrome surface doesn't have wayland resource.
  SetSurfaceResource(surface2, nullptr);
  display_->seat()->OnWindowFocused(surface2->window(), surface->window());
  // Wait until all wayland events are sent.
  PostToClientAndWait([]() {});

  EXPECT_EQ(nullptr, observer->gained_active.get());
  EXPECT_EQ(nullptr, observer->lost_active.get());
  EXPECT_EQ(1, observer->activated_call_count);
}

TEST_F(WaylandAuraShellServerTest, RotateFocus) {
  auto keys = SetupClientSurfaces();
  AttachBufferToSurfaces();

  struct RotateFocusListener {
    uint32_t last_received_serial;
    uint32_t last_received_direction;
    uint32_t last_received_restart;
  };
  RotateFocusListener listener;

  zaura_toplevel_listener listeners = {
      [](void*, zaura_toplevel*, int32_t, int32_t, int32_t, int32_t,
         wl_array*) {},
      [](void*, zaura_toplevel*, int32_t, int32_t) {},
      [](void*, zaura_toplevel*, uint32_t) {},
      [](void* data, zaura_toplevel*, uint32_t serial, uint32_t direction,
         uint32_t restart) {
        auto* listener = static_cast<RotateFocusListener*>(data);
        listener->last_received_serial = serial;
        listener->last_received_direction = direction;
        listener->last_received_restart = restart;
      },
  };

  std::unique_ptr<zaura_toplevel> zaura_toplevel;

  PostToClientAndWait([&](test::TestClient* client) {
    auto* data = client->GetDataAs<ClientData>();

    zaura_toplevel.reset(zaura_shell_get_aura_toplevel_for_xdg_toplevel(
        client->globals().aura_shell.get(),
        data->test_surfaces_list[0].xdg_toplevel.get()));
    zaura_toplevel_add_listener(zaura_toplevel.get(), &listeners, &listener);
    zaura_toplevel_set_supports_screen_coordinates(zaura_toplevel.get());
  });

  auto* xdg_surface =
      test::server_util::GetUserDataForResource<WaylandXdgSurface>(
          server_.get(), keys[0].shell_surface_key);
  ASSERT_TRUE(xdg_surface);
  XdgShellSurface* shell_surface = xdg_surface->shell_surface.get();
  ASSERT_TRUE(shell_surface);

  PostToClientAndWait([]() {});

  // Serial should be increasing.
  uint32_t received_serial = 0;

  shell_surface->RotatePaneFocusFromView(nullptr, true, true);
  PostToClientAndWait([]() {});
  EXPECT_EQ(ZAURA_TOPLEVEL_ROTATE_DIRECTION_FORWARD,
            listener.last_received_direction);
  EXPECT_EQ(ZAURA_TOPLEVEL_ROTATE_RESTART_STATE_RESTART,
            listener.last_received_restart);
  // No assertion for serial on the first run. We just need to ensure that it
  // increases next time.
  received_serial = listener.last_received_serial;

  shell_surface->RotatePaneFocusFromView(nullptr, false, true);
  PostToClientAndWait([]() {});
  EXPECT_GT(listener.last_received_serial, received_serial);
  EXPECT_EQ(ZAURA_TOPLEVEL_ROTATE_DIRECTION_BACKWARD,
            listener.last_received_direction);
  EXPECT_EQ(ZAURA_TOPLEVEL_ROTATE_RESTART_STATE_RESTART,
            listener.last_received_restart);
  received_serial = listener.last_received_serial;

  shell_surface->RotatePaneFocusFromView(nullptr, true, false);
  PostToClientAndWait([]() {});
  EXPECT_GT(listener.last_received_serial, received_serial);
  EXPECT_EQ(ZAURA_TOPLEVEL_ROTATE_DIRECTION_FORWARD,
            listener.last_received_direction);
  EXPECT_EQ(ZAURA_TOPLEVEL_ROTATE_RESTART_STATE_NO_RESTART,
            listener.last_received_restart);
  received_serial = listener.last_received_serial;
}

TEST_F(WaylandAuraShellServerTest, AckRotateFocus) {
  auto keys = SetupClientSurfaces();
  AttachBufferToSurfaces();

  auto native_widget1 = ash::TestWidgetBuilder().BuildOwnsNativeWidget();
  auto native_widget2 = ash::TestWidgetBuilder().BuildOwnsNativeWidget();

  std::unique_ptr<zaura_toplevel> zaura_toplevel;

  PostToClientAndWait([&](test::TestClient* client) {
    auto* data = client->GetDataAs<ClientData>();
    zaura_toplevel.reset(zaura_shell_get_aura_toplevel_for_xdg_toplevel(
        client->globals().aura_shell.get(),
        data->test_surfaces_list[0].xdg_toplevel.get()));
    zaura_toplevel_set_supports_screen_coordinates(zaura_toplevel.get());
  });

  uint32_t serial = 0;

  WaylandXdgSurface* xdg_surface =
      test::server_util::GetUserDataForResource<WaylandXdgSurface>(
          server_.get(), keys[0].shell_surface_key);
  ASSERT_TRUE(xdg_surface);
  xdg_surface->shell_surface->set_rotate_focus_callback(
      base::BindLambdaForTesting(
          [&serial](ash::FocusCycler::Direction, bool) { return serial; }));

  auto* focus_cycler = ash::Shell::Get()->focus_cycler();
  focus_cycler->AddWidget(native_widget1.get());
  focus_cycler->AddWidget(xdg_surface->shell_surface->GetWidget());
  focus_cycler->AddWidget(native_widget2.get());

  focus_cycler->FocusWidget(xdg_surface->shell_surface->GetWidget());
  ASSERT_TRUE(xdg_surface->shell_surface->GetWidget()->IsActive());

  // Handled should result in no change.
  xdg_surface->shell_surface->RotatePaneFocusFromView(nullptr, true, true);
  PostToClientAndWait([&](test::TestClient* client) {
    zaura_toplevel_ack_rotate_focus(
        zaura_toplevel.get(), serial++,
        ZAURA_TOPLEVEL_ROTATE_HANDLED_STATE_HANDLED);
  });
  EXPECT_TRUE(xdg_surface->shell_surface->GetWidget()->IsActive());

  // Unhandled should result in a rotation forward.
  xdg_surface->shell_surface->RotatePaneFocusFromView(nullptr, true, true);
  PostToClientAndWait([&](test::TestClient* client) {
    zaura_toplevel_ack_rotate_focus(
        zaura_toplevel.get(), serial++,
        ZAURA_TOPLEVEL_ROTATE_HANDLED_STATE_NOT_HANDLED);
  });
  EXPECT_TRUE(native_widget2->IsActive());

  // Reset
  focus_cycler->FocusWidget(xdg_surface->shell_surface->GetWidget());
  ASSERT_TRUE(xdg_surface->shell_surface->GetWidget()->IsActive());

  // Unhandled should result in a rotation backward.
  xdg_surface->shell_surface->RotatePaneFocusFromView(nullptr, false, true);
  PostToClientAndWait([&](test::TestClient* client) {
    zaura_toplevel_ack_rotate_focus(
        zaura_toplevel.get(), serial++,
        ZAURA_TOPLEVEL_ROTATE_HANDLED_STATE_NOT_HANDLED);
  });
  EXPECT_TRUE(native_widget1->IsActive());
}

}  // namespace
}  // namespace exo::wayland
