<script setup>
import { computed, onUnmounted, ref, watch } from 'vue';

// 本地素材只在浏览器内解码，不上传；主题改变不替换 src，避免动画重新开始。
const buddyGifUrl = ref('');
function loadBuddyGif(event) {
  const file = event.target.files?.[0];
  if (!file) return;
  // 清除后再次选择同一文件，也应触发 change。
  event.target.value = '';
  const previous = buddyGifUrl.value;
  buddyGifUrl.value = URL.createObjectURL(file);
  if (previous) URL.revokeObjectURL(previous);
}
function clearBuddyGif() {
  if (buddyGifUrl.value) URL.revokeObjectURL(buddyGifUrl.value);
  buddyGifUrl.value = '';
}
onUnmounted(clearBuddyGif);


const locale = ref('zh');
const page = ref('home');
const overlay = ref('');
const orientation = ref('portrait');
const isLandscape = computed(() => orientation.value === 'landscape');
const settingsView = ref('root');
const activityReady = ref(false);
const clockSynced = ref(false);
const clockOffset = ref(19800);
const localClock = computed(() => new Date(Date.UTC(2026, 9, 5, 10, 8, 8) + clockOffset.value * 1000));
const homeTime = computed(() => clockSynced.value ? localClock.value.toISOString().slice(11, 16) : '--:--');
const clockDate = computed(() => {
  if (!clockSynced.value) return locale.value === 'zh' ? '未校时' : 'Not synchronized';
  const date = localClock.value;
  if (locale.value === 'zh') return `${date.getUTCMonth()+1}月${date.getUTCDate()}日 周${'日一二三四五六'[date.getUTCDay()]}`;
  return new Intl.DateTimeFormat('en-US', {timeZone:'UTC', weekday:'short', month:'short', day:'2-digit'}).format(date).replace(',', '');
});
const themeNames = ['deepseek', 'claude', 'deepseek-dark', 'claude-dark'];
const theme = ref(themeNames.includes(localStorage.getItem('buddy-theme')) ? localStorage.getItem('buddy-theme') : 'deepseek');
const themeBrand = computed(() => theme.value.startsWith('claude') ? 'claude' : 'deepseek');
const themeDark = computed(() => theme.value.endsWith('-dark'));
function selectThemeBrand(brand) { theme.value = brand + (themeDark.value ? '-dark' : ''); }
function toggleThemeDark() { theme.value = themeBrand.value + (themeDark.value ? '' : '-dark'); }
watch(theme, value => localStorage.setItem('buddy-theme', value));
const brightness = ref(80), aodBrightness = ref(8), aodEnabled = ref(true), aodTimeoutMinutes = ref(30);
const buddyScenario = ref('disconnected');
const promptSubmitted = ref(false), promptFailed = ref(false), promptVariant = ref('long'), packScenario = ref('none');
const settingPages = ['display', 'aod', 'characterPacks', 'time', 'device', 'diagnostics'];
const copy = {
  zh: {home:'主页', buddy:'伙伴', activity:'活动', settingsTitle:'设置', diagnostics:'诊断', disconnected:'未连接', securing:'正在建立安全连接', idle:'空闲', working:'工作中', approvalNeeded:'等待审批', total:'会话', running:'运行', waitingMetric:'等待', tokenTotal:'Token', context:'上下文', noSession:'暂无会话', waitingCount:'{count} 项待审批', waitingApproval:'等待审批', replyFailed:'回复队列失败 · 点击重试', promptIncomplete:'说明未完整显示', currentPack:'当前角色包', packReady:'就绪', packInstalling:'正在切换角色包…', packFailed:'切换失败', noEvents:'暂无事件', noPack:'暂无角色包', choosePack:'选择角色包', services:'服务正常', allow:'仅允许一次', deny:'拒绝', settingsRoots:['显示','AOD','角色包','时间','设备','诊断'], diagnosticFields:['固件','提交','ESP-IDF','构建','堆','栈'],sessions:'共{total} 运{running} 等{waiting}',tokens:'Token {tokens} 上下文{context}'},
  en: {home:'Home', buddy:'Buddy', activity:'Activity', settingsTitle:'Settings', diagnostics:'Diagnostics', disconnected:'Disconnected', securing:'Securing', idle:'Idle', working:'Working', approvalNeeded:'Approval needed', total:'Total', running:'Running', waitingMetric:'Waiting', tokenTotal:'Token', context:'Context', noSession:'No session', waitingCount:'{count} waiting', waitingApproval:'Waiting approval', replyFailed:'Reply queue failed · tap to retry', promptIncomplete:'Description incomplete', currentPack:'Current pack', packReady:'Ready', packInstalling:'Switching pack…', packFailed:'Switch failed', noEvents:'No recent events', noPack:'No pack', choosePack:'Choose character pack', services:'Services healthy', allow:'ALLOW ONCE', deny:'DENY', settingsRoots:['Display','AOD','Character packs','Time','Device','Diagnostics'], diagnosticFields:['Firmware','Commit','ESP-IDF','Build','Heap','Stack'],sessions:'Total {total} R{running} W{waiting}',tokens:'Tok {tokens} Ctx {context}'}
};
const t = computed(() => copy[locale.value]);
// 用于排版检查的模拟事件，不代表设备实测。
const activityEntries = computed(() => locale.value === 'zh' ? [
  {mark:'i', event:'蓝牙已连接', detail:'来源: BLE', time:'10-05 15:38:08'},
  {mark:'i', event:'角色包已安装', detail:'来源: 角色包', time:'T+427s'},
  {mark:'i', event:'审批回复已入队', detail:'来源: 伙伴 | pwsh', time:'T+366s'}
] : [
  {mark:'i', event:'BLE connected', detail:'Source: BLE', time:'10-05 15:38:08'},
  {mark:'i', event:'Pack installed', detail:'Source: Pack', time:'T+427s'},
  {mark:'i', event:'Approval reply queued', detail:'Source: Buddy | pwsh', time:'T+366s'}
]);
const buddyStates = {
  disconnected: { hasState: false, total: '--', running: '--', waiting: '--', tokens: '--', context: null },
  securing: { hasState: false, total: '--', running: '--', waiting: '--', tokens: '--', context: null },
  idle: { hasState: true, total: 3, running: 0, waiting: 0, tokens: '999', context: { value: 18000, window: 64000 } },
  working: { hasState: true, total: 3, running: 1, waiting: 0, tokens: '184.5k', context: { value: 31300, window: 128000 } },
  approval: { hasState: true, total: 3, running: 1, waiting: 2, tokens: '18.4E', context: { value: 18000, window: 64000 } },
};
const buddyState = computed(() => buddyStates[buddyScenario.value]);
// The home card keeps its empty state until BLE can carry a complete snapshot.
const homeBuddyHasSnapshot = computed(() => buddyScenario.value !== 'securing' && buddyState.value.hasState);
const buddyStatus = computed(() => ({
  disconnected: t.value.disconnected,
  securing: t.value.securing,
  idle: t.value.idle,
  working: t.value.working,
  approval: t.value.approvalNeeded,
})[buddyScenario.value]);
const buddyGifPose = computed(() => ({
  disconnected: ' /ᗧᗧ\\\n( -.- )\n > z <',
  securing: ' /ᗧᗧ\\\n( o.o )\n > ^ <',
  idle: ' /ᗧᗧ\\\n( o.o )\n > ^ <',
  working: ' /ᗧᗧ\\\n( •.• )\n  /|\\ ',
  approval: ' /ᗧᗧ\\\n( !.! )\n > ^ <',
})[buddyScenario.value]);
const buddyMetricLabels = computed(() => (
  locale.value === 'en'
    ? { total: 'Total', running: 'Run', waiting: 'Wait' }
    : { total: t.value.total, running: t.value.running, waiting: t.value.waitingMetric }
));
function compactMetric(value) {
  if (value === '--') return value;
  const number = BigInt(value);
  const suffixes = ['', 'k', 'M', 'G', 'T', 'P', 'E'];
  let scale = 1n;
  let index = 0;
  while (number / scale >= 1000n && index < suffixes.length - 1) {
    scale *= 1000n;
    index += 1;
  }
  if (index === 0) return String(number);
  return `${number / scale}.${(number % scale) / (scale / 10n)}${suffixes[index]}`;
}

