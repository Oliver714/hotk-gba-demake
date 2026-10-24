/* Public EmulatorJS configuration; keep game identity stable across releases. */
// An embedding page may disallow wake locks inside the iframe. Treat that
// optional capability as unavailable instead of asking for a forbidden lock.
const policy = document.permissionsPolicy || document.featurePolicy;
if (policy && !policy.allowsFeature('screen-wake-lock')) {
  Object.defineProperty(navigator, 'wakeLock', {value: undefined, configurable: true});
}
window.EJS_player = '#game';
window.EJS_core = 'gba';
window.EJS_gameUrl = 'game.gba';
window.EJS_pathtodata = 'data/';
window.EJS_gameName = 'hotk-gba-demake';
window.EJS_gameID = 1001;
window.EJS_language = 'zh-CN';
window.EJS_disableAutoLang = true;
window.EJS_threads = false;
window.EJS_startOnLoaded = false;
window.EJS_startButtonName = '开始游戏';
window.EJS_color = '#a8753f';
window.EJS_backgroundColor = '#171b18';
window.EJS_backgroundImage = new URL('poster.png', location.href).href;
window.EJS_backgroundBlur = false;
window.EJS_volume = 0.5;
window.EJS_defaultOptions = {'save-save-interval': 5};
window.EJS_defaultControls = {0: {
  0: {value: 'z', value2: 'BUTTON_2'},
  8: {value: 'x', value2: 'BUTTON_1'},
  2: {value: 'shift', value2: 'SELECT'},
  3: {value: 'enter', value2: 'START'},
  4: {value: 'up arrow', value2: 'DPAD_UP'},
  5: {value: 'down arrow', value2: 'DPAD_DOWN'},
  6: {value: 'left arrow', value2: 'DPAD_LEFT'},
  7: {value: 'right arrow', value2: 'DPAD_RIGHT'},
  10: {value: 'a', value2: 'LEFT_TOP_SHOULDER'},
  11: {value: 's', value2: 'RIGHT_TOP_SHOULDER'}
}, 1: {}, 2: {}, 3: {}};
window.EJS_Buttons = {
  cheat: false, netplay: false, screenRecord: false, exitEmulation: false,
  saveSavFiles: true, loadSavFiles: true, fullscreen: true
};
