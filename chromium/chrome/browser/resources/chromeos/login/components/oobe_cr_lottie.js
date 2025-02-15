// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview Polymer element for displaying pausable lottie animation.
 */

import '//resources/cr_elements/cr_icon_button/cr_icon_button.js';
import '//resources/cros_components/lottie_renderer/lottie-renderer.js';
import './oobe_icons.html.js';

import {html, mixinBehaviors, PolymerElement} from '//resources/polymer/v3_0/polymer/polymer_bundled.min.js';

import {traceOobeLottieExecution} from '../../oobe_trace.js';

import {OobeI18nBehavior, OobeI18nBehaviorInterface} from './behaviors/oobe_i18n_behavior.js';

/**
 * @constructor
 * @extends {PolymerElement}
 * @implements {OobeI18nBehaviorInterface}
 */
const OobeCrLottieBase = mixinBehaviors([OobeI18nBehavior], PolymerElement);

/**
 * @typedef {{
 *   animation: LottieRenderer,
 *   container: HTMLElement,
 * }}
 */
OobeCrLottieBase.$;

/** @polymer */
export class OobeCrLottie extends OobeCrLottieBase {
  static get is() {
    return 'oobe-cr-lottie';
  }

  static get template() {
    return html`{__html_template__}`;
  }

  static get properties() {
    return {
      playing: {
        type: Boolean,
        observer: 'onPlayingChanged_',
        value: false,
      },

      animationUrl: {
        type: String,
        observer: 'onUrlChanged_',
        value: '',
      },

      preload: {
        type: Boolean,
        value: false,
      },
    };
  }

  constructor() {
    super();
    this.animationPlayer = null;
  }

  ready() {
    super.ready();
    this.addEventListener('click', this.onClick_);
    // Preload the player so that the first frame is shown.
    if (this.preload) {
      this.createPlayer(/*autoplay=*/false);
    }
  }

  onClick_() {
    this.playing = !this.playing;
  }

  /**
   *
   * @param {?boolean} autoplay
   * @suppress {missingProperties}
   */
  createPlayer(autoplay = true) {
    this.animationPlayer = document.createElement('cros-lottie-renderer');
    this.animationPlayer.id = 'animation';
    this.animationPlayer.setAttribute('asset-url', this.animationUrl);
    this.animationPlayer.autoplay = autoplay;
    this.$.container.insertBefore(this.animationPlayer, this.$.playPauseIcon);
  }

  // Update the URL on the player if one exists, otherwise it will be updated
  // when an instance is created.
  onUrlChanged_() {
    if (this.animationUrl && this.animationPlayer) {
      this.animationPlayer.setAttribute('asset-url', this.animationUrl);
    }
  }

  onPlayingChanged_() {
    if (this.animationPlayer) {
      if (this.playing) {
        this.animationPlayer.play();
      } else {
        this.animationPlayer.pause();
      }
    } else {
      if (this.playing) {
        // Create a player, it will autoplay.
        this.createPlayer(/*autoplay=*/true);
      } else {
        // Nothing to do.
      }
    }
  }

  getIcon_(playing) {
    if (playing) {
      return 'oobe-48:pause';
    }
    return 'oobe-48:play';
  }

  getAria_() {
    if (this.playing) {
      return this.i18n('pauseAnimationAriaLabel');
    }
    return this.i18n('playAnimationAriaLabel');
  }
}

customElements.define(OobeCrLottie.is, OobeCrLottie);
