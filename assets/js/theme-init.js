// Beautiful Jekyll Next - Theme Initialization
// This script must run before page renders to prevent flash of wrong theme (FOUC)

(function() {
  'use strict';

  const getStoredTheme = () => localStorage.getItem('theme');

  const getPreferredTheme = () => {
    const storedTheme = getStoredTheme();
    if (storedTheme) {
      return storedTheme;
    }
    return 'auto';
  };

  const getThemeToApply = (theme) => {
    if (theme === 'auto') {
      return globalThis.matchMedia('(prefers-color-scheme: dark)').matches ? 'dark' : 'light';
    }
    return theme;
  };

  const theme = getPreferredTheme();
  document.documentElement.dataset.bsTheme = getThemeToApply(theme);
})();