function homeUsage(value) {
  return value.replace(/^(\d{3})\.\d([kMGTPE])$/, '$1$2');
}
function contextMetric(value) {
  if (!Number.isFinite(value)) return '--';
  const units = ['', 'K', 'M', 'G', 'T', 'P', 'E'];
  let scale = 1;
  let unit = 0;
  while (unit < units.length - 1 && value / scale >= 1000) {
    scale *= 1000;
    unit += 1;
  }
  return unit === 0 ? `${value}` : `${Math.floor(value / scale)}${units[unit]}`;
}
function contextUsage(context, compact = false) {
  if (!context || !Number.isFinite(context.value)) return '--';
  const value = `~${contextMetric(context.value)}`;
  if (!Number.isFinite(context.window) || context.window <= 0) return compact ? `${value}/--` : `${value} / --`;
  const window = contextMetric(context.window);
  const percent = Math.floor((context.value * 100) / context.window);
  return compact ? `${value}/${window} ${percent}%` : `${value} / ${window} (${percent}%)`;
}
const buddySessions = computed(() => t.value.sessions
  .replace('{total}', buddyState.value.total)
  .replace('{running}', buddyState.value.running)
  .replace('{waiting}', buddyState.value.waiting));
const buddyTokens = computed(() => t.value.tokens
  .replace('{tokens}', buddyState.value.tokens)
  .replace('{context}', contextUsage(buddyState.value.context, true)));
