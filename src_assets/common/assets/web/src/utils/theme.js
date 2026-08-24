import { ref } from 'vue'

const getStoredTheme = () => localStorage.getItem('theme')
const setStoredTheme = theme => localStorage.setItem('theme', theme)

export const getPreferredTheme = () => {
    const storedTheme = getStoredTheme()
    if (storedTheme) {
        return storedTheme
    }

    return window.matchMedia('(prefers-color-scheme: dark)').matches ? 'dark' : 'light'
}

// The active theme, kept reactive so components render theme-dependent UI
// (active icon, active menu item, toggle label) through Vue instead of
// querying/mutating the DOM directly.
export const currentTheme = ref(getPreferredTheme())

// Define which themes are dark (for Bootstrap compatibility)
const darkThemes = new Set([
    'dark',
    'dracula',
    'ember',
    'midnight',
    'mocha',
    'moonlight',
    'nord',
    'rose-pine',
    'slate',
])

const setTheme = theme => {
    if (theme === 'auto') {
        const preferredTheme = window.matchMedia('(prefers-color-scheme: dark)').matches ? 'dark' : 'light'
        document.documentElement.dataset.bsTheme = preferredTheme
        document.documentElement.dataset.theme = preferredTheme
    } else {
        // Set Bootstrap's data-bs-theme to 'light' or 'dark' for Bootstrap's own styles
        const bsTheme = darkThemes.has(theme) ? 'dark' : 'light'
        document.documentElement.dataset.bsTheme = bsTheme

        // Set our custom data-theme attribute for our color schemes
        document.documentElement.dataset.theme = theme
    }
}

export function selectTheme(theme) {
    setStoredTheme(theme)
    setTheme(theme)
    currentTheme.value = theme
}

export function pickRandomTheme(themeValues) {
    const current = getStoredTheme()
    const values = themeValues.filter(value => value !== current)
    return values[Math.floor(Math.random() * values.length)]  // NOSONAR(javascript:S2245) random not used for cryptography here
}

export function loadAutoTheme() {
    setTheme(getPreferredTheme())

    window.matchMedia('(prefers-color-scheme: dark)').addEventListener('change', () => {
        const storedTheme = getStoredTheme()
        // Only auto-switch if theme is set to 'auto'
        if (storedTheme === 'auto' || !storedTheme) {
            setTheme(getPreferredTheme())
        }
    })
}
