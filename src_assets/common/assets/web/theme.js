const getStoredTheme = () => localStorage.getItem('theme')
const setStoredTheme = theme => localStorage.setItem('theme', theme)

export const getPreferredTheme = () => {
    const storedTheme = getStoredTheme()
    if (storedTheme) {
        return storedTheme
    }

    return window.matchMedia('(prefers-color-scheme: dark)').matches ? 'dark' : 'light'
}

// Define which themes are dark (for Bootstrap compatibility)
const darkThemes = new Set([
    'dark',
    'ember',
    'midnight',
    'moonlight',
    'nord',
    'slate',
])

const setTheme = theme => {
    if (theme === 'auto') {
        const preferredTheme = window.matchMedia('(prefers-color-scheme: dark)').matches ? 'dark' : 'light'
        document.documentElement.dataset.bsTheme = preferredTheme
        document.documentElement.dataset.theme = preferredTheme
        console.log(`Theme set to auto (resolved to: ${preferredTheme})`)
    } else {
        // Set Bootstrap's data-bs-theme to 'light' or 'dark' for Bootstrap's own styles
        const bsTheme = darkThemes.has(theme) ? 'dark' : 'light'
        document.documentElement.dataset.bsTheme = bsTheme

        // Set our custom data-theme attribute for our color schemes
        document.documentElement.dataset.theme = theme
        console.log(`Theme set to: ${theme} (Bootstrap: ${bsTheme})`)
    }
}

export const showActiveTheme = (theme, focus = false) => {
    const themeSwitcher = document.querySelector('#bd-theme')

    if (!themeSwitcher) {
        return
    }

    const themeSwitcherText = document.querySelector('#bd-theme-text')
    const activeThemeIcon = document.querySelector('.theme-icon-active svg')
    const btnToActive = document.querySelector(`[data-bs-theme-value="${theme}"]`)

    if (!btnToActive) {
        return
    }

    const btnIcon = btnToActive.querySelector('svg')

    if (!activeThemeIcon || !btnIcon) {
        return
    }

    document.querySelectorAll('[data-bs-theme-value]').forEach(element => {
        element.classList.remove('active')
        element.setAttribute('aria-pressed', 'false')
    })

    btnToActive.classList.add('active')
    btnToActive.setAttribute('aria-pressed', 'true')

    // Clone the SVG icon from the active button to the theme switcher
    const clonedIcon = btnIcon.cloneNode(true)
    activeThemeIcon.parentNode.replaceChild(clonedIcon, activeThemeIcon)

    const themeSwitcherLabel = `${themeSwitcherText.textContent} (${btnToActive.textContent.trim()})`
    themeSwitcher.setAttribute('aria-label', themeSwitcherLabel)

    if (focus) {
        themeSwitcher.focus()
    }
}

export function setupThemeToggleListener() {
    document.querySelectorAll('[data-bs-theme-value]')
        .forEach(toggle => {
            toggle.addEventListener('click', () => {
                const theme = toggle.getAttribute('data-bs-theme-value')
                setStoredTheme(theme)
                setTheme(theme)
                showActiveTheme(theme, true)
            })
        })

    showActiveTheme(getPreferredTheme(), false)
}

export function loadAutoTheme() {
    (() => {
        'use strict'

        setTheme(getPreferredTheme())

        window.matchMedia('(prefers-color-scheme: dark)').addEventListener('change', () => {
            const storedTheme = getStoredTheme()
            // Only auto-switch if theme is set to 'auto'
            if (storedTheme === 'auto' || !storedTheme) {
                setTheme(getPreferredTheme())
            }
        })

        window.addEventListener('DOMContentLoaded', () => {
            showActiveTheme(getPreferredTheme())
        })
    })()
}
