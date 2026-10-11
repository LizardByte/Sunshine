import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'

vi.mock('../../src_assets/common/assets/web/Navbar.vue', () => ({
  default: { template: '<div />' },
}))
vi.mock('../../src_assets/common/assets/web/NavbarSimple.vue', () => ({
  default: { template: '<div />' },
}))
vi.mock('../../src_assets/common/assets/web/ResourceCard.vue', () => ({
  default: { template: '<div />' },
}))
vi.mock('../../src_assets/common/assets/web/Notification.vue', () => ({
  default: { template: '<div />' },
}))
vi.mock('../../src_assets/common/assets/web/fetch_utils', () => ({
  apiFetch: vi.fn(),
}))

import { apiFetch } from '../../src_assets/common/assets/web/fetch_utils'
import Password from '../../src_assets/common/assets/web/Password.vue'
import Welcome from '../../src_assets/common/assets/web/Welcome.vue'

beforeEach(() => {
  vi.resetAllMocks()
  vi.useFakeTimers()
  vi.stubGlobal('document', { location: { reload: vi.fn() } })
})

afterEach(() => {
  vi.useRealTimers()
  vi.unstubAllGlobals()
})

describe.each([
  ['Welcome', Welcome],
  ['Password', Password],
])('%s credential saving', (_name, component) => {
  it('posts credentials and reloads only after the success delay', async () => {
    const ctx = component.data()
    ctx.error = 'Previous error'
    apiFetch.mockResolvedValue({ status: 200, json: async () => ({ status: true }) })

    await component.methods.save.call(ctx)

    expect(apiFetch).toHaveBeenCalledExactlyOnceWith('./api/password', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(ctx.passwordData),
    })
    expect(ctx.success).toBe(true)
    expect(ctx.error).toBeNull()
    vi.advanceTimersByTime(4999)
    expect(document.location.reload).not.toHaveBeenCalled()
    vi.advanceTimersByTime(1)
    expect(document.location.reload).toHaveBeenCalledOnce()
  })

  it.each([false, 'true'])('displays the API error when status is %s', async status => {
    const ctx = component.data()
    apiFetch.mockResolvedValue({
      status: 200,
      json: async () => ({ status, error: 'Invalid credentials' }),
    })

    await component.methods.save.call(ctx)

    expect(ctx.success).toBe(status)
    expect(ctx.error).toBe('Invalid credentials')
    expect(vi.getTimerCount()).toBe(0)
  })

  it.each([400, 500])('displays the server error for HTTP %s without parsing JSON', async status => {
    const ctx = component.data()
    const json = vi.fn()
    apiFetch.mockResolvedValue({ status, json })

    await component.methods.save.call(ctx)

    expect(ctx.error).toBe('Internal Server Error')
    expect(ctx.success).toBe(false)
    expect(json).not.toHaveBeenCalled()
    expect(vi.getTimerCount()).toBe(0)
  })

  it('waits for the response body before completing', async () => {
    const ctx = component.data()
    const { promise, resolve } = Promise.withResolvers()
    const json = vi.fn(() => promise)
    apiFetch.mockResolvedValue({ status: 200, json })
    const completed = vi.fn()

    const saving = component.methods.save.call(ctx).then(completed)
    await vi.waitFor(() => expect(json).toHaveBeenCalledOnce())
    expect(completed).not.toHaveBeenCalled()
    expect(ctx.success).toBe(false)

    resolve({ status: false, error: 'Rejected credentials' })
    await saving
    expect(completed).toHaveBeenCalledOnce()
    expect(ctx.error).toBe('Rejected credentials')
  })

  it('propagates request failures to the caller', async () => {
    const ctx = component.data()
    const error = new Error('Network unavailable')
    apiFetch.mockRejectedValue(error)

    await expect(component.methods.save.call(ctx)).rejects.toBe(error)

    expect(ctx.success).toBe(false)
    expect(vi.getTimerCount()).toBe(0)
  })

  it('propagates JSON failures to the caller', async () => {
    const ctx = component.data()
    const error = new Error('Invalid JSON')
    apiFetch.mockResolvedValue({ status: 200, json: vi.fn().mockRejectedValue(error) })

    await expect(component.methods.save.call(ctx)).rejects.toBe(error)

    expect(ctx.success).toBe(false)
    expect(vi.getTimerCount()).toBe(0)
  })
})

it('clears the welcome loading state when the HTTP response arrives', async () => {
  const ctx = Welcome.data()
  const response = Promise.withResolvers()
  const body = Promise.withResolvers()
  const json = vi.fn(() => body.promise)
  apiFetch.mockReturnValue(response.promise)

  const saving = Welcome.methods.save.call(ctx)
  expect(ctx.loading).toBe(true)

  response.resolve({ status: 200, json })
  await vi.waitFor(() => expect(json).toHaveBeenCalledOnce())
  expect(ctx.loading).toBe(false)

  body.resolve({ status: false, error: 'Invalid credentials' })
  await saving
  expect(ctx.error).toBe('Invalid credentials')
})
