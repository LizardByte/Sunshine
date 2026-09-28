import { mount, flushPromises } from '@vue/test-utils'
import { afterEach, describe, expect, it, vi } from 'vitest'

vi.mock('../../src_assets/common/assets/web/Navbar.vue', () => ({
  default: { template: '<div />' },
}))

import Troubleshooting from '../../src_assets/common/assets/web/Troubleshooting.vue'

async function mountTroubleshooting(platform, gamepadDriver, licenseStatus = {}) {
  vi.stubGlobal('fetch', vi.fn(async url => {
    if (url === '/api/config') {
      return { json: async () => ({ platform, gamepad_driver: gamepadDriver }) }
    }
    if (url === '/api/virtual-input/status') {
      return {
        json: async () => ({
          virtualhid: {
            installed: true,
            version: '2026.914.1218',
            version_compatible: true,
            supported_versions: '>= 2026.914.1218',
          },
          vigembus: { installed: false },
        }),
      }
    }
    if (url === '/api/virtual-input/license') {
      return { ok: true, json: async () => ({ service_available: true, state: 'licensed', licensed: true, ...licenseStatus }) }
    }
    if (url === './api/logs') {
      return { text: async () => '' }
    }
    if (url === './api/clients/list') {
      return { json: async () => ({ status: true, named_certs: [] }) }
    }
    return {
      ok: true,
      json: async () => ({ tag_name: 'v2026.914.1218.10', html_url: 'https://example.com/release' }),
    }
  }))

  const wrapper = mount(Troubleshooting, {
    global: {
      mocks: { $t: key => key },
      stubs: { Navbar: true, RouterLink: true },
    },
  })
  await flushPromises()
  return wrapper
}

afterEach(() => {
  vi.unstubAllGlobals()
})

describe('virtual input troubleshooting', () => {
  it('shows the broker version table without ViGEmBus on macOS', async () => {
    const wrapper = await mountTroubleshooting('macos', 'all')

    expect(wrapper.find('.virtual-gamepad-section').exists()).toBe(true)
    expect(wrapper.findAll('.driver-table-shell tbody tr')).toHaveLength(1)
    expect(wrapper.find('.driver-table-shell').text()).toContain('2026.914.1218')
    expect(fetch).toHaveBeenCalledWith('/api/virtual-input/status')
    wrapper.unmount()
  })

  it.each(['macos', 'windows'])('shows only the license activation limit on %s', async platform => {
    const wrapper = await mountTroubleshooting(platform, 'all', { activation_usage: 1, activation_limit: 5 })

    const activation = wrapper.findAll('.virtualhid-license-stat').find(stat =>
      stat.find('dt').text() === 'troubleshooting.virtualhid_license_activation_limit'
    )
    expect(activation.find('dd').text()).toBe('5')
    wrapper.unmount()
  })

  it('keeps both backend rows on Windows when all backends are enabled', async () => {
    const wrapper = await mountTroubleshooting('windows', 'all')

    expect(wrapper.findAll('.driver-table-shell tbody tr')).toHaveLength(2)
    wrapper.unmount()
  })
})