const promptWaiting = computed(() => t.value.waitingCount.replace('{count}', buddyState.value.waiting));
const promptHint = computed(() => {
  if (promptVariant.value === 'short') return locale.value === 'zh' ? '查看工作区文件。' : 'Read workspace files.';
  return locale.value === 'zh'
    ? '允许本次操作使用 danger-full-access 权限：这条一次性操作需要在完全权限下运行（approval 测试）。检验“中文标点”、换行、滚动；路径示例 C:\\Users\\admin，说明不执行任何命令。它会检查工作区及其上级目录的 Windows 文件权限，仅在缺少权限的位置为当前登录用户补上完全控制项，并删除外来的应用包权限项。不修改任何文件内容和所有者，同时打印可撤销本次权限改动的恢复命令。'
    : 'Allow this one-time operation to use danger-full-access permissions. It checks Windows file permissions in the workspace and its parent directory, grants the current user full control only where needed, removes unrelated app-package entries, leaves file contents and ownership unchanged, and prints a command to reverse the permission changes.';
});
const promptDetail = computed(() => [
  promptVariant.value === 'truncated' ? t.value.promptIncomplete : '',
  promptFailed.value ? t.value.replyFailed : '',
  promptWaiting.value,
].filter(Boolean).join('\n'));
const packStatus = computed(() => ({
  none: t.value.noPack,
  ready: t.value.packReady,
  installing: t.value.packInstalling,
  failed: t.value.packFailed,
  refreshing: locale.value === 'zh' ? '正在刷新...' : 'Refreshing...',
  listFailed: locale.value === 'zh' ? '列表失败' : 'List failed',
})[packScenario.value]);
const currentPack = computed(() => packScenario.value === 'none' ? t.value.noPack : 'claude-cat-ultra-long-pack-2026');

