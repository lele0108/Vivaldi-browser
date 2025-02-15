// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview
 * 'per-device-keyboard-settings-remap-keys' displays the remapped keys and
 * allow users to configure their keyboard remapped keys for each keyboard.
 */

import 'chrome://resources/cr_components/settings_prefs/prefs.js';
import '../icons.html.js';
import '../settings_shared.css.js';
import '/shared/settings/controls/settings_dropdown_menu.js';
import './input_device_settings_shared.css.js';
import './keyboard_remap_modifier_key_row.js';

import {I18nMixin, I18nMixinInterface} from 'chrome://resources/cr_elements/i18n_mixin.js';
import {assert} from 'chrome://resources/js/assert_ts.js';
import {loadTimeData} from 'chrome://resources/js/load_time_data.js';
import {PolymerElementProperties} from 'chrome://resources/polymer/v3_0/polymer/interfaces.js';
import {PolymerElement} from 'chrome://resources/polymer/v3_0/polymer/polymer_bundled.min.js';

import {RouteObserverMixin, RouteObserverMixinInterface} from '../route_observer_mixin.js';
import {Route, Router, routes} from '../router.js';

import {getInputDeviceSettingsProvider} from './input_device_mojo_interface_provider.js';
import {InputDeviceSettingsProviderInterface, Keyboard, MetaKey, ModifierKey} from './input_device_settings_types.js';
import {getTemplate} from './per_device_keyboard_remap_keys.html.js';

const SettingsPerDeviceKeyboardRemapKeysElementBase =
    RouteObserverMixin(I18nMixin(PolymerElement)) as {
      new (): PolymerElement & I18nMixinInterface & RouteObserverMixinInterface,
    };

