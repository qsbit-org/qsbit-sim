/* Theme preference for standalone tools. Documentation uses PyData's switcher. */
(() => {
  'use strict';
  if (parent !== window) document.documentElement.classList.add('embedded');
  const media = matchMedia('(prefers-color-scheme: dark)');
  let preference = 'auto';
  try { preference = localStorage.getItem('qsbit-theme') || 'auto'; } catch {}
  function apply(value, persist = true) {
    preference = ['light', 'dark', 'auto'].includes(value) ? value : 'auto';
    document.documentElement.dataset.theme = preference === 'auto' ? (media.matches ? 'dark' : 'light') : preference;
    if (persist) { try { localStorage.setItem('qsbit-theme', preference); } catch {} }
    document.querySelectorAll('[data-theme-select]').forEach(select => { select.value = preference; });
    window.dispatchEvent(new Event('qsbit-theme-change'));
  }
  window.QsbitTheme = {set: apply, get: () => preference};
  apply(preference, false);
  media.addEventListener('change', () => apply(preference, false));
  window.addEventListener('storage', event => { if (event.key === 'qsbit-theme') apply(event.newValue, false); });
  document.addEventListener('DOMContentLoaded', () => {
    document.querySelectorAll('[data-theme-select]').forEach(select => {
      select.value = preference; select.addEventListener('change', () => apply(select.value));
    });
  });
  window.addEventListener('message', event => {
    if (event.origin === location.origin && event.source === parent && event.data?.type === 'qsbit-theme') apply(event.data.theme, false);
  });
})();
