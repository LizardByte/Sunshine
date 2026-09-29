import { beforeEach, describe, expect, it, vi } from 'vitest'

vi.mock('vue-i18n', () => ({
  createI18n: vi.fn(options => options),
}))

import { createI18n } from 'vue-i18n'
import createSunshineI18n, { loadLocaleMessages } from '../../src_assets/common/assets/web/locale.js'

describe('locale initialization', () => {
  beforeEach(() => {
    createI18n.mockClear()
    document.documentElement.removeAttribute('lang')
  })

  it('loads a bundled configured locale', async () => {
    vi.stubGlobal('fetch', vi.fn()
      .mockResolvedValueOnce({ json: async () => ({ locale: 'de' }) })
      .mockResolvedValueOnce({ json: async () => ({ greeting: 'Hallo' }) }))

    const i18n = await createSunshineI18n()

    expect(fetch).toHaveBeenCalledWith('./api/configLocale')
    expect(fetch).toHaveBeenCalledWith('./assets/locale/de.json')
    expect(document.documentElement.lang).toBe('de')
    expect(i18n.locale).toBe('de')
    expect(i18n.fallbackLocale).toBe('en')
    expect(i18n.messages.en).toBeDefined()
    expect(i18n.messages.de).toEqual({ greeting: 'Hallo' })
  })

  it.each([
    ['an unsupported locale', 'unsupported'],
    ['a prototype property', '__proto__'],
    ['a non-string value', null],
  ])('falls back to English for %s', async (_description, locale) => {
    vi.stubGlobal('fetch', vi.fn(async () => ({
      json: async () => ({ locale }),
    })))

    const i18n = await createSunshineI18n()

    expect(document.documentElement.lang).toBe('en')
    expect(i18n.locale).toBe('en')
    expect(Object.keys(i18n.messages)).toEqual(['en'])
  })

  it('falls back to English when a bundled translation fails to load', async () => {
    const error = vi.spyOn(console, 'error').mockImplementation(() => {})
    const loadTranslation = async () => {
      throw new Error('translation unavailable')
    }

    const result = await loadLocaleMessages('de', loadTranslation)

    expect(result.locale).toBe('en')
    expect(Object.keys(result.messages)).toEqual(['en'])
    expect(error).toHaveBeenCalledWith('Failed to download translations', expect.any(Error))
    error.mockRestore()
  })
})
