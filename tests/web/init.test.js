import { flushPromises } from '@vue/test-utils'
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'

vi.mock('../../src_assets/common/assets/web/locale', () => ({ default: vi.fn() }))
vi.mock('bootstrap/dist/js/bootstrap', () => ({}))

import createI18n from '../../src_assets/common/assets/web/locale'
import { initApp } from '../../src_assets/common/assets/web/init'

/**
 * @brief Create application spies for initialization tests.
 *
 * @return {object} Application with plugin, dependency, and mounting spies.
 */
function appContext() {
  return { use: vi.fn(), provide: vi.fn(), mount: vi.fn() }
}

beforeEach(() => {
  vi.resetAllMocks()
  vi.spyOn(console, 'error').mockImplementation(() => {})
})

afterEach(() => vi.restoreAllMocks())

describe('initApp', () => {
  it('waits for the locale before installing and mounting the application', async () => {
    const app = appContext()
    const locale = { global: {} }
    const initialization = Promise.withResolvers()
    createI18n.mockReturnValue(initialization.promise)

    initApp(app)
    expect(app.mount).not.toHaveBeenCalled()

    initialization.resolve(locale)
    await flushPromises()

    expect(app.use).toHaveBeenCalledExactlyOnceWith(locale)
    expect(app.provide).toHaveBeenCalledExactlyOnceWith('i18n', locale.global)
    expect(app.mount).toHaveBeenCalledExactlyOnceWith('#app')
    expect(app.use.mock.invocationCallOrder[0]).toBeLessThan(app.provide.mock.invocationCallOrder[0])
    expect(app.provide.mock.invocationCallOrder[0]).toBeLessThan(app.mount.mock.invocationCallOrder[0])
    expect(console.error).not.toHaveBeenCalled()
  })

  it('runs the optional configuration callback after mounting', async () => {
    const app = appContext()
    const config = vi.fn()
    createI18n.mockResolvedValue({ global: {} })

    initApp(app, config)
    await flushPromises()

    expect(config).toHaveBeenCalledExactlyOnceWith(app)
    expect(app.mount.mock.invocationCallOrder[0]).toBeLessThan(config.mock.invocationCallOrder[0])
    expect(console.error).not.toHaveBeenCalled()
  })

  it('reports locale initialization rejection without mounting', async () => {
    const app = appContext()
    const config = vi.fn()
    const error = new Error('Locale unavailable')
    createI18n.mockRejectedValue(error)

    initApp(app, config)
    await flushPromises()

    expect(console.error).toHaveBeenCalledExactlyOnceWith('Failed to initialize Sunshine', error)
    expect(app.use).not.toHaveBeenCalled()
    expect(app.provide).not.toHaveBeenCalled()
    expect(app.mount).not.toHaveBeenCalled()
    expect(config).not.toHaveBeenCalled()
  })

  it('reports exceptions from the configuration callback', async () => {
    const app = appContext()
    const error = new Error('Configuration failed')
    const config = vi.fn(() => { throw error })
    createI18n.mockResolvedValue({ global: {} })

    initApp(app, config)
    await flushPromises()

    expect(app.mount).toHaveBeenCalledExactlyOnceWith('#app')
    expect(config).toHaveBeenCalledExactlyOnceWith(app)
    expect(console.error).toHaveBeenCalledExactlyOnceWith('Failed to initialize Sunshine', error)
  })
})