const bleName = ref(localStorage.getItem('buddy-ble-name') || 'DeepSeek-1234');
const nameInput = ref(''), nameError = ref(''), nameSaveState = ref('idle');
const keyPage = ref(0), keyShift = ref(false);
const keyChars = ['qwertyuiopasd','fghjklzxcvbnm','1234567890!?#','$%&*+=/\\:;,()','[]{}<>|~^\'"_@'];
const nameKeys = computed(() => [...keyChars[keyPage.value].split('').map(key => keyPage.value < 2 && keyShift.value ? key.toUpperCase() : key), '→',keyPage.value < 2 ? '123' : 'ABC',keyPage.value < 2 ? 'Shift' : '`','Space','Del','.','-']);
let saveTimer;
onUnmounted(() => clearTimeout(saveTimer));
function openDeviceName() { nameInput.value = bleName.value; nameError.value = ''; overlay.value = 'deviceName'; }
function nameKey(key) {
  if (key === '→') keyPage.value = keyPage.value < 2 ? 1 - keyPage.value : 2 + (keyPage.value - 1) % 3;
  else if (key === '123' || key === 'ABC') keyPage.value = keyPage.value < 2 ? 2 : 0;
  else if (key === 'Shift') keyShift.value = !keyShift.value;
  else if (key === 'Del') nameInput.value = nameInput.value.slice(0,-1);
  else if (nameInput.value.length < 29) nameInput.value += key === 'Space' ? ' ' : key;
}
function saveDeviceName() {
  const name = nameInput.value;
  if (!name || name.length > 29 || name !== name.trim() || !/^[ -~]+$/.test(name)) { nameError.value = locale.value === 'zh' ? '名称或长度无效' : 'Invalid name or length'; return; }
  bleName.value = name; nameSaveState.value = 'saving'; overlay.value = '';
  clearTimeout(saveTimer);
  saveTimer = setTimeout(() => { try { localStorage.setItem('buddy-ble-name', name); nameSaveState.value = 'saved'; } catch { nameSaveState.value = 'failed'; } }, 1500);
}
function resetSettings() { theme.value = 'deepseek'; brightness.value = 80; aodBrightness.value = 8; aodEnabled.value = true; aodTimeoutMinutes.value = 30; orientation.value = 'portrait'; locale.value = 'zh'; bleName.value = 'DeepSeek-1234'; localStorage.removeItem('buddy-ble-name'); overlay.value = ''; }
function openMain(next) { if (overlay.value) return; page.value = next; }
function openApp() { openMain('buddy'); }
function openSetting(index) { settingsView.value = settingPages[index]; }
function backSetting() { settingsView.value = 'root'; }
function setBuddyScenario(next) { buddyScenario.value = next; promptSubmitted.value = false; promptFailed.value = false; overlay.value = next === 'approval' ? 'prompt' : ''; }
function submitPrompt() { promptFailed.value = false; promptSubmitted.value = true; }
function showPasskey() { setBuddyScenario('securing'); overlay.value = 'passkey'; }
function showPromptFailure() { setBuddyScenario('approval'); promptFailed.value = true; }
</script>
<template>
  <main class="workbench">
    <section class="device-frame" :class="[{ landscape: isLandscape }, theme]" :lang="locale" aria-label="LVGL UI preview">
      <div class="lvgl-screen">
        <section v-show="page === 'home'" class="page page-home">
          <article class="card home-clock"><strong>{{ homeTime }}</strong><small>{{ clockDate }}</small></article>
          <button class="card home-ble" @click="openApp()">
            <span class="home-buddy-head"><strong>{{ t.buddy }}</strong><b :class="buddyScenario">{{ buddyStatus }}</b></span>
            <span v-if="homeBuddyHasSnapshot" class="home-buddy-metrics">
              <span><small>{{ t.total }}</small><b>{{ compactMetric(buddyState.total) }}</b></span>
              <span><small>{{ t.running }}</small><b>{{ compactMetric(buddyState.running) }}</b></span>
              <span><small>{{ t.waitingMetric }}</small><b>{{ compactMetric(buddyState.waiting) }}</b></span>
            </span>
            <span v-else class="home-buddy-empty">{{ t.noSession }}</span>
            <span v-if="homeBuddyHasSnapshot" class="home-buddy-usage"><span><small>{{ t.tokenTotal }}</small><b>{{ homeUsage(buddyState.tokens) }}</b></span><span><small>{{ t.context }}</small><b>{{ contextUsage(buddyState.context, true) }}</b></span></span>
          </button>
          <button class="card home-activity" @click="openMain('activity')"><strong>{{ t.activity }}</strong><span>{{ activityReady ? activityEntries[0].event : t.noEvents }}</span></button>
        </section>


        <section v-show="page === 'buddy'" class="page page-buddy">
          <h1>{{ t.buddy }}</h1><span class="buddy-page-status" :class="buddyScenario">{{ buddyStatus }}</span>
          <article class="card gif-card" :class="buddyScenario" aria-label="Character animation preview"><img v-if="buddyGifUrl" :src="buddyGifUrl" alt="Local GIF preview"><pre v-else>{{ buddyGifPose }}</pre></article>
          <article class="card buddy-detail">
            <div class="buddy-metric-row">
              <span><small>{{ buddyMetricLabels.total }}</small><b>{{ buddyState.total }}</b></span>
              <span><small>{{ buddyMetricLabels.running }}</small><b>{{ buddyState.running }}</b></span>
              <span><small>{{ buddyMetricLabels.waiting }}</small><b>{{ buddyState.waiting }}</b></span>
            </div>
            <div class="buddy-usage-row"><span>{{ t.tokenTotal }} <b>{{ buddyState.tokens }}</b></span><span>{{ t.context }} <b>{{ contextUsage(buddyState.context, true) }}</b></span></div>
          </article>
        </section>



        <section v-show="page === 'diagnostics'" class="page page-diagnostics">
          <div class="settings-header"><h1>{{ t.diagnostics }}</h1><button aria-label="Back" @click="openMain('settings')"><svg viewBox="0 0 24 24" aria-hidden="true"><path d="M15 5l-7 7 7 7" /></svg></button></div>
          <article v-for="(name, index) in t.diagnosticFields" :key="name" class="card diag-row"><small>{{ name }}</small><span>{{ index < 4 ? '--' : '0 KB' }}</span></article>
          <small class="diag-service">{{ t.services }}</small>
        </section>

        <section v-show="page === 'activity'" class="page page-activity">
          <h1>{{ t.activity }}</h1>
          <p v-if="!activityReady">{{ t.noEvents }}</p>
          <div v-else class="activity-cards"><article v-for="entry in activityEntries" :key="entry.time" class="card activity-card"><span class="activity-mark">{{ entry.mark }}</span><b>{{ entry.event }}</b><small>{{ entry.detail }}</small><time>{{ entry.time }}</time></article></div>
        </section>


        <section v-show="page === 'settings'" class="page page-settings">
          <div class="settings-header"><h1>{{ settingsView === 'root' ? t.settingsTitle : t.settingsRoots[settingPages.indexOf(settingsView)] }}</h1><button v-show="settingsView !== 'root'" aria-label="Back" @click="backSetting()"><svg viewBox="0 0 24 24" aria-hidden="true"><path d="M15 5l-7 7 7 7" /></svg></button></div>
          <div v-show="settingsView === 'root'" class="settings-root"><button v-for="(name,index) in t.settingsRoots" :key="name" class="settings-list-row tap-target" @click="openSetting(index)"><span>{{ name }}</span><span>›</span></button></div>
          <div v-show="settingsView === 'display'" class="settings-detail display-detail"><p>{{ locale === 'zh' ? '屏幕亮度' : 'Brightness' }}: {{ brightness }}%</p><input v-model="brightness" class="tap-target" type="range" min="1" max="100"><div class="theme-choice"><button v-for="brand in ['deepseek','claude']" :key="brand" :class="{selected: themeBrand === brand}" @click="selectThemeBrand(brand)">{{ brand === 'claude' ? 'Claude' : 'DeepSeek' }}</button></div><div class="mode-row"><span>{{ locale === 'zh' ? '深色' : 'Dark' }}</span><button class="mode-switch" role="switch" :aria-checked="themeDark" @click="toggleThemeDark"><i></i></button></div></div>
          <div v-show="settingsView === 'aod'" class="settings-detail"><div class="mode-row"><span>AOD</span><button class="mode-switch" role="switch" :aria-checked="aodEnabled" @click="aodEnabled = !aodEnabled"><i></i></button></div><p>{{ locale === 'zh' ? 'AOD 分钟' : 'AOD minutes' }}: {{ aodTimeoutMinutes }}</p><input v-model="aodTimeoutMinutes" class="tap-target" type="range" min="1" max="180"><p>{{ locale === 'zh' ? 'AOD 亮度' : 'AOD brightness' }}: {{ aodBrightness }}%</p><input v-model="aodBrightness" class="tap-target" type="range" min="1" max="100"></div>
          <div v-show="settingsView === 'characterPacks'" class="settings-detail pack-settings-detail"><article class="pack-settings-summary" :class="packScenario"><small>{{ t.currentPack }}</small><b>{{ currentPack }}</b><span>{{ packStatus }}</span></article><button class="card pack-selector tap-target" :disabled="['none', 'installing', 'refreshing'].includes(packScenario)"><span>{{ packScenario === 'refreshing' ? (locale === 'zh' ? '正在刷新角色包...' : 'Refreshing packs...') : packScenario === 'listFailed' ? (locale === 'zh' ? '重试角色包列表' : 'Retry pack list') : currentPack }}</span><span>⌄</span></button></div>
          <div v-show="settingsView === 'time'" class="settings-detail"><p>{{ clockSynced ? (locale === 'zh' ? '最近电脑校时' : 'Last computer sync') : clockDate }}</p><p v-if="clockSynced">{{ clockDate }} {{ homeTime }}:08<br>UTC{{ clockOffset >= 0 ? '+' : '-' }}{{ String(Math.floor(Math.abs(clockOffset)/3600)).padStart(2,'0') }}:{{ String(Math.floor(Math.abs(clockOffset)%3600/60)).padStart(2,'0') }}</p><small>{{ locale === 'zh' ? '通过安全蓝牙连接电脑插件校时。' : 'Connect the computer plugin over secure BLE.' }}</small></div>
          <div v-show="settingsView === 'device'" class="settings-detail device-detail"><label class="device-language"><span>{{ locale === 'zh' ? '语言' : 'Language' }}</span><select v-model="locale"><option value="zh">中文</option><option value="en">English</option></select></label><button class="device-name-card tap-target" @click="openDeviceName"><span>{{ locale === 'zh' ? '蓝牙设备名' : 'BLE device name' }}</span><b>{{ bleName }}</b><small>{{ nameSaveState === 'idle' ? (locale === 'zh' ? '修改后重启生效' : 'Reboot to apply changes') : nameSaveState }}</small></button><button class="card tap-target" @click="overlay = 'reset'">{{ locale === 'zh' ? '恢复默认' : 'Restore defaults' }}</button></div>
          <div v-show="settingsView === 'diagnostics'" class="settings-detail"><button class="card tap-target" @click="orientation = 'portrait'">{{ locale === 'zh' ? '竖屏' : 'Portrait' }}</button><button class="card tap-target" @click="orientation = 'landscape'">{{ locale === 'zh' ? '横屏' : 'Landscape' }}</button><button class="card tap-target" @click="openMain('diagnostics')">{{ t.diagnostics }}</button><small>{{ locale === 'zh' ? '存储故障时通过串口执行 storage format ERASE，将删除角色包和存储字库。' : 'Storage failure: serial console storage format ERASE deletes packs and stored fonts.' }}</small></div>
        </section>

        <section v-show="overlay === 'prompt'" class="overlay prompt-page">
          <h1>{{ locale === 'zh' ? 'pwsh 请求' : 'pwsh request' }}</h1>
          <article class="card prompt-card">
            <div class="prompt-scroll" tabindex="0" :aria-label="locale === 'zh' ? '审批说明' : 'Approval description'">
              <div class="prompt-body">{{ promptHint }}</div>
            </div>
            <small class="prompt-detail">{{ promptDetail }}</small>
          </article>
          <button class="allow" :disabled="promptSubmitted" @click="submitPrompt">{{ t.allow }}</button>
          <button class="deny" :disabled="promptSubmitted" @click="submitPrompt">{{ t.deny }}</button>
        </section>
        <section v-show="overlay === 'aod'" class="overlay aod-page" @click="overlay = ''"><strong>{{ homeTime }}</strong><small>{{ clockSynced ? '08' : '--' }}</small><p>{{ clockDate }}</p><p>{{ buddyScenario === 'disconnected' ? t.disconnected : 'BLE' }}</p></section>
        <section v-show="overlay === 'passkey'" class="overlay prompt-page passkey-page"><h1>{{ locale === 'zh' ? '配对码' : 'Passkey' }}</h1><article class="card">731204<small>{{ locale === 'zh' ? '在电脑蓝牙配对窗口输入' : 'Enter in the computer pairing dialog' }}</small></article><button class="deny" @click="overlay = ''">{{ t.deny }}</button></section>
        <section v-show="overlay === 'reset'" class="overlay dialog-page"><article class="card"><b>{{ locale === 'zh' ? '恢复默认设置？' : 'Restore defaults?' }}</b><small>{{ locale === 'zh' ? '恢复显示设置和校准，保留角色包和配对。' : 'Restore display settings and calibration; retain packs and bonds.' }}</small><button @click="overlay = ''">{{ locale === 'zh' ? '取消' : 'Cancel' }}</button><button @click="resetSettings">{{ locale === 'zh' ? '恢复' : 'Restore' }}</button></article></section>

        <section v-show="overlay === 'deviceName'" class="overlay name-editor-page"><div class="name-editor-title">{{ nameError || (locale === 'zh' ? '蓝牙设备名' : 'BLE device name') }}</div><div class="name-editor-value">{{ nameInput || '|' }}</div><div class="name-keyboard" :class="{landscape:isLandscape}"><button v-for="(key,index) in nameKeys" :key="index" @click="nameKey(key)">{{ key }}</button></div><div class="name-editor-actions"><button @click="saveDeviceName">{{ locale === 'zh' ? '保存' : 'Save' }}</button><button @click="overlay = ''">{{ locale === 'zh' ? '取消' : 'Cancel' }}</button></div></section>

        <header class="status-bar"><span>ᛒ</span><span class="status-right"><time>{{ homeTime }}</time></span></header>
        <nav class="main-nav"><button class="tap-target" :class="{ active: page === 'home' }" @click="openMain('home')">⌂</button><button class="tap-target" :class="{ active: page === 'buddy' }" @click="openMain('buddy')">ᛒ</button><button class="tap-target" :class="{ active: page === 'activity' }" @click="openMain('activity')">♩</button><button class="tap-target" :class="{ active: page === 'settings' }" @click="openMain('settings')">⚙</button></nav>
      </div>
    </section>

    <aside class="controls">
      <label>{{ locale === 'zh' ? '本地角色 GIF（仅浏览器预览）' : 'Local character GIF (browser only)' }}<input type="file" accept=".gif,image/gif" @change="loadBuddyGif"></label>
      <button v-if="buddyGifUrl" @click="clearBuddyGif">{{ locale === 'zh' ? '清除 GIF 预览' : 'Clear GIF preview' }}</button>
      <div class="control-group"><span>方向</span><button :class="{ selected: !isLandscape }" @click="orientation = 'portrait'">240×320</button><button :class="{ selected: isLandscape }" @click="orientation = 'landscape'">320×240</button></div>
      <div class="control-group"><span>语言</span><button :class="{ selected: locale === 'zh' }" @click="locale = 'zh'">中文</button><button :class="{ selected: locale === 'en' }" @click="locale = 'en'">English</button></div>
      <div class="control-group"><span>Buddy 场景</span><button v-for="scenario in ['disconnected', 'securing', 'idle', 'working', 'approval']" :key="scenario" :class="{ selected: buddyScenario === scenario }" @click="setBuddyScenario(scenario)">{{ scenario }}</button></div>
      <div class="control-group"><span>角色包状态</span><button v-for="scenario in ['none', 'ready', 'installing', 'refreshing', 'failed', 'listFailed']" :key="scenario" :class="{ selected: packScenario === scenario }" @click="packScenario = scenario">{{ scenario }}</button></div>
      <div class="control-group"><span>覆盖层</span><button @click="setBuddyScenario('approval')">Prompt</button><button @click="showPromptFailure">Reply fail</button><button @click="showPasskey">Passkey</button><button @click="overlay = 'aod'">AOD</button></div>
      <div class="control-group"><span>审批说明</span><button v-for="variant in ['short', 'long', 'truncated']" :key="variant" :class="{ selected: promptVariant === variant }" @click="promptVariant = variant; setBuddyScenario('approval')">{{ variant }}</button></div>
      <div class="control-group"><span>校时模拟</span><button @click="clockSynced = false">冷启动</button><button @click="clockSynced = true">已校时</button><select v-model.number="clockOffset"><option :value="0">UTC+0</option><option :value="-12600">UTC-3:30</option><option :value="19800">UTC+5:30</option><option :value="20700">UTC+5:45</option></select></div>
      <div class="control-group"><span>活动模拟数据</span><button :class="{ selected: activityReady }" @click="activityReady = !activityReady">{{ activityReady ? '三条事件' : '空状态' }}</button></div>
    </aside>
  </main>
</template>