export class SettingsPerDeviceKeyboardRemapKeysElement extends
    SettingsPerDeviceKeyboardRemapKeysElementBase {
  static get is(): string {
    return 'settings-per-device-keyboard-remap-keys';
  }

  static get template(): HTMLTemplateElement {
    return getTemplate();
  }

  static get properties(): PolymerElementProperties {
    return {
      fakeMetaPref: {
        type: Object,
        value() {
          return {
            key: 'fakeMetaKeyRemapPref',
            type: chrome.settingsPrivate.PrefType.NUMBER,
            value: ModifierKey.kMeta,
          };
        },
      },

      fakeCtrlPref: {
        type: Object,
        value() {
          return {
            key: 'fakeCtrlKeyRemapPref',
            type: chrome.settingsPrivate.PrefType.NUMBER,
            value: ModifierKey.kControl,
          };
        },
      },

      fakeAltPref: {
        type: Object,
        value() {
          return {
            key: 'fakeAltKeyRemapPref',
            type: chrome.settingsPrivate.PrefType.NUMBER,
            value: ModifierKey.kAlt,
          };
        },
      },

      fakeEscPref: {
        type: Object,
        value() {
          return {
            key: 'fakeEscKeyRemapPref',
            type: chrome.settingsPrivate.PrefType.NUMBER,
            value: ModifierKey.kEscape,
          };
        },
      },

      fakeBackspacePref: {
        type: Object,
        value() {
          return {
            key: 'fakeBackspaceKeyRemapPref',
            type: chrome.settingsPrivate.PrefType.NUMBER,
            value: ModifierKey.kBackspace,
          };
        },
      },

      fakeAssistantPref: {
        type: Object,
        value() {
          return {
            key: 'fakeAssistantKeyRemapPref',
            type: chrome.settingsPrivate.PrefType.NUMBER,
            value: ModifierKey.kAssistant,
          };
        },
      },

      fakeCapsLockPref: {
        type: Object,
        value() {
          return {
            key: 'fakeCapsLockKeyRemapPref',
            type: chrome.settingsPrivate.PrefType.NUMBER,
            value: ModifierKey.kCapsLock,
          };
        },
      },

      hasAssistantKey: {
        type: Boolean,
        value: false,
      },

      hasCapsLockKey: {
        type: Boolean,
        value: false,
      },

      keyboard: {
        type: Object,
      },

      keyboards: {
        type: Array,
        // Prevents the `onKeyboardListUpdated` observer from firing
        // when the page is first initialized.
        value: undefined,
      },

      metaKeyLabel: {
        type: String,
      },

      defaultRemappings: {
        type: Object,
      },

      /**
       * Set it to false when the page is initializing and prefs are being
       * synced to match those in the keyboard's settings from the provider.
       * onSettingsChanged function shouldn't be called during the
       * initialization process.
       */
      isInitialized: {
        type: Boolean,
        value: false,
      },

      keyboardId: {
        type: Number,
        value: -1,
      },

      isAltClickAndSixPackCustomizationEnabled: {
        type: Boolean,
        value() {
          return loadTimeData.getBoolean(
              'enableAltClickAndSixPackCustomization');
        },
        readOnly: true,
      },
    };
  }

  static get observers(): string[] {
    return [
      'onSettingsChanged(fakeMetaPref.value,' +
          'fakeCtrlPref.value,' +
          'fakeAltPref.value,' +
          'fakeEscPref.value,' +
          'fakeBackspacePref.value,' +
          'fakeAssistantPref.value,' +
          'fakeCapsLockPref.value)',
      'onKeyboardListUpdated(keyboards.*)',
    ];
  }

  protected get modifierKey(): typeof ModifierKey {
    return ModifierKey;
  }

  keyboard: Keyboard;
  isAltClickAndSixPackCustomizationEnabled: boolean;
  private keyboards: Keyboard[];
  protected keyboardId: number;
  protected defaultRemappings: {[key: number]: ModifierKey} = {
    [ModifierKey.kMeta]: ModifierKey.kMeta,
    [ModifierKey.kControl]: ModifierKey.kControl,
    [ModifierKey.kAlt]: ModifierKey.kAlt,
    [ModifierKey.kEscape]: ModifierKey.kEscape,
    [ModifierKey.kBackspace]: ModifierKey.kBackspace,
    [ModifierKey.kAssistant]: ModifierKey.kAssistant,
    [ModifierKey.kCapsLock]: ModifierKey.kCapsLock,
  };
  private inputDeviceSettingsProvider: InputDeviceSettingsProviderInterface =
      getInputDeviceSettingsProvider();
  private fakeAltPref: chrome.settingsPrivate.PrefObject;
  private fakeAssistantPref: chrome.settingsPrivate.PrefObject;
  private fakeBackspacePref: chrome.settingsPrivate.PrefObject;
  private fakeCtrlPref: chrome.settingsPrivate.PrefObject;
  private fakeCapsLockPref: chrome.settingsPrivate.PrefObject;
  private fakeEscPref: chrome.settingsPrivate.PrefObject;
  private fakeMetaPref: chrome.settingsPrivate.PrefObject;
  private hasAssistantKey: boolean;
  private hasCapsLockKey: boolean;
  private metaKeyLabel: string;
  private isInitialized: boolean;

  override currentRouteChanged(route: Route): void {
    // Does not apply to this page.
    if (route !== routes.PER_DEVICE_KEYBOARD_REMAP_KEYS) {
      return;
    }
    if (this.hasKeyboards() &&
        this.keyboardId !== this.getKeyboardIdFromUrl()) {
      this.initializeKeyboard();
    }
  }

  private computeModifierRemappings(): Map<ModifierKey, ModifierKey> {
    const modifierRemappings: Map<ModifierKey, ModifierKey> = new Map();
    for (const modifier of Object.keys(
             this.keyboard.settings.modifierRemappings)) {
      const from: ModifierKey = Number(modifier);
      const to: ModifierKey|undefined =
          this.keyboard.settings.modifierRemappings[from];
      if (to === undefined) {
        continue;
      }
      modifierRemappings.set(from, to);
    }
    return modifierRemappings;
  }

  /**
   * Get the keyboard to display according to the keyboardId in the url query,
   * initializing the page and pref with the keyboard data.
   */
  private initializeKeyboard(): void {
    // Set isInitialized to false to prevent calling update keyboard settings
    // api while the prefs are initializing.
    this.isInitialized = false;
    this.keyboardId = this.getKeyboardIdFromUrl();
    const searchedKeyboard = this.keyboards.find(
        (keyboard: Keyboard) => keyboard.id === this.keyboardId);
    assert(!!searchedKeyboard);
    this.keyboard = searchedKeyboard;
    this.updateDefaultRemapping();
    this.initializePrefsToIdentity();

    // Assistant key and caps lock key are optional. Their values depend on
    // keyboard modifierKeys.
    this.hasAssistantKey =
        searchedKeyboard.modifierKeys.includes(ModifierKey.kAssistant);
    this.hasCapsLockKey =
        searchedKeyboard.modifierKeys.includes(ModifierKey.kCapsLock);

    // Update Prefs according to keyboard modifierRemappings.
    Array.from(this.computeModifierRemappings().keys())
        .forEach((originalKey: ModifierKey) => {
          this.setRemappedKey(originalKey);
        });
    this.isInitialized = true;
  }

  private keyboardWasDisconnected(id: number): boolean {
    return !this.keyboards.find(keyboard => keyboard.id === id);
  }

  onKeyboardListUpdated(): void {
    if (Router.getInstance().currentRoute !==
        routes.PER_DEVICE_KEYBOARD_REMAP_KEYS) {
      return;
    }

    if (!this.hasKeyboards() ||
        this.keyboardWasDisconnected(this.getKeyboardIdFromUrl())) {
      this.keyboardId = -1;
      Router.getInstance().navigateTo(routes.PER_DEVICE_KEYBOARD);
      return;
    }
    this.initializeKeyboard();
  }

  private defaultInitializePrefs(): void {
    this.set('fakeAltPref.value', this.defaultRemappings[ModifierKey.kAlt]);
    this.set(
        'fakeAssistantPref.value',
        this.defaultRemappings[ModifierKey.kAssistant]);
    this.set(
        'fakeBackspacePref.value',
        this.defaultRemappings[ModifierKey.kBackspace]);
    this.set(
        'fakeCtrlPref.value', this.defaultRemappings[ModifierKey.kControl]);
    this.set(
        'fakeCapsLockPref.value',
        this.defaultRemappings[ModifierKey.kCapsLock]);
    this.set('fakeEscPref.value', this.defaultRemappings[ModifierKey.kEscape]);
    this.set('fakeMetaPref.value', this.defaultRemappings[ModifierKey.kMeta]);
  }

  /**
   * Sets all prefs to the "identity" value which so they can be updated by the
   * values in the remappings map.
   */
  private initializePrefsToIdentity(): void {
    this.set('fakeAltPref.value', ModifierKey.kAlt);
    this.set('fakeAssitantPref.value', ModifierKey.kAssistant);
    this.set('fakeBackspacePref.value', ModifierKey.kBackspace);
    this.set('fakeCtrlPref.value', ModifierKey.kControl);
    this.set('fakeCapsLockPref.value', ModifierKey.kCapsLock);
    this.set('fakeEscPref.value', ModifierKey.kEscape);
    this.set('fakeMetaPref.value', ModifierKey.kMeta);
  }

  restoreDefaults(): void {
    this.inputDeviceSettingsProvider.restoreDefaultKeyboardRemappings(
        this.keyboardId);
  }

  private setRemappedKey(originalKey: ModifierKey): void {
    const targetKey = this.computeModifierRemappings().get(originalKey);
    switch (originalKey) {
      case ModifierKey.kAlt: {
        this.set('fakeAltPref.value', targetKey);
        break;
      }
      case ModifierKey.kAssistant: {
        this.set('fakeAssistantPref.value', targetKey);
        break;
      }
      case ModifierKey.kBackspace: {
        this.set('fakeBackspacePref.value', targetKey);
        break;
      }
      case ModifierKey.kCapsLock: {
        this.set('fakeCapsLockPref.value', targetKey);
        break;
      }
      case ModifierKey.kControl: {
        this.set('fakeCtrlPref.value', targetKey);
        break;
      }
      case ModifierKey.kEscape: {
        this.set('fakeEscPref.value', targetKey);
        break;
      }
      case ModifierKey.kMeta: {
        this.set('fakeMetaPref.value', targetKey);
        break;
      }
    }
  }

  /**
   * Update keyboard settings when the prefs change.
   */
  private onSettingsChanged(): void {
    if (!this.isInitialized) {
      return;
    }

    this.keyboard.settings = {
      ...this.keyboard.settings,
      modifierRemappings: this.getUpdatedRemappings(),
    };

    this.inputDeviceSettingsProvider.setKeyboardSettings(
        this.keyboard.id, this.keyboard.settings);
  }

  /**
   * Get the modifier remappings with updated pref values.
   */
  private getUpdatedRemappings(): {[key: number]: ModifierKey} {
    const updatedRemappings: {[key: number]: ModifierKey} = {};

    if (ModifierKey.kAlt !== this.fakeAltPref.value) {
      updatedRemappings[ModifierKey.kAlt] = this.fakeAltPref.value;
    }
    if (ModifierKey.kAssistant !== this.fakeAssistantPref.value) {
      updatedRemappings[ModifierKey.kAssistant] = this.fakeAssistantPref.value;
    }
    if (ModifierKey.kBackspace !== this.fakeBackspacePref.value) {
      updatedRemappings[ModifierKey.kBackspace] = this.fakeBackspacePref.value;
    }
    if (ModifierKey.kCapsLock !== this.fakeCapsLockPref.value) {
      updatedRemappings[ModifierKey.kCapsLock] = this.fakeCapsLockPref.value;
    }
    if (ModifierKey.kControl !== this.fakeCtrlPref.value) {
      updatedRemappings[ModifierKey.kControl] = this.fakeCtrlPref.value;
    }
    if (ModifierKey.kEscape !== this.fakeEscPref.value) {
      updatedRemappings[ModifierKey.kEscape] = this.fakeEscPref.value;
    }
    if (ModifierKey.kMeta !== this.fakeMetaPref.value) {
      updatedRemappings[ModifierKey.kMeta] = this.fakeMetaPref.value;
    }

    return updatedRemappings;
  }

  private updateDefaultRemapping(): void {
    this.defaultRemappings = {
      ...this.defaultRemappings,
      [ModifierKey.kMeta]:
          this.keyboard.metaKey === MetaKey.kCommand ? ModifierKey.kControl :
                                                       ModifierKey.kMeta,
      [ModifierKey.kControl]:
          this.keyboard.metaKey === MetaKey.kCommand ? ModifierKey.kMeta :
                                                       ModifierKey.kControl,
    };
  }

  private getKeyboardIdFromUrl(): number {
    return Number(Router.getInstance().getQueryParameters().get('keyboardId'));
  }

  private hasKeyboards(): boolean {
    return this.keyboards?.length > 0;
  }

  private computeKeyboardKeysDescription(): string {
    if (!this.keyboard?.name) {
      return '';
    }
    const keyboardName = this.keyboard.isExternal ?
        this.keyboard.name :
        this.i18n('builtInKeyboardName');
    return this.i18n('remapKeyboardKeysDescription', keyboardName);
  }
}

declare global {
  interface HTMLElementTagNameMap {
    'settings-per-device-keyboard-remap-keys':
        SettingsPerDeviceKeyboardRemapKeysElement;
  }
}

customElements.define(
    SettingsPerDeviceKeyboardRemapKeysElement.is,
    SettingsPerDeviceKeyboardRemapKeysElement);
