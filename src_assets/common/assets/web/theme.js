/**
 * Backbone Theme - Dark Mode Only
 *
 * Simplified theme module that always applies dark mode.
 * The original theme toggle functionality has been removed for a consistent
 * Backbone-branded experience.
 */

const setTheme = () => {
    document.documentElement.setAttribute('data-bs-theme', 'dark')
}

export const getPreferredTheme = () => 'dark'

export const showActiveTheme = () => {
    // No-op: Theme toggle UI has been removed
}

export function setupThemeToggleListener() {
    // No-op: Theme toggle UI has been removed
}

export function loadAutoTheme() {
    'use strict'
    setTheme()
}
